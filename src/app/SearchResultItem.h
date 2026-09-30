#pragma once
#include "SearchResultItem.g.h"

#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
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
            : SearchResultItem(std::move(displayName), std::move(filePath), isFolder,
                std::move(thumbnail), SearchApp::MatchKind::Filename)
        {
        }

        // Additive overload carrying match provenance. Not WinRT-activatable
        // via IDL (only the parameterless constructor is projected); called
        // directly via winrt::make<implementation::SearchResultItem>(...)
        // from MainWindow.xaml.cpp.
        SearchResultItem(
            hstring displayName,
            hstring filePath,
            bool isFolder,
            Windows::Storage::FileProperties::StorageItemThumbnail thumbnail,
            SearchApp::MatchKind matchKind)
            : m_displayName(std::move(displayName))
            , m_filePath(std::move(filePath))
            , m_isFolder(isFolder)
            , m_thumbnail(std::move(thumbnail))
            , m_matchKind(matchKind)
        {
        }

        hstring DisplayName();
        hstring FilePath();
        bool IsFolder();
        Microsoft::UI::Xaml::Media::Imaging::BitmapImage ItemImage();
        SearchApp::MatchKind ResultMatchKind();
        hstring MatchKindLabel();
        Microsoft::UI::Xaml::Media::Brush MatchKindBrush();

    private:
        hstring m_displayName;
        hstring m_filePath;
        bool m_isFolder{ false };
        Windows::Storage::FileProperties::StorageItemThumbnail m_thumbnail{ nullptr };
        SearchApp::MatchKind m_matchKind{ SearchApp::MatchKind::Filename };
    };
}

namespace winrt::SearchApp::factory_implementation
{
    struct SearchResultItem : SearchResultItemT<SearchResultItem, implementation::SearchResultItem>
    {
    };
}
