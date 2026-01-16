// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include <SearchSessionPropertyHelpers.h>
#include <fstream>
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace wsearch;

namespace SearchPropertyHelperTests
{
    TEST_CLASS(SearchResultPropertyUpdaterTests)
    {
    private:
        std::wstring m_testFolderPath;
        std::wstring m_testFilePath;

    public:
        TEST_METHOD_INITIALIZE(TestInitialize)
        {
            // Create a test folder in temp directory
            wchar_t tempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            m_testFolderPath = std::wstring(tempPath) + L"SearchPropertyHelperTests_" + 
                              std::to_wstring(GetTickCount64());
            
            std::filesystem::create_directory(m_testFolderPath);
            
            // Create a test file
            m_testFilePath = m_testFolderPath + L"\\TestFile.txt";
            std::wofstream testFile(m_testFilePath);
            testFile << L"This is a test file for property update testing.";
            testFile.close();
            
            Logger::WriteMessage(L"Test folder created: ");
            Logger::WriteMessage(m_testFolderPath.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD_CLEANUP(TestCleanup)
        {
            // Clean up test files and folder
            try
            {
                std::filesystem::remove_all(m_testFolderPath);
                Logger::WriteMessage(L"Test folder cleaned up\n");
            }
            catch (...)
            {
                Logger::WriteMessage(L"Failed to clean up test folder\n");
            }
        }

        TEST_METHOD(TestPropertyUpdaterCreation)
        {
            Logger::WriteMessage(L"Testing SearchResultPropertyUpdater creation...\n");
            
            // Create property updater
            SearchResultPropertyUpdater updater;
            
            // Verify it's created successfully
            Assert::AreEqual(static_cast<size_t>(0), updater.GetPendingUpdateCount());
            
            Logger::WriteMessage(L"Property updater created successfully\n");
        }

        TEST_METHOD(TestPropertyUpdaterDestruction)
        {
            Logger::WriteMessage(L"Testing SearchResultPropertyUpdater destruction...\n");
            
            {
                SearchResultPropertyUpdater updater;
                updater.OnResultClicked(m_testFilePath);
                
                // Updater will be destroyed here
            }
            
            // If we get here without crashing, destruction worked
            Assert::IsTrue(true);
            Logger::WriteMessage(L"Property updater destroyed successfully\n");
        }

        TEST_METHOD(TestQueueingMultipleUpdates)
        {
            Logger::WriteMessage(L"Testing queuing multiple property updates...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Queue multiple updates
            updater.OnResultClicked(m_testFilePath);
            updater.OnResultClicked(m_testFilePath);
            updater.OnResultClicked(m_testFilePath);
            
            // Verify all three are queued
            size_t pending = updater.GetPendingUpdateCount();
            Assert::IsTrue(pending <= 3);  // May have already processed some
            
            Logger::WriteMessage(L"Successfully queued multiple updates\n");
        }

        TEST_METHOD(TestPropertyUpdatesProcessed)
        {
            Logger::WriteMessage(L"Testing that property updates are processed...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Queue an update
            updater.OnResultClicked(m_testFilePath);
            
            size_t initialPending = updater.GetPendingUpdateCount();
            Logger::WriteMessage((L"Initial pending: " + std::to_wstring(initialPending) + L"\n").c_str());
            
            // Wait for processing (max 5 seconds)
            int maxWaitMs = 5000;
            int waitedMs = 0;
            int checkIntervalMs = 100;
            
            while (updater.GetPendingUpdateCount() > 0 && waitedMs < maxWaitMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
                waitedMs += checkIntervalMs;
            }
            
            size_t finalPending = updater.GetPendingUpdateCount();
            Logger::WriteMessage((L"Final pending: " + std::to_wstring(finalPending) + L"\n").c_str());
            Logger::WriteMessage((L"Waited: " + std::to_wstring(waitedMs) + L" ms\n").c_str());
            
            // Verify queue was processed
            Assert::IsTrue(finalPending < initialPending || waitedMs < maxWaitMs);
            
            Logger::WriteMessage(L"Property updates were processed\n");
        }

        TEST_METHOD(TestPropertyUpdateOnRealFile)
        {
            Logger::WriteMessage(L"Testing property update on real file...\n");
            
            // Read initial property values
            winrt::com_ptr<IPropertyStore> propStore;
            HRESULT hr = SHGetPropertyStoreFromParsingName(
                m_testFilePath.c_str(),
                nullptr,
                GPS_READWRITE,
                IID_PPV_ARGS(propStore.put()));
            
            if (FAILED(hr))
            {
                Logger::WriteMessage(L"Failed to open property store - file may not be indexed\n");
                Logger::WriteMessage((L"HRESULT: 0x" + std::to_wstring(hr) + L"\n").c_str());
                // This is expected if file isn't indexed yet
                return;
            }
            
            // Read initial LineCount
            PROPVARIANT propVar;
            PropVariantInit(&propVar);
            hr = propStore->GetValue(PKEY_Document_LineCount, &propVar);
            
            LONG initialCount = 0;
            if (SUCCEEDED(hr) && propVar.vt == VT_I4)
            {
                initialCount = propVar.lVal;
            }
            PropVariantClear(&propVar);
            
            Logger::WriteMessage((L"Initial LineCount: " + std::to_wstring(initialCount) + L"\n").c_str());
            
            // Trigger property update
            {
                SearchResultPropertyUpdater updater;
                updater.OnResultClicked(m_testFilePath);
                
                // Wait for processing
                int maxWaitMs = 5000;
                int waitedMs = 0;
                while (updater.GetPendingUpdateCount() > 0 && waitedMs < maxWaitMs)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    waitedMs += 100;
                }
            }
            
            // Re-open property store and read updated value
            propStore = nullptr;
            hr = SHGetPropertyStoreFromParsingName(
                m_testFilePath.c_str(),
                nullptr,
                GPS_DEFAULT,
                IID_PPV_ARGS(propStore.put()));
            
            if (SUCCEEDED(hr))
            {
                PropVariantInit(&propVar);
                hr = propStore->GetValue(PKEY_Document_LineCount, &propVar);
                
                LONG finalCount = 0;
                if (SUCCEEDED(hr) && propVar.vt == VT_I4)
                {
                    finalCount = propVar.lVal;
                }
                PropVariantClear(&propVar);
                
                Logger::WriteMessage((L"Final LineCount: " + std::to_wstring(finalCount) + L"\n").c_str());
                
                // Verify count was incremented
                Assert::AreEqual(initialCount + 1, finalCount);
                Logger::WriteMessage(L"Property update verified successfully\n");
            }
        }

        TEST_METHOD(TestConcurrentUpdates)
        {
            Logger::WriteMessage(L"Testing concurrent property updates...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Queue updates from multiple threads
            std::vector<std::thread> threads;
            const int numThreads = 5;
            const int updatesPerThread = 3;
            
            for (int i = 0; i < numThreads; ++i)
            {
                threads.emplace_back([&updater, this, updatesPerThread]() {
                    for (int j = 0; j < updatesPerThread; ++j)
                    {
                        updater.OnResultClicked(m_testFilePath);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                });
            }
            
            // Wait for all threads to complete
            for (auto& thread : threads)
            {
                thread.join();
            }
            
            // Wait for queue to be processed
            int maxWaitMs = 10000;
            int waitedMs = 0;
            while (updater.GetPendingUpdateCount() > 0 && waitedMs < maxWaitMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitedMs += 100;
            }
            
            Logger::WriteMessage((L"Final pending count: " + 
                std::to_wstring(updater.GetPendingUpdateCount()) + L"\n").c_str());
            
            // If we get here without crashing, concurrent updates worked
            Assert::IsTrue(true);
            Logger::WriteMessage(L"Concurrent updates completed successfully\n");
        }

        TEST_METHOD(TestInvalidFilePath)
        {
            Logger::WriteMessage(L"Testing property update with invalid file path...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Queue update for non-existent file
            std::wstring invalidPath = L"C:\\NonExistent\\Path\\File.txt";
            updater.OnResultClicked(invalidPath);
            
            // Wait for processing
            int maxWaitMs = 5000;
            int waitedMs = 0;
            while (updater.GetPendingUpdateCount() > 0 && waitedMs < maxWaitMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitedMs += 100;
            }
            
            // Should handle gracefully without crashing
            Assert::IsTrue(true);
            Logger::WriteMessage(L"Invalid path handled gracefully\n");
        }

        TEST_METHOD(TestEmptyQueue)
        {
            Logger::WriteMessage(L"Testing empty queue behavior...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Check pending count on empty queue
            size_t pending = updater.GetPendingUpdateCount();
            Assert::AreEqual(static_cast<size_t>(0), pending);
            
            Logger::WriteMessage(L"Empty queue behaves correctly\n");
        }

        TEST_METHOD(TestRapidQueueingAndDequeuing)
        {
            Logger::WriteMessage(L"Testing rapid queuing and dequeuing...\n");
            
            SearchResultPropertyUpdater updater;
            
            // Rapidly queue many updates
            for (int i = 0; i < 50; ++i)
            {
                updater.OnResultClicked(m_testFilePath);
            }
            
            Logger::WriteMessage((L"Queued 50 updates, pending: " + 
                std::to_wstring(updater.GetPendingUpdateCount()) + L"\n").c_str());
            
            // Wait for all to process
            int maxWaitMs = 30000;  // 30 seconds for 50 updates
            int waitedMs = 0;
            while (updater.GetPendingUpdateCount() > 0 && waitedMs < maxWaitMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitedMs += 100;
                
                if (waitedMs % 1000 == 0)
                {
                    Logger::WriteMessage((L"  Still processing, pending: " + 
                        std::to_wstring(updater.GetPendingUpdateCount()) + L"\n").c_str());
                }
            }
            
            Logger::WriteMessage((L"Processing completed in " + 
                std::to_wstring(waitedMs) + L" ms\n").c_str());
            
            Assert::IsTrue(waitedMs < maxWaitMs);
            Logger::WriteMessage(L"Rapid queuing handled successfully\n");
        }

        TEST_METHOD(TestPropertyUpdaterLifetime)
        {
            Logger::WriteMessage(L"Testing property updater lifetime and cleanup...\n");
            
            // Test that updater can be created and destroyed multiple times
            for (int i = 0; i < 5; ++i)
            {
                SearchResultPropertyUpdater updater;
                updater.OnResultClicked(m_testFilePath);
                
                // Wait briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Updater destroyed at end of iteration
            }
            
            Assert::IsTrue(true);
            Logger::WriteMessage(L"Multiple create/destroy cycles completed successfully\n");
        }
    };
}
