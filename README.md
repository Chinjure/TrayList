# TrayVerticalList — C++ 原生版本

将 C# / WPF 版本重构为纯 Win32 + C++20 实现,功能对齐:
- COM `IUIAutomation` 枚举系统托盘图标(可见 + 溢出,含传统 `ToolbarWindow32` 与直接树遍历两段回退)
- `SendInput` 绝对坐标点击 + UIA `Invoke` / `LegacyIAccessible` 回退
- `RegisterHotKey` 全局热键(展开菜单 + 数字快捷键 0-9)
- `Shell_NotifyIcon` 托盘图标 + 右键菜单
- Win32 顶层弹窗(自绘 ListBox、深色主题、搜索、键盘导航、失焦关闭)
- Win32 设置窗口(深色主题)
- 单实例 Mutex、`TaskbarCreated` 重启恢复、托盘轮询监听线程
- 手写极简 JSON 读写 `%LOCALAPPDATA%\TrayVerticalList\usersettings.json`
- 诊断日志 `debug.log` / `crash.log`

## 目录结构

```
src/
  main.cpp            — wWinMain 入口
  app.{h,cpp}         — 隐藏消息窗口、托盘图标、热键/监听/弹窗编排
  tray_enum.{h,cpp}   — COM IUIAutomation 枚举 + STA 线程运行器
  click.{h,cpp}       — SendInput + UIA 点击
  hotkey.{h,cpp}      — RegisterHotKey 封装
  popup.{h,cpp}       — 垂直列表弹窗
  settings_window.{h,cpp} — 设置窗口
  settings.{h,cpp}    — 配置模型 + 极简 JSON
  common.{h,cpp}      — 常量、日志、路径、字符串辅助
  app.manifest        — comctl v6 / DPI 感知 / Win10-11 兼容
```

## 构建(Windows + MSVC)

需 Visual Studio 2022(含 C++ 与 Windows 11 SDK)。

```bat
cd native
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物:`build/Release/TrayVerticalList.exe`。

> 必须在 Windows 上用 MSVC(或 clang-cl)构建。WSL/Linux 无 Windows SDK,无法在此环境编译。已链接库:`user32 gdi32 comctl32 shell32 ole32 oleaut32 psapi uxtheme`。

## 与 C# 版的差异

- UI 由 WPF 改为纯 Win32/comctl32 + 自绘,视觉上模拟原深色主题。
- FlaUI UIA3 改为直接 COM `IUIAutomation`(`uiautomationclient.h`)。
- DI 容器 / 日志框架移除,改为直接实例化与文件日志。
- 弹窗为可激活窗口(非 `WS_EX_NOACTIVATE`),以支持键盘导航;失焦关闭仍保留 300ms grace。
