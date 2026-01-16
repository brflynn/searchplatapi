// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once
#ifndef SEARCHPLATAPI_H
#define SEARCHPLATAPI_H

/*
* Author: Brendan Flynn
* Date: 5/21/2025
*
* This file is a public API around the existing Windows Search Indexer platform APIs
*
* The goal is to abstract the complicated pieces into segements and functions a developer of a search
* application would most likely use
*
* This API set throws exceptions on failure.
* It is inlined, and only this header should be needed on Windows SDK builds starting with 17763.
*
* This header requires the Windows Implementation Library for resource and result macros, and other various shell helpers and property handlers
*
* Enjoy!
*/

#include <objbase.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <wil/result.h>
#include <wil/resource.h>
#include <functional>
#include <Unknwn.h>
#include <NTQuery.h>
#include <oledb.h>
#include <SearchAPI.h>
#include <shlobj.h>
#include <KnownFolders.h>
#include <intsafe.h>
#include <iostream>
#include <sstream>

#include "WSearchLogging.h"

/* Common Helpers
*
* These methods get access to the common interfaces exposed from the service
*/

/* MAIN SEARCH PROVIDER CLASSES and HELPERS
*
*  These classes assist in searching the system index in a handful of lines of code
*/
namespace wsearch
{

namespace details
{
    inline std::wstring BuildPrimingSqlFromScopes(
        const std::vector<std::wstring>& includedScopes,
        const std::vector<std::wstring>& excludedScopes,
        const std::vector<std::wstring>& additionalProperties)
    {
        wsearch::TelemetryProvider::LogInfo(L"Building priming SQL query");
        std::wstring queryStr(L"SELECT System.ItemUrl");

        // Add additional properties to SELECT clause
        for (const auto& prop : additionalProperties)
        {
            queryStr.append(L", ").append(prop);
        }

        queryStr += L" FROM SystemIndex WHERE";

        // build the included, and excluded scope lists
        for (size_t i = 0; i < includedScopes.size(); ++i)
        {
            std::wstring scope(includedScopes[i]);
            std::replace(scope.begin(), scope.end(), L'\\', L'/');
            if (i == 0)
            {
                queryStr += L" (";
            }

            queryStr += L" SCOPE='";

            // Does the scope start with a protocol?
            if (scope.find(L"file:") != 0)
            {
                queryStr += L"file:" + scope;
            }
            else
            {
                queryStr += scope;
            }

            if (i < (includedScopes.size() - 1))
            {
                queryStr += L"' OR";
            }
            else
            {
                queryStr += L"')";
            }
        }

        for (size_t i = 0; i < excludedScopes.size(); ++i)
        {
            std::wstring scope(excludedScopes[i]);
            std::replace(scope.begin(), scope.end(), L'\\', L'/');
            queryStr += L" SCOPE <> 'file:";
            queryStr += excludedScopes[i] + L'\'';
            if (i < (excludedScopes.size() - 1))
            {
                queryStr += L" AND";
            }
        }

        wsearch::TelemetryProvider::LogInfo(L"[QUERY] %ls", queryStr.c_str());
        return queryStr;
    }

    inline DWORD GetReuseWhereIDFromRowset(const winrt::com_ptr<IRowset>& rowset)
    {
        wsearch::TelemetryProvider::LogInfo(L"Getting REUSEWHERE ID from rowset");
        winrt::com_ptr<IRowsetInfo> rowsetInfo;
        THROW_IF_FAILED(rowset->QueryInterface(IID_PPV_ARGS(rowsetInfo.put())));

        DBPROPIDSET propset;
        DBPROPSET* prgPropSets;
        DBPROPID whereid = MSIDXSPROP_WHEREID;
        propset.rgPropertyIDs = &whereid;
        propset.cPropertyIDs = 1;

        propset.guidPropertySet = DBPROPSET_MSIDXS_ROWSETEXT;
        ULONG cPropertySets;

        THROW_IF_FAILED(rowsetInfo->GetProperties(1, &propset, &cPropertySets, &prgPropSets));

        wil::unique_cotaskmem_ptr<DBPROP> sprgProps(prgPropSets->rgProperties);
        wil::unique_cotaskmem_ptr<DBPROPSET> sprgPropSets(prgPropSets);

        DWORD whereId = prgPropSets->rgProperties->vValue.ulVal;
        wsearch::TelemetryProvider::LogInfo(L"REUSEWHERE ID: %lu", whereId);
        return whereId;
    }

