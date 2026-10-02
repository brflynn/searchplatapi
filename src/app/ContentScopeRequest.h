#pragma once

#include "IndexSettings.h"
#include <SearchPlatCore.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace applocal
{
    inline std::wstring FolderFileUrl(const std::wstring& folder)
    {
        auto path = NormalizeFolder(folder);
        if (path.back() != L'\\') path += L'\\';
        std::replace(path.begin(), path.end(), L'\\', L'/');
        bool unc = path.starts_with(L"//");
        if (unc) path.erase(0, 2);
        int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
        if (!count) throw std::runtime_error("Cannot encode folder URL");
        std::string bytes(count, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), bytes.data(), count, nullptr, nullptr))
            throw std::runtime_error("Cannot encode folder URL");
        std::wstring url = unc ? L"file://" : L"file:///";
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(bytes[i]);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' ||
                c == '/' || (!unc && i == 1 && c == ':'))
                url += c;
            else
            {
                url += L'%';
                url += hex[c >> 4];
                url += hex[c & 15];
            }
        }
        return url;
    }

    // Called only after an explicit user request, on a background MTA thread.
    inline ContentScope RunContentScopeRequest(const std::wstring& folder, const std::atomic<bool>& cancelled)
    {
        ContentScope result{ NormalizeFolder(folder), L"failed", L"" };
        if (cancelled.load()) return { result.folder, L"cancelled", L"Cancelled before changing Windows Search scope." };
        bool saved = false;
        try
        {
            winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            auto uninitialize = wil::scope_exit([] { CoUninitialize(); });
            if (cancelled.load()) return { result.folder, L"cancelled", L"Cancelled before changing Windows Search scope." };
            auto url = FolderFileUrl(result.folder);
            auto catalog = wsearch::details::GetSystemIndexCatalogManager();
            winrt::com_ptr<ISearchCrawlScopeManager> scopes;
            winrt::check_hresult(catalog->GetCrawlScopeManager(scopes.put()));
            BOOL included = FALSE;
            winrt::check_hresult(scopes->IncludedInCrawlScope(url.c_str(), &included));
            if (!included)
            {
                winrt::check_hresult(scopes->AddUserScopeRule(url.c_str(), TRUE, FALSE, 0));
                winrt::check_hresult(scopes->SaveAll());
            }
            saved = true;
            std::wstring escaped;
            for (auto c : url)
            {
                escaped += c;
                if (c == L'\'') escaped += c;
            }
            auto rowset = wsearch::details::ExecuteQuery(
                L"SELECT System.ItemUrl FROM SystemIndex WHERE SCOPE='" + escaped + L"'");
            auto priority = rowset.as<IRowsetPrioritization>();
            // The SDK calls background priority LOW; there is no BACKGROUND enum.
            winrt::check_hresult(priority->SetScopePriority(PRIORITY_LEVEL_LOW, 1000));
            auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
            while (std::chrono::steady_clock::now() < deadline)
            {
                // Give the service time to observe the new inclusion rule before checking idle.
                for (int i = 0; i < 20 && !cancelled.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (cancelled.load())
                    return { result.folder, L"cancelled", L"Scope included; monitoring cancelled. Windows Search continues independently." };
                CatalogStatus status{};
                CatalogPausedReason reason{};
                winrt::check_hresult(catalog->GetCatalogStatus(&status, &reason));
                if (status == CATALOG_STATUS_IDLE)
                    return { result.folder, L"idle",
                        L"Scope included; catalog is idle. This does not prove folder content is indexed; filters and Search settings determine coverage." };
            }
            return { result.folder, L"timeout",
                L"Scope included; catalog was not idle within two minutes. Windows Search continues independently." };
        }
        catch (const winrt::hresult_error& error)
        {
            wchar_t code[16]{};
            swprintf_s(code, L"0x%08X", static_cast<unsigned>(error.code().value));
            result.detail = saved ? L"Scope included, but priority/monitoring failed: " : L"Scope request failed: ";
            result.detail += code;
            result.detail += L" ";
            result.detail += error.message().c_str();
            if (error.code() == E_ACCESSDENIED)
                result.detail += L" Permission denied. Configure Indexing Options with administrator assistance; the app does not auto-elevate.";
        }
        catch (const std::exception& error)
        {
            result.detail = saved ? L"Scope included, but monitoring failed: " : L"Scope request failed: ";
            result.detail += std::wstring(error.what(), error.what() + strlen(error.what()));
        }
        return result;
    }
}
