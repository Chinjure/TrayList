#pragma once

// 共享头文件:常量、日志、路径与字符串辅助
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>

namespace tvl {

// ===== 窗口类名常量 (与 C# NativeMethods 对齐) =====
constexpr const wchar_t* kShellTrayWnd       = L"Shell_TrayWnd";
constexpr const wchar_t* kTrayNotifyWnd      = L"TrayNotifyWnd";
constexpr const wchar_t* kOverflowXamlIsland = L"TopLevelWindowForOverflowXamlIsland";
constexpr const wchar_t* kDesktopContentBridge = L"Windows.UI.Composition.DesktopWindowContentBridge";
constexpr const wchar_t* kToolbarWindow32    = L"ToolbarWindow32";
constexpr const wchar_t* kSysPager           = L"SysPager";
constexpr const wchar_t* kNotifyIconOverflowWindow = L"NotifyIconOverflowWindow";

// ===== 窗口消息常量 =====
constexpr UINT WM_HOTKEY_MSG    = 0x0312;
constexpr UINT WM_TRAYICON      = WM_APP + 1;   // 托盘回调
constexpr UINT WM_TOGGLE_MENU   = WM_APP + 2;   // 主窗口 -> 自定义
constexpr UINT WM_REINIT_HOTKEY = WM_APP + 3;

// ===== 路径辅助 =====
// 返回 %LOCALAPPDATA%\TrayVerticalList,必要时创建
std::wstring AppDataDir();
std::wstring DebugLogPath();
std::wstring UserSettingsPath();
std::wstring CrashLogPath();

// ===== 日志 =====
// 写入 debug.log,带时间戳与线程 id。前缀区分模块。
void TraceLog(const std::string& tag, const std::string& msg);
void TraceLogW(const std::string& tag, const std::wstring& msg);

// 致命错误写入 crash.log
void WriteCrashLog(const std::string& msg);

// ===== 字符串辅助 =====
std::wstring ToLower(std::wstring_view s);
bool ContainsI(std::wstring_view haystack, std::wstring_view needle);
std::string  WideToUtf8(std::wstring_view s);
std::wstring Utf8ToWide(std::string_view s);
std::wstring IntToWString(long long v);

// 进程名(不含路径、不含扩展名)由 PID 获取,失败返回空
std::wstring ProcessNameFromPid(DWORD pid);
// 进程可执行文件完整路径
std::wstring ProcessPathFromPid(DWORD pid);

// 去除两端空白
std::wstring TrimW(std::wstring_view s);

// ===== DPI 缩放辅助 =====
// 获取系统 DPI 缩放比例 (dpi / 96)
float GetSystemDpiScale();
// 获取指定窗口所在显示器的 DPI 缩放比例
float GetDpiScaleForWindow(HWND hwnd);
// 按 DPI 比例缩放数值,四舍五入到整数
int ScaleDpi(float scale, int value);

} // namespace tvl
