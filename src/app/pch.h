// pch.h: Precompiled header for SearchApp
#pragma once

// Windows SDK
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <hstring.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

// COM/OLE/Search headers needed by SearchPlatAPI
#include <NTQuery.h>
#include <oledb.h>
#include <SearchAPI.h>
#include <shlobj.h>
#include <KnownFolders.h>
#include <propkey.h>
#include <propsys.h>
#include <intsafe.h>

// TraceLogging
#include <sal.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>

// WinRT base (required by SearchPlatAPI headers)
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

// WinUI3 projections (needed so consume_* templates for controls/windowing are
// fully defined before MainWindow.xaml.cpp/App.xaml.cpp call into them)
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Interop.h>

// WIL
#include <wil/resource.h>
#include <wil/result.h>

// Standard library
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
