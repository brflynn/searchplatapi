# Windows Search Platform API

A modern C++ API wrapper around the Windows Search Indexer platform APIs, providing simplified access to Windows Search functionality with high-performance querying, search-as-you-type capabilities, and automatic click tracking.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Class Hierarchy](#class-hierarchy)
- [Getting Started](#getting-started)
- [Core Components](#core-components)
- [Search Session Property Helpers](#search-session-property-helpers)
- [API Reference](#api-reference)
- [Examples](#examples)
- [Performance Considerations](#performance-considerations)
- [Requirements](#requirements)

## Overview

This library provides a clean, modern C++ API for interacting with the Windows Search Indexer. It abstracts the complexity of OLEDB queries, rowset management, and property updates into easy-to-use classes.

**Key Goals:**
- Simplify Windows Search integration
- Provide high-performance search capabilities
- Support search-as-you-type scenarios with debouncing
- Track user engagement through automatic property updates
- Minimize boilerplate code

## Features

✅ **High-Performance Search**
- Optimized rowset priming for fast queries
- Reuse WHERE clause optimization
- Efficient result enumeration

✅ **Search-as-You-Type**
- Automatic debouncing (configurable delay)
- Background thread for non-blocking queries
- Cached result management

✅ **Click Tracking**
- Automatic property updates on result clicks
- Background thread for async updates
- Updates System.DateAccessed and System.Document.LineCount

✅ **Modern C++ Design**
- Header-only library (inline functions)
- RAII resource management with WIL
- Exception-based error handling
- Thread-safe operations

✅ **Comprehensive Query Support**
- Scope-based filtering
- Multi-word phrase matching
- Prefix matching
- Ranked results

## Class Hierarchy

```
┌─────────────────────────────────────────────────────────────────┐
│                     TelemetryProvider                           │
│                     (Logging interface)                         │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ inherits
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                      SearchSessionBase                          │
│                                                                 │
│  Protected Members:                                             │
│    - m_includedScopes: vector<wstring>                         │
│    - m_excludedScopes: vector<wstring>                         │
│    - m_additionalProperties: vector<wstring>                   │
│    - m_prefetchRowset: com_ptr<IRowset> (mutable)             │
│    - m_prefetchSql: wstring (mutable)                          │
│    - m_propertyUpdater: shared_ptr<SearchResultPropertyUpdater>│
│                                                                 │
│  Protected Methods:                                             │
│    - BuildPrimingSql(): wstring                                │
│    - PrepareForSearch(): void                                  │
│    - ExecuteSearchWithPriming(searchText): com_ptr<IRowset>    │
│                                                                 │
│  Public Methods:                                                │
│    - GetTotalFilesInIndex(): size_t                            │
│    - TrackResultClick(filePath): void                          │
│    - GetPendingPropertyUpdates(): size_t                       │
└────────────────┬───────────────────────┬────────────────────────┘
                 │                       │
                 │ inherits              │ inherits
                 │                       │
       ┌─────────▼─────────┐   ┌────────▼──────────────────────┐
       │   SearchSession   │   │  SearchAsYouTypeSession       │
       │                   │   │                               │
       │  Methods:         │   │  Additional Members:          │
       │   - Search()      │   │   - m_debounceDelay           │
       │   - Execute...    │   │   - m_debounceThread          │
       │     UsingPriming  │   │   - m_searchText              │
       │     Query()       │   │   - m_cachedRowset            │
       │                   │   │   - m_queryPending (atomic)   │
       │                   │   │   - m_shouldStop (atomic)     │
       │                   │   │   - m_mutex, m_cv             │
       │                   │   │                               │
       │                   │   │  Additional Methods:          │
       │                   │   │   - SetSearchText()           │
       │                   │   │   - AppendCharacters()        │
       │                   │   │   - GetCachedResults()        │
       │                   │   │   - ExecuteQueryNow()         │
       │                   │   │   - Clear()                   │
       │                   │   │   - SetDebounceDelay()        │
       └───────────────────┘   └───────────────────────────────┘


┌─────────────────────────────────────────────────────────────────┐
│              SearchResultPropertyUpdater                        │
│              (Standalone background property updater)           │
│                                                                 │
│  Private Members:                                               │
│    - m_mutex: mutex                                            │
│    - m_cv: condition_variable                                  │
│    - m_workerThread: thread                                    │
│    - m_updateQueue: queue<wstring>                             │
│    - m_shouldStop: atomic<bool>                                │
│                                                                 │
│  Private Methods:                                               │
│    - WorkerThreadProc(): void                                  │
│    - UpdateFileProperties(filePath): void                      │
│                                                                 │
│  Public Methods:                                                │
│    - OnResultClicked(filePath): void                           │
│    - GetPendingUpdateCount(): size_t                           │
└─────────────────────────────────────────────────────────────────┘
          ▲
          │
          │ contained by (shared_ptr)
          │
    SearchSessionBase
```

### Component Relationships

```
┌──────────────────────────────────────────────────────────────┐
│                        User Code                             │
└────────────┬─────────────────────────────────────────────────┘
             │
             │ creates
             │
┌────────────▼──────────┐      ┌────────────────────────────┐
│   SearchSession       │      │  SearchAsYouTypeSession    │
│   or                  │      │                            │
└────────────┬──────────┘      └──────────────┬─────────────┘
             │                                 │
             │ inherits                        │ inherits
             │                                 │
┌────────────▼─────────────────────────────────▼─────────────┐
│              SearchSessionBase                              │
│                                                             │
│  Contains (composition):                                    │
│    shared_ptr<SearchResultPropertyUpdater> m_propertyUpdater│
└────────────┬────────────────────────────────────────────────┘
             │
             │ owns
             │
┌────────────▼──────────────────────────────────────────────┐
│         SearchResultPropertyUpdater                       │
│                                                           │
│  Manages:                                                 │
│    - Background thread                                    │
│    - Update queue                                         │
│    - Property store access                                │
└───────────────────────────────────────────────────────────┘
```

## Getting Started

### Basic Search Example

```cpp
#include <SearchSessions.h>

int main()
{
    // Initialize COM
    auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
    
    // Create search session
    std::vector<std::wstring> scopes = { L"C:\\Users\\username\\Documents" };
    wsearch::SearchSession session(scopes);
    
    // Perform search
    auto results = session.Search(L"quarterly report");
    
    // Process results...
    
    return 0;
}
```

### Search-as-You-Type Example

```cpp
#include <SearchSessions.h>

int main()
{
    auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
    
    // Create session with 50ms debounce delay
    std::vector<std::wstring> scopes = { L"C:\\Users\\Documents" };
    wsearch::SearchAsYouTypeSession search(scopes, {}, {}, std::chrono::milliseconds(50));
    
    // User types incrementally
    search.SetSearchText(L"ann");
    search.AppendCharacters(L"ual");
    search.AppendCharacters(L" report");
    
    // Get results (waits for debounce if needed)
    auto results = search.GetCachedResults();
    
    // Or force immediate execution
    auto immediateResults = search.ExecuteQueryNow();
    
    return 0;
}
```

## Core Components

### SearchSessionBase

Base class providing common functionality for all search sessions.

**Key Features:**
- Scope management (included/excluded paths)
- Priming rowset caching for performance
- Result click tracking
- Property update management

**Protected Members:**
- `m_includedScopes`: Paths to search within
- `m_excludedScopes`: Paths to exclude from search
- `m_prefetchRowset`: Cached rowset for optimization
- `m_propertyUpdater`: Background property updater

### SearchSession

Synchronous search session for immediate queries.

**Use Cases:**
- One-time searches
- Batch processing
- Server-side search
- Scripts and automation

### SearchAsYouTypeSession

Asynchronous search session with debouncing for interactive search.

**Use Cases:**
- Interactive search boxes
- Real-time filtering
- Autocomplete
- Search suggestions

**Key Features:**
- Configurable debounce delay (default 50ms)
- Background thread for query execution
- Cached results
- Non-blocking operations

## Search Session Property Helpers

### Overview

The property helper system automatically tracks user engagement by updating file properties when search results are clicked.

### Properties Updated

#### System.DateAccessed
- **Type**: FILETIME
- **Purpose**: Tracks when file was last accessed
- **Update**: Set to current system time on click

#### System.Document.LineCount
- **Type**: INT32 (VT_I4)
- **Purpose**: Repurposed as "launch count" metric
- **Update**: Incremented by 1 on each click
- **Initial Value**: 0

### Architecture

The property updater uses a background thread to avoid blocking the UI:

1. **User clicks on search result**
2. **Call `TrackResultClick(filePath)`**
3. **File path queued in thread-safe queue**
4. **Background thread processes queue**
5. **Properties updated via COM IPropertyStore**
6. **Changes committed to file system**

### Usage

```cpp
// Create session (property updater automatically created)
wsearch::SearchSession session({L"C:\\Users\\Documents"});

// Perform search
auto results = session.Search(L"report");

// User clicks on a result
session.TrackResultClick(L"C:\\Users\\Documents\\Q4_Report.docx");

// Check pending updates
size_t pending = session.GetPendingPropertyUpdates();
std::wcout << L"Pending updates: " << pending << std::endl;
```

### Thread Safety

- All public methods are thread-safe
- Internal queue protected by mutex
- Each property update is atomic
- Background thread uses COINIT_MULTITHREADED

### Error Handling

- Property update failures are logged but don't stop processing
- If a file can't be accessed, the update is skipped
- Queue continues processing remaining items
- Uses WIL for RAII and exception handling

## API Reference

### SearchSessionBase

#### Constructor
```cpp
SearchSessionBase(
    std::vector<std::wstring> includedScopes,
    std::vector<std::wstring> excludedScopes = {},
    std::vector<std::wstring> additionalProperties = {})
```

#### Methods

**`size_t GetTotalFilesInIndex() const`**
Returns the total number of files in the Windows Search index.

**`void TrackResultClick(const std::wstring& filePath)`**
Tracks a user click on a search result, queuing property updates.

**`size_t GetPendingPropertyUpdates() const`**
Returns the number of property updates pending in the queue.

### SearchSession

#### Constructor
```cpp
SearchSession(
    std::vector<std::wstring> includedScopes,
    std::vector<std::wstring> excludedScopes = {},
    std::vector<std::wstring> additionalProperties = {})
```

#### Methods

**`winrt::com_ptr<IRowset> Search(std::wstring_view const& searchText)`**
Executes a synchronous search and returns results.

**`winrt::com_ptr<IRowset> ExecuteQueryUsingPrimingQuery(std::wstring_view const& searchText)`**
Executes a search using the cached priming rowset for optimization.

**`size_t GetTotalFilesInIndex()`**
Returns total files in index (convenience method).

### SearchAsYouTypeSession

#### Constructor
```cpp
SearchAsYouTypeSession(
    std::vector<std::wstring> includedScopes,
    std::vector<std::wstring> excludedScopes = {},
    std::vector<std::wstring> additionalProperties = {},
    std::chrono::milliseconds debounceDelay = std::chrono::milliseconds(50))
```

#### Methods

**`void SetSearchText(std::wstring_view searchText)`**
Sets the complete search text, triggering debounce timer.

**`void AppendCharacters(std::wstring_view characters)`**
Appends characters to current search text, triggering debounce timer.

**`void Clear()`**
Clears search text and cached results.

**`winrt::com_ptr<IRowset> GetCachedResults()`**
Returns cached results, waiting for debounce to complete if needed.

**`winrt::com_ptr<IRowset> ExecuteQueryNow()`**
Forces immediate query execution, bypassing debounce.

**`bool IsQueryPending() const`**
Returns true if a query is waiting for debounce delay.

**`std::wstring GetSearchText() const`**
Returns the current search text.

**`void SetDebounceDelay(std::chrono::milliseconds delay)`**
Updates the debounce delay.

**`LARGE_INTEGER GetLastQueryExecutionTime() const`**
Returns the timestamp of last query execution (performance counter ticks).

**`double GetLastQueryDurationMs() const`**
Returns the duration of last query execution in milliseconds.

### SearchResultPropertyUpdater

#### Constructor
```cpp
SearchResultPropertyUpdater()
```
Starts background thread for property updates.

#### Methods

**`void OnResultClicked(const std::wstring& filePath)`**
Queues a file path for property updates.

**`size_t GetPendingUpdateCount() const`**
Returns the number of pending updates in the queue.

## Examples

### Complete Search Application

```cpp
#include <SearchSessions.h>
#include <iostream>
#include <vector>

int main()
{
    try
    {
        // Initialize COM
        auto coInit = wil::CoInitializeEx(COINIT_MULTITHREADED);
        
        // Define search scopes
        std::vector<std::wstring> scopes = {
            L"C:\\Users\\username\\Documents",
            L"C:\\Users\\username\\Desktop"
        };
        
        // Create search-as-you-type session with 100ms debounce
        wsearch::SearchAsYouTypeSession search(
            scopes, 
            {},     // no excluded scopes
            { L"System.ItemNameDisplay", L"System.Size" },  // additional properties
            std::chrono::milliseconds(100)
        );
        
        // Simulate user typing
        std::wcout << L"User types 'annual'..." << std::endl;
        search.SetSearchText(L"annual");
        
        // Wait a bit, then user adds more text
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::wcout << L"User adds ' report'..." << std::endl;
        search.AppendCharacters(L" report");
        
        // Get results (will wait for debounce)
        std::wcout << L"Getting results..." << std::endl;
        auto results = search.GetCachedResults();
        
        if (results)
        {
            std::wcout << L"Results found!" << std::endl;
            
            // Enumerate results (simplified)
            wsearch::details::EnumerateRowsWithCallback(results.get(), [&](IPropertyStore* propStore) {
                PROPVARIANT propVar;
                PropVariantInit(&propVar);
                
                // Get file path
                if (SUCCEEDED(propStore->GetValue(PKEY_ItemUrl, &propVar)))
                {
                    if (propVar.vt == VT_LPWSTR)
                    {
                        std::wcout << L"  File: " << propVar.pwszVal << std::endl;
                    }
                }
                
                PropVariantClear(&propVar);
            });
            
            // User clicks on first result
            std::wstring clickedFile = L"C:\\Users\\username\\Documents\\Annual_Report_2024.pdf";
            std::wcout << L"\nUser clicked: " << clickedFile << std::endl;
            search.TrackResultClick(clickedFile);
            
            std::wcout << L"Pending property updates: " << search.GetPendingPropertyUpdates() << std::endl;
            std::wcout << L"Properties will be updated in background..." << std::endl;
        }
        
        // Get timing information
        std::wcout << L"\nPerformance Stats:" << std::endl;
        std::wcout << L"  Last query duration: " << search.GetLastQueryDurationMs() << L" ms" << std::endl;
        
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
```

### Filtering by File Type

```cpp
// Search only for PDF files in Documents
std::vector<std::wstring> scopes = { L"C:\\Users\\Documents" };
std::vector<std::wstring> properties = { 
    L"System.ItemNameDisplay", 
    L"System.FileExtension" 
};

wsearch::SearchSession session(scopes, {}, properties);
auto results = session.Search(L"*.pdf");
```

### Excluding Directories

```cpp
std::vector<std::wstring> included = { L"C:\\Users\\username" };
std::vector<std::wstring> excluded = { 
    L"C:\\Users\\username\\AppData",
    L"C:\\Users\\username\\Temp"
};

wsearch::SearchSession session(included, excluded);
auto results = session.Search(L"config");
```

## Performance Considerations

### Priming Rowsets

The API uses rowset priming for optimal performance:

1. **First Query**: Creates a priming rowset with scope information
2. **Subsequent Queries**: Reuses the priming rowset with `REUSEWHERE`
3. **Benefit**: Avoids repeated index decompression and decoding

### Debouncing

For search-as-you-type scenarios:

- **Too Short**: More frequent queries, higher CPU usage
- **Too Long**: Perceived lag in UI
- **Recommended**: 50-100ms for most applications
- **Adjust**: Based on index size and user feedback

### Property Updates

Property updates happen asynchronously:

- Updates don't block search operations
- Single background thread per session
- Queue is unbounded (monitor in production)
- Each update involves disk I/O

### Best Practices

1. **Reuse Sessions**: Create once, use many times
2. **Limit Scopes**: Smaller scopes = faster queries
3. **Cache Results**: Don't re-query unnecessarily
4. **Monitor Queue**: Check `GetPendingPropertyUpdates()` periodically
5. **Handle Errors**: Wrap in try-catch blocks

## Requirements

- **Windows SDK**: 17763 or later
- **C++ Standard**: C++17 or later
- **Dependencies**:
  - Windows Implementation Library (WIL)
  - Windows Runtime (WinRT)
- **Runtime Requirements**:
  - Windows Search Service must be running
  - Files must be indexed by Windows Search
  - Sufficient permissions for property updates

## Building

This is a header-only library. Simply include the headers:

```cpp
#include <SearchSessions.h>
```

Ensure your project links against:
- `ole32.lib`
- `oleaut32.lib`
- `propsys.lib`
- `shell32.lib`

## Troubleshooting

### Search Returns No Results

1. Verify Windows Search Service is running
2. Check if paths are indexed: Use `wsearch::details::IsFilePathIncludedInIndex()`
3. Verify scopes are correct
4. Check search syntax

### Property Updates Fail

1. Verify write permissions on files
2. Check if file is read-only
3. Ensure file is not open exclusively by another process
4. Check error logs from `TelemetryProvider`

### Performance Issues

1. Reduce search scope
2. Increase debounce delay
3. Monitor `GetLastQueryDurationMs()`
4. Check index health in Windows

## License

Copyright (C) Microsoft Corporation. All rights reserved.

## Author

Brendan Flynn

## See Also

- Windows Search API Documentation
- OLEDB Provider for Windows Search
- Windows Property System
