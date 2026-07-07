#pragma once
#include "common.h"
#include "settings.h"
#include <functional>

namespace tvl {

// 设置窗口 — Win32 模态对话框(程序化构建控件,深色主题)
class SettingsWindow {
public:
    SettingsWindow(AppSettings& settings, std::function<void(const AppSettings&)> onSave)
        : settings_(settings), onSave_(std::move(onSave)) {}

    // 显示模态窗口
    void Show(HWND parent);

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void RegisterWindowClass();
    void BuildControls();
    void ApplyDarkTheme();
    void LoadFromSettings();
    AppSettings CollectFromUi();
    void UpdateDisplays();

    AppSettings& settings_;
    std::function<void(const AppSettings&)> onSave_;

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH bg2Brush_ = nullptr;

    // 控件句柄
    HWND cbCtrl_ = nullptr, cbAlt_ = nullptr, cbShift_ = nullptr, cbWin_ = nullptr;
    HWND cbToggleKey_ = nullptr;
    HWND stToggleDisplay_ = nullptr;
    HWND cbNCtrl_ = nullptr, cbNAlt_ = nullptr, cbNShift_ = nullptr, cbNWin_ = nullptr;
    HWND stNumberDisplay_ = nullptr;
    HWND cbTheme_ = nullptr;
    HWND edWidth_ = nullptr, edMaxItems_ = nullptr, edItemHeight_ = nullptr;
    HWND cbShowSearch_ = nullptr, cbShowHints_ = nullptr;
    HWND cbAutoStart_ = nullptr, cbCloseOnClick_ = nullptr, cbCloseOnFocusLost_ = nullptr;
    HWND edRefresh_ = nullptr;
    HWND btnSave_ = nullptr, btnCancel_ = nullptr;
};

} // namespace tvl
