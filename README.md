# TrayList (TrayVerticalList)

[English](#english) | [中文](#chinese)

---

<a id="english"></a>
## English

A lightweight, native Windows tray icon manager. Displays all system tray icons in a vertical popup list, accessible via global hotkey or by moving the mouse to the right edge of the screen. Click any icon directly from the list — no more hunting through the tiny system tray.

### Features

- **Tray Icon Enumeration** — Lists all system tray icons (visible area + overflow) using COM `IUIAutomation`, with fallback to `ToolbarWindow32` tree traversal for compatibility across Windows 10/11.
- **One-Click Activation** — Left/right click any tray icon directly from the popup via `SendInput` absolute coordinates, with UIA `Invoke` and `LegacyIAccessible` fallback.
- **Global Hotkeys** — `Win+Shift+T` toggles the popup; number keys `0-9` activate the first 10 items.
- **Edge Trigger (Event-Driven)** — Move the mouse to the right edge of the screen to automatically open the popup. Powered by a persistent `WH_MOUSE_LL` hook (50ms throttle, no polling).
- **Auto-Hide** — Clicking anywhere outside the popup dismisses it instantly.
- **Search / Filter** — Type in the search box to filter tray icons by name in real time.
- **Dark Theme** — Custom-drawn ListBox with a modern dark color palette.
- **Settings Window** — Configure hotkey, window dimensions, and other preferences via a built-in dark-themed settings dialog.
- **Single Instance** — Only one instance runs at a time (via named mutex).
- **Auto-Recovery** — Re-registers tray icon and hooks when `TaskbarCreated` message is received (e.g., after Explorer restart).
- **Tray Monitor** — Background thread polls for new/missing tray icons so the list stays up to date.
- **Portable** — Settings stored as JSON in `%LOCALAPPDATA%\TrayVerticalList\usersettings.json`. Diagnostic logs at `debug.log` and `crash.log` in the same directory.

### Screenshot

<p align="center">
  <img src="docs/screenshot.png" alt="TrayList Screenshot" width="480">
</p>

### Requirements

- **Windows 10** (version 1809+) or **Windows 11**
- **MSVC** (Visual Studio 2022 with C++ tools and Windows 11 SDK) or **clang-cl**

### Build

#### Option 1: One-Click Build (PowerShell)

```powershell
.\build_on_windows.ps1
```

This script auto-detects Visual Studio and CMake, then builds `Release` configuration.

#### Option 2: Manual Build

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release
```

The output executable is `build/Release/TrayVerticalList.exe`.

> **Note:** This is a native Windows application. It requires the Windows SDK and cannot be built on WSL/Linux.

### Usage

1. Run `TrayVerticalList.exe`. A tray icon appears in the system tray.
2. **Open the popup** by:
   - Pressing `Win+Shift+T` (default hotkey)
   - Moving the mouse to the **right edge** of the screen
   - Right-clicking the tray icon → "Show Menu"
3. Click any item to activate it (left-click or right-click).
4. Use the **search box** at the top of the popup to filter icons.
5. Right-click the tray icon for options: **Settings**, **Refresh**, **Exit**.

### Configuration

Settings are stored in `%LOCALAPPDATA%\TrayVerticalList\usersettings.json`:

| Key | Description | Default |
|-----|-------------|---------|
| `toggleModifiers` | Hotkey modifiers (`"MOD_WIN \| MOD_SHIFT"`) | `MOD_WIN \| MOD_SHIFT` |
| `toggleVk` | Hotkey virtual-key code | `'T'` |
| `windowWidth` | Popup width in pixels | `320` |
| `maxVisibleItems` | Max items before scrolling | `20` |
| `itemHeight` | Item row height in pixels | `36` |

### Architecture

```
src/
  main.cpp             — wWinMain entry point
  app.{h,cpp}          — Hidden message window, tray icon, hotkeys, monitor, popup orchestration
  popup.{h,cpp}        — Vertical list popup (custom-drawn ListBox, search, edge detection)
  tray_enum.{h,cpp}    — COM IUIAutomation enumeration + STA thread runner
  click.{h,cpp}        — SendInput + UIA click abstraction
  hotkey.{h,cpp}       — RegisterHotKey wrapper
  settings.{h,cpp}     — Settings model + minimal JSON parser/writer
  settings_window.{h,cpp} — Settings dialog
  common.{h,cpp}       — Constants, logging, paths, string helpers, DPI scaling
  app.manifest         — comctl v6 / DPI awareness / Win10-11 compatibility
```

### License

MIT

---

<a id="chinese"></a>
## 中文

一款轻量级的 Windows 原生托盘图标管理工具。将所有系统托盘图标以垂直列表形式展示，可通过全局热键或将鼠标移到屏幕右边缘呼出。直接在列表中点击图标即可操作，不再需要在小巧的托盘区域逐个查找。

### 功能特性

- **托盘图标枚举** — 使用 COM `IUIAutomation` 列出所有系统托盘图标（可见区 + 溢出区），并支持 `ToolbarWindow32` 树遍历回退，兼容 Windows 10/11。
- **一键点击** — 从弹窗中直接左键/右键点击托盘图标，底层使用 `SendInput` 绝对坐标发送点击，辅以 UIA `Invoke` 和 `LegacyIAccessible` 回退。
- **全局热键** — `Win+Shift+T` 切换弹窗显示/隐藏；数字键 `0-9` 快速激活前 10 个图标。
- **屏幕边缘触发（事件驱动）** — 将鼠标移到屏幕右边缘即可自动弹出菜单。基于持久 `WH_MOUSE_LL` 全局钩子实现，50ms 节流，无需轮询。
- **失焦自动关闭** — 点击弹窗外的任意位置立即折叠。
- **搜索/筛选** — 在搜索框中输入关键字即可实时过滤托盘图标。
- **深色主题** — 自绘 ListBox，采用现代深色配色方案。
- **设置窗口** — 内置深色设置对话框，可配置热键、窗口尺寸等偏好。
- **单实例运行** — 通过命名 Mutex 确保同一时间只有一个实例。
- **自动恢复** — 收到 `TaskbarCreated` 消息时自动重新注册托盘图标和钩子（如 Explorer 重启后）。
- **托盘监听** — 后台线程定时轮询，检测新增/移除的托盘图标，保持列表最新。
- **便携化配置** — 设置以 JSON 格式存储在 `%LOCALAPPDATA%\TrayVerticalList\usersettings.json`。诊断日志 `debug.log` 和 `crash.log` 位于同一目录下。

### 截图

<p align="center">
  <img src="docs/screenshot.png" alt="TrayList 截图" width="480">
</p>

### 环境要求

- **Windows 10** (1809 及以上) 或 **Windows 11**
- **MSVC** (Visual Studio 2022，含 C++ 工具和 Windows 11 SDK) 或 **clang-cl**

### 编译

#### 方式一：一键编译（PowerShell）

```powershell
.\build_on_windows.ps1
```

脚本会自动检测 Visual Studio 和 CMake，然后编译 `Release` 配置。

#### 方式二：手动编译

```powershell
# 配置
cmake -B build -G "Visual Studio 17 2022" -A x64

# 编译
cmake --build build --config Release
```

编译产物为 `build/Release/TrayVerticalList.exe`。

> **注意：** 这是 Windows 原生应用，需要 Windows SDK，无法在 WSL/Linux 下编译。

### 使用方法

1. 运行 `TrayVerticalList.exe`，系统托盘区会出现程序图标。
2. **呼出弹窗** 的方式有：
   - 按下 `Win+Shift+T`（默认热键）
   - 将鼠标移到屏幕**右边缘**
   - 右键点击托盘图标 → "Show Menu"
3. 点击列表中的任意图标即可激活（支持左键和右键点击）。
4. 使用弹窗顶部的**搜索框**可以快速过滤图标。
5. 右键点击托盘图标可访问：**Settings**（设置）、**Refresh**（刷新）、**Exit**（退出）。

### 配置说明

设置保存在 `%LOCALAPPDATA%\TrayVerticalList\usersettings.json`：

| 键 | 说明 | 默认值 |
|-----|------|--------|
| `toggleModifiers` | 热键修饰键 (`"MOD_WIN \| MOD_SHIFT"`) | `MOD_WIN \| MOD_SHIFT` |
| `toggleVk` | 热键虚拟键码 | `'T'` |
| `windowWidth` | 弹窗宽度（像素） | `320` |
| `maxVisibleItems` | 最多可见条目数（超出则滚动） | `20` |
| `itemHeight` | 条目行高（像素） | `36` |

### 架构概览

```
src/
  main.cpp             — wWinMain 入口
  app.{h,cpp}          — 隐藏消息窗口、托盘图标、热键/监听/弹窗编排
  popup.{h,cpp}        — 垂直列表弹窗（自绘 ListBox、搜索、边缘检测）
  tray_enum.{h,cpp}    — COM IUIAutomation 枚举 + STA 线程运行器
  click.{h,cpp}        — SendInput + UIA 点击抽象
  hotkey.{h,cpp}       — RegisterHotKey 封装
  settings.{h,cpp}     — 配置模型 + 极简 JSON 读写
  settings_window.{h,cpp} — 设置窗口
  common.{h,cpp}       — 常量、日志、路径、字符串辅助、DPI 缩放
  app.manifest         — comctl v6 / DPI 感知 / Win10-11 兼容
```

### 许可证

MIT
