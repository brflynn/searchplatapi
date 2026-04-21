// pch.cpp: source file corresponding to the pre-compiled header
#include "pch.h"

// Define the TraceLogging provider for Search Platform Core
TRACELOGGING_DEFINE_PROVIDER(
    g_hSearchPlatformCoreProvider,
    "Microsoft.Windows.Search.Platform.Core",
    (0x3e8d3d3e, 0x1234, 0x5678, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78));
