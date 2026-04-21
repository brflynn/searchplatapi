#include "pch.h"
#include "MainWindow.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "IconCache.h"
#include <SearchSessions.h>
#include <shellapi.h>
#include <propkey.h>

// Namespace aliases for readability (at file scope)
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace imaging = media::Imaging;

using namespace winrt;
using namespace winrt::Windows::Foundation;

namespace
{
    IconCache g_iconCache;
    constexpr int MaxResults = 50;

    winrt::Windows::UI::Color MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return { a, r, g, b };
    }

    // Property extraction helpers (avoids SearchResult.h raw-pointer constructor issue)
    std::wstring GetStringProp(IPropertyStore* ps, const PROPERTYKEY& key)
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        std::wstring result;
        if (SUCCEEDED(ps->GetValue(key, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
            result = pv.pwszVal;
        PropVariantClear(&pv);
        return result;
    }

    bool IsKindFolder(IPropertyStore* ps)
    {
        auto kind = GetStringProp(ps, PKEY_Kind);
        return kind.find(L"folder") != std::wstring::npos;
    }

    std::wstring UrlToFilePath(const std::wstring& url)
    {
        std::wstring path = url;
        if (path.find(L"file:///") == 0)
            path = path.substr(8);
        else if (path.find(L"file://") == 0)
            path = path.substr(7);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        // Decode %XX
        std::wstring decoded;
        for (size_t i = 0; i < path.length(); ++i)
        {
            if (path[i] == L'%' && i + 2 < path.length())
            {
                wchar_t ch = static_cast<wchar_t>(std::wcstol(path.substr(i + 1, 2).c_str(), nullptr, 16));
                decoded += ch;
                i += 2;
            }
            else
                decoded += path[i];
        }
        return decoded;
    }

    media::SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return media::SolidColorBrush(MakeColor(a, r, g, b));
    }

    // Enumerate up to maxResults rows from a rowset
    void EnumerateTopNResults(IRowset* rowset, int maxResults,
        std::function<void(winrt::com_ptr<IPropertyStore>)> callback)
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

                callback(propStore);
                count++;
            }

            THROW_IF_FAILED(rowset->ReleaseRows(
                rowCountReturned, rowReturned, nullptr, nullptr, nullptr));
        }
        while ((count < maxResults) && (rowCountReturned > 0));
    }
}