    inline std::wstring BuildSearchWhereClause(std::wstring_view const& searchText)
    {
        wsearch::TelemetryProvider::LogInfo(L"Building search WHERE clause for: %ls", searchText.data());
        
        // Handle empty search text
        if (searchText.empty())
        {
            wsearch::TelemetryProvider::LogInfo(L"Empty search text, returning empty WHERE clause");
            return L""; // No additional WHERE clause for empty searches
        }
        
        // Escape any single quotes in the search text
        std::wstring escapedText{searchText};
        size_t pos = 0;
        while ((pos = escapedText.find(L"'", pos)) != std::wstring::npos)
        {
            escapedText.replace(pos, 1, L"''");
            pos += 2;
        }

        // Check if user already included quotes (exact phrase search)
        bool userQuoted = (escapedText.length() >= 2 && 
                          escapedText.front() == L'"' && 
                          escapedText.back() == L'"');
        
        // Check if search text contains spaces (multi-word phrase)
        bool hasSpaces = escapedText.find(L' ') != std::wstring::npos;
        
        std::wstring whereClause;
        if (userQuoted)
        {
            // User wants exact phrase search - use their quoted text as-is
            // Just do a single CONTAINS with their exact query
            whereClause = L" AND CONTAINS(*, '" + escapedText + L"')";
            wsearch::TelemetryProvider::LogInfo(L"User-quoted phrase: using exact match only");
        }
        else if (hasSpaces)
        {
            // For multi-word phrases:
            // 1. Exact phrase match with highest rank (999)
            // 2. Multi-word prefix match with rank 998
            // 3. Prefix matches on individual words (AND logic)
            
            // Split into individual words for prefix matching
            std::vector<std::wstring> wordList;
            std::wstring words = escapedText;
            size_t start = 0;
            size_t end = words.find(L' ');
            
            while (end != std::wstring::npos)
            {
                wordList.push_back(words.substr(start, end - start));
                start = end + 1;
                end = words.find(L' ', start);
            }
            wordList.push_back(words.substr(start));
            
            // Build WHERE clause:
            // CONTAINS(*, '"exact phrase"') RANK BY COERCION(ABSOLUTE, 999)
            // OR CONTAINS(*, 'multi word*') RANK BY COERCION(ABSOLUTE, 998)
            // OR (CONTAINS(*, 'word1*') AND CONTAINS(*, 'word2*'))
            whereClause = L" AND (CONTAINS(*, '\"" + escapedText + L"\"') RANK BY COERCION(ABSOLUTE, 999) OR CONTAINS(*, '" + escapedText + L"*') RANK BY COERCION(ABSOLUTE, 998) OR (";
            
            for (size_t i = 0; i < wordList.size(); ++i)
            {
                if (i > 0) whereClause += L" AND ";
                whereClause += L"CONTAINS(*, '" + wordList[i] + L"*')";
            }
            
            whereClause += L"))";
            
            wsearch::TelemetryProvider::LogInfo(L"Multi-word phrase: exact (999) OR multi-word prefix (998) OR individual word prefixes");
        }
        else
        {
            // For single words:
            // 1. Exact match on filename with highest rank (999)
            // 2. Prefix match on all content
            whereClause = L" AND (CONTAINS(System.ItemNameDisplay, '" + escapedText + 
                L"', 1033) RANK BY COERCION(ABSOLUTE, 999) OR CONTAINS(*, '" + escapedText + L"*'))";
        }
        
        
        
        
        wsearch::TelemetryProvider::LogInfo(L"Generated WHERE clause: %ls", whereClause.c_str());
        return whereClause;
    }

    /* INDEX MANAGEMENT */
    inline static CLSID CLSID_CollatorDataSource = { 0x9E175B8B, 0xF52A, 0x11D8, 0xB9, 0xA5, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30 };

    inline winrt::com_ptr<ISearchManager> GetSearchManager()
    {
        wsearch::TelemetryProvider::LogInfo(L"Creating SearchManager");
        winrt::com_ptr<ISearchManager> manager;
        THROW_IF_FAILED(CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_SERVER, IID_PPV_ARGS(manager.put())));
        return manager;
    }

