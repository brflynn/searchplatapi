#pragma once

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include "MainWindow.g.h"
#pragma pop_macro("GetCurrentTime")

#include <SearchSessions.h>
#include <atomic>
#include <memory>

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
        Windows::Foundation::IAsyncAction ExecuteSearchAsync(
            std::wstring searchText, uint32_t generation);

        std::unique_ptr<wsearch::SearchAsYouTypeSession> m_searchSession;
        std::atomic<uint32_t> m_queryGeneration{ 0 };
    };
}

namespace winrt::SearchApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