namespace winrt::SearchApp::implementation
{
    MainWindow::MainWindow()
    {
        BuildUI();

        // Set full screen for opaque overlay look
        ExtendsContentIntoTitleBar(true);
        if (auto appWindow = this->AppWindow())
        {
            appWindow.SetPresenter(
                winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
        }

        // Initialize search session over all indexed files
        try
        {
            m_searchSession = std::make_unique<wsearch::SearchAsYouTypeSession>(
                std::vector<std::wstring>{ L"file:" });
        }
        catch (...)
        {
            m_statusText.Text(L"Failed to initialize search indexer session.");
        }

        // Auto-focus search box
        m_searchBox.Loaded([this](auto&&, auto&&)
        {
            m_searchBox.Focus(xaml::FocusState::Programmatic);
        });
    }

    void MainWindow::BuildUI()
    {
        // Root grid — deep dark opaque background
        auto root = controls::Grid();
        root.Background(MakeBrush(0xFF, 0x0D, 0x0D, 0x14));

        // Row definitions: TitleBar(48) | SearchBox(Auto) | Status(Auto) | Results(*)
        auto rowTitleBar = xaml::Controls::RowDefinition();
        rowTitleBar.Height({ 48, xaml::GridUnitType::Pixel });
        auto rowSearch = xaml::Controls::RowDefinition();
        rowSearch.Height(xaml::GridLengthHelper::Auto());
        auto rowStatus = xaml::Controls::RowDefinition();
        rowStatus.Height(xaml::GridLengthHelper::Auto());
        auto rowResults = xaml::Controls::RowDefinition();
        rowResults.Height({ 1, xaml::GridUnitType::Star });

        root.RowDefinitions().Append(rowTitleBar);
        root.RowDefinitions().Append(rowSearch);
        root.RowDefinitions().Append(rowStatus);
        root.RowDefinitions().Append(rowResults);

        // Row 0: Title bar drag region
        auto titleBar = controls::Border();
        titleBar.Background(media::SolidColorBrush({ 0, 0, 0, 0 }));
        controls::Grid::SetRow(titleBar, 0);
        root.Children().Append(titleBar);
        SetTitleBar(titleBar);

        // Row 1: Search text box
        m_searchBox = controls::TextBox();
        m_searchBox.PlaceholderText(L"Type to search files...");
        m_searchBox.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        m_searchBox.Width(680);
        m_searchBox.FontSize(22);
        m_searchBox.Padding({ 16, 12, 16, 12 });
        m_searchBox.Margin({ 0, 20, 0, 0 });
        m_searchBox.CornerRadius({ 8, 8, 8, 8 });
        m_searchBox.TextChanged({ this, &MainWindow::OnSearchTextChanged });
        m_searchBox.KeyDown({ this, &MainWindow::OnSearchKeyDown });
        controls::Grid::SetRow(m_searchBox, 1);
        root.Children().Append(m_searchBox);

        // Row 2: Status text
        m_statusText = controls::TextBlock();
        m_statusText.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        m_statusText.Width(680);
        m_statusText.Margin({ 0, 10, 0, 0 });
        m_statusText.FontSize(12);
        m_statusText.Foreground(MakeBrush(0xFF, 0x7E, 0x7E, 0x8E));
        controls::Grid::SetRow(m_statusText, 2);
        root.Children().Append(m_statusText);

        // Row 3: Results ListView
        m_listView = controls::ListView();
        m_listView.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        m_listView.Width(700);
        m_listView.SelectionMode(controls::ListViewSelectionMode::Single);
        m_listView.Background(media::SolidColorBrush({ 0, 0, 0, 0 }));
        m_listView.Margin({ 0, 8, 0, 16 });
        m_listView.Padding({ 10, 0, 10, 0 });
        m_listView.DoubleTapped({ this, &MainWindow::OnResultsDoubleTapped });
        controls::Grid::SetRow(m_listView, 3);
        root.Children().Append(m_listView);

        Content(root);
    }

    controls::Grid MainWindow::CreateResultItemVisual(
        const std::wstring& name,
        const std::wstring& path,
        winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumbnail)
    {
        auto grid = controls::Grid();
        grid.ColumnSpacing(12);
        grid.Padding({ 2, 4, 2, 4 });

        // Columns: icon(28) | text(*)
        auto col0 = xaml::Controls::ColumnDefinition();
        col0.Width({ 28, xaml::GridUnitType::Pixel });
        auto col1 = xaml::Controls::ColumnDefinition();
        col1.Width({ 1, xaml::GridUnitType::Star });
        grid.ColumnDefinitions().Append(col0);
        grid.ColumnDefinitions().Append(col1);

        // Rows: name | path
        auto row0 = xaml::Controls::RowDefinition();
        row0.Height(xaml::GridLengthHelper::Auto());
        auto row1 = xaml::Controls::RowDefinition();
        row1.Height(xaml::GridLengthHelper::Auto());
        grid.RowDefinitions().Append(row0);
        grid.RowDefinitions().Append(row1);

        // Icon image
        auto image = controls::Image();
        image.Width(24);
        image.Height(24);
        image.VerticalAlignment(xaml::VerticalAlignment::Center);
        if (thumbnail)
        {
            imaging::BitmapImage bmp;
            bmp.SetSource(thumbnail.CloneStream());
            image.Source(bmp);
        }
        controls::Grid::SetColumn(image, 0);
        controls::Grid::SetRowSpan(image, 2);
        grid.Children().Append(image);

        // File name
        auto nameBlock = controls::TextBlock();
        nameBlock.Text(winrt::hstring(name));
        nameBlock.FontSize(14);
        nameBlock.Foreground(MakeBrush(0xFF, 0xFF, 0xFF, 0xFF));
        nameBlock.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
        controls::Grid::SetColumn(nameBlock, 1);
        controls::Grid::SetRow(nameBlock, 0);
        grid.Children().Append(nameBlock);

        // File path
        auto pathBlock = controls::TextBlock();
        pathBlock.Text(winrt::hstring(path));
        pathBlock.FontSize(11);
        pathBlock.Foreground(MakeBrush(0x99, 0xFF, 0xFF, 0xFF));
        pathBlock.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
        controls::Grid::SetColumn(pathBlock, 1);
        controls::Grid::SetRow(pathBlock, 1);
        grid.Children().Append(pathBlock);

        // Store file path in Tag for double-click retrieval
        grid.Tag(winrt::box_value(winrt::hstring(path)));

        return grid;
    }

    void MainWindow::OnSearchTextChanged(IInspectable const&,
        controls::TextChangedEventArgs const&)
    {
        auto text = std::wstring(m_searchBox.Text());
        auto gen = ++m_queryGeneration;

        if (text.empty())
        {
            m_listView.Items().Clear();
            m_statusText.Text(L"");
            return;
        }

        ExecuteSearchAsync(std::move(text), gen);
    }

    void MainWindow::OnSearchKeyDown(IInspectable const&,
        xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == winrt::Windows::System::VirtualKey::Escape)
        {
            if (m_searchBox.Text().empty())
            {
                this->Close();
            }
            else
            {
                m_searchBox.Text(L"");
            }
        }
        else if (e.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            OpenSelectedResult();
        }
        else if (e.Key() == winrt::Windows::System::VirtualKey::Down)
        {
            auto items = m_listView.Items();
            if (items.Size() > 0)
            {
                m_listView.SelectedIndex(0);
                m_listView.Focus(xaml::FocusState::Programmatic);
            }
        }
    }

