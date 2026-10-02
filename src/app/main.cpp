// SearchApp entry point: starts the WinUI3 / C++/WinRT application (see
// App.xaml.cpp / MainWindow.xaml.cpp). The project is built as a self-contained
// Windows App SDK deployment (WindowsAppSDKSelfContained=true), so WinRT
// activation is handled via an auto-generated UndockedRegFreeWinRT
// initializer and no explicit MddBootstrapInitialize call is required.
#include "pch.h"
#include "App.xaml.h"

int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&)
        {
            winrt::make<winrt::SearchApp::implementation::App>();
        });

    return 0;
}
