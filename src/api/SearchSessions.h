// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include "SearchPlatCore.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace wsearch
{

/* Base class for all search sessions with common functionality
 * Handles scope management and shared search operations
 */
class SearchSessionBase : public TelemetryProvider
{
protected:
    std::vector<std::wstring> m_includedScopes;
    std::vector<std::wstring> m_excludedScopes;
    std::vector<std::wstring> m_additionalProperties;
    
    // Cached priming rowset for query optimization
    mutable winrt::com_ptr<IRowset> m_prefetchRowset;
    mutable std::wstring m_prefetchSql;

    SearchSessionBase(
        std::vector<std::wstring> includedScopes,
        std::vector<std::wstring> excludedScopes,
        std::vector<std::wstring> additionalProperties)
        : m_includedScopes(std::move(includedScopes))
        , m_excludedScopes(std::move(excludedScopes))
        , m_additionalProperties(std::move(additionalProperties))
    {
    }

    virtual ~SearchSessionBase() = default;

    // Build priming SQL from scopes
    std::wstring BuildPrimingSql() const
    {
        return details::BuildPrimingSqlFromScopes(m_includedScopes, m_excludedScopes, m_additionalProperties);
    }

    /* Prepare for search initializes the provider with a starting rowset, minimizing
     * the amount of decoding and decompression of the indexes needed on a query with an input string
     * Creates the priming rowset query and caches it for reuse.
     *
     * Details:
     * This is typically called during construction to tell the system indexer "hey a query is coming"
     * The query contains basic information about the scope of the data you want to search over.
     * When executing queries, the priming rowset is reused to avoid index decoding per query instance.
     * This is the optimal way to issue queries using OLEDB/SQL.
     */
    void PrepareForSearch()
    {
        TelemetryProvider::LogInfo(L"Preparing for search");
        m_prefetchSql = BuildPrimingSql();
        m_prefetchRowset = details::ExecuteQuery(m_prefetchSql);

        // After we get the rowset, prioritize the scopes
        winrt::com_ptr<IRowsetPrioritization> rowsetPrioritization = m_prefetchRowset.as<IRowsetPrioritization>();
        rowsetPrioritization->SetScopePriority(PRIORITY_LEVEL_FOREGROUND, 100);

        TelemetryProvider::LogInfo(L"Search preparation complete");
    }

    // Execute a search query with priming optimization using cached rowset
    winrt::com_ptr<IRowset> ExecuteSearchWithPriming(const std::wstring& searchText) const
    {
        // Use cached priming rowset if available
        DWORD whereId;
        if (m_prefetchRowset)
        {
            whereId = details::GetReuseWhereIDFromRowset(m_prefetchRowset);
        }
        else
        {
            // Fallback: create priming rowset on-the-fly
            std::wstring primingSql = BuildPrimingSql();
            auto primingRowset = details::ExecuteQuery(primingSql);
            whereId = details::GetReuseWhereIDFromRowset(primingRowset);
        }

        // Build the search WHERE clause
        std::wstring whereClause = details::BuildSearchWhereClause(searchText);

        // Build the final query
        std::wstring finalSql = L"SELECT System.ItemUrl";
        for (const auto& prop : m_additionalProperties)
        {
            finalSql += L", " + prop;
        }
        finalSql += L" FROM SystemIndex WHERE REUSEWHERE(" + std::to_wstring(whereId) + L")";
        finalSql += whereClause;
        finalSql += L" ORDER BY System.Search.Rank DESC";

        // Execute and return the search query
        return details::ExecuteQuery(finalSql);
    }

public:
    // Get total files in the index
    size_t GetTotalFilesInIndex() const
    {
        TelemetryProvider::LogInfo(L"Getting total files in index");
        std::wstring sql(L"SELECT System.ItemUrl FROM SystemIndex WHERE SCOPE='file:'");
        auto rowset = details::ExecuteQuery(sql);
        return details::GetTotalRowsForRowset(rowset);
    }
};

/* Simple synchronous search session for file searches
 * Provides high performance use of the indexer SQL interface with priming rowset caching
 */
struct SearchSession : public SearchSessionBase
{
    SearchSession(
        std::vector<std::wstring> includedScopes,
        std::vector<std::wstring> excludedScopes = {},
        std::vector<std::wstring> additionalProperties = {})
        : SearchSessionBase(std::move(includedScopes), std::move(excludedScopes), std::move(additionalProperties))
    {
        TraceLoggingInfo(L"Creating SearchSession");
        TraceLoggingInfo(L"  Included scopes: %zu", m_includedScopes.size());
        TraceLoggingInfo(L"  Excluded scopes: %zu", m_excludedScopes.size());
        TraceLoggingInfo(L"  Additional properties: %zu", m_additionalProperties.size());
        
        // Prepare for search immediately if we have scopes
        if (!m_includedScopes.empty())
        {
            PrepareForSearch();
        }
    }

    ~SearchSession()
    {
        TraceLoggingInfo(L"Destroying SearchSession");
    }

public:

winrt::com_ptr<IRowset> Search(std::wstring_view const& searchText)
    {
        TraceLoggingInfo(L"Search called with text: %ls", searchText.data());
        return ExecuteQueryUsingPrimingQuery(searchText);
    }

    /* Executes searching the entire system index with a string using the priming query as the base IRowset
     *
     *  The initial priming query if not run prior will be run during this method call.
     *  This method then takes in a string and searches across that query using enhanced content matching:
     *  - CONTAINS on System.ItemNameDisplay with high ranking (999) for filename matches
     *  - FREETEXT for broader content search across all indexed text
     *
     */
    winrt::com_ptr<IRowset> ExecuteQueryUsingPrimingQuery(std::wstring_view const& searchText)
    {
        TraceLoggingInfo(L"ExecuteQueryUsingPrimingQuery called");

        auto reuseWhereId = details::GetReuseWhereIDFromRowset(m_prefetchRowset);

        // Build enhanced search query
        std::wstring querySql = m_prefetchSql;

        // Add search WHERE clause if search text is provided
        std::wstring whereClause = details::BuildSearchWhereClause(searchText);
        if (!whereClause.empty())
        {
            querySql += whereClause;
        }

        // Add REUSEWHERE clause
        querySql += L" AND REUSEWHERE(" + std::to_wstring(reuseWhereId) + L")";

        // Add ORDER BY clause to rank results by relevance
        querySql += L" ORDER BY System.Search.Rank DESC";

        TraceLoggingInfo(L"Executing search query with REUSEWHERE and ORDER BY rank");
        return details::ExecuteQuery(querySql);
    }

    inline size_t GetTotalFilesInIndex()
    {
        TraceLoggingInfo(L"Getting total files in index");
        std::wstring sql(L"SELECT System.ItemUrl FROM SystemIndex WHERE SCOPE='file:'");
        auto rowset = details::ExecuteQuery(sql);

        return details::GetTotalRowsForRowset(rowset);
    }

private:
};

/* Search-as-you-type session with debouncing functionality
 * Enables efficient search-as-you-type with automatic debouncing
 * to minimize unnecessary queries to the Windows Search Index.
 *
 * Features:
 * - Automatic debouncing with configurable delay (default 50ms)
 * - Background thread for query execution
 * - Cached rowset results for quick retrieval
 * - Thread-safe operations
 *
 * Example usage:
 *   wsearch::SearchAsYouTypeSession search({L"C:\\Users\\username\\Documents"});
 *   search.SetSearchText(L"report");
 *   search.AppendCharacters(L"s");
 *   auto results = search.GetCachedResults(); // May be null if debouncing
 *   auto results = search.ExecuteQueryNow();  // Force immediate execution
 */
struct SearchAsYouTypeSession : public SearchSessionBase
{
public:
    // Constructor
    // includedScopes: List of file paths to search within
    // excludedScopes: List of file paths to exclude from search
    // additionalProperties: Additional properties to include in SELECT clause (e.g., System.ItemNameDisplay)
    // debounceDelay: Time to wait after last text change before executing query (default 50ms)
    SearchAsYouTypeSession(
        std::vector<std::wstring> includedScopes,
        std::vector<std::wstring> excludedScopes = {},
        std::vector<std::wstring> additionalProperties = {},
        std::chrono::milliseconds debounceDelay = std::chrono::milliseconds(50))
        : SearchSessionBase(std::move(includedScopes), std::move(excludedScopes), std::move(additionalProperties))
        , m_debounceDelay(debounceDelay)
        , m_shouldStop(false)
        , m_queryPending(false)
    {
        TelemetryProvider::LogInfo(L"Initializing SearchAsYouTypeSession with debounce delay: %lld ms", debounceDelay.count());
        
        m_lastQueryExecutionTime.QuadPart = 0;
        m_lastQueryDurationMs = 0.0;
        
        // Get the performance counter frequency for timing conversions
        QueryPerformanceFrequency(&m_performanceFrequency);
        
        // Prepare for search immediately if we have scopes
        if (!m_includedScopes.empty())
        {
            PrepareForSearch();
        }
        
        m_debounceThread = std::thread(&SearchAsYouTypeSession::DebounceThreadProc, this);
    }

    ~SearchAsYouTypeSession()
    {
        TelemetryProvider::LogInfo(L"Shutting down SearchAsYouTypeSession");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shouldStop = true;
        }
        m_cv.notify_one();
        
        if (m_debounceThread.joinable())
        {
            m_debounceThread.join();
        }
    }

    // Non-copyable
    SearchAsYouTypeSession(const SearchAsYouTypeSession&) = delete;
    SearchAsYouTypeSession& operator=(const SearchAsYouTypeSession&) = delete;

    // Append characters to the current search text and trigger debouncing
    void AppendCharacters(std::wstring_view characters)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_searchText += characters;
        m_lastUpdateTime = std::chrono::steady_clock::now();
        m_queryPending = true;
        
        TelemetryProvider::LogInfo(L"Search text updated: %ls", m_searchText.c_str());
        m_cv.notify_one();
    }

    // Set the complete search text (replaces existing text) and trigger debouncing
    void SetSearchText(std::wstring_view searchText)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_searchText = searchText;
        m_lastUpdateTime = std::chrono::steady_clock::now();
        m_queryPending = true;
        
        TelemetryProvider::LogInfo(L"Search text set to: %ls", m_searchText.c_str());
        m_cv.notify_one();
    }

    // Clear the search text and cached results
    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_searchText.clear();
        m_cachedRowset = nullptr;
        m_queryPending = false;
        
        TelemetryProvider::LogInfo(L"Search text and cache cleared");
    }

    // Get the cached rowset results
    // This method blocks and waits for the debouncing delay to complete if a query is pending
    winrt::com_ptr<IRowset> GetCachedResults()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        // Wait for any pending query to complete
        if (m_queryPending)
        {
            TelemetryProvider::LogInfo(L"GetCachedResults: waiting for pending query to complete");
            m_queryCompletedCv.wait(lock, [this] { return !m_queryPending; });
        }
        
        return m_cachedRowset;
    }

    // Force immediate query execution and wait for results
    // This bypasses the debounce delay and returns the fresh results
    winrt::com_ptr<IRowset> ExecuteQueryNow()
    {
        std::wstring searchText;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            searchText = m_searchText;
            m_queryPending = false; // Cancel any pending debounced query
        }

        TelemetryProvider::LogInfo(L"Executing immediate query for: %ls", searchText.c_str());
        auto rowset = ExecuteTimedSearchQuery(searchText);
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cachedRowset = rowset;
        }

        return rowset;
    }

    // Check if a query is currently pending (waiting for debounce delay)
    bool IsQueryPending() const
    {
        return m_queryPending;
    }

    // Get the current search text
    std::wstring GetSearchText() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_searchText;
    }

    // Get the timestamp (in performance counter ticks) of the last query execution
    // Returns 0 if no query has been executed yet
    LARGE_INTEGER GetLastQueryExecutionTime() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastQueryExecutionTime;
    }

    // Get the duration (in milliseconds) of the last query execution
    // Returns 0.0 if no query has been executed yet
    double GetLastQueryDurationMs() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastQueryDurationMs;
    }

    // Set a new debounce delay
    void SetDebounceDelay(std::chrono::milliseconds delay)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_debounceDelay = delay;
        TelemetryProvider::LogInfo(L"Debounce delay set to: %lld ms", delay.count());
    }