    void MainWindow::OnResultsDoubleTapped(IInspectable const&,
        xaml::Input::DoubleTappedRoutedEventArgs const&)
    {
        OpenSelectedResult();
    }

    void MainWindow::OpenSelectedResult()
    {
        auto selected = m_listView.SelectedItem();
        if (!selected) return;

        try
        {
            auto element = selected.as<xaml::FrameworkElement>();
            auto tag = element.Tag();
            if (tag)
            {
                auto path = winrt::unbox_value<winrt::hstring>(tag);
                if (!path.empty())
                {
                    ShellExecuteW(nullptr, L"open", path.c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
        }
        catch (...) {}
    }

    IAsyncAction MainWindow::ExecuteSearchAsync(
        std::wstring searchText, uint32_t generation)
    {
        auto lifetime = get_strong();
        winrt::apartment_context ui_thread;

        co_await winrt::resume_background();

        if (m_queryGeneration != generation || !m_searchSession)
            co_return;

        // Data collected on background thread
        struct ResultData
        {
            std::wstring name;
            std::wstring path;
            bool isFolder;
            winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumb;
        };

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

            std::vector<ResultData> results;
            results.reserve(MaxResults);

            EnumerateTopNResults(rowset.get(), MaxResults,
                [&](winrt::com_ptr<IPropertyStore> propStore)
                {
                    if (m_queryGeneration != generation) return;

                    auto name = GetStringProp(propStore.get(), PKEY_ItemNameDisplay);
                    auto url = GetStringProp(propStore.get(), PKEY_ItemUrl);
                    auto path = UrlToFilePath(url);
                    bool isFolder = IsKindFolder(propStore.get());

                    if (name.empty() || path.empty()) return;

                    // Get icon from per-extension cache (background thread)
                    auto thumbnail = g_iconCache.GetOrLoadThumbnail(path, isFolder);

                    results.push_back({ name, path, isFolder, thumbnail });
                });

            // Switch to UI thread to build visual tree
            co_await ui_thread;

            if (m_queryGeneration != generation)
                co_return;

            auto items = m_listView.Items();
            items.Clear();

            for (auto& r : results)
            {
                auto visual = CreateResultItemVisual(r.name, r.path, r.thumb);
                items.Append(visual);
            }

            // Status: result count + timing
            int resultCount = static_cast<int>(results.size());
            std::wstring status = std::to_wstring(resultCount) + L" results";
            if (resultCount >= MaxResults)
                status += L"+";
            status += L"  \u00B7  " + std::to_wstring(static_cast<int>(queryMs)) + L" ms";
            m_statusText.Text(winrt::hstring(status));
        }
        catch (...)
        {
            // Error handled below
        }
    }
}
