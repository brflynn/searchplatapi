#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "SearchResultItem.h"
#include "IconCache.h"
#include "MatchKindClassifier.h"
#include <SearchSessions.h>
#include <SearchResult.h>
#include <shellapi.h>
#include <algorithm>
#include <unordered_set>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Windows::Foundation;

namespace
{
    IconCache g_iconCache;
    constexpr int MaxResults = 50;

    std::wstring FormatCount(uint64_t value)
    {
        std::wstring text = std::to_wstring(value);
        for (size_t position = text.size(); position > 3; position -= 3)
        {
            text.insert(position - 3, 1, L',');
        }
        return text;
    }

    std::wstring FormatElapsed(std::chrono::milliseconds elapsed)
    {
        auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        auto hours = totalSeconds / 3600;
        auto minutes = (totalSeconds % 3600) / 60;
        auto seconds = totalSeconds % 60;

        if (hours > 0)
        {
            return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
        }
        if (minutes > 0)
        {
            return std::to_wstring(minutes) + L"m " + std::to_wstring(seconds) + L"s";
        }
        return std::to_wstring(seconds) + L"s";
    }

    // Case-insensitive, backslash-normalized key used to dedupe a result
    // that both the indexer and the local index produced for the same file.
    std::wstring NormalizePathKey(std::wstring path)
    {
        for (auto& c : path)
        {
            c = static_cast<wchar_t>(towlower(c));
            if (c == L'/') c = L'\\';
        }
        return path;
    }

    SearchApp::MatchKind ToWinRtMatchKind(applocal::ClassifiedMatchKind kind)
    {
        switch (kind)
        {
        case applocal::ClassifiedMatchKind::Metadata: return SearchApp::MatchKind::Metadata;
        case applocal::ClassifiedMatchKind::Content:  return SearchApp::MatchKind::Content;
        case applocal::ClassifiedMatchKind::Filename:
        default:                                      return SearchApp::MatchKind::Filename;
        }
    }

    // A source-agnostic intermediate result used to merge the indexer's
    // fast-path hits with the LocalIndex's full-filesystem hits by path
    // before materializing WinRT SearchResultItem objects (so a path
    // present in both sources only loads one thumbnail and keeps the
    // higher-priority MatchKind - Filename > Metadata > Content).
    struct MergedResult
    {
        std::wstring name;
        std::wstring path;
        bool isFolder = false;
        applocal::ClassifiedMatchKind matchKind = applocal::ClassifiedMatchKind::Filename;
    };

    // Enumerate up to maxResults rows from a rowset, calling callback for each
    void EnumerateTopNResults(IRowset* rowset, int maxResults,
        std::function<void(IPropertyStore*)> callback)
    {
        winrt::com_ptr<IGetRow> getRow;
        THROW_IF_FAILED(rowset->QueryInterface(IID_PPV_ARGS(getRow.put())));

        int count = 0;
        DBCOUNTITEM rowCountReturned;

        do
        {
            HROW rowBuffer[100];
            HROW* rowReturned = rowBuffer;

            THROW_IF_FAILED(rowset->GetNextRows(
                DB_NULL_HCHAPTER, 0, ARRAYSIZE(rowBuffer),
                &rowCountReturned, &rowReturned));

            for (DBCOUNTITEM i = 0; (i < rowCountReturned) && (count < maxResults); i++)
            {
                winrt::com_ptr<IPropertyStore> propStore;
                winrt::com_ptr<::IUnknown> unknown;

                THROW_IF_FAILED(getRow->GetRowFromHROW(
                    nullptr, rowBuffer[i], __uuidof(IPropertyStore), unknown.put()));
                propStore = unknown.as<IPropertyStore>();

                callback(propStore.get());
                count++;
            }

            THROW_IF_FAILED(rowset->ReleaseRows(
                rowCountReturned, rowReturned, nullptr, nullptr, nullptr));

        } while ((count < maxResults) && (rowCountReturned > 0));
    }
}

