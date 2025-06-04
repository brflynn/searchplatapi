#include "pch.h"
#define SEARCHPLAT_API_CACHE 1
#include "SearchPlatAPI.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SearchPlatAPITests
{
	TEST_CLASS(SearchPlatAPITestsCache)
	{
	public:

		TEST_METHOD(TestGetSearchManager)
		{
			auto searchManager = searchapi::GetSearchManager();
			Assert::IsNotNull(searchManager.get());

		}

		TEST_METHOD(TestGetSystemIndexSearchCatalogManager)
		{
			auto searchCatalogManager = searchapi::GetSystemIndexCatalogManager();
			Assert::IsNotNull(searchCatalogManager.get());

		}

		TEST_METHOD(TestGetSystemIndexSearchCrawlScopeManager)
		{
			auto searchCrawlScopeManager = searchapi::GetSystemIndexCrawlScopeManager();
			Assert::IsNotNull(searchCrawlScopeManager.get());

		}

		TEST_METHOD(ValidateBuildPrimingSQL)
		{
			std::wstring expectedSql = L"";
			std::vector<std::wstring> includedScopes = { searchapi::GetKnownFolderScope(FOLDERID_Documents), searchapi::GetKnownFolderScope(FOLDERID_Desktop) };
			std::wstring builtSql = searchapi::internal::BuildPrimingSqlFromScopes(includedScopes, std::vector<std::wstring>());

			auto documentsScope = searchapi::GetKnownFolderScope(FOLDERID_Documents);
			std::replace(documentsScope.begin(), documentsScope.end(), L'\\', L'/');
			auto desktopScope = searchapi::GetKnownFolderScope(FOLDERID_Desktop);
			std::replace(desktopScope.begin(), desktopScope.end(), L'\\', L'/');
			expectedSql += L"SELECT System.ItemUrl FROM SystemIndex WHERE ( SCOPE='file:" + documentsScope + L"' OR SCOPE='file:" + desktopScope + L"')";

			Assert::AreEqual(builtSql.c_str(), expectedSql.c_str());
		}

		TEST_METHOD(TestFileSearchProviderPrepareForSearch)
		{
			std::vector<std::wstring> includedScopes = { searchapi::GetKnownFolderScope(FOLDERID_Documents), searchapi::GetKnownFolderScope(FOLDERID_Desktop) };
			searchapi::FileSearchProvider searchProvider;
			searchProvider.PrepareForSearch(includedScopes, std::vector<std::wstring>());
		}

		TEST_METHOD(TestFileSearchProviderIssueQuery)
		{
			std::vector<std::wstring> includedScopes = { searchapi::GetKnownFolderScope(FOLDERID_Documents), searchapi::GetKnownFolderScope(FOLDERID_Desktop) };
			searchapi::FileSearchProvider searchProvider;
			searchProvider.PrepareForSearch(includedScopes, std::vector<std::wstring>());
			auto results = searchProvider.Search(L"Foo");

			Assert::AreEqual(static_cast<unsigned long>(results.size()), 0ul);
		}
	};
}
