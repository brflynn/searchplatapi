#pragma once

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include "MainWindow.g.h"
#pragma pop_macro("GetCurrentTime")

#include <SearchSessions.h>
#include <atomic>
#include <memory>

#include "LocalIndex.h"
#include "BackgroundIndexer.h"

namespace winrt::SearchApp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void SearchTextBox_TextChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

        void SearchTextBox_KeyDown(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        void SearchResults_DoubleTapped(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

    private:
        void OpenSelectedResult();
        void UpdateIndexStatus();
        Windows::Foundation::IAsyncAction ExecuteSearchAsync(
            std::wstring searchText, uint32_t generation);

        std::unique_ptr<wsearch::SearchAsYouTypeSession> m_searchSession;
        std::atomic<uint32_t> m_queryGeneration{ 0 };

        // "True index" full-filesystem search (superset of whatever the
        // Windows Search indexer covers). Queried alongside m_searchSession
        // on every keystroke and merged by path in ExecuteSearchAsync; may
        // be null if the on-disk index couldn't be opened, in which case
        // search silently falls back to indexer-only results.
        std::shared_ptr<applocal::LocalIndex> m_localIndex;
        std::unique_ptr<applocal::BackgroundIndexer> m_backgroundIndexer;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_indexStatusTimer{ nullptr };
    };
}

namespace winrt::SearchApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
