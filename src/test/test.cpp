#include "pch.h"
#include "SearchPlatAPI.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SearchPlatAPITests
{
	TEST_CLASS(SearchPlatAPITests)
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
			std::vector<std::wstring> includedScopes = { GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop) };
			std::wstring builtSql = BuildPrimingSqlFromScopes(includedScopes, std::vector<std::wstring>());

			auto documentsScope = GetKnownFolderScope(FOLDERID_Documents);
			std::replace(documentsScope.begin(), documentsScope.end(), L'\\', L'/');
			auto desktopScope = GetKnownFolderScope(FOLDERID_Desktop);
			std::replace(desktopScope.begin(), desktopScope.end(), L'\\', L'/');
			expectedSql += L"SELECT System.ItemUrl FROM SystemIndex WHERE ( SCOPE='file:" + documentsScope + L"' OR SCOPE='file:" + desktopScope + L"')";

			Assert::AreEqual(builtSql.c_str(), expectedSql.c_str());
		}

		TEST_METHOD(TestPrimeQueryAndReuse)
		{
			std::vector<std::wstring> includedScopes = { GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop) };
			auto primingRowset = CreateQueryPrimingRowset(includedScopes, std::vector<std::wstring>());
			auto reuseWhere = GetReuseWhereIDFromRowset(primingRowset);
			Assert::AreNotSame(reuseWhere, 0ul);
		}

		TEST_METHOD(TestExecuteSearchAllQueryUsePrimingQuery)
		{
			std::vector<std::wstring> includedScopes = { GetKnownFolderScope(FOLDERID_Documents), GetKnownFolderScope(FOLDERID_Desktop) };
			auto primingRowset = CreateQueryPrimingRowset(includedScopes, std::vector<std::wstring>());
			auto reuseWhere = GetReuseWhereIDFromRowset(primingRowset);
			auto rowset = ExecuteQueryUsingPrimingQuery(L"Find all text");

			Assert::IsNotNull(rowset.get());

			// Enumerate the rows
			// Ensures we got a real handle
		}
	};
}
