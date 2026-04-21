#include "pch.h"
#include "App.h"

#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

#include "MainWindow.h"

namespace winrt::SearchApp::implementation
{
    App::App()
    {
        // Load WinUI 3 control resources (replaces App.xaml)
        auto resources = winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources();
        Resources().MergedDictionaries().Append(resources);

        // Set dark theme
        RequestedTheme(winrt::Microsoft::UI::Xaml::ApplicationTheme::Dark);

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
