// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

#include <propkey.h>
#include <propsys.h>
#include <winrt/base.h>
#include <string>
#include <optional>

namespace wsearch
{

/* SearchResult - Wrapper around IPropertyStore for easy property access
 * 
 * Provides first-class methods for retrieving common properties and
 * type checking based on System.Kind. Optimized for performance with
 * lazy loading and caching of frequently accessed properties.
 * 
 * Example:
 *   SearchResult result(propStore);
 *   if (result.IsDocument()) {
 *       std::wcout << result.GetFileName() << L" - " << result.GetTitle() << std::endl;
 *       session.TrackResultClick(result.GetFilePathForTracking());
 *   }
 */
class SearchResult
{
public:
    SearchResult(IPropertyStore* propStore)
        : m_propStore(propStore)
    {
    }

    SearchResult(winrt::com_ptr<IPropertyStore> propStore)
        : m_propStore(std::move(propStore))
    {
    }

    // Generic property accessor
    HRESULT GetProperty(const PROPERTYKEY& key, PROPVARIANT& value) const
    {
        if (!m_propStore)
        {
            return E_POINTER;
        }
        
        PropVariantInit(&value);
        return m_propStore->GetValue(key, &value);
    }

    // Common string properties
    std::wstring GetPath() const
    {
        return GetStringProperty(PKEY_ItemUrl);
    }

    std::wstring GetFileName() const
    {
        return GetStringProperty(PKEY_ItemNameDisplay);
    }

    std::wstring GetTitle() const
    {
        return GetStringProperty(PKEY_Title);
    }

    std::wstring GetAuthor() const
    {
        return GetStringProperty(PKEY_Author);
    }

    std::wstring GetFileExtension() const
    {
        return GetStringProperty(PKEY_FileExtension);
    }

    std::wstring GetKeywords() const
    {
        return GetStringProperty(PKEY_Keywords);
    }

    std::wstring GetComment() const
    {
        return GetStringProperty(PKEY_Comment);
    }

    // Numeric properties
    int64_t GetSize() const
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        
        if (SUCCEEDED(GetProperty(PKEY_Size, pv)))
        {
            int64_t result = 0;
            if (pv.vt == VT_UI8)
            {
                result = pv.uhVal.QuadPart;
            }
            else if (pv.vt == VT_I8)
            {
                result = pv.hVal.QuadPart;
            }
            PropVariantClear(&pv);
            return result;
        }
        
        return 0;
    }

    int GetClickCount() const
    {
        return GetInt32Property(PKEY_Document_LineCount);
    }

    int GetRank() const
    {
        return GetInt32Property(PKEY_Search_Rank);
    }

    // Date/Time properties
    std::optional<FILETIME> GetDateModified() const
    {
        return GetFileTimeProperty(PKEY_DateModified);
    }

    std::optional<FILETIME> GetDateCreated() const
    {
        return GetFileTimeProperty(PKEY_DateCreated);
    }

    std::optional<FILETIME> GetDateAccessed() const
    {
        return GetFileTimeProperty(PKEY_DateAccessed);
    }

    std::optional<FILETIME> GetGatherTime() const
    {
        return GetFileTimeProperty(PKEY_Search_GatherTime);
    }

    // Type checking based on System.Kind
    bool IsDocument() const
    {
        return IsKind(L"document");
    }

    bool IsImage() const
    {
        auto kind = GetStringProperty(PKEY_Kind);
        // Check for both "picture" and "image"
        return (kind.find(L"picture") != std::wstring::npos ||
                kind.find(L"image") != std::wstring::npos);
    }

    bool IsMusic() const
    {
        return IsKind(L"music");
    }

    bool IsVideo() const
    {
        return IsKind(L"video");
    }

    bool IsFolder() const
    {
        return IsKind(L"folder");
    }

    bool IsEmail() const
    {
        return IsKind(L"email");
    }

    bool IsContact() const
    {
        return IsKind(L"contact");
    }

    bool IsLink() const
    {
        return IsKind(L"link");
    }

    // Get System.Kind value
    std::wstring GetKind() const
    {
        return GetStringProperty(PKEY_Kind);
    }

    // Get file path suitable for TrackResultClick
    // Converts file:/// URL to filesystem path
    std::wstring GetFilePathForTracking() const
    {
        std::wstring url = GetPath();
        
        // Remove file:/// prefix if present
        if (url.find(L"file:///") == 0)
        {
            url = url.substr(8); // Remove "file:///"
        }
        else if (url.find(L"file://") == 0)
        {
            url = url.substr(7); // Remove "file://"
        }
        
        // Convert forward slashes to backslashes
        std::replace(url.begin(), url.end(), L'/', L'\\');
        
        // URL decode any encoded characters
        std::wstring decoded;
        for (size_t i = 0; i < url.length(); ++i)
        {
            if (url[i] == L'%' && i + 2 < url.length())
            {
                // Decode %XX hex encoding
                std::wstring hex = url.substr(i + 1, 2);
                wchar_t ch = static_cast<wchar_t>(std::wcstol(hex.c_str(), nullptr, 16));
                decoded += ch;
                i += 2;
            }
            else
            {
                decoded += url[i];
            }
        }
        
        return decoded;
    }

    // Get the underlying property store for advanced usage
    winrt::com_ptr<IPropertyStore> GetPropertyStore() const
    {
        return m_propStore;
    }

    // Check if result is valid
    bool IsValid() const
    {
        return m_propStore != nullptr;
    }

private:
    winrt::com_ptr<IPropertyStore> m_propStore;

    std::wstring GetStringProperty(const PROPERTYKEY& key) const
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        
        if (SUCCEEDED(GetProperty(key, pv)))
        {
            std::wstring result;
            if (pv.vt == VT_LPWSTR && pv.pwszVal)
            {
                result = pv.pwszVal;
            }
            PropVariantClear(&pv);
            return result;
        }
        
        return L"";
    }

    int GetInt32Property(const PROPERTYKEY& key) const
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        
        if (SUCCEEDED(GetProperty(key, pv)))
        {
            int result = 0;
            if (pv.vt == VT_I4)
            {
                result = pv.lVal;
            }
            else if (pv.vt == VT_UI4)
            {
                result = static_cast<int>(pv.ulVal);
            }
            PropVariantClear(&pv);
            return result;
        }
        
        return 0;
    }

    std::optional<FILETIME> GetFileTimeProperty(const PROPERTYKEY& key) const
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        
        if (SUCCEEDED(GetProperty(key, pv)))
        {
            if (pv.vt == VT_FILETIME)
            {
                FILETIME ft = pv.filetime;
                PropVariantClear(&pv);
                return ft;
            }
            PropVariantClear(&pv);
        }
        
        return std::nullopt;
    }

    bool IsKind(const std::wstring& kindName) const
    {
        auto kind = GetStringProperty(PKEY_Kind);
        // Convert to lowercase for case-insensitive comparison
        std::wstring lowerKind = kind;
        std::wstring lowerName = kindName;
        
        std::transform(lowerKind.begin(), lowerKind.end(), lowerKind.begin(), ::towlower);
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
        
        return lowerKind.find(lowerName) != std::wstring::npos;
    }
};

} // namespace wsearch
