// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include <windows.h>
#include <SearchSessions.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace wsearch;
using namespace wsearch::details;

namespace SearchPlatCoreTests
{
    TEST_CLASS(WindowsSearchPlatformTests)
    {
    public:
        TEST_METHOD(TestGetSearchManager)
        {
            auto searchManager = GetSearchManager();
            Assert::IsNotNull(searchManager.get());
        }

        TEST_METHOD(TestGetSystemIndexSearchCatalogManager)
        {
            auto searchCatalogManager = GetSystemIndexCatalogManager();
            Assert::IsNotNull(searchCatalogManager.get());
        }

        TEST_METHOD(TestGetSystemIndexSearchCrawlScopeManager)
        {
            auto searchCrawlScopeManager = GetSystemIndexCrawlScopeManager();
            Assert::IsNotNull(searchCrawlScopeManager.get());
        }

        TEST_METHOD(ValidateBuildPrimingSQL)
        {
            std::wstring expectedSql = L"";
            std::vector<std::wstring> includedScopes = {
                GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop)};
            std::wstring builtSql =
                BuildPrimingSqlFromScopes(includedScopes, std::vector<std::wstring>(), std::vector<std::wstring>());

            auto documentsScope = GetKnownFolderScope(FOLDERID_Documents);
            std::replace(documentsScope.begin(), documentsScope.end(), L'\\', L'/');
            auto desktopScope = GetKnownFolderScope(FOLDERID_Desktop);
            std::replace(desktopScope.begin(), desktopScope.end(), L'\\', L'/');
            expectedSql += L"SELECT System.ItemUrl FROM SystemIndex WHERE ( SCOPE='file:" + documentsScope + L"' OR SCOPE='file:" +
                           desktopScope + L"')";

            Assert::AreEqual(builtSql.length(), expectedSql.length());
            Assert::AreEqual(builtSql.c_str(), expectedSql.c_str());
        }

        TEST_METHOD(TestFileSearchProviderPrepareForSearchNoScopes)
        {
            auto session = wsearch::SearchSession({L"file:"}, std::vector<std::wstring>(), std::vector<std::wstring>());
        }

        TEST_METHOD(TestFileSearchProviderPrepareForSearch)
        {
            std::vector<std::wstring> includedScopes = {
                GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop)};
            auto session = wsearch::SearchSession(includedScopes, std::vector<std::wstring>(), std::vector<std::wstring>());
        }

        TEST_METHOD(TestFileSearchProviderIssueQuery)
        {
            std::vector<std::wstring> includedScopes = {
                GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop)};
            auto session = wsearch::SearchSession(includedScopes, std::vector<std::wstring>(), std::vector<std::wstring>());
            auto results = session.Search(L"Foo");

            Assert::AreEqual(static_cast<unsigned long>(GetTotalRowsForRowset(results)), 0ul);
        }

        TEST_METHOD(TestFileCountInIndex)
        {
            auto session =
                wsearch::SearchSession(std::vector<std::wstring>(), std::vector<std::wstring>(), std::vector<std::wstring>());

            Assert::IsTrue(session.GetTotalFilesInIndex() >= 0);
        }
    };
}

