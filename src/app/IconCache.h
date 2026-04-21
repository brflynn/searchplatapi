// IconCache.h - Per-extension file icon cache using StorageItemThumbnail
// Adapted from SearchResultImageUriManager pattern (github.com/brflynn/searchapp)
#pragma once

#include <unordered_map>
#include <string>
#include <wil/resource.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>

struct IconCache
{
    winrt::Windows::Storage::FileProperties::StorageItemThumbnail
    GetOrLoadThumbnail(const std::wstring& filePath, bool isFolder)
    {
        // Check cache first
        {
            auto lock = m_cs.lock();
            if (isFolder)
            {
                if (m_folderThumbnail != nullptr)
                    return m_folderThumbnail;
            }
            else
            {
                auto dot = filePath.rfind(L'.');
                if (dot != std::wstring::npos)
                {
                    auto ext = filePath.substr(dot);
                    auto it = m_extCache.find(ext);
                    if (it != m_extCache.end())
                        return it->second;
                }
            }
        }

        // Cache miss - load the thumbnail (called on background thread)
        try
        {
            if (isFolder)
            {
                auto folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
                    winrt::hstring(filePath)).get();
                auto thumb = folder.GetThumbnailAsync(
                    winrt::Windows::Storage::FileProperties::ThumbnailMode::ListView, 24).get();

                auto lock = m_cs.lock();
                m_folderThumbnail = thumb;
                return thumb;
            }
            else
            {
                auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                    winrt::hstring(filePath)).get();
                auto thumb = file.GetThumbnailAsync(
                    winrt::Windows::Storage::FileProperties::ThumbnailMode::ListView, 24).get();

                auto dot = filePath.rfind(L'.');
                if (dot != std::wstring::npos)
                {
                    auto ext = filePath.substr(dot);
                    auto lock = m_cs.lock();
                    m_extCache.emplace(ext, thumb);
                }
                return thumb;
            }
        }
        catch (...)
        {
            return nullptr;
        }
    }

private:
    std::unordered_map<std::wstring,
        winrt::Windows::Storage::FileProperties::StorageItemThumbnail> m_extCache;
    winrt::Windows::Storage::FileProperties::StorageItemThumbnail m_folderThumbnail{ nullptr };
    wil::critical_section m_cs;
};
