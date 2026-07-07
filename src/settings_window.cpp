#include "settings_window.h"
#include <uxtheme.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace tvl {

namespace {
constexpr COLORREF C_BG  = RGB(30, 30, 34);
constexpr COLORREF C_BG2 = RGB(40, 40, 46);
constexpr COLORREF C_BG3 = RGB(52, 52, 60);
constexpr COLORREF C_FG  = RGB(235, 235, 238);
constexpr COLORREF C_FG2 = RGB(170, 170, 178);
constexpr int ID_BTN_SAVE = 200, ID_BTN_CANCEL = 201;
constexpr int ID_CB_CTRL=300, ID_CB_ALT=301, ID_CB_SHIFT=302, ID_CB_WIN=303, ID_CB_KEY=304,
               ID_ST_TOGGLEDISP=305, ID_CB_NCTRL=306, ID_CB_NALT=307, ID_CB_NSHIFT=308, ID_CB_NWIN=309,
               ID_ST_NUMBERDISP=310, ID_CB_THEME=311, ID_ED_WIDTH=312, ID_ED_MAXITEMS=313,
               ID_CB_SEARCH=314, ID_CB_HINTS=315, ID_CB_AUTOSTART=316, ID_CB_CLOSECLICK=317,
               ID_CB_CLOSEFOCUS=318, ID_ED_REFRESH=319, ID_ED_ITEMHEIGHT=320;

struct KeyEntry { const wchar_t* name; UINT vk; };
const KeyEntry kKeys[] = {
    {L"Space", VK_SPACE}, {L"Enter", VK_RETURN}, {L"Tab", VK_TAB}, {L"Esc", VK_ESCAPE},
    {L"A",'A'},{L"B",'B'},{L"C",'C'},{L"D",'D'},{L"E",'E'},{L"F",'F'},{L"G",'G'},{L"H",'H'},
    {L"I",'I'},{L"J",'J'},{L"K",'K'},{L"L",'L'},{L"M",'M'},{L"N",'N'},{L"O",'O'},{L"P",'P'},
    {L"Q",'Q'},{L"R",'R'},{L"S",'S'},{L"T",'T'},{L"U",'U'},{L"V",'V'},{L"W",'W'},{L"X",'X'},
    {L"Y",'Y'},{L"Z",'Z'},
    {L"F1",VK_F1},{L"F2",VK_F2},{L"F3",VK_F3},{L"F4",VK_F4},{L"F5",VK_F5},{L"F6",VK_F6},
    {L"F7",VK_F7},{L"F8",VK_F8},{L"F9",VK_F9},{L"F10",VK_F10},{L"F11",VK_F11},{L"F12",VK_F12},
    {L"0",'0'},{L"1",'1'},{L"2",'2'},{L"3",'3'},{L"4",'4'},
    {L"5",'5'},{L"6",'6'},{L"7",'7'},{L"8",'8'},{L"9",'9'},
    {L"Home",VK_HOME},{L"End",VK_END},{L"PgUp",VK_PRIOR},{L"PgDn",VK_NEXT},
    {L"Insert",VK_INSERT},{L"Delete",VK_DELETE},
};

// 窗口类注册 flag（只注册一次）
static bool g_settingsClassRegistered = false;

HWND CreateCtrl(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle,
                int x, int y, int w, int h, HWND parent, int id) {
    return CreateWindowExW(exStyle, cls, text ? text : L"", style | WS_CHILD | WS_VISIBLE,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}
}

void SettingsWindow::RegisterWindowClass() {
    if (g_settingsClassRegistered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc   = &SettingsWindow::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW); // 占位，实际由 WM_ERASEBKGND 绘制
    wc.lpszClassName = L"TVLSettings";
    if (!RegisterClassW(&wc)) {
        // 如果已存在（其他模块或上次未正确清理），忽略错误
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) return;
    }
    g_settingsClassRegistered = true;
}

