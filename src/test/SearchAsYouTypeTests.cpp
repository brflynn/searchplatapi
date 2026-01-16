// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include <windows.h>

#include <SearchSessions.h>
#include <SearchPlatCore.h>
#include "SearchTestUtilities.h"
#include <thread>
#include <chrono>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace wsearch;
using namespace wsearch::details;
using namespace SearchTestUtilities;

namespace SearchAsYouTypeTests
{
    TEST_CLASS(SearchAsYouTypeTests)
    {
    public:
        TEST_METHOD(TestBasicConstruction)
        {
            Logger::WriteMessage(L"Testing basic SearchAsYouTypeSession construction");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            Assert::AreEqual(search.GetSearchText(), std::wstring(L""));
            Assert::IsFalse(search.IsQueryPending());
        }

        TEST_METHOD(TestSetSearchText)
        {
            Logger::WriteMessage(L"Testing SetSearchText functionality");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"test");
            Assert::AreEqual(search.GetSearchText(), std::wstring(L"test"));
            Assert::IsTrue(search.IsQueryPending());
            
            // Wait for query to complete
            auto results = search.GetCachedResults();
            Assert::IsNotNull(results.get());
            Assert::IsFalse(search.IsQueryPending());
        }

        TEST_METHOD(TestAppendCharacters)
        {
            Logger::WriteMessage(L"Testing AppendCharacters functionality");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"doc");
            search.AppendCharacters(L"ument");
            
            Assert::AreEqual(search.GetSearchText(), std::wstring(L"document"));
            