private:
    void DebounceThreadProc()
    {
        TelemetryProvider::LogInfo(L"Debounce thread started");

        while (true)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            
            // Wait for a signal or until we should stop
            m_cv.wait(lock, [this] { return m_queryPending || m_shouldStop; });

            if (m_shouldStop)
            {
                TelemetryProvider::LogInfo(L"Debounce thread stopping");
                break;
            }

            if (m_queryPending)
            {
                auto timeSinceLastUpdate = std::chrono::steady_clock::now() - m_lastUpdateTime;
                
                if (timeSinceLastUpdate >= m_debounceDelay)
                {
                    // Enough time has passed, execute the query
                    std::wstring searchText = m_searchText;
                    lock.unlock(); // Unlock before executing query

                    TelemetryProvider::LogInfo(L"Debounce period elapsed, executing query for: %ls", searchText.c_str());
                    
                    try
                    {
                        auto rowset = ExecuteTimedSearchQuery(searchText);
                        
                        lock.lock();
                        m_cachedRowset = rowset;
                        m_queryPending = false;
                        TelemetryProvider::LogInfo(L"Query executed successfully, results cached");
                        
                        // Notify any threads waiting in GetCachedResults()
                        m_queryCompletedCv.notify_all();
                    }
                    catch (...)
                    {
                        TelemetryProvider::LogInfo(L"Query execution failed");
                        lock.lock();
                        m_cachedRowset = nullptr;
                        m_queryPending = false;
                        
                        // Notify even on failure
                        m_queryCompletedCv.notify_all();
                    }
                }
                else
                {
                    // Not enough time has passed, wait for the remaining time
                    auto remainingTime = m_debounceDelay - timeSinceLastUpdate;
                    m_cv.wait_for(lock, remainingTime);
                }
            }
        }

        TelemetryProvider::LogInfo(L"Debounce thread exited");
    }

    winrt::com_ptr<IRowset> ExecuteTimedSearchQuery(const std::wstring& searchText)
    {
        LARGE_INTEGER startTime, endTime;
        QueryPerformanceCounter(&startTime);
        
        TelemetryProvider::LogInfo(L"[TIMING] Query execution started at: %lld", startTime.QuadPart);
        
        // Use base class method to execute search with priming
        auto result = ExecuteSearchWithPriming(searchText);
        
        QueryPerformanceCounter(&endTime);
        
        // Calculate duration in milliseconds
        double durationMs = (static_cast<double>(endTime.QuadPart - startTime.QuadPart) * 1000.0) / 
                            static_cast<double>(m_performanceFrequency.QuadPart);
        
        // Store timing information
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastQueryExecutionTime = startTime;
            m_lastQueryDurationMs = durationMs;
        }
        
        TelemetryProvider::LogInfo(L"[TIMING] Query execution completed at: %lld (duration: %.3f ms)", 
                                endTime.QuadPart, durationMs);
        
        return result;
    }

    std::chrono::milliseconds m_debounceDelay;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_queryCompletedCv;
    std::thread m_debounceThread;
    
    std::wstring m_searchText;
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    std::atomic<bool> m_shouldStop;
    std::atomic<bool> m_queryPending;
    
    winrt::com_ptr<IRowset> m_cachedRowset;
    
    // Timing information
    LARGE_INTEGER m_performanceFrequency;
    LARGE_INTEGER m_lastQueryExecutionTime;
    double m_lastQueryDurationMs;
};

} // namespace wsearch
