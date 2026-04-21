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

    private:
        // UI construction
        void BuildUI();
        winrt::Microsoft::UI::Xaml::Controls::Grid CreateResultItemVisual(
            const std::wstring& name,
            const std::wstring& path,
            winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumbnail);

        // Event handlers
        void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnSearchKeyDown(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnResultsDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
        void OpenSelectedResult();

        // Async search
        winrt::Windows::Foundation::IAsyncAction ExecuteSearchAsync(
            std::wstring searchText, uint32_t generation);

        // UI elements
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_searchBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView m_listView{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_statusText{ nullptr };

        // Search state
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