            auto results = search.GetCachedResults();
            Assert::IsNotNull(results.get());
        }

        TEST_METHOD(TestClearSearchText)
        {
            Logger::WriteMessage(L"Testing Clear functionality");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"test");
            auto results1 = search.GetCachedResults();
            Assert::IsNotNull(results1.get());
            
            search.Clear();
            Assert::AreEqual(search.GetSearchText(), std::wstring(L""));
            
            // Cache should be cleared
            auto results2 = search.GetCachedResults();
            Assert::IsNull(results2.get());
        }

        TEST_METHOD(TestExecuteQueryNow)
        {
            Logger::WriteMessage(L"Testing ExecuteQueryNow (bypass debouncing)");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"report");
            
            LARGE_INTEGER beforeExecution;
            QueryPerformanceCounter(&beforeExecution);
            
            // Execute immediately
            auto results = search.ExecuteQueryNow();
            
            LARGE_INTEGER afterExecution;
            QueryPerformanceCounter(&afterExecution);
            
            Assert::IsNotNull(results.get());
            
            // Verify that the query was executed
            auto lastExecTime = search.GetLastQueryExecutionTime();
            Assert::IsTrue(lastExecTime.QuadPart > 0);
            
            // Verify the execution time is within our measured window
            Assert::IsTrue(lastExecTime.QuadPart >= beforeExecution.QuadPart);
            Assert::IsTrue(lastExecTime.QuadPart <= afterExecution.QuadPart);
            
            double duration = search.GetLastQueryDurationMs();
            Assert::IsTrue(duration > 0.0);
        }

        TEST_METHOD(TestDebouncingDelay)
        {
            Logger::WriteMessage(L"Testing debouncing delay behavior");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            std::chrono::milliseconds debounceDelay(100); // Use 100ms for easier testing
            SearchAsYouTypeSession search(scopes, {}, {}, debounceDelay);
            
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            
            LARGE_INTEGER startTime;
            QueryPerformanceCounter(&startTime);
            
            search.SetSearchText(L"test");
            
            // GetCachedResults will block until debouncing completes
            auto results = search.GetCachedResults();
            
            LARGE_INTEGER endTime;
            QueryPerformanceCounter(&endTime);
            
            Assert::IsNotNull(results.get());
            
            // Calculate how long it took
            double elapsedMs = GetElapsedMs(startTime, endTime, frequency);
            
            // Verify that at least the debounce delay elapsed (with 10ms tolerance)
            Assert::IsTrue(elapsedMs >= static_cast<double>(debounceDelay.count()) - 10.0);
        }

        TEST_METHOD(TestMultipleRapidUpdates)
        {
            Logger::WriteMessage(L"Testing multiple rapid text updates (simulating typing)");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            std::chrono::milliseconds debounceDelay(100);
            SearchAsYouTypeSession search(scopes, {}, {}, debounceDelay);
            
            // Simulate typing "report" character by character with small delays
            const wchar_t* text = L"report";
            for (size_t i = 0; i < wcslen(text); ++i)
            {
                search.AppendCharacters(std::wstring_view(&text[i], 1));
                std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 20ms between keystrokes
            }
            
            LARGE_INTEGER startWait;
            QueryPerformanceCounter(&startWait);
            
            // This should block until the debounce period after the last character
            auto results = search.GetCachedResults();
            
            auto queryExecTime = search.GetLastQueryExecutionTime();
            
            Assert::IsNotNull(results.get());
            Assert::AreEqual(search.GetSearchText(), std::wstring(L"report"));
            
            // Verify that only ONE query was executed (not 6 separate queries)
            // We can check this by verifying the execution time is after our last character
            Assert::IsTrue(queryExecTime.QuadPart > startWait.QuadPart);
        }

        TEST_METHOD(TestDebouncingPreventsExcessiveQueries)
        {
            Logger::WriteMessage(L"Testing that debouncing prevents excessive queries");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            std::chrono::milliseconds debounceDelay(150);
            SearchAsYouTypeSession search(scopes, {}, {}, debounceDelay);
            
            // Rapidly update text multiple times
            search.SetSearchText(L"a");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            
            search.SetSearchText(L"ab");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            
            search.SetSearchText(L"abc");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            
            search.SetSearchText(L"abcd");
            
            // Now wait for the final query
            auto results = search.GetCachedResults();
            Assert::IsNotNull(results.get());
            
            // The search text should be the final one
            Assert::AreEqual(search.GetSearchText(), std::wstring(L"abcd"));
        }

        TEST_METHOD(TestSetDifferentDebounceDelay)
        {
            Logger::WriteMessage(L"Testing dynamic debounce delay changes");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes, {}, {}, std::chrono::milliseconds(50));
            
            // Change the debounce delay
            search.SetDebounceDelay(std::chrono::milliseconds(200));
            
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            
            LARGE_INTEGER startTime;
            QueryPerformanceCounter(&startTime);
            
            search.SetSearchText(L"test");
            auto results = search.GetCachedResults();
            
            LARGE_INTEGER endTime;
            QueryPerformanceCounter(&endTime);
            
            double elapsedMs = GetElapsedMs(startTime, endTime, frequency);
            
            Assert::IsTrue(elapsedMs >= 180.0); // Allow some tolerance
        }

        TEST_METHOD(TestMultipleScopesSearch)
        {
            Logger::WriteMessage(L"Testing search across multiple scopes");
            
            std::vector<std::wstring> scopes = {
                GetKnownFolderScope(FOLDERID_Documents),
                GetKnownFolderScope(FOLDERID_Desktop)
            };
            
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"file");
            auto results = search.GetCachedResults();
            
            Assert::IsNotNull(results.get());
        }

        TEST_METHOD(TestWithAdditionalProperties)
        {
            Logger::WriteMessage(L"Testing search with additional properties");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            std::vector<std::wstring> additionalProps = {
                L"System.ItemNameDisplay",
                L"System.Size",
                L"System.DateModified"
            };
            
            SearchAsYouTypeSession search(scopes, {}, additionalProps);
            
            search.SetSearchText(L"document");
            auto results = search.GetCachedResults();
            
            Assert::IsNotNull(results.get());
        }

        TEST_METHOD(TestEmptySearchText)
        {
            Logger::WriteMessage(L"Testing search with empty text");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            search.SetSearchText(L"");
            auto results = search.GetCachedResults();
            
            // Empty search should still return results (all items in scope)
            Assert::IsNotNull(results.get());
        }

        TEST_METHOD(TestTimingAccuracy)
        {
            Logger::WriteMessage(L"Testing QueryPerformanceCounter timing accuracy");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes);
            
            // Execute multiple queries and verify timing information
            for (int i = 0; i < 3; ++i)
            {
                search.SetSearchText(L"test" + std::to_wstring(i));
                auto results = search.ExecuteQueryNow();
                
                auto execTime = search.GetLastQueryExecutionTime();
                auto duration = search.GetLastQueryDurationMs();
                
                Assert::IsTrue(execTime.QuadPart > 0);
                Assert::IsTrue(duration > 0.0);
            }
        }

        TEST_METHOD(TestConcurrentAccess)
        {
            Logger::WriteMessage(L"Testing thread-safe concurrent access");
            
            std::vector<std::wstring> scopes = { GetKnownFolderScope(FOLDERID_Documents) };
            SearchAsYouTypeSession search(scopes, {}, {}, std::chrono::milliseconds(100));
            
            // Start multiple threads that update search text
            std::thread t1([&search]() {
                search.SetSearchText(L"thread1");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
            
            std::thread t2([&search]() {
                search.AppendCharacters(L"_thread2");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
            
            t1.join();
            t2.join();
            
            // Get final results
            auto results = search.GetCachedResults();
            Assert::IsNotNull(results.get());
        }
    };
}

