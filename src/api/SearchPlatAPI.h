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
* This header requires the Windows Implementation Library for resource and result macros
* 
* Enjoy!
*/

#include <winrt/base.h>
#include <wil/result.h>
#include <wil/resource.h>
#include <Unknwn.h>
#include <NTQuery.h>
#include <oledb.h>
#include <SearchAPI.h>
#include <shlobj.h>
#include "KnownFolders.h"

/* Common Helpers
* 
* These methods get access to the common interfaces exposed from the service
* If #SEARCHPLAT_API_CACHE is defined, the interfaces will cache them otherwise will be created on each function call as needed
*/

#ifdef SEARCHPLAT_API_CACHE
#define CACHE_OBJECTS 1
#else
#define CACHE_OBJECTS 0
#endif

/* INDEX MANAGEMENT */
static winrt::com_ptr<ISearchManager> s_cachedManager;
static winrt::com_ptr<ISearchCatalogManager> s_cachedSystemIndexCatalogManager;
static winrt::com_ptr<ISearchCrawlScopeManager> s_cachedSystemIndexCrawlScopeManager;

__declspec(selectany) CLSID CLSID_CollatorDataSource = { 0x9E175B8B, 0xF52A, 0x11D8, 0xB9, 0xA5, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30 };

__forceinline winrt::com_ptr<ISearchManager> GetSearchManager()
{
#if CACHE_OBJECTS
    if (!s_cachedManager.get())
    {
        THROW_IF_FAILED(CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_SERVER, IID_PPV_ARGS(s_cachedManager.put())));
    }
    return s_cachedManager;
#else
    winrt::com_ptr<ISearchManager> manager;
    THROW_IF_FAILED(CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_SERVER, IID_PPV_ARGS(manager.put())));
    return manager;
#endif
}

__forceinline winrt::com_ptr<ISearchCatalogManager> GetSystemIndexCatalogManager()
{
#if CACHE_OBJECTS
    if (!s_cachedSystemIndexCatalogManager.get())
    {
        THROW_IF_FAILED(s_cachedManager->GetCatalog(L"SystemIndex", s_cachedSystemIndexCatalogManager.put()));
    }
    return s_cachedSystemIndexCatalogManager;
#else
    winrt::com_ptr<ISearchManager> manager = GetSearchManager();
    winrt::com_ptr<ISearchCatalogManager> catalogManager;
    THROW_IF_FAILED(manager->GetCatalog(L"SystemIndex", catalogManager.put()));
    return catalogManager;
#endif
}

__forceinline winrt::com_ptr<ISearchCrawlScopeManager> GetSystemIndexCrawlScopeManager()
{
#if CACHE_OBJECTS
    if (!s_cachedSystemIndexCrawlScopeManager.get())
    {
        winrt::com_ptr<ISearchCrawlScopeManager> crawlScopeManager;
        THROW_IF_FAILED(s_cachedSystemIndexCatalogManager->GetCrawlScopeManager(s_cachedSystemIndexCrawlScopeManager.put()));
    }
    return s_cachedSystemIndexCrawlScopeManager;
#else
    winrt::com_ptr<ISearchCatalogManager> catalogManager = GetSystemIndexCatalogManager();
    winrt::com_ptr<ISearchCrawlScopeManager> crawlScopeManager;
    THROW_IF_FAILED(catalogManager->GetCrawlScopeManager(crawlScopeManager.put()));
    return crawlScopeManager;
#endif
}

__forceinline bool
IsFilePathIncludedInIndex(PCWSTR path)
{
    winrt::com_ptr<ISearchCrawlScopeManager> crawlScopeManager = GetSystemIndexCrawlScopeManager();

    BOOL included {FALSE};
    CLUSION_REASON reason{}; // unused
    THROW_IF_FAILED(crawlScopeManager->IncludedInCrawlScopeEx(path, &included, &reason));
    return included;
}

/* INDEX QUERY OPERATIONS
* 
*  These methods help developers with the complex nature of performance around indexer queries. 
*  They include best practices and helpers to make building parts of the queries easy* 
* 
*/
__forceinline std::wstring GetKnownFolderScope(const KNOWNFOLDERID& knownFolderId)
{
    // in the majority of cases, MAX_PATH is sufficient for known folder ids....
    // we can expand if we get feedback we need to
    wil::unique_cotaskmem_string path;
    THROW_IF_FAILED(SHGetKnownFolderPath(knownFolderId, 0, nullptr, &path));

    return std::wstring(path.get());
}

