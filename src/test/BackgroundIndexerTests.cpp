// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include "../app/BackgroundIndexer.h"
#include <fstream>
#include <functional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace applocal;

namespace BackgroundIndexerTests
{
    namespace
    {
        struct ScanFixture
        {
            std::wstring root;
            std::wstring db;
            ScanFixture()
            {
                wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
                GetTempPathW(MAX_PATH, temp);
                GetTempFileNameW(temp, L"idx", 0, unique);
                DeleteFileW(unique);
                root = unique;
                CreateDirectoryW(root.c_str(), nullptr);
                db = root + L".db";
            }
            ~ScanFixture()
            {
                DeleteFileW((root + L"\\lifecycle.txt").c_str());
                RemoveDirectoryW(root.c_str());
                DeleteFileW(db.c_str());
                DeleteFileW((db + L"-wal").c_str());
                DeleteFileW((db + L"-shm").c_str());
            }
        };

        bool WaitFor(const std::function<bool()>& predicate)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        }
    }

    TEST_CLASS(ReindexLifecycleTests)
    {
    public:
        TEST_METHOD(TestReindexAfterCompletionUsesExistingWorkersAndRetainsContent)
        {
            ScanFixture fixture;
            auto index = std::make_shared<LocalIndex>(fixture.db);
            IndexerOptions options;
            options.scanRoots = { fixture.root };
            BackgroundIndexer worker(index, options);
            worker.Start();
            Assert::IsTrue(WaitFor([&] { return !index->GetScanRoots().empty() &&
                index->GetScanRoots().front().status == L"done"; }));
            std::ofstream file(fixture.root + L"\\lifecycle.txt");
            file << "lifecyclecontent";
            file.close();
            worker.RequestReindex();
            worker.RequestReindex();
            worker.Start(); // Must not create duplicate scanner/content threads.
            Assert::IsTrue(WaitFor([&] { return !index->Search(L"lifecycle", 1).empty(); }));
            Assert::IsTrue(WaitFor([&] { return !index->Search(L"lifecyclecontent", 1).empty(); }));
            Assert::IsTrue(WaitFor([&] { return worker.GetProgress().reindexStatus.starts_with(L"Scan completed"); }));
            index->AddExclusion(L"extension", L".txt");
            worker.RequestReindex();
            Assert::IsTrue(WaitFor([&] { return worker.GetProgress().reindexStatus.starts_with(L"Scan completed"); }));
            Assert::IsTrue(index->Search(L"lifecyclecontent", 1).empty());
            index->RemoveExclusion(L"extension", L".txt");
            Assert::IsFalse(index->Search(L"lifecyclecontent", 1).empty());
            worker.Stop();
        }

        TEST_METHOD(TestIncompleteScanDoesNotSweepAndReportsError)
        {
            ScanFixture fixture;
            RemoveDirectoryW(fixture.root.c_str());
            auto index = std::make_shared<LocalIndex>(fixture.db);
            auto path = fixture.root + L"\\lifecycle.txt";
            index->UpsertFile(path, L"lifecycle.txt", fixture.root, false, 1, 0, 1, 1, 1, 1);
            IndexerOptions options;
            options.scanRoots = { fixture.root };
            BackgroundIndexer worker(index, options);
            worker.RequestReindex();
            worker.Start();
            Assert::IsTrue(WaitFor([&] { return worker.GetProgress().reindexStatus == L"Error"; }));
            Assert::IsFalse(worker.GetProgress().error.empty());
            Assert::IsFalse(index->Search(L"lifecycle", 1).empty());
            Assert::AreEqual(std::wstring(L"incomplete"), index->GetScanRoots().front().status);
            worker.Stop();
        }

        TEST_METHOD(TestQueuedRequestWhilePausedAndShutdown)
        {
            ScanFixture fixture;
            auto index = std::make_shared<LocalIndex>(fixture.db);
            IndexerOptions options;
            options.scanRoots = { fixture.root };
            BackgroundIndexer worker(index, options);
            worker.Start();
            worker.Pause();
            worker.RequestReindex();
            worker.RescanRoot(fixture.root);
            worker.Stop(); // Wakes both pause and work waits.
            Assert::IsTrue(worker.GetProgress().state == IndexerState::Stopped);
        }

        TEST_METHOD(TestRequestDuringActivePassQueuesAnotherGeneration)
        {
            ScanFixture fixture;
            auto index = std::make_shared<LocalIndex>(fixture.db);
            IndexerOptions options;
            options.scanRoots = { fixture.root };
            BackgroundIndexer worker(index, options);
            worker.Start();
            Assert::IsTrue(WaitFor([&] { auto roots = index->GetScanRoots();
                return !roots.empty() && roots.front().status == L"done"; }));
            auto initialGeneration = index->GetScanRoots().front().lastFullScan;
            worker.Pause();
            worker.RequestReindex();
            Assert::IsTrue(WaitFor([&] { return index->GetScanRoots().front().status == L"in_progress"; }));
            worker.RequestReindex();
            worker.RequestReindex(); // Coalesces while the active pass is paused.
            Assert::AreEqual(std::wstring(L"Queued"), worker.GetProgress().reindexStatus);
            worker.Resume();
            Assert::IsTrue(WaitFor([&] { return worker.GetProgress().reindexStatus.starts_with(L"Scan completed"); }));
            auto roots = index->GetScanRoots();
            Assert::AreEqual(std::wstring(L"done"), roots.front().status);
            Assert::IsTrue(roots.front().lastFullScan > initialGeneration + 1);
            worker.Stop();
        }
    };

    TEST_CLASS(ShouldIndexContentTests)
    {
    public:
        TEST_METHOD(TestKnownTextExtensionIsIndexed)
        {
            IndexerOptions options;
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\readme.txt", options));
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\main.cpp", options));
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\notes.MD", options));
        }

        TEST_METHOD(TestUnknownExtensionIsNotIndexed)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\bin\\app.exe", options));
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\media\\photo.jpg", options));
        }

        TEST_METHOD(TestNoExtensionIsNotIndexed)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\Users\\me\\Makefile", options));
        }

        TEST_METHOD(TestDotInDirectoryNameDoesNotCountAsExtension)
        {
            IndexerOptions options;
            // The only '.' is in a directory component, not the filename.
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\repo.v2\\Makefile", options));
        }

        TEST_METHOD(TestTrailingDotIsNotAnExtension)
        {
            IndexerOptions options;
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\src\\weirdname.", options));
        }

        TEST_METHOD(TestConfigurableExtensionListIsRespected)
        {
            IndexerOptions options;
            options.contentExtensions = { L".foo" };
            Assert::IsTrue(BackgroundIndexer::ShouldIndexContent(L"C:\\data\\thing.foo", options));
            Assert::IsFalse(BackgroundIndexer::ShouldIndexContent(L"C:\\data\\thing.txt", options));
        }
    };

    TEST_CLASS(LooksBinaryTests)
    {
    public:
        TEST_METHOD(TestTextBufferIsNotBinary)
        {
            const char text[] = "Hello, world! This is plain text.";
            Assert::IsFalse(BackgroundIndexer::LooksBinary(text, sizeof(text) - 1));
        }

        TEST_METHOD(TestNulByteMarksBufferBinary)
        {
            const char data[] = { 'A', 'B', '\0', 'C' };
            Assert::IsTrue(BackgroundIndexer::LooksBinary(data, sizeof(data)));
        }

        TEST_METHOD(TestEmptyBufferIsNotBinary)
        {
            Assert::IsFalse(BackgroundIndexer::LooksBinary(nullptr, 0));
        }
    };

    TEST_CLASS(FileTimeToInt64Tests)
    {
    public:
        TEST_METHOD(TestRoundTripsHighAndLowParts)
        {
            FILETIME ft{};
            ft.dwLowDateTime = 0x1234'5678;
            ft.dwHighDateTime = 0x0000'0001;

            int64_t value = BackgroundIndexer::FileTimeToInt64(ft);
            Assert::AreEqual(static_cast<int64_t>(0x1'1234'5678), value);
        }

        TEST_METHOD(TestZeroFileTimeIsZero)
        {
            FILETIME ft{};
            Assert::AreEqual(static_cast<int64_t>(0), BackgroundIndexer::FileTimeToInt64(ft));
        }

        TEST_METHOD(TestOrderingIsPreserved)
        {
            FILETIME earlier{};
            earlier.dwLowDateTime = 100;

            FILETIME later{};
            later.dwLowDateTime = 200;

            Assert::IsTrue(BackgroundIndexer::FileTimeToInt64(earlier) < BackgroundIndexer::FileTimeToInt64(later));
        }
    };
}
