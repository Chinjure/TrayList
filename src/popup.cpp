#include "popup.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace tvl {

namespace {
constexpr UINT WM_REFRESH_DONE = WM_APP + 10;
constexpr UINT WM_REAPPEAR     = WM_APP + 11;

// 深色主题色板
constexpr COLORREF C_BG     = RGB(30, 30, 34);
constexpr COLORREF C_BG2    = RGB(40, 40, 46);
constexpr COLORREF C_BG3    = RGB(52, 52, 60);
constexpr COLORREF C_FG     = RGB(235, 235, 238);
constexpr COLORREF C_FG2    = RGB(170, 170, 178);
constexpr COLORREF C_BORDER = RGB(58, 58, 66);
constexpr COLORREF C_ACCENT = RGB(88, 130, 220);
constexpr COLORREF C_HOVER = RGB(56, 56, 64);      // 悬停高亮
constexpr COLORREF C_SELECTED = RGB(50, 80, 130);  // 选中高亮

HBRUSH MakeBrush(COLORREF c) { return CreateSolidBrush(c); }

// 格式化列表项显示文本：" 1  Microsoft Teams"
std::wstring FormatItemText(int index, const TrayIconInfo& icon) {
    int num = (index == 9) ? 0 : index + 1;
    std::wstring prefix = L" " + std::to_wstring(num);
    if (prefix.size() < 4) prefix += L" ";
    return prefix + L"  " + icon.DisplayName();
}
} // end anonymous namespace

// ===== 持久全局鼠标钩子(事件驱动,不轮询) =====
HHOOK VerticalListPopup::s_mouseHook = nullptr;
VerticalListPopup* VerticalListPopup::s_activePopup = nullptr;
std::function<void()> VerticalListPopup::s_edgeCallback = nullptr;
DWORD VerticalListPopup::s_lastEdgeCheckMs = 0;

