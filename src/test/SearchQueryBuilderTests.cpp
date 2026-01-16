// Copyright (C) Microsoft Corporation. All rights reserved.
#include "pch.h"
#include <SearchQueryBuilder.h>
#include <SearchTokenizer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace wsearch;

namespace SearchQueryBuilderTests
{
    TEST_CLASS(SearchTokenizerTests)
    {
    public:
        TEST_METHOD(TestSingleWord)
        {
            Logger::WriteMessage(L"Testing single word tokenization...\n");
            
            SearchTokenizer tokenizer(L"hello");
            
            Assert::IsTrue(tokenizer.IsSingleToken());
            Assert::AreEqual(static_cast<size_t>(1), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            
            Logger::WriteMessage(L"Single word tokenization passed\n");
        }

        TEST_METHOD(TestMultipleWords)
        {
            Logger::WriteMessage(L"Testing multiple word tokenization...\n");
            
            SearchTokenizer tokenizer(L"hello world");
            
            Assert::IsTrue(tokenizer.HasMultipleTokens());
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
            
            Logger::WriteMessage(L"Multiple word tokenization passed\n");
        }

        TEST_METHOD(TestQuotedPhrase)
        {
            Logger::WriteMessage(L"Testing quoted phrase tokenization...\n");
            
            SearchTokenizer tokenizer(L"\"annual report\"");
            
            Assert::IsTrue(tokenizer.IsQuoted());
            Assert::IsTrue(tokenizer.IsSingleToken());
            Assert::AreEqual(L"annual report", tokenizer.GetTokens()[0].c_str());
            
            Logger::WriteMessage(L"Quoted phrase tokenization passed\n");
        }

        TEST_METHOD(TestEmptyString)
        {
            Logger::WriteMessage(L"Testing empty string tokenization...\n");
            
            SearchTokenizer tokenizer(L"");
            
            Assert::IsTrue(tokenizer.IsEmpty());
            Assert::AreEqual(static_cast<size_t>(0), tokenizer.GetTokenCount());
            
            Logger::WriteMessage(L"Empty string tokenization passed\n");
        }

        TEST_METHOD(TestPunctuationFiltering)
        {
            Logger::WriteMessage(L"Testing punctuation filtering...\n");
            
            SearchTokenizer tokenizer(L"hello, world!");
            
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
            
            Logger::WriteMessage(L"Punctuation filtering passed\n");
        }

        TEST_METHOD(TestMultipleSpaces)
        {
            Logger::WriteMessage(L"Testing multiple spaces...\n");
            
            SearchTokenizer tokenizer(L"hello   world");
            
            Assert::AreEqual(static_cast<size_t>(2), tokenizer.GetTokenCount());
            Assert::AreEqual(L"hello", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"world", tokenizer.GetTokens()[1].c_str());
            
            Logger::WriteMessage(L"Multiple spaces handled correctly\n");
        }

        TEST_METHOD(TestThreeWords)
        {
            Logger::WriteMessage(L"Testing three words...\n");
            
            SearchTokenizer tokenizer(L"the quick fox");
            
            Assert::AreEqual(static_cast<size_t>(3), tokenizer.GetTokenCount());
            Assert::AreEqual(L"the", tokenizer.GetTokens()[0].c_str());
            Assert::AreEqual(L"quick", tokenizer.GetTokens()[1].c_str());
            Assert::AreEqual(L"fox", tokenizer.GetTokens()[2].c_str());
            
            Logger::WriteMessage(L"Three word tokenization passed\n");
        }
    };

    TEST_CLASS(SearchQueryBuilderTests)
    {
    public:
        TEST_METHOD(TestBasicQueryBuilding)
        {
            Logger::WriteMessage(L"Testing basic query building...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"test")
                .Build();
            
            Assert::IsFalse(query.empty());
            Assert::IsTrue(query.find(L"SELECT") != std::wstring::npos);
            Assert::IsTrue(query.find(L"FROM SystemIndex") != std::wstring::npos);
            
            Logger::WriteMessage(L"Query built: ");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestSingleWordQuery)
        {
            Logger::WriteMessage(L"Testing single word query shape...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"index")
                .Build();
            
            // Should contain exact match with ABSOLUTE ranking
            Assert::IsTrue(query.find(L"CONTAINS(#MRProps") != std::wstring::npos);
            Assert::IsTrue(query.find(L"RANK BY COERCION(ABSOLUTE, 990)") != std::wstring::npos);
            
            // Should contain prefix match with MINMAX
            Assert::IsTrue(query.find(L"RANK BY COERCION(MINMAX, 900, 980)") != std::wstring::npos);
            
            // Should contain content match with MINMAX
            Assert::IsTrue(query.find(L"RANK BY COERCION(MINMAX, 0, 899)") != std::wstring::npos);
            
            Logger::WriteMessage(L"Single word query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestMultiWordQuery)
        {
            Logger::WriteMessage(L"Testing multi-word query shape...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"annual report")
                .Build();
            
            // Should contain exact phrase with ABSOLUTE ranking
            Assert::IsTrue(query.find(L"annual report") != std::wstring::npos);
            Assert::IsTrue(query.find(L"RANK BY COERCION(ABSOLUTE, 990)") != std::wstring::npos);
            
            // Should contain prefix phrase with MINMAX
            Assert::IsTrue(query.find(L"RANK BY COERCION(MINMAX, 900, 980)") != std::wstring::npos);
            
            // Should contain AND clause for individual words
            Assert::IsTrue(query.find(L"AND") != std::wstring::npos);
            Assert::IsTrue(query.find(L"CONTAINS(*, '\"annual*\"'") != std::wstring::npos);
            Assert::IsTrue(query.find(L"CONTAINS(*, '\"report*\"'") != std::wstring::npos);
            
            Logger::WriteMessage(L"Multi-word query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestQuotedPhraseQuery)
        {
            Logger::WriteMessage(L"Testing quoted phrase query...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"\"quarterly report\"")
                .Build();
            
            // Should only have exact match, no prefix or AND clauses
            Assert::IsTrue(query.find(L"quarterly report") != std::wstring::npos);
            Assert::IsTrue(query.find(L"RANK BY COERCION(ABSOLUTE, 999)") != std::wstring::npos);
            
            // Should NOT have multiple CONTAINS clauses (only one for exact match)
            size_t containsCount = 0;
            size_t pos = 0;
            while ((pos = query.find(L"CONTAINS", pos)) != std::wstring::npos)
            {
                ++containsCount;
                pos += 8; // length of "CONTAINS"
            }
            Assert::AreEqual(static_cast<size_t>(1), containsCount);
            
            Logger::WriteMessage(L"Quoted phrase query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestQueryWithScopes)
        {
            Logger::WriteMessage(L"Testing query with scopes...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithScopes({L"C:\\Users\\Documents", L"C:\\Users\\Desktop"})
                .WithSearchText(L"test")
                .Build();
            
            Assert::IsTrue(query.find(L"SCOPE=") != std::wstring::npos);
            Assert::IsTrue(query.find(L"Documents") != std::wstring::npos);
            Assert::IsTrue(query.find(L"Desktop") != std::wstring::npos);
            Assert::IsTrue(query.find(L" OR ") != std::wstring::npos);
            
            Logger::WriteMessage(L"Query with scopes:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestQueryWithTopN)
        {
            Logger::WriteMessage(L"Testing query with TOP N...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"test")
                .WithTopN(30)
                .Build();
            
            Assert::IsTrue(query.find(L"SELECT TOP 30") != std::wstring::npos);
            
            Logger::WriteMessage(L"Query with TOP 30:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestQueryWithAdditionalProperties)
        {
            Logger::WriteMessage(L"Testing query with additional properties...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"test")
                .WithProperties({L"System.Title", L"System.Author"})
                .Build();
            
            Assert::IsTrue(query.find(L"System.Title") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Author") != std::wstring::npos);
            
            Logger::WriteMessage(L"Query with additional properties:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestOrderByClause)
        {
            Logger::WriteMessage(L"Testing ORDER BY clause...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"test")
                .Build();
            
            // Check for proper ordering
            Assert::IsTrue(query.find(L"ORDER BY System.Search.Rank DESC") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Document.LineCount DESC") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.DateAccessed DESC") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Search.GatherTime DESC") != std::wstring::npos);
            
            // Verify order (Rank should come before LineCount)
            size_t rankPos = query.find(L"System.Search.Rank");
            size_t lineCountPos = query.find(L"System.Document.LineCount");
            size_t dateAccessedPos = query.find(L"System.DateAccessed");
            size_t gatherTimePos = query.find(L"System.Search.GatherTime");
            
            Assert::IsTrue(rankPos < lineCountPos);
            Assert::IsTrue(lineCountPos < dateAccessedPos);
            Assert::IsTrue(dateAccessedPos < gatherTimePos);
            
            Logger::WriteMessage(L"ORDER BY clause verified\n");
        }

        TEST_METHOD(TestPrimingQuery)
        {
            Logger::WriteMessage(L"Testing priming query (no search text)...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithScopes({L"C:\\Users\\Documents"})
                .WithSearchText(L"test")
                .BuildPrimingQuery();
            
            // Should have scopes but NO search conditions
            Assert::IsTrue(query.find(L"SCOPE=") != std::wstring::npos);
            Assert::IsTrue(query.find(L"CONTAINS") == std::wstring::npos);
            Assert::IsTrue(query.find(L"#MRProps") == std::wstring::npos);
            
            Logger::WriteMessage(L"Priming query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestThreeWordQuery)
        {
            Logger::WriteMessage(L"Testing three-word query...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"annual financial report")
                .Build();
            
            // Should have all three words in AND clause
            Assert::IsTrue(query.find(L"annual") != std::wstring::npos);
            Assert::IsTrue(query.find(L"financial") != std::wstring::npos);
            Assert::IsTrue(query.find(L"report") != std::wstring::npos);
            
            // Count AND operators (should be 2 for 3 words)
            size_t andCount = 0;
            size_t pos = 0;
            while ((pos = query.find(L" AND ", pos)) != std::wstring::npos)
            {
                ++andCount;
                pos += 5;
            }
            Assert::IsTrue(andCount >= 2);
            
            Logger::WriteMessage(L"Three-word query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestSpecialCharacterEscaping)
        {
            Logger::WriteMessage(L"Testing special character escaping...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"don't")
                .Build();
            
            // Single quote should be escaped
            Assert::IsTrue(query.find(L"don''t") != std::wstring::npos);
            
            Logger::WriteMessage(L"Query with special characters:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestComplexRealWorldQuery)
        {
            Logger::WriteMessage(L"Testing complex real-world query...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithScopes({L"C:\\Users\\Documents", L"C:\\Users\\Desktop"})
                .WithExcludedScopes({L"C:\\Users\\Documents\\Archive"})
                .WithProperties({
                    L"System.Title",
                    L"System.Author",
                    L"System.Keywords"
                })
                .WithSearchText(L"quarterly financial report")
                .WithTopN(30)
                .WithLocale(1033)
                .Build();
            
            // Verify all components
            Assert::IsTrue(query.find(L"SELECT TOP 30") != std::wstring::npos);
            Assert::IsTrue(query.find(L"SCOPE=") != std::wstring::npos);
            Assert::IsTrue(query.find(L"SCOPE<>") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Title") != std::wstring::npos);
            Assert::IsTrue(query.find(L"WITH (System.ItemNameDisplay) AS #MRProps") != std::wstring::npos);
            Assert::IsTrue(query.find(L"ORDER BY") != std::wstring::npos);
            
            Logger::WriteMessage(L"Complex real-world query:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestEmptySearchText)
        {
            Logger::WriteMessage(L"Testing empty search text...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithScopes({L"C:\\Users\\Documents"})
                .WithSearchText(L"")
                .Build();
            
            // Should have scope but no CONTAINS clause
            Assert::IsTrue(query.find(L"SCOPE=") != std::wstring::npos);
            Assert::IsTrue(query.find(L"CONTAINS") == std::wstring::npos);
            
            Logger::WriteMessage(L"Query with empty search text:\n");
            Logger::WriteMessage(query.c_str());
            Logger::WriteMessage(L"\n");
        }

        TEST_METHOD(TestCorePropertiesAlwaysIncluded)
        {
            Logger::WriteMessage(L"Testing core properties are always included...\n");
            
            SearchQueryBuilder builder;
            auto query = builder
                .WithSearchText(L"test")
                .Build();
            
            // Verify core properties
            Assert::IsTrue(query.find(L"System.ItemUrl") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Search.Rank") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.Document.LineCount") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.DateAccessed") != std::wstring::npos);
            Assert::IsTrue(query.find(L"System.ItemNameDisplay") != std::wstring::npos);
            
            Logger::WriteMessage(L"Core properties verified\n");
        }
    };
}
