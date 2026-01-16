# Query Builder Examples

This document demonstrates how to use the `SearchQueryBuilder` for constructing Windows Search queries with advanced ranking.

## Table of Contents

1. [Basic Queries](#basic-queries)
2. [Scoped Searches](#scoped-searches)
3. [Multi-Word Queries](#multi-word-queries)
4. [Advanced Ranking](#advanced-ranking)
5. [Filtering by File Type](#filtering-by-file-type)

---

## Basic Queries

### Example 1: Simple Search

```cpp
#include <SearchQueryBuilder.h>

// Build a simple query
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"budget")
    .Build();

// Execute the query
auto results = wsearch::details::ExecuteQuery(query);
```

### Example 2: Limited Results

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"report")
    .WithTopN(25)  // Limit to 25 results
    .Build();
```

### Example 3: Additional Properties

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"presentation")
    .WithProperties({
        L"System.Title",
        L"System.Author",
        L"System.Keywords"
    })
    .Build();
```

---

## Scoped Searches

### Example 4: Single Folder

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithScopes({L"C:\\Users\\Documents"})
    .WithSearchText(L"annual report")
    .Build();
```

### Example 5: Multiple Folders

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithScopes({
        L"C:\\Users\\Documents",
        L"C:\\Users\\Desktop",
        L"C:\\Projects"
    })
    .WithSearchText(L"project")
    .Build();
```

### Example 6: Excluded Folders

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithScopes({L"C:\\Users"})
    .WithExcludedScopes({
        L"C:\\Users\\AppData",
        L"C:\\Users\\Temp"
    })
    .WithSearchText(L"config")
    .Build();
```

---

## Multi-Word Queries

### Example 7: Two-Word Search

```cpp
// Automatically creates:
// 1. Exact phrase match: "financial report"
// 2. Prefix phrase match: "financial report*"
// 3. AND logic: financial* AND report*

wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"financial report")
    .Build();
```

### Example 8: Three-Word Search

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"annual financial statement")
    .Build();

// Generates:
// - Exact: "annual financial statement" (ABSOLUTE 990)
// - Prefix: "annual financial statement*" (MINMAX 900-980)
// - AND: annual* AND financial* AND statement* (MINMAX 0-899)
```

### Example 9: Quoted Phrase (Exact Match Only)

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"\"quarterly earnings report\"")
    .Build();

// Generates only exact match, no prefix or AND variants
```

---

## Advanced Ranking

### Example 10: Understanding Ranking Tiers

```cpp
// Single word "budget":
// Tier 1 (990): Exact filename match - "budget.xlsx"
// Tier 2 (900-980): Prefix filename - "budget_2024.xlsx"
// Tier 3 (0-899): Content match - File containing "budget"

wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"budget")
    .Build();
```

###Example 11: Multi-Word Ranking

```cpp
// Multi-word "annual report":
// Tier 1 (990): Exact phrase in filename - "annual report.pdf"
// Tier 2 (900-980): Prefix phrase - "annual report 2024.pdf"
// Tier 3 (0-899): All words present - file with "annual" AND "report"

wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"annual report")
    .Build();
```

---

## Filtering by File Type

### Example 12: Search PDFs Only

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithSearchText(L"contract *.pdf")
    .WithScopes({L"C:\\Users\\Documents"})
    .Build();
```

### Example 13: Complex Filter Query

```cpp
wsearch::SearchQueryBuilder builder;
auto query = builder
    .WithScopes({L"C:\\Users\\Documents", L"C:\\Users\\Desktop"})
    .WithExcludedScopes({L"C:\\Users\\Documents\\Archive"})
    .WithSearchText(L"project specification")
    .WithProperties({
        L"System.Kind",
        L"System.FileExtension",
        L"System.Title",
        L"System.Author"
    })
    .WithTopN(50)
    .WithLocale(1033)  // en-US
    .Build();
```

---

## Priming Queries for Performance

### Example 14: Create Priming Query