__forceinline std::wstring BuildPrimingSqlFromScopes(const std::vector<std::wstring>& includedScopes, const std::vector<std::wstring>& excludedScopes)
{
    std::wstring queryStr(L"SELECT System.ItemUrl FROM SystemIndex WHERE");

    // build the included, and excluded scope lists
    for (size_t i = 0; i < includedScopes.size(); ++i)
    {
        std::wstring scope(includedScopes[i]);
        std::replace(scope.begin(), scope.end(), L'\\', L'/');
        if (i == 0)
        {
            queryStr += L" (";
        }
        queryStr += L" SCOPE='file:";
        queryStr += scope + L'\'';
        if (i < (includedScopes.size() - 1))
        {
            queryStr += L" OR";
        }
        else
        {
            queryStr += L")";
        }
    }

    for (size_t i = 0; i < excludedScopes.size(); ++i)
    {
        std::wstring scope(excludedScopes[i]);
        std::replace(scope.begin(), scope.end(), L'\\', L'/');
        queryStr += L" SCOPE <> 'file:";
        queryStr += excludedScopes[i].c_str() + L'\'';
        if (i < (excludedScopes.size() - 1))
        {
            queryStr += L" AND";
        }
    }

    return queryStr;
}

__forceinline winrt::com_ptr<IRowset> ExecuteQuery(std::wstring sql)
{
    winrt::com_ptr<IDBInitialize> dataSource;
    THROW_IF_FAILED(CoCreateInstance(CLSID_CollatorDataSource, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dataSource.put())));
    THROW_IF_FAILED(dataSource->Initialize());

    winrt::com_ptr<IDBCreateSession> session = dataSource.as<IDBCreateSession>();
    winrt::com_ptr<::IUnknown> unkSessionPtr;
    THROW_IF_FAILED(session->CreateSession(0, IID_IDBCreateCommand, unkSessionPtr.put()));

    winrt::com_ptr<IDBCreateCommand> createCommand = unkSessionPtr.as<IDBCreateCommand>();
    winrt::com_ptr<::IUnknown> unkCmdPtr;
    THROW_IF_FAILED(createCommand->CreateCommand(0, IID_ICommandText, unkCmdPtr.put()));

    winrt::com_ptr<ICommandText> cmdTxt = unkCmdPtr.as<ICommandText>();
    THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, sql.c_str()));

    DBROWCOUNT rowCount = 0;
    winrt::com_ptr<::IUnknown> unkRowsetPtr;
    THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

    return unkRowsetPtr.as<IRowset>();
}

/* Creates the priming rowset query. 
*  
*  Typically this is done when a user in an application interacts with a search box experience.
*  When the user clicks the box, you want to tell the system indexer "hey a query is coming"
*  The query should contain basic information about the scope of the data you want to search over.
*  
*  When the user starts typing, the developer can use the priming rowset and update it with more information. This avoids index decoding
*  per query instance and is the optimal way to issue queries using OLEDB/SQL
* 
*  Calling this method a second time will deallocate the previous priming rowset, and do the work to create one again
*/
struct IndexerRowsetQuery
{
    std::wstring sql;
    winrt::com_ptr<IRowset> rowset;
};
static IndexerRowsetQuery s_primingQuery;
__forceinline winrt::com_ptr<IRowset> CreateQueryPrimingRowset(const std::vector<std::wstring>& includedScopes, const std::vector<std::wstring>& excludedScopes)
{
    FAIL_FAST_IF(!CACHE_OBJECTS); // caching is required for priming and rowset caching
    std::wstring sqlStr = BuildPrimingSqlFromScopes(includedScopes, excludedScopes);

    auto rowset = ExecuteQuery(sqlStr);

    s_primingQuery.sql = std::move(sqlStr);
    s_primingQuery.rowset = rowset;
    return s_primingQuery.rowset;
}

__forceinline DWORD GetReuseWhereIDFromRowset(const winrt::com_ptr<IRowset>& rowset)
{
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

    return prgPropSets->rgProperties->vValue.ulVal;
}

/* Executes searching the entire system index with a string using the priming query as the base IRowset
*  
*  The initial priming query should have been generated via CreateQueryPrimingRowset with included and excluded scopes.
*  This method then takes in a string and searches across that query. 
*
*/
__forceinline winrt::com_ptr<IRowset> ExecuteQueryUsingPrimingQuery(const std::wstring& searchText)
{
    auto reuseWhereId = GetReuseWhereIDFromRowset(s_primingQuery.rowset);
    std::wstring querySql = s_primingQuery.sql + L" AND CONTAINS('" + searchText + L"') AND REUSEWHERE(" + std::to_wstring(reuseWhereId) + L")";

    return ExecuteQuery(s_primingQuery.sql);
}
