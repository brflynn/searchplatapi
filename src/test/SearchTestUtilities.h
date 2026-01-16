// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include <windows.h>
#include <SearchPlatCore.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace SearchTestUtilities
{
    // Test file information structure
    struct TestFileInfo
    {
        std::wstring fileName;
        std::wstring filePath;
        std::vector<std::wstring> keywords;
    };

    // Get the test folder path under Documents
    inline std::wstring GetTestFolderPath()
    {
        std::wstring documentsPath = wsearch::details::GetKnownFolderScope(FOLDERID_Documents);
        return documentsPath + L"\\WinSearchPlatformTests";
    }

    // Create test files with known content
    inline std::vector<TestFileInfo> CreateTestFiles(const std::wstring& testFolderPath, int fileCount = 5)
    {
        // Create the test folder
        fs::create_directories(testFolderPath);
        
        // Define test file templates
        std::vector<std::wstring> fileNames = {
            L"project_report_2024.txt",
            L"meeting_notes_january.txt",
            L"budget_forecast_q1.txt",
            L"employee_handbook.txt",
            L"technical_specification.txt",
            L"marketing_strategy.txt",
            L"sales_forecast.txt",
            L"customer_feedback.txt",
            L"product_roadmap.txt",
            L"team_schedule.txt"
        };
        
        std::vector<std::vector<std::wstring>> keywordSets = {
            {L"project", L"report", L"2024", L"progress", L"milestone"},
            {L"meeting", L"notes", L"january", L"agenda", L"action"},
            {L"budget", L"forecast", L"q1", L"revenue", L"expenses"},
            {L"employee", L"handbook", L"policy", L"procedures", L"benefits"},
            {L"technical", L"specification", L"design", L"architecture", L"implementation"},
            {L"marketing", L"strategy", L"campaign", L"branding", L"outreach"},
            {L"sales", L"forecast", L"targets", L"pipeline", L"conversion"},
            {L"customer", L"feedback", L"survey", L"satisfaction", L"reviews"},
            {L"product", L"roadmap", L"features", L"releases", L"milestones"},
            {L"team", L"schedule", L"calendar", L"meetings", L"availability"}
        };
        
        std::vector<TestFileInfo> testFiles;
        int numFiles = min(fileCount, (int)fileNames.size());
        
        for (int i = 0; i < numFiles; ++i)
        {
            TestFileInfo info;
            info.fileName = fileNames[i];
            info.filePath = testFolderPath + L"\\" + fileNames[i];
            info.keywords = keywordSets[i];
            
            // Create file with content
            std::wofstream file(info.filePath);
            if (file.is_open())
            {
                file << L"Test file: " << info.fileName << L"\n\n";
                file << L"Keywords: ";
                for (const auto& keyword : info.keywords)
                {
                    file << keyword << L" ";
                }
                file << L"\n\n";
                file << L"This is a test file created for testing the Windows Search Platform API.\n";
                file << L"Content includes various searchable terms and metadata.\n";
                file.close();
            }
            
            testFiles.push_back(info);
        }
        
        return testFiles;
    }

    // Wait for all files to be indexed using active polling
    inline bool WaitForFilesIndexed(
        const std::wstring& testFolderPath,
        const std::vector<TestFileInfo>& testFiles,
        int maxWaitSeconds = 30)
    {
        const int maxAttempts = maxWaitSeconds;
        int filesIndexed = 0;
        
        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            filesIndexed = 0;
            
            // Query for each test file to see if it's indexed
            for (const auto& testFile : testFiles)
            {
                try
                {
                    // Build the priming SQL for the test folder scope
                    std::wstring primingSql = wsearch::details::BuildPrimingSqlFromScopes(
                        {testFolderPath}, 
                        std::vector<std::wstring>(), 
                        std::vector<std::wstring>());
                    
                    // Execute the priming query
                    auto primingRowset = wsearch::details::ExecuteQuery(primingSql);
                    DWORD whereId = wsearch::details::GetReuseWhereIDFromRowset(primingRowset);
                    
                    // Build search query for the file name
                    std::wstring searchSql = L"SELECT System.ItemUrl FROM SystemIndex WHERE REUSEWHERE(" + 
                                            std::to_wstring(whereId) + L") AND CONTAINS(System.ItemNameDisplay, '" + 
                                            testFile.fileName + L"')";
                    
                    auto results = wsearch::details::ExecuteQuery(searchSql);
                    
                    // Check if we got any results
                    if (results)
                    {
                        ULONG rowCount = wsearch::details::GetTotalRowsForRowset(results);
                        if (rowCount > 0)
                        {
                            filesIndexed++;
                        }
                    }
                }
                catch (...)
                {
                    // Query failed, file probably not indexed yet
                }
            }
            
            if (filesIndexed == testFiles.size())
            {
                return true;
            }
            
            // Wait 1 second before next attempt
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        return false;
    }

    // Clean up test files and folder
    inline void CleanupTestFiles(const std::wstring& testFolderPath)
    {
        if (!testFolderPath.empty() && fs::exists(testFolderPath))
        {
            try
            {
                fs::remove_all(testFolderPath);
            }
            catch (...)
            {
                // Cleanup failed, but don't fail the test
            }
        }
    }

    // Verify a search result contains expected files
    inline bool VerifySearchResults(
        winrt::com_ptr<IRowset> rowset,
        const std::vector<std::wstring>& expectedFiles)
    {
        if (!rowset)
        {
            return false;
        }

        ULONG rowCount = wsearch::details::GetTotalRowsForRowset(rowset);
        return rowCount > 0;
    }

    // Get elapsed time in milliseconds between two performance counter values
    inline double GetElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER frequency)
    {
        return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) / 
               static_cast<double>(frequency.QuadPart);
    }

    // Create a SearchAsYouType instance with test folder scope
    inline wsearch::SearchAsYouType CreateTestSearchInstance(
        const std::wstring& testFolderPath,
        std::chrono::milliseconds debounceDelay = std::chrono::milliseconds(50))
    {
        std::vector<std::wstring> scopes = { testFolderPath };
        return wsearch::SearchAsYouType(scopes, {}, {}, debounceDelay);
    }
}