LRESULT CALLBACK VerticalListPopup::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        if (s_activePopup && s_activePopup->IsShowing()) {
            // --- 菜单展开时: 点击菜单外 → 折叠 ---
            if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
                wParam == WM_NCLBUTTONDOWN || wParam == WM_NCRBUTTONDOWN) {
                HWND h = s_activePopup->Hwnd();
                if (h) {
                    RECT rc; GetWindowRect(h, &rc);
                    if (!PtInRect(&rc, ms->pt)) {
                        s_activePopup->Hide();
                    }
                }
            }
        } else if (s_edgeCallback && s_activePopup) {
            // --- 菜单折叠时: 鼠标移到屏幕右边缘 → 触发展开 ---
            if (wParam == WM_MOUSEMOVE) {
                // 节流: 每 50ms 最多检查一次
                DWORD now = GetTickCount();
                if (now - s_lastEdgeCheckMs < kEdgeThrottleMs)
                    return CallNextHookEx(nullptr, nCode, wParam, lParam);
                s_lastEdgeCheckMs = now;

                // 边缘触发判断
                RECT target = s_activePopup->GetTargetRect();
                bool atRightEdge = (ms->pt.x >= target.right - kEdgeTriggerMargin);
                bool inYRange = (ms->pt.y >= target.top && ms->pt.y <= target.bottom);

                if (atRightEdge && inYRange) {
                    s_edgeCallback();  // 触发展开
                }
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void VerticalListPopup::EnableEdgeDetection(VerticalListPopup* popup,
                                            std::function<void()> onEdgeTrigger) {
    s_activePopup = popup;
    s_edgeCallback = std::move(onEdgeTrigger);
    if (s_mouseHook) return;  // 钩子已安装,仅更新指针和回调(支持弹窗重建)
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc,
                                    GetModuleHandleW(nullptr), 0);
    TraceLog("POPUP", s_mouseHook ? "Edge detection hook installed"
                                  : "Edge detection hook FAILED");
}

void VerticalListPopup::DisableEdgeDetection() {
    if (s_mouseHook) {
        UnhookWindowsHookEx(s_mouseHook);
        s_mouseHook = nullptr;
    }
    s_activePopup = nullptr;
    s_edgeCallback = nullptr;
    TraceLog("POPUP", "Edge detection hook removed");
}

VerticalListPopup::VerticalListPopup(const AppSettings& s) : settings_(s) {
    bgBrush_ = MakeBrush(C_BG);
    secondaryBrush_ = MakeBrush(C_BG2);
    accentBrush_ = MakeBrush(C_ACCENT);
    hoverBrush_ = MakeBrush(C_HOVER);
    selectedBrush_ = MakeBrush(C_SELECTED);
    float dpiScale = GetSystemDpiScale();
    int dpi = static_cast<int>(96.0f * dpiScale);
    font_ = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

VerticalListPopup::~VerticalListPopup() {
    *alive_ = false;
    // 如果是当前活跃弹窗,清除指针(但不卸载钩子,留给新实例)
    if (s_activePopup == this) {
        s_activePopup = nullptr;
    }
    if (hwnd_) DestroyWindow(hwnd_);
    if (bgBrush_) DeleteObject(bgBrush_);
    if (secondaryBrush_) DeleteObject(secondaryBrush_);
    if (accentBrush_) DeleteObject(accentBrush_);
    if (hoverBrush_) DeleteObject(hoverBrush_);
    if (selectedBrush_) DeleteObject(selectedBrush_);
    if (font_) DeleteObject(font_);
}

static bool g_popupClassRegistered = false;

void VerticalListPopup::RegisterPopupClass() {
    if (g_popupClassRegistered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc   = &VerticalListPopup::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"TVLPopup";
    if (!RegisterClassW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    }
    g_popupClassRegistered = true;
}

bool VerticalListPopup::Create(HWND owner) {
    RegisterPopupClass();
    float s = GetSystemDpiScale();
    int w = ScaleDpi(s, settings_.appearance.windowWidth);
    int h = ScaleDpi(s, 400);
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    DWORD style = WS_POPUP;  // 无边框，由 WM_ERASEBKGND 自绘
    hwnd_ = CreateWindowExW(exStyle, L"TVLPopup", L"系统托盘图标", style,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    CreateChildControls();
    return true;
}

void VerticalListPopup::CreateChildControls() {
    float s = GetSystemDpiScale();
    int w = ScaleDpi(s, settings_.appearance.windowWidth);
    int h = ScaleDpi(s, 400);
    auto S = [s](int v) { return ScaleDpi(s, v); };

    // 标题栏背景
    hwndHeaderBg_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, w, S(32), hwnd_, (HMENU)105, nullptr, nullptr);

    // 标题文字
    hwndHeader_ = CreateWindowExW(0, L"STATIC", L"系统托盘图标",
        WS_CHILD | WS_VISIBLE | SS_LEFT, S(12), S(8), w - S(60), S(20), hwnd_, (HMENU)100, nullptr, nullptr);

    // 刷新按钮
    hwndRefresh_ = CreateWindowExW(0, L"BUTTON", L"刷新",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, w - S(48), S(4), S(40), S(24), hwnd_, (HMENU)101, nullptr, nullptr);

    // 搜索框
    int searchY = S(36);
    hwndSearch_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, S(8), searchY, w - S(16), S(24), hwnd_, (HMENU)102, nullptr, nullptr);
    if (!settings_.appearance.showSearchBox) ShowWindow(hwndSearch_, SW_HIDE);

    // === 自绘 ListBox，支持悬停高亮 ===
    int listY = settings_.appearance.showSearchBox ? S(68) : S(36);
    int statusY = h - S(22);
    hwndList_ = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, listY, w, statusY - listY, hwnd_, (HMENU)103, nullptr, nullptr);

    // 状态栏
    hwndStatus_ = CreateWindowExW(0, L"STATIC", L"共 0 个图标",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(8), statusY, w - S(16), S(20), hwnd_, (HMENU)104, nullptr, nullptr);

    // 子类化 ListBox
    SetWindowLongPtrW(hwndList_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    listDefProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwndList_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&VerticalListPopup::SubProcStatic)));

    // 应用字体
    for (HWND c : {hwndHeaderBg_, hwndHeader_, hwndRefresh_, hwndSearch_, hwndList_, hwndStatus_})
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

LRESULT CALLBACK VerticalListPopup::WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
    VerticalListPopup* self = reinterpret_cast<VerticalListPopup*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) {
        if (m == WM_CREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            self = reinterpret_cast<VerticalListPopup*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else return DefWindowProcW(h, m, w, l);
    }
    return self->WndProc(m, w, l);
}