    inline winrt::com_ptr<ISearchCatalogManager> GetSystemIndexCatalogManager()
    {
        wsearch::TelemetryProvider::LogInfo(L"Creating SystemIndex CatalogManager");
        winrt::com_ptr<ISearchManager> manager = GetSearchManager();
        winrt::com_ptr<ISearchCatalogManager> catalogManager;
        THROW_IF_FAILED(manager->GetCatalog(L"SystemIndex", catalogManager.put()));
        return catalogManager;
    }

    inline winrt::com_ptr<ISearchCrawlScopeManager> GetSystemIndexCrawlScopeManager()
    {
        wsearch::TelemetryProvider::LogInfo(L"Creating CrawlScopeManager");
        winrt::com_ptr<ISearchCatalogManager> catalogManager = GetSystemIndexCatalogManager();
        winrt::com_ptr<ISearchCrawlScopeManager> crawlScopeManager;
        THROW_IF_FAILED(catalogManager->GetCrawlScopeManager(crawlScopeManager.put()));
        return crawlScopeManager;
    }

    inline bool IsFilePathIncludedInIndex(PCWSTR path)
    {
        wsearch::TelemetryProvider::LogInfo(L"Checking if path is indexed: %ls", path);
        winrt::com_ptr<ISearchCrawlScopeManager> crawlScopeManager = GetSystemIndexCrawlScopeManager();

        BOOL included{ FALSE };
        CLUSION_REASON reason{}; // unused
        THROW_IF_FAILED(crawlScopeManager->IncludedInCrawlScopeEx(path, &included, &reason));
        wsearch::TelemetryProvider::LogInfo(L"Path indexed: %ls", included ? L"YES" : L"NO");
        return included;
    }

    /* INDEX QUERY OPERATIONS
    *
    *  These methods help developers with the complex nature of performance around indexer queries.
    *  They include best practices and helpers to make building parts of the queries easy*
    *
    */
    inline std::wstring GetKnownFolderScope(const KNOWNFOLDERID& knownFolderId)
    {
        wsearch::TelemetryProvider::LogInfo(L"Getting known folder scope");
        // in the majority of cases, MAX_PATH is sufficient for known folder ids....
        // we can expand if we get feedback we need to
        wil::unique_cotaskmem_string path;
        THROW_IF_FAILED(SHGetKnownFolderPath(knownFolderId, 0, nullptr, &path));

        wsearch::TelemetryProvider::LogInfo(L"Known folder path: %ls", path.get());
        return std::wstring(path.get());
    }

    inline size_t GetTotalRowsForRowset(winrt::com_ptr<IRowset> const& rowset)
    {
        wsearch::TelemetryProvider::LogInfo(L"Getting total rows from rowset using MSIDXSPROP_RESULTS_FOUND");
        
        // Query the MSIDXSPROP_RESULTS_FOUND property from the rowset
        winrt::com_ptr<IRowsetInfo> rowsetInfo = rowset.as<IRowsetInfo>();
        
        DBPROPID resultsPropId = MSIDXSPROP_RESULTS_FOUND;
        DBPROPIDSET propidset;
        propidset.rgPropertyIDs = &resultsPropId;
        propidset.cPropertyIDs = 1;
        propidset.guidPropertySet = DBPROPSET_MSIDXS_ROWSETEXT;
        
        ULONG cPropertySets = 0;
        DBPROPSET* rgPropertySets = nullptr;
        
        THROW_IF_FAILED(rowsetInfo->GetProperties(1, &propidset, &cPropertySets, &rgPropertySets));
        
        wil::unique_cotaskmem_ptr<DBPROP> sprgProps(rgPropertySets->rgProperties);
        wil::unique_cotaskmem_ptr<DBPROPSET> sprgPropSets(rgPropertySets);
        
        size_t totalRows = 0;
        if (rgPropertySets->rgProperties->vValue.vt == VT_I4)
        {
            totalRows = static_cast<size_t>(rgPropertySets->rgProperties->vValue.lVal);
        }
        
        wsearch::TelemetryProvider::LogInfo(L"Total rows found: %zu", totalRows);
        return totalRows;
    }

