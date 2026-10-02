#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "SearchResultItem.h"
#include "IconCache.h"
#include "MatchKindClassifier.h"
#include "ContentScopeRequest.h"
#include <SearchSessions.h>
#include <SearchResult.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
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

    winrt::fire_and_forget StopIndexerAsync(std::unique_ptr<applocal::BackgroundIndexer> indexer)
    {
        co_await winrt::resume_background();
        indexer->Stop();
    }

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
        std::function<bool(IPropertyStore*)> callback)
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

                if (callback(propStore.get())) count++;
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
            std::wstring dbPath = applocal::LocalIndex::DefaultDbPath();
            applocal::IndexerOptions options;
            wchar_t testRoot[32768]{};
            DWORD testRootLength = GetEnvironmentVariableW(L"SEARCHAPP_UI_SMOKE_ROOT", testRoot, ARRAYSIZE(testRoot));
            if (testRootLength && testRootLength < ARRAYSIZE(testRoot))
            {
                auto root = applocal::NormalizeFolder(testRoot);
                options.scanRoots = { root };
                dbPath = root + L"\\ui-smoke.db";
                m_uiSmokeTest = true;
            }
            m_localIndex = std::make_shared<applocal::LocalIndex>(dbPath);
            m_localIndex->RecoverInterruptedScopeRequests();
            m_backgroundIndexer = std::make_unique<applocal::BackgroundIndexer>(m_localIndex, options);
            m_backgroundIndexer->Start();
        }
        catch (...)
        {
            m_localIndex.reset();
            m_backgroundIndexer.reset();
            StatusText().Text(L"Local index/settings unavailable. Exclusion controls are disabled.");
        }
        Closed([this](auto&&, auto&&)
        {
            m_closed->store(true);
            ++m_queryGeneration;
            if (m_searchDebounceTimer) m_searchDebounceTimer.Stop();
            if (m_indexStatusTimer) m_indexStatusTimer.Stop();
            if (m_backgroundIndexer) StopIndexerAsync(std::move(m_backgroundIndexer));
        });

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
        ReloadSettingsAsync();

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
            ReindexButton().IsEnabled(false);
            CompactReindexButton().IsEnabled(false);
            IndexerStateText().Text(L"Unavailable");
            CompactIndexStatusText().Text(L"Local reindex unavailable: local database could not be opened.");
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
        case applocal::IndexerState::Error:
            stateText = L"Scan error / incomplete";
            break;
        case applocal::IndexerState::Stopped:
        default:
            stateText = L"Idle";
            break;
        }

        IndexerStateText().Text(stateText);
        ReindexStatusText().Text(progress.reindexStatus);
        CompactIndexStatusText().Text(L"Local reindex: " + progress.reindexStatus +
            (progress.error.empty() ? L"" : L" - " + progress.error));
        ReindexButton().IsEnabled(true);
        CompactReindexButton().IsEnabled(true);
        SessionScannedText().Text(FormatCount(progress.filesScanned));
        IndexElapsedText().Text(FormatElapsed(progress.elapsed));
        CurrentIndexPathText().Text(!progress.error.empty() ? progress.error : progress.currentPath.empty()
            ? L"Waiting for scan work..."
            : progress.currentPath);
    }

    IAsyncAction MainWindow::UpdateIndexStatisticsAsync()
    {
        if (m_closed->load() || !m_localIndex || m_indexStatisticsRefreshInFlight.exchange(true))
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
        if (m_closed->load()) co_return;
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
        CompactReindexButton().Visibility(compact ? Visibility::Visible : Visibility::Collapsed);
        CompactIndexStatusText().Visibility(compact ? Visibility::Visible : Visibility::Collapsed);

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
        IInspectable const&, DoubleTappedRoutedEventArgs const& args)
    {
        auto source = args.OriginalSource().try_as<DependencyObject>();
        while (source)
        {
            if (source.try_as<Controls::Primitives::ButtonBase>())
            {
                args.Handled(true);
                return;
            }
            source = Media::VisualTreeHelper::GetParent(source);
        }
        OpenSelectedResult();
    }

    void MainWindow::Reindex_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_backgroundIndexer)
        {
            StatusText().Text(L"Cannot reindex: local index unavailable.");
            return;
        }
        m_backgroundIndexer->RequestReindex();
        ReindexStatusText().Text(L"Queued");
        CompactIndexStatusText().Text(L"Local reindex: Queued");
        StatusText().Text(L"Local rescan queued. Existing indexed data remains searchable.");
    }

    void MainWindow::RefreshSearch()
    {
        SearchResults().ItemsSource(nullptr);
        SearchTextBox_TextChanged(nullptr, nullptr);
    }

    IAsyncAction MainWindow::ReloadSettingsAsync()
    {
        auto lifetime = get_strong();
        auto generation = ++m_settingsReloadGeneration;
        apartment_context ui;
        if (!m_localIndex || m_closed->load()) co_return;
        applocal::IndexSettings settings;
        std::wstring error;
        co_await resume_background();
        try { settings = m_localIndex->GetSettings(); }
        catch (const std::exception& e) { error = L"Settings error: " + to_hstring(e.what()); }
        co_await ui;
        if (m_closed->load() || generation != m_settingsReloadGeneration) co_return;
        m_settingsReady = error.empty();
        m_settingsError = error;
        if (m_settingsReady) m_settings = std::move(settings);
        else StatusText().Text(error);
        PopulateSettings();
    }

    void MainWindow::Settings_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_settingsDialog) return;
        m_settingsPanel = StackPanel();
        m_settingsPanel.Spacing(12);
        auto scroll = ScrollViewer();
        scroll.MaxHeight(480);
        scroll.Content(m_settingsPanel);
        m_settingsDialog = ContentDialog();
        Automation::AutomationProperties::SetAutomationId(m_settingsDialog, L"IndexSettingsDialog");
        m_settingsDialog.XamlRoot(LayoutRoot().XamlRoot());
        m_settingsDialog.Title(box_value(L"Index settings"));
        m_settingsDialog.CloseButtonText(L"Close");
        m_settingsDialog.Content(scroll);
        m_settingsDialog.Closed([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
            {
                self->m_settingsPanel = nullptr;
                self->m_settingsDialog = nullptr;
            }
        });
        PopulateSettings();
        ReloadSettingsAsync();
        m_settingsDialog.ShowAsync();
    }

    void MainWindow::PopulateSettings()
    {
        if (!m_settingsPanel) return;
        m_settingsPanel.Children().Clear();
        auto text = [&](const std::wstring& value)
        {
            TextBlock label;
            label.Text(value);
            label.TextWrapping(TextWrapping::Wrap);
            m_settingsPanel.Children().Append(label);
        };
        if (!m_operationError.empty()) text(m_operationError);
        if (!m_settingsReady)
        {
            text(!m_settingsError.empty() ? m_settingsError : m_localIndex ?
                L"Loading settings..." : L"Settings unavailable: local database could not be opened.");
            return;
        }
        text(L"Exclusions hide results from both search sources without deleting indexed data.");
        if (m_settings.exclusions.empty()) text(L"No exclusions.");
        for (const auto& rule : m_settings.exclusions)
        {
            text(rule.kind + L": " + rule.value);
            Button restore;
            restore.Content(box_value(L"Restore"));
            Automation::AutomationProperties::SetName(restore, L"Restore " + rule.value);
            restore.IsEnabled(!m_settingChangeInFlight);
            restore.Click([weak = get_weak(), rule](auto&&, auto&&)
            {
                if (auto self = weak.get()) self->ChangeSettingAsync(L"remove", rule.kind, rule.value);
            });
            m_settingsPanel.Children().Append(restore);
        }
        text(L"Windows Search content requests (separate from local indexing)");
        text(L"Include scope is not a guarantee of content coverage. File filters and Indexing Options control content. Forget removes only this app record, not Windows scope rules.");
        if (m_settings.contentScopes.empty()) text(L"No content scope requests.");
        for (const auto& scope : m_settings.contentScopes)
        {
            text(scope.folder + L"\n" + scope.state + L": " + scope.detail);
            bool active = m_scopeRequests.contains(scope.folder);
            if (!active && (scope.state == L"failed" || scope.state == L"pending" || scope.state == L"cancelled"))
            {
                Button retry;
                retry.Content(box_value(L"Retry"));
                Automation::AutomationProperties::SetName(retry, L"Retry " + scope.folder);
                retry.IsEnabled(!m_uiSmokeTest);
                retry.Click([weak = get_weak(), folder = scope.folder](auto&&, auto&&)
                {
                    if (auto self = weak.get()) self->RequestContentScopeAsync(folder);
                });
                m_settingsPanel.Children().Append(retry);
            }
            Button forget;
            forget.Content(box_value(L"Forget app record"));
            Automation::AutomationProperties::SetName(forget, L"Forget request " + scope.folder);
            forget.IsEnabled(!active && !m_settingChangeInFlight);
            forget.Click([weak = get_weak(), folder = scope.folder](auto&&, auto&&)
            {
                if (auto self = weak.get()) self->ChangeSettingAsync(L"forget", L"", folder);
            });
            m_settingsPanel.Children().Append(forget);
        }
    }

    void MainWindow::ResultActions_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto button = sender.as<Button>();
        auto item = button.DataContext().try_as<SearchApp::SearchResultItem>();
        if (!item) return;
        try
        {
            std::wstring path(item.FilePath());
            auto folder = applocal::ResultFolder(path, item.IsFolder());
            auto extension = applocal::ResultExtension(path, item.IsFolder());
            MenuFlyout menu;
            auto action = [&](const std::wstring& label, bool enabled, auto handler)
            {
                MenuFlyoutItem entry;
                entry.Text(label);
                entry.IsEnabled(enabled && m_settingsReady && !m_settingChangeInFlight);
                entry.Click(handler);
                menu.Items().Append(entry);
            };
            action(L"Exclude folder: " + folder, !folder.empty() && !m_settings.IsExcluded(folder, true),
                [weak = get_weak(), folder](auto&&, auto&&)
                {
                    if (auto self = weak.get()) self->ChangeSettingAsync(L"add", L"folder", folder);
                });
            if (!extension.empty())
                action(L"Exclude extension: " + extension, true,
                    [weak = get_weak(), extension](auto&&, auto&&)
                    {
                        if (auto self = weak.get()) self->ChangeSettingAsync(L"add", L"extension", extension);
                    });
            bool requested = m_scopeRequests.contains(folder) ||
                std::any_of(m_settings.contentScopes.begin(), m_settings.contentScopes.end(), [&](const auto& scope)
                {
                    return scope.folder == folder && scope.state != L"failed" && scope.state != L"cancelled" && scope.state != L"pending";
                });
            action(L"Request Windows Search content indexing for folder", !m_uiSmokeTest && !folder.empty() && !requested,
                [weak = get_weak(), folder](auto&&, auto&&)
                {
                    if (auto self = weak.get()) self->RequestContentScopeAsync(folder);
                });
            menu.ShowAt(button);
        }
        catch (const std::exception& e)
        {
            StatusText().Text(L"Result actions unavailable: " + to_hstring(e.what()));
        }
    }

    IAsyncAction MainWindow::ChangeSettingAsync(std::wstring operation, std::wstring kind, std::wstring value)
    {
        auto lifetime = get_strong();
        if (!m_localIndex || m_settingChangeInFlight || m_closed->load()) co_return;
        apartment_context ui;
        m_settingChangeInFlight = true;
        m_visibilityChanging.store(true);
        ++m_queryGeneration;
        SearchResults().ItemsSource(nullptr);
        PopulateSettings();
        std::wstring error;
        co_await resume_background();
        try
        {
            if (operation == L"add") m_localIndex->AddExclusion(kind, value);
            else if (operation == L"remove") m_localIndex->RemoveExclusion(kind, value);
            else m_localIndex->ForgetContentScope(value);
        }
        catch (const std::exception& e) { error = L"Could not save settings: " + to_hstring(e.what()); }
        co_await ui;
        m_visibilityChanging.store(false);
        m_settingChangeInFlight = false;
        m_operationError = error;
        if (m_closed->load()) co_return;
        co_await ReloadSettingsAsync();
        RefreshSearch();
        if (!error.empty()) StatusText().Text(error);
    }

    IAsyncAction MainWindow::RequestContentScopeAsync(std::wstring folder)
    {
        auto lifetime = get_strong();
        if (!m_localIndex || m_closed->load() || m_uiSmokeTest || !m_scopeRequests.insert(folder).second) co_return;
        apartment_context ui;
        StatusText().Text(L"Windows Search scope request pending; see Index settings for details.");
        auto cancelled = m_closed;
        auto index = m_localIndex;
        std::wstring persistenceError;
        co_await resume_background();
        try
        {
            index->SetContentScope({ folder, L"pending", L"Including scope and monitoring catalog at background priority." });
        }
        catch (const std::exception& e) { persistenceError = L"Scope request not started: " + to_hstring(e.what()); }
        co_await ui;
        if (persistenceError.empty() && !cancelled->load()) co_await ReloadSettingsAsync();
        co_await resume_background();
        applocal::ContentScope result;
        if (persistenceError.empty())
        {
            result = applocal::RunContentScopeRequest(folder, *cancelled);
            try { index->SetContentScope(result); }
            catch (const std::exception& e) { persistenceError = L"Could not persist scope outcome: " + to_hstring(e.what()); }
        }
        co_await ui;
        m_scopeRequests.erase(folder);
        m_operationError = persistenceError;
        if (cancelled->load()) co_return;
        co_await ReloadSettingsAsync();
        StatusText().Text(persistenceError.empty() ? result.detail : persistenceError);
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

        if (m_queryGeneration != generation || m_closed->load() || m_visibilityChanging.load())
            co_return;

        bool searchFailed = false;
        try
        {
            LARGE_INTEGER startTime, endTime, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&startTime);

            auto settings = m_localIndex ? m_localIndex->GetSettings() : applocal::IndexSettings{};
            winrt::com_ptr<IRowset> rowset;
            {
                std::lock_guard lock(m_searchSessionMutex);
                if (m_queryGeneration != generation) co_return;
                if (m_searchSession)
                {
                    m_searchSession->SetSearchText(searchText);
                    rowset = m_searchSession->GetCachedResults();
                }
            }

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
                        if (m_queryGeneration != generation) return true;

                        winrt::com_ptr<IPropertyStore> propStoreCopy;
                        propStoreCopy.copy_from(ps);
                        wsearch::SearchResult sr(std::move(propStoreCopy));
                        auto name = sr.GetFileName();
                        auto path = sr.GetFilePathForTracking();
                        bool isFolder = sr.IsFolder();

                        if (name.empty() || path.empty()) return false;
                        if (settings.IsExcluded(path, isFolder)) return false;

                        auto pathKey = applocal::NormalizeFolder(path);
                        if (!seenPathKeys.insert(pathKey).second) return false;

                        auto matchKind = applocal::ClassifyIndexerMatch(
                            sr.GetRank(), searchText,
                            applocal::MetadataFields{
                                sr.GetTitle(), sr.GetAuthor(), sr.GetKeywords(), sr.GetComment() });

                        merged.push_back(MergedResult{ std::move(name), std::move(path), isFolder, matchKind });
                        return true;
                    });
            }

            // --- "True index" full-filesystem local search, merged in by
            // path. Only fills in files the indexer's fast path didn't
            // already return (its metadata/thumbnail is preferred when a
            // path comes from both sources). ---
            if (m_localIndex && merged.size() < MaxResults)
            {
                auto localResults = m_localIndex->Search(
                    searchText, MaxResults, settings);

                for (auto& lr : localResults)
                {
                    if (m_queryGeneration != generation) break;

                    auto pathKey = applocal::NormalizeFolder(lr.path);
                    if (!seenPathKeys.insert(pathKey).second) continue;

                    auto matchKind = (lr.matchKind == applocal::LocalMatchKind::Filename)
                        ? applocal::ClassifiedMatchKind::Filename
                        : applocal::ClassifiedMatchKind::Content;

                    merged.push_back(MergedResult{ lr.name, lr.path, lr.isFolder, matchKind });

                    if (merged.size() >= MaxResults) break;
                }
            }

            if (m_queryGeneration != generation || m_closed->load() || m_visibilityChanging.load())
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

            if (m_queryGeneration != generation || m_closed->load() || m_visibilityChanging.load())
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
            if (m_queryGeneration != generation || m_closed->load() || m_visibilityChanging.load()) co_return;
            StatusText().Text(L"Search error");
        }
    }
}
