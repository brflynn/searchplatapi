// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include "SearchPlatCore.h"

namespace wsearch
{

 /* This class is the main class to use to search the system for files based on an input string
 *  It is biased to provide a high performance use of the indexer SQL interface, making it hard to misuse and get slow query results
 */
struct SearchProvider : public TelemetryProvider
{
    SearchProvider(std::vector<std::wstring> includedScopes, std::vector<std::wstring> excludedScopes, std::vector<std::wstring> additionalProperties) :
        m_includedScopes(std::move(includedScopes)),
        m_excludedScopes(std::move(excludedScopes)),
        m_additionalProperties(std::move(additionalProperties))
    {
        TraceLoggingInfo(L"Creating SearchProvider");
        TraceLoggingInfo(L"  Included scopes: %zu", m_includedScopes.size());
        TraceLoggingInfo(L"  Excluded scopes: %zu", m_excludedScopes.size());
        TraceLoggingInfo(L"  Additional properties: %zu", m_additionalProperties.size());
    }

    ~SearchProvider()
    {
        TraceLoggingInfo(L"Destroying SearchProvider");
    }

public:

    /* Prepare for search initializes the provider with a starting rowset, minimizing
     *  the amount of decoding and decompression of the indexes needed on a query with an input string
     *  Included scopes are file paths that you wish to include. If none are included it defaults to searching all files
     *  Excluded scopes are file paths that you do not want to include in the query. If none are included then only the
     * includedScopes are used Additional properties are system properties to include in the SELECT clause beyond System.ItemUrl
     *  Creates the priming rowset query.
     *
     *  Details:
     *  Typically this is done when a user in an application interacts with a search box experience.
     *  When the user clicks the box, you want to tell the system indexer "hey a query is coming"
     *  The query should contain basic information about the scope of the data you want to search over.
     *
     *  When the user starts typing, the developer can use the priming rowset and update it with more information. This avoids
     * index decoding per query instance and is the optimal way to issue queries using OLEDB/SQL
     *
     *  Calling this method a second time will deallocate the previous priming rowset, and do the work to create one again
     */
    inline void PrepareForSearch()
    {
        TraceLoggingInfo(L"Preparing for search");
        m_prefetchSql = details::BuildPrimingSqlFromScopes(m_includedScopes, m_excludedScopes, m_additionalProperties);
        m_prefetchRowset = details::ExecuteQuery(m_prefetchSql);

        // After we get the rowset, prioritize the scopes
        winrt::com_ptr<IRowsetPrioritization> rowsetPrioritization = m_prefetchRowset.as<IRowsetPrioritization>();
        rowsetPrioritization->SetScopePriority(PRIORITY_LEVEL_FOREGROUND, 100);

        TraceLoggingInfo(L"Search preparation complete");
    }

    inline winrt::com_ptr<IRowset> Search(std::wstring_view const& searchText)
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
    inline winrt::com_ptr<IRowset> ExecuteQueryUsingPrimingQuery(std::wstring_view const& searchText)
    {
        TraceLoggingInfo(L"ExecuteQueryUsingPrimingQuery called");

        if (!m_prefetchRowset)
        {
            TraceLoggingInfo(L"No priming rowset found, preparing for search");
            PrepareForSearch();
        }

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
    winrt::com_ptr<IRowset> m_prefetchRowset;
    std::wstring m_prefetchSql;
    std::vector<std::wstring> m_includedScopes;
    std::vector<std::wstring> m_excludedScopes;
    std::vector<std::wstring> m_additionalProperties;
};

} // namespace wsearch