LRESULT CALLBACK VerticalListPopup::SubProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
    VerticalListPopup* self = reinterpret_cast<VerticalListPopup*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) {
        if (m == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
        if (m == WM_KEYDOWN) {
            // 阻止上下键和回车的默认导航行为
            if (w == VK_UP || w == VK_DOWN || w == VK_RETURN) return 0;
            if (w == VK_ESCAPE || (w >= '0' && w <= '9')) {
                return self->WndProc(m, w, l);
            }
        }
        if (m == WM_MOUSEMOVE) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            self->TrackHoverItem(pt);
            // 首次鼠标移动时请求 WM_MOUSELEAVE 通知
            if (!self->trackMouseLeave_) {
                self->trackMouseLeave_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        if (m == WM_MOUSELEAVE) {
            self->hoveredIdx_ = -1;
            self->trackMouseLeave_ = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (m == WM_LBUTTONUP || m == WM_RBUTTONUP) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            self->OnListItemClick(m == WM_RBUTTONUP, pt);
            return 0;
        }
    }
    return CallWindowProcW(self ? self->listDefProc_ : DefWindowProcW, h, m, w, l);
}

LRESULT VerticalListPopup::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        if (ctrl == hwndHeaderBg_) {
            SetBkColor(dc, C_BG2);
            return reinterpret_cast<LRESULT>(secondaryBrush_);
        }
        if (ctrl == hwndStatus_) {
            SetTextColor(dc, C_FG2);
            SetBkColor(dc, C_BG2);
            return reinterpret_cast<LRESULT>(secondaryBrush_);
        }
        if (ctrl == hwndHeader_) {
            SetTextColor(dc, C_FG);
            SetBkColor(dc, C_BG2);
            return reinterpret_cast<LRESULT>(secondaryBrush_);
        }
        SetTextColor(dc, C_FG2);
        SetBkColor(dc, C_BG);
        return reinterpret_cast<LRESULT>(bgBrush_);
    }
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_FG);
        SetBkColor(dc, C_BG2);
        return reinterpret_cast<LRESULT>(secondaryBrush_);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_FG); SetBkColor(dc, C_BG3);
        return reinterpret_cast<LRESULT>(secondaryBrush_);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_FG);
        SetBkColor(dc, C_BG);
        return reinterpret_cast<LRESULT>(bgBrush_);
    }
    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc; GetClientRect(hwnd_, &rc);
        FillRect(dc, &rc, bgBrush_);
        // 底部状态栏分隔线
        RECT line = rc;
        line.top = line.bottom - ScaleDpi(GetDpiScaleForWindow(hwnd_), 24);
        HBRUSH lineBr = CreateSolidBrush(C_BG2);
        FillRect(dc, &line, lineBr);
        DeleteObject(lineBr);
        // 边框
        SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, C_BORDER);
        Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        return 1;
    }
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (di->CtlID == 103) DrawListItem(di);
        return TRUE;
    }
    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mi->CtlID == 103) {
            float s = GetDpiScaleForWindow(hwnd_);
            mi->itemHeight = ScaleDpi(s, settings_.appearance.itemHeight);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == 101) { // 刷新
            if (reenumHandler_) {
                std::vector<TrayIconInfo> fresh = reenumHandler_();
                UpdateIconList(fresh);
            }
        } else if (id == 102 && HIWORD(wParam) == EN_CHANGE) {
            wchar_t buf[256] = {};
            GetWindowTextW(hwndSearch_, buf, 256);
            FilterIcons(buf);
        } else if (id == 103 && HIWORD(wParam) == LBN_DBLCLK) {
            ActivateSelected();
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) { Hide(); return 0; }
        if ((wParam >= '0' && wParam <= '9')) {
            SHORT ctrl = GetKeyState(VK_CONTROL), alt = GetKeyState(VK_MENU),
                  win = GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN);
            if (!(ctrl & 0x8000) && !(alt & 0x8000) && !(win & 0x8000)) {
                ActivateByNumber(static_cast<int>(wParam - '0'));
                return 0;
            }
        }
        break;
    }
    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) Hide();
        return 0;
    }
    case WM_REFRESH_DONE: {
        auto* icons = reinterpret_cast<std::vector<TrayIconInfo>*>(lParam);
        if (icons) { UpdateIconList(*icons); delete icons; }
        return 0;
    }
    case WM_REAPPEAR: {
        if (!showing_ && !settings_.behavior.closeOnClick) {
            Show(allIcons_);
        }
        return 0;
    }
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void VerticalListPopup::OnListItemClick(bool rightClick, POINT pt) {
    // 直接从鼠标 y 坐标计算点击的是第几项（不依赖 LB_ITEMFROMPOINT 或 LB_GETCURSEL）
    int itemH = (int)SendMessageW(hwndList_, LB_GETITEMHEIGHT, 0, 0);
    if (itemH <= 0) itemH = 20;
    int topIdx = (int)SendMessageW(hwndList_, LB_GETTOPINDEX, 0, 0);
    int sel = topIdx + pt.y / itemH;
    if (sel < 0 || sel >= (int)filtered_.size()) return;
    TrayIconInfo icon = filtered_[sel];
    bool wasShowing = showing_;
    if (wasShowing) Hide();
    auto alive = alive_; auto reenum = reenumHandler_; auto click = clickHandler_; HWND h = hwnd_;
    std::thread([icon, rightClick, alive, reenum, click, h]() {
        TrayIconInfo resolved = icon;
        if (!resolved.hasValidBounds && reenum) {
            auto fresh = reenum();
            for (auto& f : fresh) {
                if (f.DisplayName() == resolved.DisplayName() && f.hasValidBounds) { resolved = f; break; }
            }
        }
        if (click) click(resolved, rightClick);
        if (*alive) PostMessageW(h, WM_REAPPEAR, 0, 0);
    }).detach();
    SetTimer(hwnd_, 1, 150, [](HWND h2, UINT, UINT_PTR, DWORD) {
        KillTimer(h2, 1);
        PostMessageW(h2, WM_REAPPEAR, 0, 0);
    });
}

