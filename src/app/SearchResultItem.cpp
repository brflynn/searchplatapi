#include "pch.h"
#include "SearchResultItem.h"

#include <winrt/Windows.UI.h>

#if __has_include("SearchResultItem.g.cpp")
#include "SearchResultItem.g.cpp"
#endif

namespace winrt::SearchApp::implementation
{
    hstring SearchResultItem::DisplayName()
    {
        return m_displayName;
    }

    hstring SearchResultItem::FilePath()
    {
        return m_filePath;
    }

    bool SearchResultItem::IsFolder()
    {
        return m_isFolder;
    }

    Microsoft::UI::Xaml::Media::Imaging::BitmapImage SearchResultItem::ItemImage()
    {
        Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmapImage{};
        if (m_thumbnail != nullptr)
        {
            bitmapImage.SetSource(m_thumbnail.CloneStream());
        }
        return bitmapImage;
    }

    SearchApp::MatchKind SearchResultItem::ResultMatchKind()
    {
        return m_matchKind;
    }

    hstring SearchResultItem::MatchKindLabel()
    {
        switch (m_matchKind)
        {
        case SearchApp::MatchKind::Metadata: return L"META";
        case SearchApp::MatchKind::Content:  return L"TEXT";
        case SearchApp::MatchKind::Filename:
        default:                             return L"NAME";
        }
    }

    Microsoft::UI::Xaml::Media::Brush SearchResultItem::MatchKindBrush()
    {
        using winrt::Windows::UI::ColorHelper;
        winrt::Windows::UI::Color color{};
        switch (m_matchKind)
        {
        case SearchApp::MatchKind::Metadata:
            color = ColorHelper::FromArgb(0xFF, 0x8A, 0x5C, 0xF6); // purple-ish
            break;
        case SearchApp::MatchKind::Content:
            color = ColorHelper::FromArgb(0xFF, 0x3D, 0xB8, 0x6A); // green-ish
            break;
        case SearchApp::MatchKind::Filename:
        default:
            color = ColorHelper::FromArgb(0xFF, 0x4A, 0x9E, 0xF6); // blue-ish
            break;
        }
        return Microsoft::UI::Xaml::Media::SolidColorBrush(color);
    }
}
