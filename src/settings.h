#pragma once
#include "common.h"

namespace tvl {

// 修饰键组合 (Win32 MOD_* 位标志)
struct ModifierSet {
    bool ctrl = false;
    bool alt  = false;
    bool shift = false;
    bool win  = false;

    UINT Flags() const;                       // 转 MOD_* 位标志,含 MOD_NOREPEAT
    std::wstring Display() const;             // "Ctrl+Shift"
    static ModifierSet FromFlags(UINT mod);   // 反向解析(用于加载/默认)
};

struct HotkeyConfig {
    ModifierSet modifiers;     // 修饰键
    UINT key = 0;              // 虚拟键码 (VK_*)
    std::wstring description;
    bool enabled = true;

    std::wstring Display() const;             // "Ctrl+Shift+Space"
    static std::wstring VkToName(UINT vk);
    static UINT NameToVk(const std::wstring& name);
};

struct AppearanceSettings {
    std::wstring theme = L"Dark";
    int maxVisibleItems = 20;
    int itemHeight = 40;
    int windowWidth = 280;
    bool showSearchBox = true;
    bool showNumberHints = true;
};

struct BehaviorSettings {
    bool autoStart = false;
    bool closeOnClick = true;
    bool closeOnFocusLost = true;
    int  refreshIntervalSeconds = 5;
};

struct AppSettings {
    HotkeyConfig toggleMenu;
    HotkeyConfig numberPrefix;
    AppearanceSettings appearance;
    BehaviorSettings behavior;

    static AppSettings Default();
};

AppSettings LoadSettings();      // 读取 usersettings.json,失败返回默认
bool SaveSettings(const AppSettings& s);

} // namespace tvl
