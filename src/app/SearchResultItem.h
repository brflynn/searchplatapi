#pragma once
#include "SearchResultItem.g.h"

#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::SearchApp::implementation
{
    struct SearchResultItem : SearchResultItemT<SearchResultItem>
    {
        SearchResultItem() = default;

        SearchResultItem(
            hstring displayName,
            hstring filePath,
            bool isFolder,
            Windows::Storage::FileProperties::StorageItemThumbnail thumbnail)
            : m_displayName(std::move(displayName))
            , m_filePath(std::move(filePath))
            , m_isFolder(isFolder)
            , m_thumbnail(std::move(thumbnail))
        {
        }

        hstring DisplayName();
        hstring FilePath();
        bool IsFolder();
        Microsoft::UI::Xaml::Media::Imaging::BitmapImage ItemImage();

    private:
        hstring m_displayName;
        hstring m_filePath;
        bool m_isFolder{ false };
        Windows::Storage::FileProperties::StorageItemThumbnail m_thumbnail{ nullptr };
    };
}

namespace winrt::SearchApp::factory_implementation
{
    struct SearchResultItem : SearchResultItemT<SearchResultItem, implementation::SearchResultItem>
    {
    };
}
