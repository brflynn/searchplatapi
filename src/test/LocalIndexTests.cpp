// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include "../app/LocalIndex.h"

#include <windows.h>
#include <atomic>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace applocal;

namespace LocalIndexTests
{
    namespace
    {
        // Each test gets its own on-disk SQLite file under %TEMP% so tests
        // can run concurrently/repeatedly without sharing (or leaking
        // between runs) database state. Uses a simple monotonic counter
        // combined with the process ID rather than CoCreateGuid to avoid
        // pulling in an extra COM library dependency just for test scaffolding.
        std::wstring MakeTempDbPath()
        {
            static std::atomic<uint32_t> counter{ 0 };

            wchar_t tempDir[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tempDir);

            std::wstring path = tempDir;
            path += L"LocalIndexTests_";
            path += std::to_wstring(GetCurrentProcessId());
            path += L"_";
            path += std::to_wstring(GetTickCount64());
            path += L"_";
            path += std::to_wstring(counter.fetch_add(1));
            path += L".db";
            return path;
        }

        bool ContainsPath(const std::vector<LocalSearchResult>& results, const std::wstring& path)
        {
            for (const auto& r : results)
            {
                if (_wcsicmp(r.path.c_str(), path.c_str()) == 0)
                {
                    return true;
                }
            }
            return false;
        }
    }

    TEST_CLASS(FilenameSearchTests)
    {
    public:
        TEST_METHOD(TestUpsertedFileIsFoundByExactName)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(
                L"C:\\Reports\\QuarterlyReport.docx", L"QuarterlyReport.docx", L"C:\\Reports",
                /*isFolder*/ false, /*size*/ 1024, /*attributes*/ 0,
                /*dateCreated*/ 1, /*dateModified*/ 1, /*dateAccessed*/ 1, /*scanGeneration*/ 1);

            auto results = index.Search(L"QuarterlyReport", 50);
            Assert::IsTrue(ContainsPath(results, L"C:\\Reports\\QuarterlyReport.docx"));
            Assert::IsTrue(results.front().matchKind == LocalMatchKind::Filename);

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestSearchIsCaseInsensitiveAndPrefixTolerant)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(
                L"D:\\Code\\MainWindow.xaml.cpp", L"MainWindow.xaml.cpp", L"D:\\Code",
                false, 2048, 0, 1, 1, 1, 1);

