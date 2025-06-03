#include "pch.h"
#define SEARCHPLAT_API_CACHE 1
#include "SearchPlatAPI.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SearchPlatAPITests
{
	TEST_CLASS(SearchPlatAPITestsCache)
	{
	public:

		TEST_METHOD(TestGetSearchManager)
		{
			auto searchManager = GetSearchManager();
			Assert::IsNotNull(searchManager.get());
			Assert::IsNotNull(s_cachedManager.get());
		}

		TEST_METHOD(TestGetSystemIndexSearchCatalogManager)
		{
			auto searchCatalogManager = GetSystemIndexCatalogManager();
			Assert::IsNotNull(searchCatalogManager.get());
			Assert::IsNotNull(s_cachedSystemIndexCatalogManager.get());
		}

		TEST_METHOD(TestGetSystemIndexSearchCrawlScopeManager)
		{
			auto searchCrawlScopeManager = GetSystemIndexCrawlScopeManager();
			Assert::IsNotNull(searchCrawlScopeManager.get());
			Assert::IsNotNull(s_cachedSystemIndexCrawlScopeManager.get());
		}

		TEST_METHOD(ValidateBuildPrimingSQL)
		{
			std::wstring expectedSql = L"";
			std::vector<std::wstring> includedScopes = { GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop) };
			std::wstring builtSql = BuildPrimingSqlFromScopes(includedScopes, std::vector<std::wstring>());
		}

		TEST_METHOD(TestPrimeQueryAndReuse)
		{
			std::vector<std::wstring> includedScopes = { GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop) };
			auto primingRowset = CreateQueryPrimingRowset(includedScopes, std::vector<std::wstring>());
		}
	};
}
