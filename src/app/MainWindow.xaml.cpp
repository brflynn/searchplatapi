#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "SearchResultItem.h"
#include "IconCache.h"
#include <SearchSessions.h>
#include <SearchResult.h>
#include <shellapi.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Windows::Foundation;

namespace
{
    IconCache g_iconCache;
    constexpr int MaxResults = 50;

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
                winrt::com_ptr<IUnknown> unknown;

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

        // Extend content into title bar for immersive overlay look
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        // Go full screen
        if (auto appWindow = this->AppWindow())
        {
            appWindow.SetPresenter(
                Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
        }

        // Initialize the search session over all indexed files
        try
        {
            m_searchSession = std::make_unique<wsearch::SearchAsYouTypeSession>(
                std::vector<std::wstring>{ L"file:" }
            );
        }
        catch (...)
        {
            StatusText().Text(L"Failed to initialize Windows Search indexer session.");
        }

        // Auto-focus the search box
        SearchTextBox().Loaded([this](auto&&, auto&&)
        {
            SearchTextBox().Focus(FocusState::Programmatic);
        });
    }

    void MainWindow::SearchTextBox_TextChanged(
        IInspectable const&, TextChangedEventArgs const&)
    {
        auto text = std::wstring(SearchTextBox().Text());
        auto gen = ++m_queryGeneration;

        if (text.empty())
        {
            SearchResults().ItemsSource(nullptr);
            StatusText().Text(L"");
            return;
        }

        ExecuteSearchAsync(std::move(text), gen);
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

            if (!rowset || m_queryGeneration != generation)
                co_return;

            auto items = winrt::single_threaded_observable_vector<IInspectable>();
            int resultCount = 0;

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

                    // Get thumbnail from the per-extension icon cache
                    auto thumbnail = g_iconCache.GetOrLoadThumbnail(path, isFolder);

                    auto item = winrt::make<implementation::SearchResultItem>(
                        winrt::hstring(name),
                        winrt::hstring(path),
                        isFolder,
                        thumbnail
                    );
                    items.Append(item);
                    resultCount++;
                });

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
            co_await ui_thread;
            StatusText().Text(L"Search error");
        }
    }
}