namespace winrt::SearchApp::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        // Keep the immersive custom title bar, but use an overlapped
        // presenter so restore/resize/minimize/maximize work normally.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        if (auto appWindow = this->AppWindow())
        {
            appWindow.SetPresenter(
                Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
            if (auto presenter = appWindow.Presenter().try_as<
                Microsoft::UI::Windowing::OverlappedPresenter>())
            {
                presenter.Maximize();
            }

            auto titleBar = appWindow.TitleBar();
            titleBar.ButtonBackgroundColor(Windows::UI::Color{ 255, 13, 13, 20 });
            titleBar.ButtonForegroundColor(Windows::UI::Color{ 255, 255, 255, 255 });
            titleBar.ButtonInactiveBackgroundColor(Windows::UI::Color{ 255, 13, 13, 20 });
            titleBar.ButtonInactiveForegroundColor(Windows::UI::Color{ 255, 150, 150, 160 });
            titleBar.ButtonHoverBackgroundColor(Windows::UI::Color{ 255, 58, 58, 74 });
            titleBar.ButtonHoverForegroundColor(Windows::UI::Color{ 255, 255, 255, 255 });
            titleBar.ButtonPressedBackgroundColor(Windows::UI::Color{ 255, 82, 82, 102 });
            titleBar.ButtonPressedForegroundColor(Windows::UI::Color{ 255, 255, 255, 255 });

            m_windowHandle = Microsoft::UI::GetWindowFromWindowId(appWindow.Id());
            auto module = GetModuleHandleW(nullptr);
            constexpr wchar_t HotkeyWindowClass[] = L"SearchAppHotkeyMessageWindow";
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = HotkeyWindowProc;
            windowClass.hInstance = module;
            windowClass.lpszClassName = HotkeyWindowClass;
            if (RegisterClassW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            {
                m_hotkeyWindow = CreateWindowExW(
                    0, HotkeyWindowClass, L"", 0,
                    0, 0, 0, 0, HWND_MESSAGE, nullptr, module, this);
                if (m_hotkeyWindow)
                {
                    m_hotkeyRegistered = RegisterHotKey(
                        m_hotkeyWindow, GlobalSearchHotkeyId,
                        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'F') != FALSE;
                }
            }
        }

        // Initialize the search session over all indexed files. Also request
        // Title/Author/Keywords/Comment so MatchKind classification can
        // cheaply confirm metadata hits (see MatchKindClassifier.h) without
        // any change to SearchQueryBuilder/SearchSessions.
        try
        {
            m_searchSession = std::make_unique<wsearch::SearchAsYouTypeSession>(
                std::vector<std::wstring>{ L"file:" },
                std::vector<std::wstring>{},
                std::vector<std::wstring>{
                    L"System.Title", L"System.Author", L"System.Keywords", L"System.Comment" }
            );
        }
        catch (...)
        {
            StatusText().Text(L"Failed to initialize Windows Search indexer session.");
        }

        // Initialize the "true index" full-filesystem background indexer.
        // This is additive/best-effort: if the on-disk index can't be
        // opened (e.g. no write access to %LOCALAPPDATA%), search silently
        // falls back to indexer-only results exactly as before this feature.
        try
        {
            m_localIndex = std::make_shared<applocal::LocalIndex>(applocal::LocalIndex::DefaultDbPath());
            m_backgroundIndexer = std::make_unique<applocal::BackgroundIndexer>(m_localIndex);
            m_backgroundIndexer->Start();
        }
        catch (...)
        {
            m_localIndex.reset();
            m_backgroundIndexer.reset();
        }

        m_searchDebounceTimer = DispatcherQueue().CreateTimer();
        m_searchDebounceTimer.Interval(std::chrono::milliseconds(150));
        m_searchDebounceTimer.IsRepeating(false);
        auto weakThis = get_weak();
        m_searchDebounceTimer.Tick([weakThis](auto&&, auto&&)
        {
            if (auto strongThis = weakThis.get())
            {
                strongThis->ExecuteSearchAsync(
                    strongThis->m_pendingSearchText,
                    strongThis->m_pendingSearchGeneration);
            }
        });

        m_indexStatusTimer = DispatcherQueue().CreateTimer();
        m_indexStatusTimer.Interval(std::chrono::seconds(1));
        m_indexStatusTimer.Tick([weakThis](auto&&, auto&&)
        {
            if (auto strongThis = weakThis.get())
            {
                strongThis->UpdateIndexerProgress();
                if (++strongThis->m_indexStatusTick % 5 == 0)
                {
                    strongThis->UpdateIndexStatisticsAsync();
                }
            }
        });
        m_indexStatusTimer.Start();
        UpdateIndexerProgress();
        UpdateIndexStatisticsAsync();

        // Auto-focus the search box
        SearchTextBox().Loaded([this](auto&&, auto&&)
        {
            SearchTextBox().Focus(FocusState::Programmatic);
        });
    }

    MainWindow::~MainWindow()
    {
        if (m_searchDebounceTimer)
        {
            m_searchDebounceTimer.Stop();
        }
        if (m_indexStatusTimer)
        {
            m_indexStatusTimer.Stop();
        }
        if (m_hotkeyWindow)
        {
            if (m_hotkeyRegistered)
            {
                UnregisterHotKey(m_hotkeyWindow, GlobalSearchHotkeyId);
            }
            DestroyWindow(m_hotkeyWindow);
        }
    }

    void MainWindow::UpdateIndexerProgress()
    {
        if (!m_backgroundIndexer)
        {
            IndexerStateText().Text(L"Unavailable");
            CurrentIndexPathText().Text(L"The local index could not be opened.");
            return;
        }

        auto progress = m_backgroundIndexer->GetProgress();
        std::wstring stateText;
        switch (progress.state)
        {
        case applocal::IndexerState::Running:
            stateText = L"Indexing";
            break;
        case applocal::IndexerState::Paused:
            stateText = L"Paused";
            break;
        case applocal::IndexerState::Stopped:
        default:
            stateText = L"Idle";
            break;
        }

        IndexerStateText().Text(stateText);
        SessionScannedText().Text(FormatCount(progress.filesScanned));
        IndexElapsedText().Text(FormatElapsed(progress.elapsed));
        CurrentIndexPathText().Text(progress.currentPath.empty()
            ? L"Waiting for scan work..."
            : progress.currentPath);
    }

    IAsyncAction MainWindow::UpdateIndexStatisticsAsync()
    {
        if (!m_localIndex || m_indexStatisticsRefreshInFlight.exchange(true))
        {
            co_return;
        }

        auto lifetime = get_strong();
        auto resetInFlight = wil::scope_exit([this]
        {
            m_indexStatisticsRefreshInFlight.store(false);
        });
        apartment_context uiThread;

        applocal::IndexStatistics statistics;
        std::vector<applocal::ScanRootStatus> roots;
        bool succeeded = false;
        co_await resume_background();
        try
        {
            statistics = m_localIndex->GetStatistics();
            roots = m_localIndex->GetScanRoots();
            succeeded = true;
        }
        catch (...)
        {
        }

        co_await uiThread;
        if (succeeded)
        {
            auto completedRoots = static_cast<uint64_t>(std::count_if(
                roots.begin(), roots.end(), [](const applocal::ScanRootStatus& root)
                {
                    return root.status == L"done";
                }));
            TotalIndexedItemsText().Text(FormatCount(statistics.totalItems));
            IndexedFilesText().Text(FormatCount(statistics.files));
            IndexedFoldersText().Text(FormatCount(statistics.folders));
            ContentIndexedFilesText().Text(FormatCount(statistics.contentIndexedFiles));
            IndexedVolumesText().Text(
                FormatCount(completedRoots) + L" / " + FormatCount(roots.size()));

            if (m_backgroundIndexer->GetProgress().state == applocal::IndexerState::Stopped &&
                !roots.empty() && completedRoots == roots.size())
            {
                IndexerStateText().Text(L"Up to date");
            }
        }
        else
        {
            IndexerStateText().Text(L"Status unavailable");
        }
    }

    void MainWindow::ToggleFromHotkey()
    {
        if (!m_windowHandle)
        {
            return;
        }

        if (IsWindowVisible(m_windowHandle) && GetForegroundWindow() == m_windowHandle)
        {
            ShowWindow(m_windowHandle, SW_HIDE);
            return;
        }

        ShowWindow(m_windowHandle, IsIconic(m_windowHandle) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(m_windowHandle);
        BringWindowToTop(m_windowHandle);
        SearchTextBox().Text(L"");
        SearchResults().ItemsSource(nullptr);
        StatusText().Text(L"");
        SearchTextBox().Focus(FocusState::Programmatic);
    }

    LRESULT CALLBACK MainWindow::HotkeyWindowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto self = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && message == WM_HOTKEY && wParam == GlobalSearchHotkeyId)
        {
            self->ToggleFromHotkey();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void MainWindow::SearchTextBox_TextChanged(
        IInspectable const&, TextChangedEventArgs const&)
    {
        auto text = std::wstring(SearchTextBox().Text());
        auto gen = ++m_queryGeneration;
        if (m_searchDebounceTimer)
        {
            m_searchDebounceTimer.Stop();
        }

        if (text.empty())
        {
            SearchResults().ItemsSource(nullptr);
            StatusText().Text(L"");
            return;
        }

        m_pendingSearchText = std::move(text);
        m_pendingSearchGeneration = gen;
        if (m_searchDebounceTimer)
        {
            m_searchDebounceTimer.Start();
        }
    }

    void MainWindow::LayoutRoot_SizeChanged(
        IInspectable const&, SizeChangedEventArgs const& args)
    {
        bool compact = args.NewSize().Width < 1240.0;
        auto visibility = compact ? Visibility::Collapsed : Visibility::Visible;
        LeftStatusPanel().Visibility(visibility);
        RightStatusPanel().Visibility(visibility);

        if (compact)
        {
            LeftLayoutColumn().Width(GridLengthHelper::FromPixels(0));
            CenterLayoutColumn().Width(
                GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            RightLayoutColumn().Width(GridLengthHelper::FromPixels(0));
            SearchTextBox().Margin(Thickness{ 24, 20, 24, 0 });
            StatusText().Margin(Thickness{ 24, 10, 24, 0 });
        }
        else
        {
            LeftLayoutColumn().Width(
                GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            CenterLayoutColumn().Width(GridLengthHelper::FromPixels(700));
            RightLayoutColumn().Width(
                GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            SearchTextBox().Margin(Thickness{ 0, 20, 0, 0 });
            StatusText().Margin(Thickness{ 0, 10, 0, 0 });
        }
    }

    void MainWindow::SearchTextBox_KeyDown(
        IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Escape)
        {
            if (SearchTextBox().Text().empty())
            {
                this->Close();
            }
            else
            {
                SearchTextBox().Text(L"");
            }
        }
        else if (e.Key() == Windows::System::VirtualKey::Enter)
        {
            OpenSelectedResult();
        }
        else if (e.Key() == Windows::System::VirtualKey::Down)
        {
            if (SearchResults().Items().Size() > 0)
            {
                SearchResults().SelectedIndex(0);
                SearchResults().Focus(FocusState::Programmatic);
            }
        }
    }

    void MainWindow::SearchResults_DoubleTapped(
        IInspectable const&, DoubleTappedRoutedEventArgs const&)
    {
        OpenSelectedResult();
    }

    void MainWindow::OpenSelectedResult()
    {
        if (auto selected = SearchResults().SelectedItem())
        {
            auto result = selected.as<SearchApp::SearchResultItem>();
            auto path = result.FilePath();
            if (!path.empty())
            {
                ShellExecuteW(nullptr, L"open", path.c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }

    IAsyncAction MainWindow::ExecuteSearchAsync(
        std::wstring searchText, uint32_t generation)
    {
        auto lifetime = get_strong();
        winrt::apartment_context ui_thread;

        co_await winrt::resume_background();

        if (m_queryGeneration != generation || !m_searchSession)
            co_return;

        bool searchFailed = false;
        try
        {
            LARGE_INTEGER startTime, endTime, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&startTime);

            m_searchSession->SetSearchText(searchText);
            auto rowset = m_searchSession->GetCachedResults();

            QueryPerformanceCounter(&endTime);
            double queryMs = static_cast<double>(endTime.QuadPart - startTime.QuadPart)
                * 1000.0 / static_cast<double>(freq.QuadPart);

            if (m_queryGeneration != generation)
                co_return;

            // --- Fast path: Windows Search indexer (unchanged behavior) ---
            std::vector<MergedResult> merged;
            std::unordered_set<std::wstring> seenPathKeys;

            if (rowset)
            {
                EnumerateTopNResults(rowset.get(), MaxResults,
                    [&](IPropertyStore* ps)
                    {
                        if (m_queryGeneration != generation) return;

                        winrt::com_ptr<IPropertyStore> propStoreCopy;
                        propStoreCopy.copy_from(ps);
                        wsearch::SearchResult sr(std::move(propStoreCopy));
                        auto name = sr.GetFileName();
                        auto path = sr.GetFilePathForTracking();
                        bool isFolder = sr.IsFolder();

                        if (name.empty() || path.empty()) return;

                        auto pathKey = NormalizePathKey(path);
                        if (!seenPathKeys.insert(pathKey).second) return;

                        auto matchKind = applocal::ClassifyIndexerMatch(
                            sr.GetRank(), searchText,
                            applocal::MetadataFields{
                                sr.GetTitle(), sr.GetAuthor(), sr.GetKeywords(), sr.GetComment() });

                        merged.push_back(MergedResult{ std::move(name), std::move(path), isFolder, matchKind });
                    });
            }

            // --- "True index" full-filesystem local search, merged in by
            // path. Only fills in files the indexer's fast path didn't
            // already return (its metadata/thumbnail is preferred when a
            // path comes from both sources). ---
            if (m_localIndex && merged.size() < MaxResults)
            {
                auto localResults = m_localIndex->Search(
                    searchText, static_cast<uint32_t>(MaxResults - merged.size()));

                for (auto& lr : localResults)
                {
                    if (m_queryGeneration != generation) break;

                    auto pathKey = NormalizePathKey(lr.path);
                    if (!seenPathKeys.insert(pathKey).second) continue;

                    auto matchKind = (lr.matchKind == applocal::LocalMatchKind::Filename)
                        ? applocal::ClassifiedMatchKind::Filename
                        : applocal::ClassifiedMatchKind::Content;

                    merged.push_back(MergedResult{ lr.name, lr.path, lr.isFolder, matchKind });

                    if (merged.size() >= MaxResults) break;
                }
            }

            if (m_queryGeneration != generation)
                co_return;

            auto items = winrt::single_threaded_observable_vector<IInspectable>();
            for (auto& mr : merged)
            {
                if (m_queryGeneration != generation) break;

                // Get thumbnail from the per-extension icon cache (works
                // identically for indexer- and local-index-only results).
                auto thumbnail = g_iconCache.GetOrLoadThumbnail(mr.path, mr.isFolder);

                auto item = winrt::make<implementation::SearchResultItem>(
                    winrt::hstring(mr.name),
                    winrt::hstring(mr.path),
                    mr.isFolder,
                    thumbnail,
                    ToWinRtMatchKind(mr.matchKind)
                );
                items.Append(item);
            }

            int resultCount = static_cast<int>(merged.size());

            co_await ui_thread;

            if (m_queryGeneration != generation)
                co_return;

            SearchResults().ItemsSource(items);

            // Show result count and query timing
            std::wstring status = std::to_wstring(resultCount) + L" results";
            if (resultCount >= MaxResults)
                status += L"+";
            status += L"  \u00B7  " + std::to_wstring(static_cast<int>(queryMs)) + L" ms";
            StatusText().Text(winrt::hstring(status));
        }
        catch (...)
        {
            searchFailed = true;
        }

        if (searchFailed)
        {
            co_await ui_thread;
            StatusText().Text(L"Search error");
        }
    }
}