            auto results = index.Search(L"mainwin", 50);
            Assert::IsTrue(ContainsPath(results, L"D:\\Code\\MainWindow.xaml.cpp"));

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestNonMatchingQueryReturnsNoResults)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(L"C:\\a\\b.txt", L"b.txt", L"C:\\a", false, 1, 0, 1, 1, 1, 1);

            auto results = index.Search(L"doesnotexistanywhere", 50);
            Assert::IsFalse(ContainsPath(results, L"C:\\a\\b.txt"));

            DeleteFileW(dbPath.c_str());
        }
    };

    TEST_CLASS(ContentSearchTests)
    {
    public:
        TEST_METHOD(TestContentSearchFindsFileNotMatchedByName)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(L"C:\\notes\\untitled.txt", L"untitled.txt", L"C:\\notes",
                false, 100, 0, 1, 1, 1, 1);
            index.UpsertContent(L"C:\\notes\\untitled.txt",
                { L"the quick brown fox jumps over the lazy dog" }, 1, 100, false);

            auto results = index.Search(L"jumps", 50);
            Assert::IsTrue(ContainsPath(results, L"C:\\notes\\untitled.txt"));

            bool foundAsContent = false;
            for (const auto& r : results)
            {
                if (_wcsicmp(r.path.c_str(), L"C:\\notes\\untitled.txt") == 0 &&
                    r.matchKind == LocalMatchKind::Content)
                {
                    foundAsContent = true;
                }
            }
            Assert::IsTrue(foundAsContent);

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestIsContentUpToDateMatchesOnSameMtimeAndSize)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            // content_meta has a FK on files(path), so a file must be
            // known to the name index before its content can be indexed -
            // this mirrors BackgroundIndexer's real scan order (UpsertFile
            // during the walk, then content indexing as a second phase).
            index.UpsertFile(L"C:\\notes\\a.txt", L"a.txt", L"C:\\notes",
                false, 5, 0, 1, 1, 1, 1);
            index.UpsertContent(L"C:\\notes\\a.txt", { L"hello" }, 500, 5, false);

            Assert::IsTrue(index.IsContentUpToDate(L"C:\\notes\\a.txt", 500, 5));
            Assert::IsFalse(index.IsContentUpToDate(L"C:\\notes\\a.txt", 999, 5));
            Assert::IsFalse(index.IsContentUpToDate(L"C:\\notes\\a.txt", 500, 6));
            Assert::IsFalse(index.IsContentUpToDate(L"C:\\notes\\never-indexed.txt", 500, 5));

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestDeleteContentRemovesContentMatchButKeepsFilenameMatch)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(L"C:\\notes\\report.txt", L"report.txt", L"C:\\notes",
                false, 10, 0, 1, 1, 1, 1);
            index.UpsertContent(L"C:\\notes\\report.txt", { L"confidential figures" }, 1, 10, false);

            index.DeleteContent(L"C:\\notes\\report.txt");

            auto contentResults = index.Search(L"confidential", 50);
            Assert::IsFalse(ContainsPath(contentResults, L"C:\\notes\\report.txt"));

            auto nameResults = index.Search(L"report", 50);
            Assert::IsTrue(ContainsPath(nameResults, L"C:\\notes\\report.txt"));

            DeleteFileW(dbPath.c_str());
        }
    };

    TEST_CLASS(ScanGenerationSweepTests)
    {
    public:
        TEST_METHOD(TestSweepStaleRemovesFilesNotSeenInLatestGeneration)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            int64_t gen1 = index.BeginScanGeneration(L"C:\\");
            index.UpsertFile(L"C:\\old.txt", L"old.txt", L"C:\\", false, 1, 0, 1, 1, 1, gen1);
            index.UpsertFile(L"C:\\keep.txt", L"keep.txt", L"C:\\", false, 1, 0, 1, 1, 1, gen1);

            // Second walk only re-touches keep.txt (old.txt was deleted
            // from disk between scans).
            int64_t gen2 = index.BeginScanGeneration(L"C:\\");
            Assert::IsTrue(gen2 >= gen1);
            index.UpsertFile(L"C:\\keep.txt", L"keep.txt", L"C:\\", false, 1, 0, 1, 1, 1, gen2);

            index.SweepStale(L"C:\\", gen2);

            auto oldResults = index.Search(L"old", 50);
            Assert::IsFalse(ContainsPath(oldResults, L"C:\\old.txt"));

            auto keepResults = index.Search(L"keep", 50);
            Assert::IsTrue(ContainsPath(keepResults, L"C:\\keep.txt"));

            DeleteFileW(dbPath.c_str());
        }
    };

    TEST_CLASS(ScanRootStatusTests)
    {
    public:
        TEST_METHOD(TestSetAndGetScanRootStatusRoundTrips)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.SetScanRootStatus(L"C:\\", L"done", 12345, 42);

            auto roots = index.GetScanRoots();
            bool found = false;
            for (const auto& r : roots)
            {
                if (_wcsicmp(r.root.c_str(), L"C:\\") == 0)
                {
                    found = true;
                    Assert::AreEqual(std::wstring(L"done"), r.status);
                    Assert::AreEqual(static_cast<int64_t>(12345), r.lastFullScan);
                    Assert::AreEqual(static_cast<uint64_t>(42), r.filesScanned);
                }
            }
            Assert::IsTrue(found);

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestUpdatingStatusOverwritesPreviousValue)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.SetScanRootStatus(L"D:\\", L"pending", 0, 0);
            index.SetScanRootStatus(L"D:\\", L"in_progress", 0, 10);
            index.SetScanRootStatus(L"D:\\", L"done", 999, 100);

            auto roots = index.GetScanRoots();
            int matchCount = 0;
            for (const auto& r : roots)
            {
                if (_wcsicmp(r.root.c_str(), L"D:\\") == 0)
                {
                    matchCount++;
                    Assert::AreEqual(std::wstring(L"done"), r.status);
                }
            }
            Assert::AreEqual(1, matchCount);

            DeleteFileW(dbPath.c_str());
        }
    };

    TEST_CLASS(TransactionTests)
    {
    public:
        TEST_METHOD(TestCommittedTransactionPersists)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.BeginTransaction();
            index.UpsertFile(L"C:\\tx\\committed.txt", L"committed.txt", L"C:\\tx",
                false, 1, 0, 1, 1, 1, 1);
            index.EndTransaction(/*commit*/ true);

            auto results = index.Search(L"committed", 50);
            Assert::IsTrue(ContainsPath(results, L"C:\\tx\\committed.txt"));

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestRolledBackTransactionDoesNotPersist)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.BeginTransaction();
            index.UpsertFile(L"C:\\tx\\rolledback.txt", L"rolledback.txt", L"C:\\tx",
                false, 1, 0, 1, 1, 1, 1);
            index.EndTransaction(/*commit*/ false);

            auto results = index.Search(L"rolledback", 50);
            Assert::IsFalse(ContainsPath(results, L"C:\\tx\\rolledback.txt"));

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestNestedUpsertsWithinOneTransactionAllCommit)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            // BeginTransaction()/EndTransaction() must tolerate nested
            // Upsert*() calls from the same writer thread (recursive
            // writer mutex) without deadlocking.
            index.BeginTransaction();
            for (int i = 0; i < 5; ++i)
            {
                std::wstring name = L"batch" + std::to_wstring(i) + L".txt";
                std::wstring path = L"C:\\tx\\" + name;
                index.UpsertFile(path, name, L"C:\\tx", false, 1, 0, 1, 1, 1, 1);
            }
            index.EndTransaction(true);

            for (int i = 0; i < 5; ++i)
            {
                auto results = index.Search(L"batch" + std::to_wstring(i), 50);
                Assert::IsTrue(ContainsPath(results, L"C:\\tx\\batch" + std::to_wstring(i) + L".txt"));
            }

            DeleteFileW(dbPath.c_str());
        }
    };
}
