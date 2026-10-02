#pragma once

#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace applocal
{
    inline std::wstring NormalizeFolder(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (path.starts_with(L"\\\\?\\UNC\\")) path = L"\\\\" + path.substr(8);
        else if (path.starts_with(L"\\\\?\\")) path.erase(0, 4);
        if (!(path.size() >= 3 && path[1] == L':' && path[2] == L'\\') &&
            !path.starts_with(L"\\\\"))
            throw std::invalid_argument("Folder must be an absolute Windows path");
        DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (!length) throw std::runtime_error("Cannot normalize folder path");
        std::wstring full(length, L'\0');
        DWORD written = GetFullPathNameW(path.c_str(), length, full.data(), nullptr);
        if (!written || written >= length) throw std::runtime_error("Cannot normalize folder path");
        full.resize(written);
        while (full.size() > 3 && full.back() == L'\\') full.pop_back();
        std::wstring lower(full.size(), L'\0');
        if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            full.data(), static_cast<int>(full.size()), lower.data(), static_cast<int>(lower.size()),
            nullptr, nullptr, 0))
            throw std::runtime_error("Cannot normalize folder case");
        return lower;
    }

    inline bool IsWithinFolder(const std::wstring& normalizedPath, const std::wstring& folder)
    {
        if (folder.empty()) return false;
        return normalizedPath == folder ||
            (normalizedPath.starts_with(folder) &&
                (folder.back() == L'\\' || normalizedPath[folder.size()] == L'\\'));
    }

    inline std::wstring ResultFolder(const std::wstring& path, bool isFolder)
    {
        auto normalized = NormalizeFolder(path);
        if (isFolder) return normalized;
        auto slash = normalized.find_last_of(L'\\');
        if (slash == std::wstring::npos) return {};
        return normalized.substr(0, slash == 2 ? 3 : slash);
    }

    inline std::wstring ResultExtension(const std::wstring& path, bool isFolder)
    {
        if (isFolder) return {};
        auto nameStart = path.find_last_of(L"\\/");
        nameStart = nameStart == std::wstring::npos ? 0 : nameStart + 1;
        auto dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos || dot <= nameStart || dot + 1 == path.size()) return {};
        auto extension = path.substr(dot);
        CharLowerBuffW(extension.data(), static_cast<DWORD>(extension.size()));
        return extension;
    }

    struct Exclusion
    {
        std::wstring kind; // folder or extension
        std::wstring value;
    };

    struct ContentScope
    {
        std::wstring folder;
        std::wstring state;
        std::wstring detail;
    };

    struct IndexSettings
    {
        std::vector<Exclusion> exclusions;
        std::vector<ContentScope> contentScopes;

        bool IsExcluded(const std::wstring& path, bool isFolder) const
        {
            auto normalized = NormalizeFolder(path);
            auto extension = ResultExtension(normalized, isFolder);
            for (const auto& rule : exclusions)
            {
                if (rule.kind == L"folder" && IsWithinFolder(normalized, rule.value)) return true;
                if (rule.kind == L"extension" && !extension.empty() && extension == rule.value) return true;
            }
            return false;
        }
    };
}
