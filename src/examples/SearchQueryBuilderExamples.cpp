// Copyright (C) Microsoft Corporation. All rights reserved.
// SearchQueryBuilder Integration Examples

#include <SearchQueryBuilder.h>
#include <SearchSessions.h>
#include <SearchPlatCore.h>
#include <iostream>

// Example 1: Basic Search with Advanced Ranking
void Example1_BasicSearch()
{
    std::wcout << L"=== Example 1: Basic Search ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithSearchText(L"budget")
        .WithTopN(20)
        .Build();
    
    std::wcout << L"Query: " << query << std::endl << std::endl;
    
    // Execute query
    auto results = wsearch::details::ExecuteQuery(query);
    std::wcout << L"Results found: " << wsearch::details::GetTotalRowsForRowset(results) << std::endl;
}

// Example 2: Scoped Multi-Word Search
void Example2_ScopedMultiWord()
{
    std::wcout << L"=== Example 2: Scoped Multi-Word Search ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithScopes({L"C:\\Users\\Documents", L"C:\\Users\\Desktop"})
        .WithSearchText(L"annual report")
        .WithTopN(30)
        .Build();
    
    std::wcout << L"Query: " << query << std::endl << std::endl;
    
    auto results = wsearch::details::ExecuteQuery(query);
    std::wcout << L"Results found: " << wsearch::details::GetTotalRowsForRowset(results) << std::endl;
}

// Example 3: Exact Phrase Search
void Example3_ExactPhrase()
{
    std::wcout << L"=== Example 3: Exact Phrase Search ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithSearchText(L"\"quarterly financial report\"")
        .Build();
    
    std::wcout << L"Query: " << query << std::endl << std::endl;
    
    auto results = wsearch::details::ExecuteQuery(query);
    std::wcout << L"Results found: " << wsearch::details::GetTotalRowsForRowset(results) << std::endl;
}

// Example 4: Complex Query with All Features
void Example4_ComplexQuery()
{
    std::wcout << L"=== Example 4: Complex Query ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithScopes({
            L"C:\\Users\\Documents",
            L"C:\\Users\\Desktop"
        })
        .WithExcludedScopes({
            L"C:\\Users\\Documents\\Archive",
            L"C:\\Users\\Documents\\Temp"
        })
        .WithProperties({
            L"System.Title",
            L"System.Author",
            L"System.Keywords"
        })
        .WithSearchText(L"project proposal")
        .WithTopN(50)
        .WithLocale(1033)
        .Build();
    
    std::wcout << L"Query: " << query << std::endl << std::endl;
    
    auto results = wsearch::details::ExecuteQuery(query);
    std::wcout << L"Results found: " << wsearch::details::GetTotalRowsForRowset(results) << std::endl;
}

// Example 5: Using with SearchSession (Integration)
void Example5_WithSearchSession()
{
    std::wcout << L"=== Example 5: Integration with SearchSession ===" << std::endl;
    
    // Create a search session
    std::vector<std::wstring> scopes = {L"C:\\Users\\Documents"};
    wsearch::SearchSession session(scopes);
    
    // Build a custom query using SearchQueryBuilder
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithScopes(scopes)
        .WithSearchText(L"meeting notes")
        .WithTopN(20)
        .Build();
    
    std::wcout << L"Query: " << query << std::endl << std::endl;
    
    // Execute the query
    auto results = wsearch::details::ExecuteQuery(query);
    
    // Enumerate results
    size_t count = 0;
    wsearch::details::EnumerateRowsWithCallback(results.get(), [&count](IPropertyStore* propStore) {
        PROPVARIANT propVar;
        PropVariantInit(&propVar);
        
        // Get file path
        if (SUCCEEDED(propStore->GetValue(PKEY_ItemUrl, &propVar)))
        {
            if (propVar.vt == VT_LPWSTR)
            {
                std::wcout << L"  [" << (++count) << L"] " << propVar.pwszVal << std::endl;
                
                // Track click (example)
                // session.TrackResultClick(propVar.pwszVal);
            }
        }
        
        PropVariantClear(&propVar);
    });
    
    std::wcout << L"Total results enumerated: " << count << std::endl;
}

