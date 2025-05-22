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
#include <SearchAPI.h>

/* Common Helpers
* 
* These methods get access to the common interfaces exposed from the service
* If #SEARCHPLAT_API_CACHE is defined, the interfaces will cache them otherwise will be created on each function call as needed
*/

#ifdef SEARCHPLAT_API_CACHE
#define CACHE_OBJECTS 1
#endif

static winrt::com_ptr<ISearchManager> s_cachedManager;
static winrt::com_ptr<ISearchCatalogManager> s_cachedSystemIndexCatalogManager;
static winrt::com_ptr<ISearchCrawlScopeManager> s_cachedSystemIndexCrawlScopeManager;

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