void VerticalListPopup::ActivateSelected() {
    int sel = (int)SendMessageW(hwndList_, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)filtered_.size()) return;
    TrayIconInfo icon = filtered_[sel];
    bool wasShowing = showing_;
    if (wasShowing) Hide();
    auto alive = alive_; auto reenum = reenumHandler_; auto click = clickHandler_; HWND h = hwnd_;
    std::thread([icon, alive, reenum, click, h]() {
        TrayIconInfo resolved = icon;
        if (!resolved.hasValidBounds && reenum) {
            auto fresh = reenum();
            for (auto& f : fresh) if (f.DisplayName() == resolved.DisplayName() && f.hasValidBounds) { resolved = f; break; }
        }
        if (click) click(resolved, false);
        if (*alive) PostMessageW(h, WM_REAPPEAR, 0, 0);
    }).detach();
    SetTimer(hwnd_, 1, 150, [](HWND h2, UINT, UINT_PTR, DWORD) {
        KillTimer(h2, 1); PostMessageW(h2, WM_REAPPEAR, 0, 0);
    });
}

void VerticalListPopup::ActivateByNumber(int digit) {
    int index = (digit == 0) ? 9 : digit - 1;
    if (index < 0 || index >= (int)filtered_.size()) return;
    TrayIconInfo icon = filtered_[index];
    bool wasShowing = showing_;
    if (wasShowing) Hide();
    auto alive = alive_; auto reenum = reenumHandler_; auto click = clickHandler_; HWND h = hwnd_;
    std::thread([icon, alive, reenum, click, h]() {
        TrayIconInfo resolved = icon;
        if (!resolved.hasValidBounds && reenum) {
            auto fresh = reenum();
            for (auto& f : fresh) if (f.DisplayName() == resolved.DisplayName() && f.hasValidBounds) { resolved = f; break; }
        }
        if (click) click(resolved, false);
        if (*alive) PostMessageW(h, WM_REAPPEAR, 0, 0);
    }).detach();
    SetTimer(hwnd_, 1, 150, [](HWND h2, UINT, UINT_PTR, DWORD) {
        KillTimer(h2, 1); PostMessageW(h2, WM_REAPPEAR, 0, 0);
    });
}

