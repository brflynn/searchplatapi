// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include "SearchPlatCore.h"
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <propkey.h>
#include <propsys.h>

namespace wsearch
{

/* Helper class to update file properties when search results are clicked
 * 
 * This class manages a background thread that updates file properties:
 * - System.DateAccessed: Set to current time when clicked
 * - System.Document.LineCount: Incremented as a "launch count" metric
 * 
 * Usage:
 *   wsearch::SearchResultPropertyUpdater updater;
 *   updater.OnResultClicked(L"C:\\Users\\Documents\\file.txt");
 *   // Properties are updated asynchronously on background thread
 */
class SearchResultPropertyUpdater
{
public:
    SearchResultPropertyUpdater()
        : m_shouldStop(false)
        , m_workerThread(&SearchResultPropertyUpdater::WorkerThreadProc, this)
    {
        TelemetryProvider::LogInfo(L"SearchResultPropertyUpdater initialized");
    }

    ~SearchResultPropertyUpdater()
    {
        TelemetryProvider::LogInfo(L"Shutting down SearchResultPropertyUpdater");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shouldStop = true;
        }
        m_cv.notify_one();
        
        if (m_workerThread.joinable())
        {
            m_workerThread.join();
        }
    }

    // Non-copyable
    SearchResultPropertyUpdater(const SearchResultPropertyUpdater&) = delete;
    SearchResultPropertyUpdater& operator=(const SearchResultPropertyUpdater&) = delete;

    // Called when a user clicks on a search result
    // Queues the file path for property updates on the background thread
    void OnResultClicked(const std::wstring& filePath)
    {
        TelemetryProvider::LogInfo(L"Result clicked: %ls", filePath.c_str());
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_updateQueue.push(filePath);
        }
        m_cv.notify_one();
    }

    // Get the number of pending property updates
    size_t GetPendingUpdateCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_updateQueue.size();
    }

private:
    void WorkerThreadProc()
    {
        TelemetryProvider::LogInfo(L"Property updater worker thread started");
        
        // Initialize COM for this thread
        auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
        
        while (true)
        {
            std::wstring filePath;
            
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_updateQueue.empty() || m_shouldStop; });
                
                if (m_shouldStop)
                {
                    TelemetryProvider::LogInfo(L"Worker thread stopping");
                    break;
                }
                
                if (!m_updateQueue.empty())
                {
                    filePath = m_updateQueue.front();
                    m_updateQueue.pop();
                }
            }
            
            if (!filePath.empty())
            {
                try
                {
                    UpdateFileProperties(filePath);
                }
                catch (...)
                {
                    TelemetryProvider::LogError(L"Failed to update properties for: %ls", filePath.c_str());
                    // Continue processing other items even if one fails
                }
            }
        }
        
        TelemetryProvider::LogInfo(L"Property updater worker thread exited");
    }

    void UpdateFileProperties(const std::wstring& filePath)
    {
        TelemetryProvider::LogInfo(L"Updating properties for: %ls", filePath.c_str());
        
        // Open the file's property store for writing
        winrt::com_ptr<IPropertyStore> propStore;
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            filePath.c_str(),
            nullptr,
            GPS_READWRITE,
            IID_PPV_ARGS(propStore.put()));
        
        if (FAILED(hr))
        {
            TelemetryProvider::LogError(L"Failed to open property store (0x%08X): %ls", hr, filePath.c_str());
            THROW_IF_FAILED(hr);
        }

        // Update System.DateAccessed to current time
        SYSTEMTIME systemTime;
        GetSystemTime(&systemTime);
        
        FILETIME fileTime;
        if (SystemTimeToFileTime(&systemTime, &fileTime))
        {
            PROPVARIANT propVarDate;
            PropVariantInit(&propVarDate);
            propVarDate.vt = VT_FILETIME;
            propVarDate.filetime = fileTime;
            
            hr = propStore->SetValue(PKEY_DateAccessed, propVarDate);
            if (SUCCEEDED(hr))
            {
                TelemetryProvider::LogInfo(L"Updated System.DateAccessed");
            }
            else
            {
                TelemetryProvider::LogError(L"Failed to set DateAccessed (0x%08X)", hr);
            }
            
            PropVariantClear(&propVarDate);
        }

        // Increment System.Document.LineCount (repurposed as launch count)
        PROPVARIANT propVarLineCount;
        PropVariantInit(&propVarLineCount);
        
        // Try to read current value
        hr = propStore->GetValue(PKEY_Document_LineCount, &propVarLineCount);
        
        LONG currentCount = 0;
        if (SUCCEEDED(hr) && propVarLineCount.vt == VT_I4)
        {
            currentCount = propVarLineCount.lVal;
        }
        PropVariantClear(&propVarLineCount);
        
        // Increment the count
        LONG newCount = currentCount + 1;
        
        // Set the new value
        PropVariantInit(&propVarLineCount);
        propVarLineCount.vt = VT_I4;
        propVarLineCount.lVal = newCount;
        
        hr = propStore->SetValue(PKEY_Document_LineCount, propVarLineCount);
        if (SUCCEEDED(hr))
        {
            TelemetryProvider::LogInfo(L"Updated System.Document.LineCount: %ld -> %ld", currentCount, newCount);
        }
        else
        {
            TelemetryProvider::LogError(L"Failed to set Document.LineCount (0x%08X)", hr);
        }
        
        PropVariantClear(&propVarLineCount);

        // Commit the changes
        hr = propStore->Commit();
        if (SUCCEEDED(hr))
        {
            TelemetryProvider::LogInfo(L"Successfully committed property changes for: %ls", filePath.c_str());
        }
        else
        {
            TelemetryProvider::LogError(L"Failed to commit property changes (0x%08X): %ls", hr, filePath.c_str());
            THROW_IF_FAILED(hr);
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_workerThread;
    std::queue<std::wstring> m_updateQueue;
    std::atomic<bool> m_shouldStop;
};

} // namespace wsearch

