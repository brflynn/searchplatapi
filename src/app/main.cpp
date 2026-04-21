// SearchApp - Win32 full-screen overlay search using SearchPlatAPI
#include "pch.h"
#include "SearchSessions.h"

// ─── Constants ───────────────────────────────────────────────────────────────
static constexpr UINT WM_SEARCH_RESULTS = WM_APP + 1;
static constexpr UINT WM_TRAYICON       = WM_APP + 2;
static constexpr int IDC_SEARCHBOX = 101;
static constexpr int IDC_LISTVIEW  = 102;
static constexpr int IDC_STATUS    = 103;
static constexpr int IDM_EXIT      = 200;
static constexpr int HOTKEY_ID     = 1;
static constexpr int SEARCH_BOX_HEIGHT = 48;
static constexpr int CONTENT_WIDTH = 900;
static constexpr int MAX_RESULTS   = 200;
static constexpr BYTE OVERLAY_ALPHA = 128; // 50% opacity

// Dark theme colors
static constexpr COLORREF CLR_BG       = RGB(30, 30, 30);
static constexpr COLORREF CLR_BG_ALT   = RGB(38, 38, 38);
static constexpr COLORREF CLR_TEXT      = RGB(230, 230, 230);
static constexpr COLORREF CLR_TEXTDIM   = RGB(160, 160, 160);
static constexpr COLORREF CLR_SELECT   = RGB(0, 90, 158);
static constexpr COLORREF CLR_EDITBG   = RGB(50, 50, 50);
static constexpr COLORREF CLR_BORDER   = RGB(70, 70, 70);

// ─── Per-result data ─────────────────────────────────────────────────────────
struct ResultItem
{
    std::wstring displayName;
    std::wstring filePath; // filesystem path for opening
    std::wstring parentDir;
    bool isFolder = false;
};

// ─── Globals ─────────────────────────────────────────────────────────────────
static HWND g_hwndMain     = nullptr; // Full-screen overlay window
static HWND g_hwndHost     = nullptr; // Hidden host window (owns hotkey + tray)
static HWND g_hwndSearch   = nullptr; // Edit control
static HWND g_hwndList     = nullptr; // ListView
static HWND g_hwndStatus   = nullptr; // Status text
static HIMAGELIST g_hImageList = nullptr;
static HBRUSH g_hBgBrush   = nullptr;
static HBRUSH g_hEditBrush = nullptr;
static HFONT g_hFontNormal = nullptr;
static HFONT g_hFontSearch = nullptr;
static HFONT g_hFontSmall  = nullptr;
static WNDPROC g_origEditProc = nullptr;
static NOTIFYICONDATAW g_nid{};
static bool g_overlayVisible = false;

static std::vector<ResultItem> g_results;
static std::unique_ptr<wsearch::SearchAsYouTypeSession> g_searchSession;
static std::atomic<uint64_t> g_queryGeneration{ 0 };

// Icon cache: extension -> ImageList index
static std::unordered_map<std::wstring, int> g_iconCache;
static int g_folderIconIndex = -1;

// ─── Icon Cache ──────────────────────────────────────────────────────────────
static std::wstring GetExtension(const std::wstring& path)
{
    auto dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = towlower(c);
    return ext;
}