```cpp
// Create a priming query (no search text)
wsearch::SearchQueryBuilder builder;
auto primingQuery = builder
    .WithScopes({L"C:\\Users\\Documents"})
    .WithProperties({L"System.ItemNameDisplay"})
    .BuildPrimingQuery();

// Execute priming query
auto primingRowset = wsearch::details::ExecuteQuery(primingQuery);
auto whereId = wsearch::details::GetReuseWhereIDFromRowset(primingRowset);

// Now use REUSEWHERE for subsequent queries (handled automatically by SearchSession)
```

---

## Real-World Complete Example

### Example 15: Search Application

```cpp
#include <SearchQueryBuilder.h>
#include <SearchResult.h>
#include <iostream>

void SearchDocuments(const std::wstring& userQuery)
{
    // Build query with all best practices
    wsearch::SearchQueryBuilder builder;
    auto query = builder
        .WithScopes({
            L"C:\\Users\\Documents",
            L"C:\\Users\\Desktop"
        })
        .WithExcludedScopes({
            L"C:\\Users\\Documents\\Archive",
            L"C:\\Users\\Documents\\Backup"
        })
        .WithSearchText(userQuery)
        .WithProperties({
            L"System.Kind",
            L"System.Title",
            L"System.Author",
            L"System.FileExtension",
            L"System.Size"
        })
        .WithTopN(100)
        .Build();
    
    // Execute query
    auto rowset = wsearch::details::ExecuteQuery(query);
    
    // Convert to SearchResult objects
    auto results = wsearch::details::ConvertRowsetToResults(rowset);
    
    // Display results
    std::wcout << L"Found " << results.size() << L" results:\n\n";
    
    for (const auto& result : results)
    {
        std::wcout << L"File: " << result.GetFileName() << L"\n";
        std::wcout << L"  Path: " << result.GetPath() << L"\n";
        std::wcout << L"  Type: " << result.GetKind() << L"\n";
        std::wcout << L"  Size: " << result.GetSize() << L" bytes\n";
        std::wcout << L"  Rank: " << result.GetRank() << L"\n";
        std::wcout << L"  Clicks: " << result.GetClickCount() << L"\n\n";
    }
}

int main()
{
    auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
    
    SearchDocuments(L"financial report 2024");
    
    return 0;
}
```

---

## Query Structure Reference

### Single Word Query Structure

```sql
SELECT TOP N <properties>
FROM SystemIndex
WHERE <scopes>
  AND WITH (System.ItemNameDisplay) AS #MRProps
  (
    (CONTAINS(#MRProps, '"word"', 1033) RANK BY COERCION(ABSOLUTE, 990))
    OR (CONTAINS(#MRProps, '"word*"', 1033) RANK BY COERCION(MINMAX, 900, 980))
    OR (CONTAINS(*, '"word"', 1033) RANK BY COERCION(MINMAX, 0, 899))
  )
ORDER BY System.Search.Rank DESC,
         System.Document.LineCount DESC,
         System.DateAccessed DESC,
         System.Search.GatherTime DESC
```

### Multi-Word Query Structure

```sql
SELECT TOP N <properties>
FROM SystemIndex
WHERE <scopes>
  AND WITH (System.ItemNameDisplay) AS #MRProps
  (
    (CONTAINS(#MRProps, '"word1 word2"', 1033) RANK BY COERCION(ABSOLUTE, 990))
    OR (CONTAINS(#MRProps, '"word1 word2*"', 1033) RANK BY COERCION(MINMAX, 900, 980))
    OR (CONTAINS(*, '"word1*"', 1033) AND CONTAINS(*, '"word2*"', 1033)
        RANK BY COERCION(MINMAX, 0, 899))
  )
ORDER BY ...
```

---

## Best Practices

1. **Always limit results** with `WithTopN()` to avoid returning huge result sets
2. **Scope narrowly** - smaller scopes = faster queries
3. **Request only needed properties** - reduces memory and improves performance
4. **Use priming queries** for repeated searches in the same scope
5. **Leverage MINMAX ranking** to let Windows Search's natural ranking influence results
6. **Monitor performance** with timing metrics

---

## See Also

- [Session Examples](sessionsexamples.md)
- [Full App Example](fullappexample.md)
- [SearchResult Helper](searchresultexamples.md)
