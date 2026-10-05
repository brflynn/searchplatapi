// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include "../app/LocalIndex.h"
#include "../app/ContentScopeRequest.h"
#include "../app/thirdparty/sqlite/sqlite3.h"

#include <windows.h>
#include <atomic>
#include <thread>

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

    TEST_CLASS(IndexSettingsTests)
    {
    public:
        TEST_METHOD(TestNormalizedFolderBoundariesAndResultActions)
        {
            IndexSettings settings;
            settings.exclusions.push_back({ L"folder", NormalizeFolder(L"C:/Foo/./Bar/../") });
            Assert::IsTrue(settings.IsExcluded(L"c:\\FOO\\report.TXT", false));
            Assert::IsTrue(settings.IsExcluded(L"C:\\Foo", true));
            Assert::IsFalse(settings.IsExcluded(L"C:\\Foobar\\report.txt", false));
            Assert::AreEqual(std::wstring(L"c:\\foo"), ResultFolder(L"C:\\Foo\\report.txt", false));
            Assert::AreEqual(std::wstring(L"c:\\foo"), ResultFolder(L"C:\\Foo", true));
            Assert::AreEqual(std::wstring(L"c:\\"), ResultFolder(L"C:\\file.txt", false));
            Assert::AreEqual(NormalizeFolder(L"\\\\server\\share\\foo"),
                NormalizeFolder(L"\\\\?\\UNC\\SERVER\\share\\foo\\"));
            Assert::AreEqual(NormalizeFolder(L"C:\\Foo"), NormalizeFolder(L"\\\\?\\C:\\FOO\\"));
        }

        TEST_METHOD(TestExtensionRulesAndNoExtensionActions)
        {
            IndexSettings settings;
            settings.exclusions.push_back({ L"extension", L".txt" });
            Assert::IsTrue(settings.IsExcluded(L"C:\\notes\\REPORT.TXT", false));
            Assert::IsFalse(settings.IsExcluded(L"C:\\notes\\folder.txt", true));
            Assert::IsFalse(settings.IsExcluded(L"C:\\notes.txt\\README", false));
            Assert::IsTrue(ResultExtension(L"C:\\README", false).empty());
            Assert::IsTrue(ResultExtension(L"C:\\file.", false).empty());
            Assert::IsTrue(ResultExtension(L"C:\\.gitignore", false).empty());
        }

        TEST_METHOD(TestSettingsRoundTripDuplicatesAndUndoRetainContent)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                index.UpsertFile(L"C:\\foo\\report.TXT", L"report.TXT", L"C:\\foo", false, 1, 0, 1, 1, 1, 1);
                index.UpsertContent(L"C:\\foo\\report.TXT", { L"retainedcontent" }, 1, 1, false);
                index.AddExclusion(L"folder", L"C:/FOO/");
                index.AddExclusion(L"folder", L"c:\\foo");
                index.AddExclusion(L"extension", L".TXT");
                index.SetContentScope({ L"C:\\Foo", L"failed", L"Permission denied" });
                Assert::AreEqual(size_t(2), index.GetSettings().exclusions.size());
            }
            {
                LocalIndex reopened(db);
                auto settings = reopened.GetSettings();
                Assert::AreEqual(size_t(2), settings.exclusions.size());
                Assert::AreEqual(std::wstring(L"c:\\foo"), settings.contentScopes.at(0).folder);
                Assert::AreEqual(std::wstring(L"failed"), settings.contentScopes.at(0).state);
                Assert::IsTrue(reopened.Search(L"report", 1).empty());
                Assert::IsTrue(reopened.Search(L"retainedcontent", 1).empty());
                Assert::AreEqual(uint64_t(1), reopened.GetStatistics().contentIndexedFiles);
                reopened.RemoveExclusion(L"folder", L"C:\\FOO\\");
                reopened.RemoveExclusion(L"extension", L".TXT");
                Assert::AreEqual(size_t(1), reopened.Search(L"retainedcontent", 1).size());
                reopened.ForgetContentScope(L"C:\\FOO");
                Assert::IsTrue(reopened.GetSettings().contentScopes.empty());
            }
            DeleteFileW(db.c_str());
        }

        TEST_METHOD(TestBothSourcesUseSameFilterBeforeNameAndContentCaps)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                for (int i = 0; i < 70; ++i)
                {
                    auto name = L"common" + std::to_wstring(i) + L".txt";
                    auto path = L"C:\\hidden\\" + name;
                    index.UpsertFile(path, name, L"C:\\hidden", false, 1, 0, 1, 1, 1, 1);
                    index.UpsertContent(path, { L"sharedcontent" }, 1, 1, false);
                }
                index.UpsertFile(L"C:\\allowed\\commonallowed.md", L"commonallowed.md", L"C:\\allowed", false, 1, 0, 1, 1, 1, 1);
                index.UpsertContent(L"C:\\allowed\\commonallowed.md", { L"sharedcontent" }, 1, 1, false);
                index.AddExclusion(L"folder", L"C:\\hidden");
                auto settings = index.GetSettings();
                // The Windows Search merge uses this same source-independent predicate.
                Assert::IsTrue(settings.IsExcluded(L"C:\\HIDDEN\\common0.txt", false));
                Assert::IsFalse(settings.IsExcluded(L"C:\\allowed\\commonallowed.md", false));
                Assert::AreEqual(std::wstring(L"C:\\allowed\\commonallowed.md"), index.Search(L"common", 1, settings).at(0).path);
                Assert::AreEqual(std::wstring(L"C:\\allowed\\commonallowed.md"), index.Search(L"sharedcontent", 1, settings).at(0).path);
            }
            DeleteFileW(db.c_str());
        }

        TEST_METHOD(TestFolderFileUrlEscapesAndUnc)
        {
            Assert::AreEqual(std::wstring(L"file:///c:/a%20b/%23%25%3F/"), FolderFileUrl(L"C:\\a b\\#%?"));
            Assert::AreEqual(std::wstring(L"file://server/share/a%20b/"), FolderFileUrl(L"\\\\server\\share\\a b"));
            Assert::AreEqual(std::wstring(L"file:///c:/caf%C3%A9/"), FolderFileUrl(L"C:\\caf\u00E9"));
        }

        TEST_METHOD(TestInvalidRulesFailExplicitly)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                Assert::ExpectException<std::invalid_argument>([&] { index.AddExclusion(L"folder", L"relative"); });
                Assert::ExpectException<std::invalid_argument>([&] { index.AddExclusion(L"extension", L"."); });
                Assert::ExpectException<std::invalid_argument>([&] { index.AddExclusion(L"extension", L".a/b"); });
                Assert::IsTrue(index.GetSettings().exclusions.empty());
            }
            DeleteFileW(db.c_str());
        }

        TEST_METHOD(TestInterruptedScopeRecoveryAndCancellationNeverChangeWindowsScope)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                index.SetContentScope({ L"C:\\pending", L"pending", L"Started" });
                index.SetContentScope({ L"C:\\done", L"idle", L"Catalog idle only" });
                index.RecoverInterruptedScopeRequests();
                auto scopes = index.GetSettings().contentScopes;
                Assert::AreEqual(std::wstring(L"idle"), scopes.at(0).state);
                Assert::AreEqual(std::wstring(L"cancelled"), scopes.at(1).state);
                Assert::IsTrue(scopes.at(1).detail.find(L"No Windows scope rules") != std::wstring::npos);
                std::atomic<bool> cancelled{ true };
                auto result = RunContentScopeRequest(L"C:\\pending", cancelled);
                Assert::AreEqual(std::wstring(L"cancelled"), result.state);
                Assert::IsTrue(result.detail.find(L"before changing") != std::wstring::npos);
            }
            DeleteFileW(db.c_str());
        }
    };

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
        TEST_METHOD(TestLegacyGenerationSeedAndReopenPreserveSequence)
        {
            auto db = MakeTempDbPath();
            constexpr int64_t future = 800000000000000000;
            {
                LocalIndex index(db);
                index.UpsertFile(L"C:\\legacy.txt", L"legacy.txt", L"C:\\", false, 1, 0, 1, 1, 1, future);
            }
            sqlite3* connection = nullptr;
            Assert::AreEqual(SQLITE_OK, sqlite3_open16(db.c_str(), &connection));
            Assert::AreEqual(SQLITE_OK, sqlite3_exec(connection, "DROP TABLE scan_sequence;", nullptr, nullptr, nullptr));
            Assert::AreEqual(SQLITE_OK, sqlite3_close(connection));
            {
                LocalIndex migrated(db);
                Assert::AreEqual(future + 1, migrated.BeginScanGeneration(L"C:\\"));
                migrated.UpsertFile(L"C:\\later.txt", L"later.txt", L"C:\\", false, 1, 0, 1, 1, 1, future + 100);
            }
            {
                LocalIndex reopened(db);
                // An existing sequence is never reseeded by scanning the entire files table.
                Assert::AreEqual(future + 2, reopened.BeginScanGeneration(L"C:\\"));
                Assert::IsFalse(reopened.Search(L"legacy", 1).empty());
            }
            DeleteFileW(db.c_str());
        }

        TEST_METHOD(TestCancelledInitializationCanBeReopenedWithoutLosingData)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                index.UpsertFile(L"C:\\retained.txt", L"retained.txt", L"C:\\", false, 1, 0, 1, 1, 1, 1);
                index.AddExclusion(L"extension", L".txt");
            }
            std::atomic<bool> cancelled{ true };
            Assert::ExpectException<std::runtime_error>([&] { LocalIndex index(db, &cancelled); });
            {
                LocalIndex reopened(db);
                Assert::AreEqual(size_t(1), reopened.GetSettings().exclusions.size());
                reopened.RemoveExclusion(L"extension", L".txt");
                Assert::IsFalse(reopened.Search(L"retained", 1).empty());
            }
            DeleteFileW(db.c_str());
        }

        TEST_METHOD(TestSweepHonorsFolderBoundaryAndSqlWildcardNames)
        {
            auto db = MakeTempDbPath();
            {
                LocalIndex index(db);
                index.UpsertFile(L"C:\\foo_%\\stale.txt", L"stale.txt", L"C:\\foo_%", false, 1, 0, 1, 1, 1, 1);
                index.UpsertFile(L"C:\\foo_%bar\\keep.txt", L"keep.txt", L"C:\\foo_%bar", false, 1, 0, 1, 1, 1, 1);
                index.UpsertFile(L"C:\\fooxxx\\other.txt", L"other.txt", L"C:\\fooxxx", false, 1, 0, 1, 1, 1, 1);
                index.SweepStale(L"C:\\foo_%", 2);
                Assert::IsTrue(index.Search(L"stale", 1).empty());
                Assert::IsFalse(index.Search(L"keep", 1).empty());
                Assert::IsFalse(index.Search(L"other", 1).empty());
            }
            DeleteFileW(db.c_str());
        }

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
            Assert::IsTrue(gen2 > gen1);
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

    TEST_CLASS(IndexStatisticsTests)
    {
    public:
        TEST_METHOD(TestStatisticsCountItemsFilesFoldersAndContent)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            index.UpsertFile(L"C:\\docs", L"docs", L"C:\\", true, 0, 0, 1, 1, 1, 1);
            index.UpsertFile(L"C:\\docs\\one.txt", L"one.txt", L"C:\\docs", false, 3, 0, 1, 1, 1, 1);
            index.UpsertFile(L"C:\\docs\\two.bin", L"two.bin", L"C:\\docs", false, 4, 0, 1, 1, 1, 1);
            index.UpsertContent(L"C:\\docs\\one.txt", { L"one" }, 1, 3, false);

            auto statistics = index.GetStatistics();
            Assert::AreEqual(static_cast<uint64_t>(3), statistics.totalItems);
            Assert::AreEqual(static_cast<uint64_t>(2), statistics.files);
            Assert::AreEqual(static_cast<uint64_t>(1), statistics.folders);
            Assert::AreEqual(static_cast<uint64_t>(1), statistics.contentIndexedFiles);

            DeleteFileW(dbPath.c_str());
        }

        TEST_METHOD(TestEmptyIndexStatisticsAreZero)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);

            auto statistics = index.GetStatistics();
            Assert::AreEqual(static_cast<uint64_t>(0), statistics.totalItems);
            Assert::AreEqual(static_cast<uint64_t>(0), statistics.files);
            Assert::AreEqual(static_cast<uint64_t>(0), statistics.folders);
            Assert::AreEqual(static_cast<uint64_t>(0), statistics.contentIndexedFiles);

            DeleteFileW(dbPath.c_str());
        }
    };

    TEST_CLASS(LocalIndexConcurrencyTests)
    {
    public:
        TEST_METHOD(TestSearchStatisticsContentChecksAndWritesCanRunTogether)
        {
            auto dbPath = MakeTempDbPath();
            LocalIndex index(dbPath);
            index.UpsertFile(
                L"C:\\seed.txt", L"seed.txt", L"C:\\", false,
                4, 0, 1, 1, 1, 1);
            index.UpsertContent(L"C:\\seed.txt", { L"seed content" }, 1, 4, false);

            std::atomic<bool> failed{ false };
            auto guard = [&](auto operation)
            {
                try
                {
                    operation();
                }
                catch (...)
                {
                    failed.store(true);
                }
            };

            std::thread searchThread([&]
            {
                guard([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        auto results = index.Search(L"seed", 20);
                        if (!ContainsPath(results, L"C:\\seed.txt"))
                        {
                            failed.store(true);
                        }
                    }
                });
            });
            std::thread statisticsThread([&]
            {
                guard([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        if (index.GetStatistics().totalItems == 0)
                        {
                            failed.store(true);
                        }
                    }
                });
            });
            std::thread contentThread([&]
            {
                guard([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        if (!index.IsContentUpToDate(L"C:\\seed.txt", 1, 4))
                        {
                            failed.store(true);
                        }
                    }
                });
            });
            std::thread writerThread([&]
            {
                guard([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        auto name = L"background-" + std::to_wstring(i) + L".txt";
                        index.UpsertFile(
                            L"C:\\" + name, name, L"C:\\", false,
                            static_cast<uint64_t>(i), 0, 1, i + 2, 1, 2);
                    }
                });
            });

            searchThread.join();
            statisticsThread.join();
            contentThread.join();
            writerThread.join();

            Assert::IsFalse(failed.load());
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
