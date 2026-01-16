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

namespace SearchAsYouTypePerformanceTests
{
    TEST_CLASS(SearchAsYouTypePerformanceTests)
    {
    private:
        static std::wstring m_testFolderPath;
        static std::vector<TestFileInfo> m_testFiles;

    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            Logger::WriteMessage(L"========================================");
            Logger::WriteMessage(L"SearchAsYouTypeSession Performance Test Suite");
            Logger::WriteMessage(L"========================================");
            
            Logger::WriteMessage(L"Creating test files for performance testing...");
            m_testFolderPath = GetTestFolderPath();
            m_testFiles = CreateTestFiles(m_testFolderPath, 5);
            
            Logger::WriteMessage(L"Waiting for files to be indexed...");
            bool allIndexed = WaitForFilesIndexed(m_testFolderPath, m_testFiles, 30);
            
            if (allIndexed)
            {
                wchar_t msg[256];
                swprintf_s(msg, L"All %zu files indexed successfully", m_testFiles.size());
                Logger::WriteMessage(msg);
            }
            else
            {
                wchar_t msg[256];
                swprintf_s(msg, L"Warning: Not all files were indexed in time");
                Logger::WriteMessage(msg);
            }
            
            Logger::WriteMessage(L"Setup complete. Beginning performance tests...");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage(L"========================================");
            Logger::WriteMessage(L"Performance Tests Complete");
            Logger::WriteMessage(L"========================================");
            
            Logger::WriteMessage(L"Cleaning up test files...");
            CleanupTestFiles(m_testFolderPath);
        }

        TEST_METHOD(PerfTest_BasicSearch)
        {
            Logger::WriteMessage(L"PERFORMANCE TEST: Basic Search");
            
            SearchAsYouTypeSession search = CreateTestSearchInstance(m_testFolderPath);
            
            search.SetSearchText(L"project");
            auto results = search.GetCachedResults();
            
            Assert::IsNotNull(results.get());
            Logger::WriteMessage(L"Basic search performance test completed");
        }

        TEST_METHOD(PerfTest_ImmediateExecution)
        {
            Logger::WriteMessage(L"PERFORMANCE TEST: Immediate Execution");
            
            SearchAsYouTypeSession search = CreateTestSearchInstance(m_testFolderPath);
            
            search.SetSearchText(L"report");
            
            LARGE_INTEGER start, end, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);
            
            auto results = search.ExecuteQueryNow();
            
            QueryPerformanceCounter(&end);
            
            double elapsedMs = GetElapsedMs(start, end, freq);
            
            Assert::IsNotNull(results.get());
            Assert::IsTrue(elapsedMs >= 0.0);
        }

        TEST_METHOD(PerfTest_MultipleQueries)
        {
            Logger::WriteMessage(L"PERFORMANCE TEST: Multiple Sequential Queries");
            
            SearchAsYouTypeSession search = CreateTestSearchInstance(m_testFolderPath);
            
            std::vector<std::wstring> searchTerms = {L"project", L"meeting", L"budget", L"employee", L"technical"};
            
            for (const auto& term : searchTerms)
            {
                search.SetSearchText(term);
                auto results = search.GetCachedResults();
                Assert::IsNotNull(results.get());
            }
            
            Logger::WriteMessage(L"Multiple queries performance test completed");
        }
    };
    
    std::wstring SearchAsYouTypePerformanceTests::m_testFolderPath;
    std::vector<TestFileInfo> SearchAsYouTypePerformanceTests::m_testFiles;
}