void SettingsWindow::Show(HWND parent) {
    float s = GetSystemDpiScale();
    int dpi = static_cast<int>(96.0f * s);

    bgBrush_  = CreateSolidBrush(C_BG);
    bg2Brush_ = CreateSolidBrush(C_BG2);
    font_ = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    boldFont_ = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    RegisterWindowClass();

    RECT prc{}; if (parent) GetWindowRect(parent, &prc);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"TVLSettings",
        L"TrayVerticalList 设置", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, ScaleDpi(s, 460), ScaleDpi(s, 560),
        parent, nullptr, GetModuleHandleW(nullptr), this);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    BuildControls();
    LoadFromSettings();
    UpdateDisplays();

    // 模态:禁用父窗口,自有消息循环
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        if (IsDialogMessageW(hwnd_, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (parent) { EnableWindow(parent, TRUE); SetFocus(parent); }

    // 清理 GDI 资源（窗口已销毁）
    if (boldFont_) { DeleteObject(boldFont_); boldFont_ = nullptr; }
    if (font_)     { DeleteObject(font_);     font_     = nullptr; }
    if (bg2Brush_) { DeleteObject(bg2Brush_); bg2Brush_ = nullptr; }
    if (bgBrush_)  { DeleteObject(bgBrush_);  bgBrush_  = nullptr; }
}

void SettingsWindow::BuildControls() {
    float s = GetSystemDpiScale();
    auto S = [s](int v) { return ScaleDpi(s, v); };

    int x = S(20), y = S(14), line = S(26);
    int col1 = x + S(130);   // 第二列起始
    int cbW = S(58);          // 复选框宽度
    int cbGap = S(6);         // 复选框间距
    auto Label = [&](const wchar_t* t, int xx, int yy) {
        HWND h = CreateCtrl(L"STATIC", t, SS_LEFT, 0, xx, yy, S(200), S(18), hwnd_, 0);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return h;
    };
    auto Section = [&](const wchar_t* t, int xx, int yy) {
        HWND h = CreateCtrl(L"STATIC", t, SS_LEFT, 0, xx, yy, S(200), S(20), hwnd_, 0);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(boldFont_), TRUE);
        return h;
    };

    Section(L"热键设置", x, y);
    y += line;
    Label(L"展开菜单修饰键:", x, y);
    cbCtrl_  = CreateCtrl(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX, 0, col1, y - S(2), cbW, S(22), hwnd_, ID_CB_CTRL);
    cbAlt_   = CreateCtrl(L"BUTTON", L"Alt",  BS_AUTOCHECKBOX, 0, col1 + cbW + cbGap, y - S(2), cbW, S(22), hwnd_, ID_CB_ALT);
    cbShift_ = CreateCtrl(L"BUTTON", L"Shift",BS_AUTOCHECKBOX, 0, col1 + 2*(cbW+cbGap), y - S(2), cbW, S(22), hwnd_, ID_CB_SHIFT);
    cbWin_   = CreateCtrl(L"BUTTON", L"Win",  BS_AUTOCHECKBOX, 0, col1 + 3*(cbW+cbGap), y - S(2), cbW, S(22), hwnd_, ID_CB_WIN);
    y += line;
    Label(L"主键:", x, y);
    cbToggleKey_ = CreateCtrl(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, 0, col1, y - S(2), S(120), S(300), hwnd_, ID_CB_KEY);
    for (auto& k : kKeys) SendMessageW(cbToggleKey_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(k.name));
    y += line;
    Label(L"当前组合:", x, y);
    stToggleDisplay_ = CreateCtrl(L"STATIC", L"", SS_LEFT, 0, col1, y, S(280), S(18), hwnd_, ID_ST_TOGGLEDISP);
    y += line + S(6);

    Section(L"数字快捷键修饰键:", x, y);
    y += line;
    cbNCtrl_  = CreateCtrl(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX, 0, col1, y - S(2), cbW, S(22), hwnd_, ID_CB_NCTRL);
    cbNAlt_   = CreateCtrl(L"BUTTON", L"Alt",  BS_AUTOCHECKBOX, 0, col1 + cbW + cbGap, y - S(2), cbW, S(22), hwnd_, ID_CB_NALT);
    cbNShift_ = CreateCtrl(L"BUTTON", L"Shift",BS_AUTOCHECKBOX, 0, col1 + 2*(cbW+cbGap), y - S(2), cbW, S(22), hwnd_, ID_CB_NSHIFT);
    cbNWin_   = CreateCtrl(L"BUTTON", L"Win",  BS_AUTOCHECKBOX, 0, col1 + 3*(cbW+cbGap), y - S(2), cbW, S(22), hwnd_, ID_CB_NWIN);
    y += line;
    Label(L"当前组合:", x, y);
    stNumberDisplay_ = CreateCtrl(L"STATIC", L"", SS_LEFT, 0, col1, y, S(280), S(18), hwnd_, ID_ST_NUMBERDISP);
    y += line + S(10);

    Section(L"外观", x, y);
    y += line;
    Label(L"主题:", x, y);
    cbTheme_ = CreateCtrl(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, 0, col1, y - S(2), S(120), S(200), hwnd_, ID_CB_THEME);
    SendMessageW(cbTheme_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
    SendMessageW(cbTheme_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
    SendMessageW(cbTheme_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System"));
    y += line;
    Label(L"窗口宽度:", x, y);
    edWidth_ = CreateCtrl(L"EDIT", L"280", ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, col1, y - S(2), S(80), S(22), hwnd_, ID_ED_WIDTH);
    y += line;
    Label(L"最大可见项:", x, y);
    edMaxItems_ = CreateCtrl(L"EDIT", L"20", ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, col1, y - S(2), S(80), S(22), hwnd_, ID_ED_MAXITEMS);
    y += line;
    Label(L"项高度(px):", x, y);
    edItemHeight_ = CreateCtrl(L"EDIT", L"40", ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, col1, y - S(2), S(80), S(22), hwnd_, ID_ED_ITEMHEIGHT);
    y += line;
    cbShowSearch_ = CreateCtrl(L"BUTTON", L"显示搜索框", BS_AUTOCHECKBOX, 0, x, y, S(150), S(22), hwnd_, ID_CB_SEARCH);
    cbShowHints_  = CreateCtrl(L"BUTTON", L"显示数字提示", BS_AUTOCHECKBOX, 0, x + S(160), y, S(150), S(22), hwnd_, ID_CB_HINTS);
    y += line + S(10);

    Section(L"行为", x, y);
    y += line;
    cbAutoStart_       = CreateCtrl(L"BUTTON", L"开机自启动", BS_AUTOCHECKBOX, 0, x, y, S(150), S(22), hwnd_, ID_CB_AUTOSTART);
    cbCloseOnClick_    = CreateCtrl(L"BUTTON", L"点击后关闭", BS_AUTOCHECKBOX, 0, x + S(160), y, S(150), S(22), hwnd_, ID_CB_CLOSECLICK);
    y += line;
    cbCloseOnFocusLost_= CreateCtrl(L"BUTTON", L"失焦关闭", BS_AUTOCHECKBOX, 0, x, y, S(150), S(22), hwnd_, ID_CB_CLOSEFOCUS);
    y += line;
    Label(L"刷新间隔(秒):", x, y);
    edRefresh_ = CreateCtrl(L"EDIT", L"5", ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE, col1, y - S(2), S(80), S(22), hwnd_, ID_ED_REFRESH);
    y += line + S(14);

    btnSave_   = CreateCtrl(L"BUTTON", L"保存", BS_PUSHBUTTON, 0, x + S(220), y, S(90), S(30), hwnd_, ID_BTN_SAVE);
    btnCancel_ = CreateCtrl(L"BUTTON", L"取消", BS_PUSHBUTTON, 0, x + S(320), y, S(90), S(30), hwnd_, ID_BTN_CANCEL);

    // 深色主题：去除所有子控件视觉样式，让 WM_CTLCOLOR* 接管绘制
    ApplyDarkTheme();

    // 字体应用到所有子控件
    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font_));
}

void SettingsWindow::ApplyDarkTheme() {
    // 移除视觉样式，使 WM_CTLCOLOR* 消息能接管上色
    auto untheme = [](HWND h) {
        if (h) SetWindowTheme(h, L" ", L" ");
    };
    // 所有按钮和复选框
    untheme(btnSave_);
    untheme(btnCancel_);
    untheme(cbCtrl_);  untheme(cbAlt_);  untheme(cbShift_);  untheme(cbWin_);
    untheme(cbNCtrl_); untheme(cbNAlt_); untheme(cbNShift_); untheme(cbNWin_);
    untheme(cbShowSearch_); untheme(cbShowHints_);
    untheme(cbAutoStart_); untheme(cbCloseOnClick_); untheme(cbCloseOnFocusLost_);
    // ComboBox
    untheme(cbToggleKey_);
    untheme(cbTheme_);
    // Edit
    untheme(edWidth_);
    untheme(edMaxItems_);
    untheme(edItemHeight_);
    untheme(edRefresh_);
}

void SettingsWindow::LoadFromSettings() {
    auto& t = settings_.toggleMenu;
    SendMessageW(cbCtrl_,  BM_SETCHECK, t.modifiers.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbAlt_,   BM_SETCHECK, t.modifiers.alt  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbShift_, BM_SETCHECK, t.modifiers.shift? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbWin_,   BM_SETCHECK, t.modifiers.win  ? BST_CHECKED : BST_UNCHECKED, 0);
    int sel = 0;
    for (int i = 0; i < _countof(kKeys); ++i) if (kKeys[i].vk == t.key) { sel = i; break; }
    SendMessageW(cbToggleKey_, CB_SETCURSEL, sel, 0);

    auto& n = settings_.numberPrefix;
    SendMessageW(cbNCtrl_,  BM_SETCHECK, n.modifiers.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbNAlt_,   BM_SETCHECK, n.modifiers.alt  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbNShift_, BM_SETCHECK, n.modifiers.shift? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbNWin_,   BM_SETCHECK, n.modifiers.win  ? BST_CHECKED : BST_UNCHECKED, 0);

    int themeSel = 0;
    if (settings_.appearance.theme == L"Light") themeSel = 1;
    else if (settings_.appearance.theme == L"System") themeSel = 2;
    SendMessageW(cbTheme_, CB_SETCURSEL, themeSel, 0);

    SetWindowTextW(edWidth_, std::to_wstring(settings_.appearance.windowWidth).c_str());
    SetWindowTextW(edMaxItems_, std::to_wstring(settings_.appearance.maxVisibleItems).c_str());
    SetWindowTextW(edItemHeight_, std::to_wstring(settings_.appearance.itemHeight).c_str());
    SendMessageW(cbShowSearch_, BM_SETCHECK, settings_.appearance.showSearchBox ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbShowHints_,  BM_SETCHECK, settings_.appearance.showNumberHints ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbAutoStart_,       BM_SETCHECK, settings_.behavior.autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbCloseOnClick_,    BM_SETCHECK, settings_.behavior.closeOnClick ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cbCloseOnFocusLost_,BM_SETCHECK, settings_.behavior.closeOnFocusLost ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(edRefresh_, std::to_wstring(settings_.behavior.refreshIntervalSeconds).c_str());
}

AppSettings SettingsWindow::CollectFromUi() {
    AppSettings s = settings_;
    auto readMod = [](HWND c1, HWND c2, HWND c3, HWND c4) {
        ModifierSet m;
        m.ctrl  = SendMessageW(c1, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.alt   = SendMessageW(c2, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.shift = SendMessageW(c3, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.win   = SendMessageW(c4, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return m;
    };
    s.toggleMenu.modifiers = readMod(cbCtrl_, cbAlt_, cbShift_, cbWin_);
    int ksel = (int)SendMessageW(cbToggleKey_, CB_GETCURSEL, 0, 0);
    if (ksel >= 0 && ksel < _countof(kKeys)) s.toggleMenu.key = kKeys[ksel].vk;
    s.toggleMenu.description = L"展开/收起垂直列表菜单";

    s.numberPrefix.modifiers = readMod(cbNCtrl_, cbNAlt_, cbNShift_, cbNWin_);
    s.numberPrefix.key = VK_SPACE;
    s.numberPrefix.description = L"数字快捷键前缀";

    int thsel = (int)SendMessageW(cbTheme_, CB_GETCURSEL, 0, 0);
    s.appearance.theme = (thsel == 1) ? L"Light" : (thsel == 2 ? L"System" : L"Dark");

    wchar_t buf[32] = {};
    auto readInt = [](HWND h, wchar_t* b, int def, int lo, int hi) {
        GetWindowTextW(h, b, 32);
        int v = def;
        try { v = std::stoi(b); } catch (...) {}
        if (v < lo) v = lo; if (v > hi) v = hi;
        return v;
    };
    s.appearance.windowWidth = readInt(edWidth_, buf, 280, 200, 600);
    s.appearance.maxVisibleItems = readInt(edMaxItems_, buf, 20, 5, 50);
    s.appearance.itemHeight = readInt(edItemHeight_, buf, 40, 18, 80);
    s.appearance.showSearchBox = SendMessageW(cbShowSearch_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.appearance.showNumberHints = SendMessageW(cbShowHints_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.behavior.autoStart = SendMessageW(cbAutoStart_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.behavior.closeOnClick = SendMessageW(cbCloseOnClick_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.behavior.closeOnFocusLost = SendMessageW(cbCloseOnFocusLost_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.behavior.refreshIntervalSeconds = readInt(edRefresh_, buf, 5, 1, 60);
    return s;
}

void SettingsWindow::UpdateDisplays() {
    auto modsStr = [](HWND c1, HWND c2, HWND c3, HWND c4) {
        ModifierSet m;
        m.ctrl  = SendMessageW(c1, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.alt   = SendMessageW(c2, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.shift = SendMessageW(c3, BM_GETCHECK, 0, 0) == BST_CHECKED;
        m.win   = SendMessageW(c4, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return m.Display();
    };
    std::wstring td = modsStr(cbCtrl_, cbAlt_, cbShift_, cbWin_);
    int ksel = (int)SendMessageW(cbToggleKey_, CB_GETCURSEL, 0, 0);
    if (!td.empty()) td += L"+";
    td += (ksel >= 0 && ksel < _countof(kKeys)) ? kKeys[ksel].name : L"Space";
    SetWindowTextW(stToggleDisplay_, td.c_str());

    std::wstring nd = modsStr(cbNCtrl_, cbNAlt_, cbNShift_, cbNWin_);
    if (!nd.empty()) nd += L"+";
    nd += L"1~9";
    SetWindowTextW(stNumberDisplay_, nd.c_str());
}

LRESULT CALLBACK SettingsWindow::WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self && m == WM_CREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->WndProc(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT SettingsWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        // 手动绘制深色背景（替代窗口类画刷，避免画刷生命周期问题）
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc; GetClientRect(hwnd_, &rc);
        FillRect(dc, &rc, bgBrush_);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        // 节标题用粗体颜色区分
        SetTextColor(dc, C_FG);
        SetBkColor(dc, C_BG);
        return reinterpret_cast<LRESULT>(bgBrush_);
    }
    case WM_CTLCOLORBTN: {
        // 按钮和复选框：统一深色背景+浅色文字
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_FG);
        SetBkColor(dc, C_BG);
        return reinterpret_cast<LRESULT>(bgBrush_);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_FG);
        SetBkColor(dc, C_BG2);
        return reinterpret_cast<LRESULT>(bg2Brush_); // 复用成员画刷，不再每次创建
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_CANCEL) { DestroyWindow(hwnd_); return 0; }
        if (id == ID_BTN_SAVE) {
            AppSettings s = CollectFromUi();
            settings_ = s;
            if (onSave_) onSave_(s);
            DestroyWindow(hwnd_);
            return 0;
        }
        // 任意热键/主键变化 -> 刷新显示
        if (id == ID_CB_CTRL || id == ID_CB_ALT || id == ID_CB_SHIFT || id == ID_CB_WIN ||
            id == ID_CB_KEY || id == ID_CB_NCTRL || id == ID_CB_NALT || id == ID_CB_NSHIFT || id == ID_CB_NWIN) {
            if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE)
                UpdateDisplays();
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0); // 退出模态消息循环
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace tvl
