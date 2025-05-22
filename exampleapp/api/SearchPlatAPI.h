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

/* Common Helpers
* 
* These methods get access to the common interfaces exposed from the service
* If #SEARCHPLAT_API_CACHE is defined, the interfaces will cache them otherwise will be created on each function call as needed
*/

#ifdef SEARCHPLAT_API_CACHE
#define CACHE_OBJECTS 1
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

/* INDEX QUERY OPERATIONS */
__forceinline std::wstring BuildPrimingSqlFromScopes(const std::vector<std::wstring>& includedScopes, const std::vector<std::wstring>& excludedScopes)
{
    std::wstring queryStr(L"SELECT System.ItemUrl FROM SystemIndex WHERE");

    // build the included, and excluded scope lists
    for (size_t i = 0; i < includedScopes.size(); ++i)
    {
        queryStr += L" SCOPE=";
        queryStr += includedScopes[i].c_str();
        if (i < (includedScopes.size() - 1))
        {
            queryStr += L" AND";
        }
    }

    for (size_t i = 0; i < excludedScopes.size(); ++i)
    {
        queryStr += L" SCOPE!=";
        queryStr += excludedScopes[i].c_str();
        if (i < (includedScopes.size() - 1))
        {
            queryStr += L" AND";
        }
    }

    return queryStr;
}


__forceinline winrt::com_ptr<IRowset> CreateQueryPrimingRowset(const std::vector<std::wstring>& includedScopes, const std::vector<std::wstring>& excludedScopes)
{
    // Query CommandText
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
    std::wstring sqlStr = BuildPrimingSqlFromScopes(includedScopes, excludedScopes);
    THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, sqlStr.c_str()));

    DBROWCOUNT rowCount = 0;
    winrt::com_ptr<::IUnknown> unkRowsetPtr;
    THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

    winrt::com_ptr<IRowset> rowset = unkRowsetPtr.as<IRowset>();

    return rowset;
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