static int GetIconIndex(const std::wstring& filePath, bool isFolder)
{
    if (isFolder)
    {
        if (g_folderIconIndex >= 0) return g_folderIconIndex;
        SHFILEINFOW sfi{};
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        if (sfi.hIcon)
        {
            g_folderIconIndex = ImageList_AddIcon(g_hImageList, sfi.hIcon);
            DestroyIcon(sfi.hIcon);
        }
        return g_folderIconIndex;
    }

    std::wstring ext = GetExtension(filePath);
    if (ext.empty()) ext = L".file";

    auto it = g_iconCache.find(ext);
    if (it != g_iconCache.end()) return it->second;

    SHFILEINFOW sfi{};
    SHGetFileInfoW(ext.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    int idx = -1;
    if (sfi.hIcon)
    {
        idx = ImageList_AddIcon(g_hImageList, sfi.hIcon);
        DestroyIcon(sfi.hIcon);
    }
    g_iconCache[ext] = idx;
    return idx;
}

// ─── URL → file path converter ──────────────────────────────────────────────
static std::wstring UrlToFilePath(const std::wstring& url)
{
    std::wstring s = url;
    if (s.find(L"file:///") == 0)      s = s.substr(8);
    else if (s.find(L"file://") == 0)  s = s.substr(7);
    std::replace(s.begin(), s.end(), L'/', L'\\');
    // Decode %XX
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == L'%' && i + 2 < s.size())
        {
            wchar_t hex[3] = { s[i + 1], s[i + 2], 0 };
            out += static_cast<wchar_t>(wcstol(hex, nullptr, 16));
            i += 2;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

static std::wstring GetParentDir(const std::wstring& path)
{
    auto pos = path.rfind(L'\\');
    return (pos != std::wstring::npos) ? path.substr(0, pos) : L"";
}

// ─── Property helpers ────────────────────────────────────────────────────────
static std::wstring GetStringProp(IPropertyStore* ps, const PROPERTYKEY& key)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring result;
    if (SUCCEEDED(ps->GetValue(key, &pv)))
    {
        if (pv.vt == VT_LPWSTR && pv.pwszVal)
            result = pv.pwszVal;
        else if (pv.vt == VT_BSTR && pv.bstrVal)
            result = pv.bstrVal;
    }
    PropVariantClear(&pv);
    return result;
}

static bool IsKindFolder(IPropertyStore* ps)
{
    std::wstring kind = GetStringProp(ps, PKEY_Kind);
    return kind.find(L"folder") != std::wstring::npos;
}

// ─── Search execution ────────────────────────────────────────────────────────
static void PostSearchResults(uint64_t generation, std::vector<ResultItem> items)
{
    // Allocate on heap, the WM handler will delete
    auto* data = new std::pair<uint64_t, std::vector<ResultItem>>(generation, std::move(items));
    PostMessageW(g_hwndMain, WM_SEARCH_RESULTS, 0, reinterpret_cast<LPARAM>(data));
}

static void ExecuteSearch(const std::wstring& text, uint64_t generation)
{
    if (!g_searchSession) return;

    std::vector<ResultItem> items;

    if (text.empty())
    {
        PostSearchResults(generation, std::move(items));
        return;
    }

    try
    {
        g_searchSession->SetSearchText(text);
        auto rowset = g_searchSession->GetCachedResults();
        if (!rowset)
        {
            rowset = g_searchSession->ExecuteQueryNow();
        }
        if (!rowset)
        {
            PostSearchResults(generation, std::move(items));
            return;
        }

        winrt::com_ptr<IGetRow> getRow;
        THROW_IF_FAILED(rowset->QueryInterface(IID_PPV_ARGS(getRow.put())));

        DBCOUNTITEM rowCount = 0;
        HROW rowBuffer[MAX_RESULTS];
        HROW* pRows = rowBuffer;
        THROW_IF_FAILED(rowset->GetNextRows(DB_NULL_HCHAPTER, 0, MAX_RESULTS, &rowCount, &pRows));

        for (DBCOUNTITEM i = 0; i < rowCount && generation == g_queryGeneration.load(); ++i)
        {
            winrt::com_ptr<IUnknown> unk;
            if (FAILED(getRow->GetRowFromHROW(nullptr, rowBuffer[i], __uuidof(IPropertyStore), unk.put())))
                continue;

            auto ps = unk.as<IPropertyStore>();
            ResultItem item;
            item.displayName = GetStringProp(ps.get(), PKEY_ItemNameDisplay);
            std::wstring urlPath = GetStringProp(ps.get(), PKEY_ItemUrl);
            item.filePath = UrlToFilePath(urlPath);
            item.parentDir = GetParentDir(item.filePath);
            item.isFolder = IsKindFolder(ps.get());

            if (!item.displayName.empty())
                items.push_back(std::move(item));
        }

        rowset->ReleaseRows(rowCount, pRows, nullptr, nullptr, nullptr);
    }
    catch (...) { /* swallow search errors */ }

    PostSearchResults(generation, std::move(items));
}

// ─── Open a result ───────────────────────────────────────────────────────────
static void OpenSelectedResult()
{
    int sel = ListView_GetNextItem(g_hwndList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(g_results.size())) return;

    const auto& item = g_results[sel];
    ShellExecuteW(nullptr, L"open", item.filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if (g_searchSession)
        g_searchSession->TrackResultClick(item.filePath);
}

// ─── Show / hide overlay ─────────────────────────────────────────────────────
static void ShowOverlay()
{
    if (g_overlayVisible) return;
    g_overlayVisible = true;

    // Clear previous search state
    SetWindowTextW(g_hwndSearch, L"");
    g_results.clear();
    ListView_SetItemCountEx(g_hwndList, 0, 0);
    SetWindowTextW(g_hwndStatus, L"Type to search");

    ShowWindow(g_hwndMain, SW_SHOW);
    SetForegroundWindow(g_hwndMain);
    SetFocus(g_hwndSearch);
}

static void HideOverlay()
{
    if (!g_overlayVisible) return;
    g_overlayVisible = false;
    ShowWindow(g_hwndMain, SW_HIDE);
}

static void ToggleOverlay()
{
    if (g_overlayVisible)
        HideOverlay();
    else
        ShowOverlay();
}

// ─── Subclassed Edit proc (handle Enter/Escape/arrows) ──────────────────────
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            HideOverlay();
            return 0;
        }
        if (wp == VK_RETURN)
        {
            OpenSelectedResult();
            return 0;
        }
        if (wp == VK_DOWN)
        {
            int count = ListView_GetItemCount(g_hwndList);
            if (count > 0)
            {
                int cur = ListView_GetNextItem(g_hwndList, -1, LVNI_SELECTED);
                int next = (cur < 0) ? 0 : min(cur + 1, count - 1);
                ListView_SetItemState(g_hwndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(g_hwndList, next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_hwndList, next, FALSE);
            }
            return 0;
        }
        if (wp == VK_UP)
        {
            int cur = ListView_GetNextItem(g_hwndList, -1, LVNI_SELECTED);
            if (cur > 0)
            {
                ListView_SetItemState(g_hwndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(g_hwndList, cur - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_hwndList, cur - 1, FALSE);
            }
            return 0;
        }
        break;

    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0; // eat beep
        break;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wp, lp);
}

// ─── Main window proc ───────────────────────────────────────────────────────
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        // Content area: centered panel
        int panelX = (screenW - CONTENT_WIDTH) / 2;
        int panelY = 60;

        // Search edit
        g_hwndSearch = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            panelX + 16, panelY + 16, CONTENT_WIDTH - 32, SEARCH_BOX_HEIGHT,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCHBOX)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_hwndSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSearch), TRUE);
        SendMessageW(g_hwndSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search files..."));
        g_origEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_hwndSearch, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc)));

        // Status label
        int statusY = panelY + 16 + SEARCH_BOX_HEIGHT + 8;
        g_hwndStatus = CreateWindowExW(0, L"STATIC", L"Type to search",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            panelX + 20, statusY, CONTENT_WIDTH - 40, 20,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_hwndStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSmall), TRUE);

        // ListView
        int lvTop = statusY + 24;
        int lvHeight = screenH - lvTop - 60;
        g_hwndList = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
            LVS_OWNERDATA | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
            panelX, lvTop, CONTENT_WIDTH, lvHeight,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LISTVIEW)),
            GetModuleHandleW(nullptr), nullptr);

        SetWindowTheme(g_hwndList, L"Explorer", nullptr);
        ListView_SetBkColor(g_hwndList, CLR_BG);
        ListView_SetTextColor(g_hwndList, CLR_TEXT);
        ListView_SetTextBkColor(g_hwndList, CLR_NONE);
        ListView_SetImageList(g_hwndList, g_hImageList, LVSIL_SMALL);
        ListView_SetExtendedListViewStyle(g_hwndList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = CONTENT_WIDTH;
        ListView_InsertColumn(g_hwndList, 0, &col);

        SetFocus(g_hwndSearch);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SEARCHBOX && HIWORD(wp) == EN_CHANGE)
        {
            wchar_t text[512]{};
            GetWindowTextW(g_hwndSearch, text, ARRAYSIZE(text));
            uint64_t gen = ++g_queryGeneration;

            std::wstring searchText(text);
            std::thread([searchText, gen]()
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                ExecuteSearch(searchText, gen);
            }).detach();

            if (searchText.empty())
            {
                g_results.clear();
                ListView_SetItemCountEx(g_hwndList, 0, 0);
                SetWindowTextW(g_hwndStatus, L"Type to search");
            }
            else
            {
                SetWindowTextW(g_hwndStatus, L"Searching...");
            }
            return 0;
        }
        break;

    case WM_SEARCH_RESULTS:
    {
        auto* data = reinterpret_cast<std::pair<uint64_t, std::vector<ResultItem>>*>(lp);
        if (data->first == g_queryGeneration.load())
        {
            g_results = std::move(data->second);
            ListView_SetItemCountEx(g_hwndList, static_cast<int>(g_results.size()), LVSICF_NOINVALIDATEALL);
            InvalidateRect(g_hwndList, nullptr, TRUE);

            wchar_t status[128];
            double ms = g_searchSession ? g_searchSession->GetLastQueryDurationMs() : 0.0;
            swprintf_s(status, L"%zu results (%.1f ms)", g_results.size(), ms);
            SetWindowTextW(g_hwndStatus, status);

            if (!g_results.empty())
            {
                ListView_SetItemState(g_hwndList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        }
        delete data;
        return 0;
    }

    case WM_NOTIFY:
    {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lp);
        if (nmhdr->idFrom == IDC_LISTVIEW)
        {
            switch (nmhdr->code)
            {
            case LVN_GETDISPINFOW:
            {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
                int idx = di->item.iItem;
                if (idx < 0 || idx >= static_cast<int>(g_results.size())) break;

                const auto& item = g_results[idx];
                if (di->item.mask & LVIF_TEXT)
                    di->item.pszText = const_cast<LPWSTR>(item.displayName.c_str());
                if (di->item.mask & LVIF_IMAGE)
                    di->item.iImage = GetIconIndex(item.filePath, item.isFolder);
                break;
            }

            case NM_CUSTOMDRAW:
            {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                switch (cd->nmcd.dwDrawStage)
                {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;

                case CDDS_ITEMPREPAINT:
                {
                    int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    bool selected = (ListView_GetItemState(g_hwndList, idx, LVIS_SELECTED) & LVIS_SELECTED);
                    cd->clrTextBk = selected ? CLR_SELECT : ((idx % 2) ? CLR_BG_ALT : CLR_BG);
                    cd->clrText = CLR_TEXT;
                    return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                }

                case CDDS_ITEMPOSTPAINT:
                {
                    int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx < 0 || idx >= static_cast<int>(g_results.size())) break;

                    const auto& item = g_results[idx];
                    if (item.parentDir.empty()) break;

                    RECT rcItem;
                    ListView_GetItemRect(g_hwndList, idx, &rcItem, LVIR_LABEL);
                    rcItem.top += 18;

                    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(cd->nmcd.hdc, g_hFontSmall));
                    SetTextColor(cd->nmcd.hdc, CLR_TEXTDIM);
                    SetBkMode(cd->nmcd.hdc, TRANSPARENT);
                    DrawTextW(cd->nmcd.hdc, item.parentDir.c_str(), -1, &rcItem,
                        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(cd->nmcd.hdc, oldFont);
                    break;
                }
                }
                break;
            }

            case NM_DBLCLK:
                OpenSelectedResult();
                break;
            }
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, CLR_TEXT);
        SetBkColor(hdc, CLR_EDITBG);
        return reinterpret_cast<LRESULT>(g_hEditBrush);
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, CLR_TEXTDIM);
        SetBkColor(hdc, CLR_BG);
        return reinterpret_cast<LRESULT>(g_hBgBrush);
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hBgBrush);
        return 1;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && g_hwndSearch)
            SetFocus(g_hwndSearch);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            HideOverlay();
            return 0;
        }
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── Host window proc (hidden — owns hotkey + tray icon) ─────────────────────
static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_HOTKEY:
        if (wp == HOTKEY_ID)
        {
            ToggleOverlay();
            return 0;
        }
        break;

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
            return 0;
        }
        if (LOWORD(lp) == WM_LBUTTONDBLCLK)
        {
            ToggleOverlay();
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == IDM_EXIT)
        {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── Entry point ─────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SearchApp_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Common controls (ListView)
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Create GDI resources
    g_hBgBrush = CreateSolidBrush(CLR_BG);
    g_hEditBrush = CreateSolidBrush(CLR_EDITBG);
    g_hFontSearch = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_hFontNormal = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_hFontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    // ImageList for file icons (16x16 small icons)
    g_hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 64, 64);

    // ── Hidden host window (hotkey + tray owner) ──
    WNDCLASSEXW wcHost{};
    wcHost.cbSize = sizeof(wcHost);
    wcHost.lpfnWndProc = HostWndProc;
    wcHost.hInstance = hInst;
    wcHost.lpszClassName = L"SearchAppHost";
    RegisterClassExW(&wcHost);

    g_hwndHost = CreateWindowExW(0, L"SearchAppHost", L"",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    // Register global hotkey: Ctrl+Shift+F
    if (!RegisterHotKey(g_hwndHost, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'F'))
    {
        MessageBoxW(nullptr, L"Failed to register Ctrl+Shift+F hotkey.\nAnother app may be using it.",
            L"SearchApp", MB_OK | MB_ICONWARNING);
    }

    // ── System tray icon ──
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwndHost;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"SearchApp (Ctrl+Shift+F)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // ── Overlay window (starts hidden) ──
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_hBgBrush;
    wc.lpszClassName = L"SearchOverlay";
    RegisterClassExW(&wc);

    g_hwndMain = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        L"SearchOverlay", L"Search",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, screenW, screenH,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwndMain)
    {
        MessageBoxW(nullptr, L"Failed to create overlay window.", L"SearchApp", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetLayeredWindowAttributes(g_hwndMain, 0, OVERLAY_ALPHA, LWA_ALPHA);

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_hwndMain, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Initialize search session
    try
    {
        g_searchSession = std::make_unique<wsearch::SearchAsYouTypeSession>(
            std::vector<std::wstring>{ L"file:" },
            std::vector<std::wstring>{},
            std::vector<std::wstring>{ L"System.ItemNameDisplay", L"System.Kind" },
            std::chrono::milliseconds(50));
    }
    catch (winrt::hresult_error const& ex)
    {
        wchar_t buf[512];
        swprintf_s(buf, L"Search init failed: 0x%08X\n%s", ex.code().value, ex.message().c_str());
        MessageBoxW(nullptr, buf, L"SearchApp", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (std::exception const& ex)
    {
        MessageBoxA(nullptr, ex.what(), "SearchApp", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (...)
    {
        MessageBoxW(nullptr, L"Unknown exception during search init.", L"SearchApp", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Message loop (app stays running in tray)
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_searchSession.reset();
    if (g_hImageList) ImageList_Destroy(g_hImageList);
    DeleteObject(g_hBgBrush);
    DeleteObject(g_hEditBrush);
    DeleteObject(g_hFontSearch);
    DeleteObject(g_hFontNormal);
    DeleteObject(g_hFontSmall);
    CloseHandle(hMutex);

    return 0;
}
