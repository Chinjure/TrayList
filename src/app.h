#pragma once
#include "common.h"
#include "settings.h"
#include "tray_enum.h"
#include "click.h"
#include "hotkey.h"
#include "popup.h"
#include <atomic>
#include <shellapi.h>
#include <objbase.h>

namespace tvl {

// 应用核心:隐藏消息窗口 + 托盘图标 + 热键 + 监听 + 弹窗编排
// 菜单通过 WH_MOUSE_LL 全局钩子(事件驱动)响应屏幕右边缘触碰
class App {
public:
    App();
    ~App();

    int Run(); // 阻塞消息循环,直到退出

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateHiddenWindow();
    bool CreateTrayIcon();
    void ShowTrayMenu();
    void InitializeHotkeys();
    void ReinitializeHotkeys();
    void OnToggle();
    void OnNumber(int digit);
    void OpenSettings();
    void Exit();
    void StartMonitor();
    void StopMonitor();

    // 自定义消息
    static constexpr UINT WM_TOGGLE_MENU   = WM_APP + 2;
    static constexpr UINT WM_REINIT_HOTKEY = WM_APP + 3;
    static constexpr UINT WM_ENUM_DONE     = WM_APP + 20;
    static constexpr UINT WM_MONITOR_DONE  = WM_APP + 21;

    AppSettings settings_;
    HWND hwnd_ = nullptr;
    GlobalHotkeyService hotkeys_;
    VerticalListPopup* popup_ = nullptr;
    NOTIFYICONDATAW nid_{};
    UINT taskbarCreatedMsg_ = 0;
    int toggleHotkeyId_ = -1;
    std::vector<int> numberHotkeyIds_;
    std::atomic<bool> enumerating_{false};
    std::atomic<bool> monitorRun_{false};
    std::thread monitorThread_;
    HMENU trayMenu_ = nullptr;
};

} // namespace tvl
