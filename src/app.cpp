#include "app.h"
#include "settings_window.h"
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")

namespace tvl {

namespace {
const wchar_t* kMutexName = L"TrayVerticalList_SingleInstance_Mutex";
HANDLE g_singleMutex = nullptr;
}

App::App() {
    settings_ = LoadSettings();
}

App::~App() {
    StopMonitor();
    if (popup_) delete popup_;
    if (trayMenu_) DestroyMenu(trayMenu_);
}

bool App::CreateHiddenWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &App::WndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TVLMainWindow";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, L"TVLMainWindow", L"TrayVerticalList", WS_OVERLAPPED,
                            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return hwnd_ != nullptr;
}

LRESULT CALLBACK App::WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self && m == WM_CREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->WndProc(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    // 托盘回调
    if (msg == WM_TRAYICON) {
        if (lParam == WM_LBUTTONUP) { OnToggle(); }
        else if (lParam == WM_RBUTTONUP) { ShowTrayMenu(); }
        return 0;
    }
    // TaskbarCreated
    if (msg == taskbarCreatedMsg_) {
        TraceLog("APP", "TaskbarCreated received, re-registering hotkeys");
        ReinitializeHotkeys();
        return 0;
    }
    if (msg == WM_HOTKEY_MSG) {
        hotkeys_.HandleWParam(wParam);
        return 0;
    }
    if (msg == WM_TOGGLE_MENU) { OnToggle(); return 0; }
    if (msg == WM_REINIT_HOTKEY) { ReinitializeHotkeys(); return 0; }
    if (msg == WM_ENUM_DONE) {
        auto* icons = reinterpret_cast<std::vector<TrayIconInfo>*>(lParam);
        if (icons) {
            if (popup_) popup_->Show(*icons);
            delete icons;
        }
        return 0;
    }
    if (msg == WM_MONITOR_DONE) {
        auto* icons = reinterpret_cast<std::vector<TrayIconInfo>*>(lParam);
        if (icons) {
            // 后台监听到的变化:弹窗显示时刷新,否则仅丢弃
            if (popup_ && popup_->IsShowing()) popup_->UpdateIconList(*icons);
            delete icons;
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case 1: OnToggle(); break;
        case 2: OpenSettings(); break;
        case 3: Exit(); break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

bool App::CreateTrayIcon() {
    trayMenu_ = CreatePopupMenu();
    AppendMenuW(trayMenu_, MF_STRING, 1, (std::wstring(L"📋 显示图标列表 (") + settings_.toggleMenu.Display() + L")").c_str());
    AppendMenuW(trayMenu_, MF_STRING, 2, L"⚙️ 设置...");
    AppendMenuW(trayMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(trayMenu_, MF_STRING, 3, L"❌ 退出");

    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    std::wstring tip = L"TrayVerticalList — 展开菜单: " + settings_.toggleMenu.Display();
    wcsncpy_s(nid_.szTip, _countof(nid_.szTip), tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid_);
    return true;
}

void App::ShowTrayMenu() {
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(hwnd_);
    // 不使用 TPM_RETURNCMD,让选中项经 WM_COMMAND 投递到 hwnd_
    TrackPopupMenu(trayMenu_, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   p.x, p.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0); // 确保 menu 正常关闭
}

void App::InitializeHotkeys() {
    hotkeys_.Initialize(hwnd_);
    toggleHotkeyId_ = hotkeys_.Register(settings_.toggleMenu, [this]() {
        PostMessageW(hwnd_, WM_TOGGLE_MENU, 0, 0);
    });
    numberHotkeyIds_.clear();
    numberHotkeyIds_ = hotkeys_.RegisterNumbers(settings_.numberPrefix, [this](int d) {
        OnNumber(d);
    });
}

void App::ReinitializeHotkeys() {
    hotkeys_.UnregisterAll();
    InitializeHotkeys();
}

void App::OnToggle() {
    if (enumerating_.exchange(true)) return;
    TraceLog("APP", "toggle: enumerating on STA thread...");
    std::thread([this]() {
        auto* icons = new std::vector<TrayIconInfo>(EnumerateTrayIconsAsync());
        PostMessageW(hwnd_, WM_ENUM_DONE, 0, reinterpret_cast<LPARAM>(icons));
        enumerating_ = false;
    }).detach();
}

void App::OnNumber(int digit) {
    if (enumerating_.exchange(true)) return;
    std::thread([this, digit]() {
        auto icons = EnumerateTrayIconsAsync();
        int index = (digit == 0) ? 9 : digit - 1;
        if (index < (int)icons.size()) {
            TrayIconInfo icon = icons[index];
            ClickIconAsync(icon, false);
        }
        enumerating_ = false;
    }).detach();
}

void App::OpenSettings() {
    SettingsWindow sw(settings_, [this](const AppSettings& s) {
        settings_ = s;
        SaveSettings(s);
        // 重新创建弹窗以应用外观,并重新注册热键
        if (popup_) { delete popup_; popup_ = nullptr; }
        popup_ = new VerticalListPopup(settings_);
        popup_->Create(hwnd_);
        popup_->SetClickHandler([this](const TrayIconInfo& ic, bool right) {
            std::thread([this, ic, right]() { ClickIconAsync(ic, right); }).detach();
        });
        popup_->SetReenumerateHandler([this]() { return EnumerateTrayIconsAsync(); });
        // 重新绑定边缘检测钩子到新弹窗实例
        VerticalListPopup::EnableEdgeDetection(popup_, [this]() {
            PostMessageW(hwnd_, WM_TOGGLE_MENU, 0, 0);
        });
        PostMessageW(hwnd_, WM_REINIT_HOTKEY, 0, 0);
    });
    sw.Show(hwnd_);
}

void App::Exit() {
    hotkeys_.UnregisterAll();
    StopMonitor();
    VerticalListPopup::DisableEdgeDetection();
    if (popup_) popup_->Hide();
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    if (nid_.hIcon) DestroyIcon(nid_.hIcon);
    PostQuitMessage(0);
}

void App::StartMonitor() {
    monitorRun_ = true;
    monitorThread_ = std::thread([this]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int ms = std::max(1, settings_.behavior.refreshIntervalSeconds) * 1000;
        while (monitorRun_) {
            for (int i = 0; i < ms / 100 && monitorRun_; ++i) Sleep(100);
            if (!monitorRun_) break;
            auto* icons = new std::vector<TrayIconInfo>(EnumerateTrayIcons());
            PostMessageW(hwnd_, WM_MONITOR_DONE, 0, reinterpret_cast<LPARAM>(icons));
        }
        CoUninitialize();
    });
}

void App::StopMonitor() {
    monitorRun_ = false;
    if (monitorThread_.joinable()) monitorThread_.join();
}

int App::Run() {
    // 单实例
    g_singleMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"TrayVerticalList 已经在运行中。\n请查看系统托盘中的图标。",
                    L"TrayVerticalList", MB_OK | MB_ICONINFORMATION);
        if (g_singleMutex) CloseHandle(g_singleMutex);
        return 0;
    }

    // COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 公共控件初始化
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_USEREX_CLASSES};
    InitCommonControlsEx(&icc);

    if (!CreateHiddenWindow()) return 1;

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    // 弹窗
    popup_ = new VerticalListPopup(settings_);
    popup_->Create(hwnd_);
    popup_->SetClickHandler([this](const TrayIconInfo& ic, bool right) {
        std::thread([this, ic, right]() { ClickIconAsync(ic, right); }).detach();
    });
    popup_->SetReenumerateHandler([this]() { return EnumerateTrayIconsAsync(); });

    // 安装全局 WH_MOUSE_LL 钩子(事件驱动,不轮询):
    // 鼠标移到屏幕右边缘 → 自动展开菜单
    VerticalListPopup::EnableEdgeDetection(popup_, [this]() {
        PostMessageW(hwnd_, WM_TOGGLE_MENU, 0, 0);
    });

    CreateTrayIcon();
    InitializeHotkeys();

    TraceLog("APP", "=== TrayVerticalList started (event-driven edge detection) ===");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    if (g_singleMutex) { ReleaseMutex(g_singleMutex); CloseHandle(g_singleMutex); }
    return 0;
}

} // namespace tvl
