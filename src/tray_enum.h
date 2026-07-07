#pragma once
#include "common.h"

namespace tvl {

// 托盘图标信息模型(对齐 C# TrayIconInfo)
struct TrayIconInfo {
    int index = 0;
    std::wstring name;
    std::wstring tooltip;
    std::wstring processName;
    std::wstring executablePath;
    DWORD processId = 0;
    std::wstring automationId;
    std::wstring runtimeId;
    RECT bounds{};
    bool hasValidBounds = false;
    bool isSystemIcon = false;

    std::wstring DisplayName() const {
        if (!tooltip.empty()) return tooltip;
        if (!name.empty()) return name;
        if (!processName.empty()) return processName;
        return L"未知图标 #" + std::to_wstring(index + 1);
    }
};

// 枚举系统托盘全部图标(可见 + 溢出),内部含三段回退逻辑
// 必须在已 CoInitialize 的 STA 线程上调用
std::vector<TrayIconInfo> EnumerateTrayIcons();

// 打开/关闭溢出窗口(^ 按钮弹出的隐藏图标窗口)
bool OpenOverflowWindow();
void CloseOverflowWindow();

// COM 初始化辅助:在专用 STA 线程上运行回调
void RunOnSta(const std::function<void()>& fn);

// 在 STA 线程上枚举图标(线程安全入口)
std::vector<TrayIconInfo> EnumerateTrayIconsAsync();

} // namespace tvl