// Example 6: Tokenization Examples
void Example6_Tokenization()
{
    std::wcout << L"=== Example 6: Tokenization ===" << std::endl;
    
    // Single word
    wsearch::SearchTokenizer single(L"report");
    std::wcout << L"Single word 'report': " << single.GetTokenCount() << L" token(s)" << std::endl;
    
    // Multiple words
    wsearch::SearchTokenizer multi(L"annual financial report");
    std::wcout << L"Multi-word 'annual financial report': " << multi.GetTokenCount() << L" token(s)" << std::endl;
    for (const auto& token : multi.GetTokens())
    {
        std::wcout << L"  - " << token << std::endl;
    }
    
    // Quoted phrase
    wsearch::SearchTokenizer quoted(L"\"project proposal\"");
    std::wcout << L"Quoted '\"project proposal\"': " << quoted.GetTokenCount() << L" token(s), ";
    std::wcout << L"IsQuoted: " << (quoted.IsQuoted() ? L"Yes" : L"No") << std::endl;
    
    // Punctuation
    wsearch::SearchTokenizer punct(L"hello, world!");
    std::wcout << L"Punctuation 'hello, world!': " << punct.GetTokenCount() << L" token(s)" << std::endl;
    for (const auto& token : punct.GetTokens())
    {
        std::wcout << L"  - " << token << std::endl;
    }
}

// Example 7: Priming Query for Optimization
void Example7_PrimingQuery()
{
    std::wcout << L"=== Example 7: Priming Query ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    
    // Build priming query (no search text)
    auto primingQuery = builder
        .WithScopes({L"C:\\Users\\Documents"})
        .WithProperties({L"System.ItemNameDisplay"})
        .BuildPrimingQuery();
    
    std::wcout << L"Priming Query: " << primingQuery << std::endl << std::endl;
    
    // Execute priming query
    auto primingRowset = wsearch::details::ExecuteQuery(primingQuery);
    auto whereId = wsearch::details::GetReuseWhereIDFromRowset(primingRowset);
    
    std::wcout << L"REUSEWHERE ID: " << whereId << std::endl;
    
    // Now build actual search query (would use REUSEWHERE in real implementation)
    auto searchQuery = builder
        .WithScopes({L"C:\\Users\\Documents"})
        .WithSearchText(L"report")
        .Build();
    
    std::wcout << L"Search Query: " << searchQuery << std::endl;
}

// Example 8: Different Query Shapes
void Example8_QueryShapes()
{
    std::wcout << L"=== Example 8: Different Query Shapes ===" << std::endl;
    
    wsearch::SearchQueryBuilder builder;
    
    // Shape 1: Single word
    std::wcout << L"--- Single Word ---" << std::endl;
    auto q1 = builder.WithSearchText(L"index").Build();
    std::wcout << L"Query: " << q1 << std::endl << std::endl;
    
    // Shape 2: Two words
    std::wcout << L"--- Two Words ---" << std::endl;
    auto q2 = builder.WithSearchText(L"annual report").Build();
    std::wcout << L"Query: " << q2 << std::endl << std::endl;
    
    // Shape 3: Three words
    std::wcout << L"--- Three Words ---" << std::endl;
    auto q3 = builder.WithSearchText(L"quarterly financial report").Build();
    std::wcout << L"Query: " << q3 << std::endl << std::endl;
    
    // Shape 4: Quoted phrase
    std::wcout << L"--- Quoted Phrase ---" << std::endl;
    auto q4 = builder.WithSearchText(L"\"project proposal\"").Build();
    std::wcout << L"Query: " << q4 << std::endl << std::endl;
}

int main()
{
    try
    {
        // Initialize COM
        auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
        
        std::wcout << L"SearchQueryBuilder Examples" << std::endl;
        std::wcout << L"============================" << std::endl << std::endl;
        
        // Run examples
        Example1_BasicSearch();
        std::wcout << std::endl;
        
        Example2_ScopedMultiWord();
        std::wcout << std::endl;
        
        Example3_ExactPhrase();
        std::wcout << std::endl;
        
        Example4_ComplexQuery();
        std::wcout << std::endl;
        
        Example5_WithSearchSession();
        std::wcout << std::endl;
        
        Example6_Tokenization();
        std::wcout << std::endl;
        
        Example7_PrimingQuery();
        std::wcout << std::endl;
        
        Example8_QueryShapes();
        std::wcout << std::endl;
        
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