    // Simple helper to execute a query against the indexer
    // NOTE: Does not have be the SystemIndex
    inline winrt::com_ptr<IRowset> ExecuteQuery(std::wstring_view const& sql)
    {
        wsearch::TelemetryProvider::LogInfo(L"=== EXECUTING QUERY ===");
        wsearch::TelemetryProvider::LogInfo(L"[QUERY] %ls", sql.data());
        
        try
        {
            winrt::com_ptr<IDBInitialize> dataSource;
            HRESULT hr = CoCreateInstance(CLSID_CollatorDataSource, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dataSource.put()));
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to create CollatorDataSource (0x%08X)", hr);
                THROW_IF_FAILED(hr);
            }

            hr = dataSource->Initialize();
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to initialize data source (0x%08X)", hr);
                THROW_IF_FAILED(hr);
            }

            winrt::com_ptr<IDBCreateSession> session = dataSource.as<IDBCreateSession>();
            winrt::com_ptr<::IUnknown> unkSessionPtr;
            hr = session->CreateSession(0, IID_IDBCreateCommand, unkSessionPtr.put());
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to create session (0x%08X)", hr);
                THROW_IF_FAILED(hr);
            }

            winrt::com_ptr<IDBCreateCommand> createCommand = unkSessionPtr.as<IDBCreateCommand>();
            winrt::com_ptr<::IUnknown> unkCmdPtr;
            hr = createCommand->CreateCommand(0, IID_ICommandText, unkCmdPtr.put());
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to create command (0x%08X)", hr);
                THROW_IF_FAILED(hr);
            }

            winrt::com_ptr<ICommandText> cmdTxt = unkCmdPtr.as<ICommandText>();
            hr = cmdTxt->SetCommandText(DBGUID_DEFAULT, sql.data());
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to set command text. Query may be malformed. (0x%08X)", hr);
                wsearch::TelemetryProvider::LogError(L"[QUERY] %ls", sql.data());
                THROW_IF_FAILED(hr);
            }

            DBROWCOUNT rowCount = 0;
            winrt::com_ptr<::IUnknown> unkRowsetPtr;
            hr = cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put());
            if (FAILED(hr))
            {
                wsearch::TelemetryProvider::LogError(L"[ERROR] Failed to execute query. Check query syntax. (0x%08X)", hr);
                wsearch::TelemetryProvider::LogError(L"[QUERY] %ls", sql.data());
                THROW_IF_FAILED(hr);
            }

            wsearch::TelemetryProvider::LogInfo(L"Query executed successfully. Row count: %lld", rowCount);
            return unkRowsetPtr.as<IRowset>();
        }
        catch (const std::exception&)
        {
            wsearch::TelemetryProvider::LogError(L"Exception during query execution");
            throw;
        }
        catch (...)
        {
            wsearch::TelemetryProvider::LogError(L"Unknown exception during query execution");
            throw;
        }
    }

    template <typename Func>
    void EnumerateRowsWithCallback(
        _In_ IRowset* rowset,
        Func callback)
    {
        wsearch::TelemetryProvider::LogInfo(L"Enumerating rows with callback");
        winrt::com_ptr<IGetRow> getRow;
        THROW_IF_FAILED(rowset->QueryInterface(IID_PPV_ARGS(getRow.put())));

        bool continueFetch = true;
        DBCOUNTITEM rowCountReturned;

        do
        {
            HROW rowBuffer[1000]; // Request enough large batch to increase efficiency
            HROW* rowReturned = rowBuffer;

            THROW_IF_FAILED(
                rowset->GetNextRows(DB_NULL_HCHAPTER, 0, ARRAYSIZE(rowBuffer), &rowCountReturned, &rowReturned));

            wsearch::TelemetryProvider::LogInfo(L"Fetched %llu rows", static_cast<unsigned long long>(rowCountReturned));

            for (unsigned int i = 0; continueFetch && (i < rowCountReturned); i++)
            {
                winrt::com_ptr<IPropertyStore> propStore;
                winrt::com_ptr<IUnknown> unknown;

                THROW_IF_FAILED(getRow->GetRowFromHROW(nullptr, rowBuffer[i], __uuidof(IPropertyStore), unknown.put()));
                propStore = unknown.as<IPropertyStore>();

                callback(propStore.get());
            }

            THROW_IF_FAILED(rowset->ReleaseRows(rowCountReturned, rowReturned, nullptr, nullptr, nullptr));
        } while (continueFetch && (rowCountReturned > 0));

        wsearch::TelemetryProvider::LogInfo(L"Finished enumerating rows");
    }
} // namespace details
} // namespace wsearch
#endif // SEARCHPLATAPI_H

