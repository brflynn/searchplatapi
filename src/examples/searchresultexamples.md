# SearchResult Helper Examples

The `SearchResult` class provides an easy-to-use wrapper around `IPropertyStore` with first-class methods for common properties and type checking.

## Table of Contents

1. [Basic Usage](#basic-usage)
2. [Type Checking](#type-checking)
3. [Property Access](#property-access)
4. [File Type Differentiation](#file-type-differentiation)
5. [Click Tracking Integration](#click-tracking-integration)

---

## Basic Usage

### Example 1: Simple Result Access

```cpp
#include <SearchResult.h>
#include <SearchSessions.h>

// Search and get results
wsearch::SearchSession session({L"C:\\Users\\Documents"});
auto results = session.SearchResults(L"report");

for (const auto& result : results)
{
    std::wcout << result.GetFileName() << std::endl;
}
```

### Example 2: Detailed Result Information

```cpp
for (const auto& result : results)
{
    std::wcout << L"File: " << result.GetFileName() << L"\n";
    std::wcout << L"  Path: " << result.GetPath() << L"\n";
    std::wcout << L"  Size: " << result.GetSize() << L" bytes\n";
    std::wcout << L"  Modified: " << FormatFileTime(result.GetDateModified()) << L"\n";
    std::wcout << L"  Rank: " << result.GetRank() << L"\n";
    std::wcout << L"  Clicks: " << result.GetClickCount() << L"\n\n";
}
```

---

## Type Checking

### Example 3: Filter by File Type

```cpp
wsearch::SearchSession session({L"C:\\Users"});
auto results = session.SearchResults(L"project");

// Separate by type
std::vector<SearchResult> documents;
std::vector<SearchResult> images;
std::vector<SearchResult> others;

for (const auto& result : results)
{
    if (result.IsDocument()) {
        documents.push_back(result);
    }
    else if (result.IsImage()) {
        images.push_back(result);
    }
    else {
        others.push_back(result);
    }
}

std::wcout << L"Documents: " << documents.size() << L"\n";
std::wcout << L"Images: " << images.size() << L"\n";
std::wcout << L"Others: " << others.size() << L"\n";
```

### Example 4: Type-Specific Processing

```cpp
for (const auto& result : results)
{
    if (result.IsDocument())
    {
        // Show author and title for documents
        std::wcout << L"Document: " << result.GetTitle() << L"\n";
        std::wcout << L"  By: " << result.GetAuthor() << L"\n";
    }
    else if (result.IsImage())
    {
        // Show dimensions for images (if available)
        std::wcout << L"Image: " << result.GetFileName() << L"\n";
        std::wcout << L"  Size: " << result.GetSize() << L" bytes\n";
    }
    else if (result.IsMusic())
    {
        // Show music-specific properties
        std::wcout << L"Music: " << result.GetFileName() << L"\n";
    }
}
```

---

## Property Access

### Example 5: Common Properties

```cpp
SearchResult result = results[0];

// String properties
std::wstring fileName = result.GetFileName();
std::wstring path = result.GetPath();
std::wstring title = result.GetTitle();
std::wstring author = result.GetAuthor();
std::wstring extension = result.GetFileExtension();
std::wstring keywords = result.GetKeywords();

// Numeric properties
int64_t fileSize = result.GetSize();
int rank = result.GetRank();
int clickCount = result.GetClickCount();

// Date/Time properties (optional)
auto dateModified = result.GetDateModified();
if (dateModified.has_value()) {
    // Use FILETIME value
}

auto dateAccessed = result.GetDateAccessed();
auto dateCreated = result.GetDateCreated();
```

### Example 6: Custom Property Access

```cpp
// Access any property using PROPERTYKEY
PROPVARIANT pv;
PropVariantInit(&pv);

HRESULT hr = result.GetProperty(PKEY_Comment, pv);
if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR)
{
    std::wcout << L"Comment: " << pv.pwszVal << std::endl;
}

PropVariantClear(&pv);
```

---

## File Type Differentiation

### Example 7: Complete Type Checking

```cpp
void ProcessResult(const SearchResult& result)
{
    std::wstring kind = result.GetKind();
    
    if (result.IsDocument())
    {
        ProcessDocument(result);
    }
    else if (result.IsImage())
    {
        ProcessImage(result);
    }
    else if (result.IsMusic())
    {
        ProcessMusic(result);
    }
    else if (result.IsVideo())
    {
        ProcessVideo(result);
    }
    else if (result.IsEmail())
    {
        ProcessEmail(result);
    }
    else if (result.IsFolder())
    {
        ProcessFolder(result);
    }
    else if (result.IsContact())
    {
        ProcessContact(result);
    }
    else
    {
        std::wcout << L"Unknown type: " << kind << std::endl;
    }
}
```

### Example 8: Using System.Kind for Custom Logic

```cpp
std::wstring DetermineIcon(const SearchResult& result)
{
    if (result.IsDocument()) {
        // Check extension for specific document types
        auto ext = result.GetFileExtension();
        if (ext == L".pdf") return L"icon_pdf.png";
        if (ext == L".docx") return L"icon_word.png";
        if (ext == L".xlsx") return L"icon_excel.png";
        return L"icon_document.png";
    }
    else if (result.IsImage()) {
        return L"icon_image.png";
    }
    else if (result.IsMusic()) {
        return L"icon_music.png";
    }
    else if (result.IsVideo()) {
        return L"icon_video.png";
    }
    
    return L"icon_file.png";
}
```

---

## Click Tracking Integration

### Example 9: Track Clicks with SearchResult

```cpp
wsearch::SearchSession session({L"C:\\Users\\Documents"});
auto results = session.SearchResults(L"budget");

// Display results in UI...

// User clicks on a result
const auto& clickedResult = results[selectedIndex];

// Track the click using the helper method
std::wstring filePath = clickedResult.GetFilePathForTracking();
session.TrackResultClick(filePath);

// Open the file
ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOW);
```

### Example 10: GetFilePathForTracking()

```cpp
// Converts URL to file path automatically

SearchResult result = ...;

// System.ItemUrl might be: "file:///C:/Users/Documents/Report.pdf"
std::wstring url = result.GetPath();
std::wcout << L"URL: " << url << L"\n";

// GetFilePathForTracking() returns: "C:\Users\Documents\Report.pdf"
std::wstring path = result.GetFilePathForTracking();
std::wcout << L"Path: " << path << L"\n";

// Use path for click tracking
session.TrackResultClick(path);
```

---

## Real-World Integration Example

### Example 11: Search UI with Type Icons and Click Tracking

```cpp
struct SearchResultViewModel
{
    std::wstring displayName;
    std::wstring iconPath;
    std::wstring path;
    int64_t size;
    int rank;
    int popularity;  // click count
};

std::vector<SearchResultViewModel> BuildSearchUI(
    const std::vector<SearchResult>& results)
{
    std::vector<SearchResultViewModel> viewModels;
    
    for (const auto& result : results)
    {
        SearchResultViewModel vm;
        vm.displayName = result.GetFileName();
        vm.path = result.GetFilePathForTracking();
        vm.size = result.GetSize();
        vm.rank = result.GetRank();
        vm.popularity = result.GetClickCount();
        
        // Determine icon based on type
        if (result.IsDocument()) {
            auto ext = result.GetFileExtension();
            vm.iconPath = GetDocumentIcon(ext);
        }
        else if (result.IsImage()) {
            vm.iconPath = L"icons/image.png";
        }
        else if (result.IsMusic()) {
            vm.iconPath = L"icons/music.png";
        }
        else if (result.IsVideo()) {
            vm.iconPath = L"icons/video.png";
        }
        else {
            vm.iconPath = L"icons/file.png";
        }
        
        viewModels.push_back(vm);
    }
    
    return viewModels;
}

void OnResultClicked(const SearchResultViewModel& vm,
                     wsearch::SearchSession& session)
{
    // Track click for analytics
    session.TrackResultClick(vm.path);
    
    // Open file
    ShellExecuteW(nullptr, L"open", vm.path.c_str(),
                  nullptr, nullptr, SW_SHOW);
}
```

### Example 12: Sorting and Filtering

```cpp
// Sort by popularity (click count)
auto results = session.SearchResults(L"report");

std::sort(results.begin(), results.end(),
    [](const SearchResult& a, const SearchResult& b) {
        return a.GetClickCount() > b.GetClickCount();
    });

// Filter documents only
std::vector<SearchResult> documents;
std::copy_if(results.begin(), results.end(),
    std::back_inserter(documents),
    [](const SearchResult& r) { return r.IsDocument(); });

// Filter by size (files > 1MB)
std::vector<SearchResult> largeFiles;
std::copy_if(results.begin(), results.end(),
    std::back_inserter(largeFiles),
    [](const SearchResult& r) { return r.GetSize() > 1024 * 1024; });
```

---

## Type Checking Matrix

| Method | System.Kind Values |
|--------|-------------------|
| `IsDocument()` | "document" |
| `IsImage()` | "picture", "image" |
| `IsMusic()` | "music" |
| `IsVideo()` | "video" |
| `IsFolder()` | "folder" |
| `IsEmail()` | "email" |
| `IsContact()` | "contact" |
| `IsLink()` | "link" |

---

## Best Practices

1. **Use type checking** to provide appropriate UI for different file types
2. **Always use `GetFilePathForTracking()`** when passing paths to `TrackResultClick()`
3. **Cache frequently accessed properties** if processing many results
4. **Check `IsValid()`** before accessing result properties
5. **Use the underlying PropertyStore** for advanced scenarios with `GetPropertyStore()`

---

## See Also

- [Query Builder Examples](querybuildersamples.md)
- [Session Examples](sessionsexamples.md)
- [Full App Example](fullappexample.md)
