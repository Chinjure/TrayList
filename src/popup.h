#pragma once
#include "common.h"
#include "tray_enum.h"
#include "settings.h"
#include <functional>
#include <memory>

namespace tvl {

// 垂直列表弹窗 — Win32 顶层窗口 + 标准 ListBox
class VerticalListPopup {
public:
    explicit VerticalListPopup(const AppSettings& settings);
    ~VerticalListPopup();

    // 创建窗口(不显示)
    bool Create(HWND owner);
    HWND Hwnd() const { return hwnd_; }

    // 显示弹窗
    void Show(const std::vector<TrayIconInfo>& icons);
    void Hide();
    bool IsShowing() const { return showing_; }

    // 更新图标列表(不显示)
    void UpdateIconList(const std::vector<TrayIconInfo>& icons);

    // 数字键激活(0->第10项即索引9,1-9->索引0-8)
    void ActivateByNumber(int digit);

    // 设置点击回调(在后台线程触发,内部已 detach)
    void SetClickHandler(std::function<void(const TrayIconInfo&, bool rightClick)> h) {
        clickHandler_ = std::move(h);
    }
    // 设置弹窗需要重新枚举时的回调
    void SetReenumerateHandler(std::function<std::vector<TrayIconInfo>()> h) {
        reenumHandler_ = std::move(h);
    }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SubProcStatic(HWND, UINT, WPARAM, LPARAM);

    void RegisterPopupClass();
    void CreateChildControls();
    void PositionWindow();
    void FilterIcons(const std::wstring& text);
    void RefreshList();
    void ActivateSelected();
    void OnListItemClick(bool rightClick, POINT pt);
    void DrawListItem(LPDRAWITEMSTRUCT di);
    void TrackHoverItem(POINT pt);

    const AppSettings& settings_;
    HWND hwnd_ = nullptr;
    HWND hwndHeaderBg_ = nullptr;   // 标题栏背景
    HWND hwndHeader_ = nullptr;     // 标题文字
    HWND hwndRefresh_ = nullptr;    // 刷新按钮
    HWND hwndSearch_ = nullptr;     // 搜索框
    HWND hwndList_ = nullptr;       // 标准 ListBox
    HWND hwndStatus_ = nullptr;     // 状态栏

    HFONT font_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH secondaryBrush_ = nullptr;
    HBRUSH accentBrush_ = nullptr;  // 选中/快捷键色
    HBRUSH hoverBrush_ = nullptr;   // 悬停高亮
    HBRUSH selectedBrush_ = nullptr;// 选中高亮

    std::vector<TrayIconInfo> allIcons_;
    std::vector<TrayIconInfo> filtered_;
    bool showing_ = false;
    DWORD lastShowTime_ = 0;
    int hoveredIdx_ = -1;           // 当前鼠标悬停的项索引
    bool trackMouseLeave_ = false;  // 是否已请求 WM_MOUSELEAVE

    std::function<void(const TrayIconInfo&, bool)> clickHandler_;
    std::function<std::vector<TrayIconInfo>()> reenumHandler_;

    WNDPROC listDefProc_ = nullptr;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace tvl