void VerticalListPopup::DrawListItem(LPDRAWITEMSTRUCT di) {
    int idx = static_cast<int>(di->itemID);
    if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return;

    RECT rc = di->rcItem;
    HDC dc = di->hDC;

    // 确定背景色（仅区分普通/悬停，不显示选中状态）
    HBRUSH bgBrush = (idx == hoveredIdx_) ? hoverBrush_ : bgBrush_;

    // 填充背景
    FillRect(dc, &rc, bgBrush);

    // 绘制文本
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_FG);
    SelectObject(dc, font_);

    std::wstring text = FormatItemText(idx, filtered_[idx]);
    RECT textRc = rc;
    textRc.left += 4;
    DrawTextW(dc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void VerticalListPopup::TrackHoverItem(POINT pt) {
    // 通过 y 坐标和项高度手动计算悬停项索引（避免 LB_ITEMFROMPOINT 在自绘模式下的兼容问题）
    float s = GetDpiScaleForWindow(hwnd_);
    int itemH = ScaleDpi(s, settings_.appearance.itemHeight);
    if (itemH <= 0) itemH = settings_.appearance.itemHeight;
    int topIdx = static_cast<int>(SendMessageW(hwndList_, LB_GETTOPINDEX, 0, 0));
    int idx = topIdx + pt.y / itemH;
    if (idx < 0 || idx >= static_cast<int>(filtered_.size())) idx = -1;

    if (idx != hoveredIdx_) {
        hoveredIdx_ = idx;
        InvalidateRect(hwndList_, nullptr, FALSE);
    }
}

void VerticalListPopup::UpdateIconList(const std::vector<TrayIconInfo>& icons) {
    allIcons_ = icons;
    filtered_ = icons;
    RefreshList();
}

void VerticalListPopup::RefreshList() {
    hoveredIdx_ = -1;
    SendMessageW(hwndList_, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < filtered_.size(); ++i) {
        std::wstring text = FormatItemText(static_cast<int>(i), filtered_[i]);
        SendMessageW(hwndList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    // 不设置光标选中项，由鼠标悬停高亮代替
    std::wstring status = L"共 " + std::to_wstring(filtered_.size()) + L" 个图标";
    SetWindowTextW(hwndStatus_, status.c_str());
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void VerticalListPopup::FilterIcons(const std::wstring& text) {
    filtered_.clear();
    if (text.empty()) {
        filtered_ = allIcons_;
    } else {
        for (auto& ic : allIcons_) {
            if (ContainsI(ic.DisplayName(), text) || ContainsI(ic.processName, text))
                filtered_.push_back(ic);
        }
    }
    RefreshList();
    std::wstring status = L"找到 " + std::to_wstring(filtered_.size()) + L" 个图标 (共 " +
        std::to_wstring(allIcons_.size()) + L" 个)";
    SetWindowTextW(hwndStatus_, status.c_str());
}

void VerticalListPopup::PositionWindow() {
    float s = GetDpiScaleForWindow(hwnd_);
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT mon = work;
    HMONITOR hm = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(hm, &mi)) {
        mon = mi.rcWork;
        // 右侧使用显示器实际右边界（贴屏幕边缘）
        mon.right = mi.rcMonitor.right;
    }
    int w = ScaleDpi(s, settings_.appearance.windowWidth);
    int h = ScaleDpi(s, 400);
    int x = mon.right - w;
    int y = mon.bottom - h - ScaleDpi(s, 4);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOZORDER);
}

void VerticalListPopup::Show(const std::vector<TrayIconInfo>& icons) {
    if (showing_) { Hide(); return; }
    UpdateIconList(icons);
    PositionWindow();
    showing_ = true;
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(hwndList_);
    // 更新活跃弹窗指针(钩子已在 EnableEdgeDetection 中持久安装)
    s_activePopup = this;
}

RECT VerticalListPopup::GetTargetRect() const {
    RECT rc{};
    POINT pt; GetCursorPos(&pt);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(hm, &mi)) {
        // 回退:使用主显示器工作区
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
        return rc;
    }
    float s = GetSystemDpiScale();
    int w = ScaleDpi(s, settings_.appearance.windowWidth);
    int h = ScaleDpi(s, 400);
    rc.left   = mi.rcMonitor.right - w;
    rc.top    = mi.rcMonitor.bottom - h - ScaleDpi(s, 4);
    rc.right  = mi.rcMonitor.right;
    rc.bottom = rc.top + h;
    return rc;
}

void VerticalListPopup::Hide() {
    if (!showing_) return;
    showing_ = false;
    // 不卸载钩子 — 持久保留用于边缘检测
    ShowWindow(hwnd_, SW_HIDE);
}

} // namespace tvl
