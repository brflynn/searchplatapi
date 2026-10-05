#pragma once

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include "MainWindow.g.h"
#pragma pop_macro("GetCurrentTime")

#include <SearchSessions.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "LocalIndex.h"
#include "BackgroundIndexer.h"

namespace winrt::SearchApp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void SearchTextBox_TextChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

        void SearchTextBox_KeyDown(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        void SearchResults_DoubleTapped(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

        void LayoutRoot_SizeChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void Reindex_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Settings_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ResultActions_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        static constexpr int GlobalSearchHotkeyId = 1;

        void OpenSelectedResult();
        Windows::Foundation::IAsyncAction InitializeLocalIndexAsync();
        void RefreshSearch();
        Windows::Foundation::IAsyncAction ReloadSettingsAsync();
        Windows::Foundation::IAsyncAction ChangeSettingAsync(std::wstring operation, std::wstring kind, std::wstring value);
        Windows::Foundation::IAsyncAction RequestContentScopeAsync(std::wstring folder);
        void PopulateSettings();
        void UpdateIndexerProgress();
        Windows::Foundation::IAsyncAction UpdateIndexStatisticsAsync();
        void ToggleFromHotkey();
        static LRESULT CALLBACK HotkeyWindowProc(
            HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        Windows::Foundation::IAsyncAction ExecuteSearchAsync(
            std::wstring searchText, uint32_t generation);

        std::unique_ptr<wsearch::SearchAsYouTypeSession> m_searchSession;
        std::atomic<uint32_t> m_queryGeneration{ 0 };
        std::mutex m_searchSessionMutex;
        std::shared_ptr<std::atomic<bool>> m_closed = std::make_shared<std::atomic<bool>>(false);
        applocal::IndexSettings m_settings;
        bool m_settingsReady = false;
        std::wstring m_settingsError;
        std::wstring m_operationError;
        bool m_uiSmokeTest = false;
        bool m_settingChangeInFlight = false;
        std::atomic<bool> m_visibilityChanging{ false };
        uint32_t m_settingsReloadGeneration = 0;
        std::unordered_set<std::wstring> m_scopeRequests;
        Microsoft::UI::Xaml::Controls::StackPanel m_settingsPanel{ nullptr };
        Microsoft::UI::Xaml::Controls::ContentDialog m_settingsDialog{ nullptr };

        // "True index" full-filesystem search (superset of whatever the
        // Windows Search indexer covers). Queried alongside m_searchSession
        // on every keystroke and merged by path in ExecuteSearchAsync; may
        // be null if the on-disk index couldn't be opened, in which case
        // search falls back to Windows Search with an explicit settings error.
        std::shared_ptr<applocal::LocalIndex> m_localIndex;
        std::atomic<bool> m_localInitializationComplete{ false };
        std::unique_ptr<applocal::BackgroundIndexer> m_backgroundIndexer;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_searchDebounceTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_indexStatusTimer{ nullptr };
        std::atomic<bool> m_indexStatisticsRefreshInFlight{ false };
        uint32_t m_indexStatusTick = 0;
        std::wstring m_pendingSearchText;
        uint32_t m_pendingSearchGeneration = 0;
        HWND m_windowHandle = nullptr;
        HWND m_hotkeyWindow = nullptr;
        bool m_hotkeyRegistered = false;
    };
}

namespace winrt::SearchApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
