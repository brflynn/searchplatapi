// Copyright (C) Microsoft Corporation

#include <sal.h>
#include <wil/result.h>
#include <windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>

#pragma push_macro("_Printf_format_string_")
#undef _Printf_format_string_
#define _Printf_format_string_ _In_

TRACELOGGING_DECLARE_PROVIDER(g_hSearchPlatformCoreProvider);

namespace wsearch
{

/* Base class for search provider telemetry using WIL TraceLogging
 * Provider: Windows.Search.Platform.Core
 * Derived classes can override OnErrorReported to provide custom error handling
 */
struct TelemetryProvider
{
    virtual ~TelemetryProvider() = default;

    template <typename... args_t>
    void TraceLoggingInfo(_Printf_format_string_ const wchar_t* format, args_t&&... args)
    {
        auto str = wil::str_printf_failfast<wil::unique_cotaskmem_string>(format, wistd::forward<args_t>(args)...);
        TraceLoggingWrite(
            g_hSearchPlatformCoreProvider,
            "SearchProviderInfo",
            TraceLoggingLevel(WINEVENT_LEVEL_INFO),
            TraceLoggingWideString(str.get(), "Message"));
        OnTraceLoggingInfo(str.get());
    }

    template <typename... args_t>
    void TraceLoggingError(_Printf_format_string_ const wchar_t* format, args_t&&... args)
    {
        auto str = wil::str_printf_failfast<wil::unique_cotaskmem_string>(format, wistd::forward<args_t>(args)...);
        TraceLoggingWrite(
            g_hSearchPlatformCoreProvider,
            "SearchProviderError",
            TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
            TraceLoggingWideString(str.get(), "Message"));
        OnErrorReported(str.get());
    }

    template <typename... args_t>
    static void LogInfo(_Printf_format_string_ const wchar_t* format, args_t&&... args)
    {
        auto str = wil::str_printf_failfast<wil::unique_cotaskmem_string>(format, wistd::forward<args_t>(args)...);
        TraceLoggingWrite(
            g_hSearchPlatformCoreProvider,
            "SearchProviderInfo",
            TraceLoggingLevel(WINEVENT_LEVEL_INFO),
            TraceLoggingWideString(str.get(), "Message"));
        wprintf(L"[INFO] %ls\r\n", str.get());
        OutputDebugStringW(L"[INFO] ");
        OutputDebugStringW(str.get());
        OutputDebugStringW(L"\n");
    }

    template <typename... args_t>
    static void LogError(_Printf_format_string_ const wchar_t* format, args_t&&... args)
    {
        auto str = wil::str_printf_failfast<wil::unique_cotaskmem_string>(format, wistd::forward<args_t>(args)...);
        TraceLoggingWrite(
            g_hSearchPlatformCoreProvider,
            "SearchProviderError",
            TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
            TraceLoggingWideString(str.get(), "Message"));
        wprintf(L"[ERROR] %ls\r\n", str.get());
        OutputDebugStringW(L"[ERROR] ");
        OutputDebugStringW(str.get());
        OutputDebugStringW(L"\n");
    }

protected:
    virtual void OnTraceLoggingInfo(const wchar_t* message)
    {
        // Default implementation - can be overridden
    }

    virtual void OnErrorReported(const wchar_t* message)
    {
        wprintf(L"[ERROR] %ls\r\n", message);
        OutputDebugStringW(L"[ERROR] ");
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
    }
};

} // namespace wsearch
