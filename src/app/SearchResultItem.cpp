#include "pch.h"
#include "SearchResultItem.h"

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
}
