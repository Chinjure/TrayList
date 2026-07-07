#include "common.h"
#include <shlobj.h>
#include <psapi.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "psapi.lib")

namespace tvl {

std::wstring AppDataDir() {
    wchar_t* local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        std::wstring dir = std::wstring(local) + L"\\TrayVerticalList";
        CoTaskMemFree(local);
        CreateDirectoryW(dir.c_str(), nullptr); // 忽略已存在
        return dir;
    }
    return L".";
}

std::wstring DebugLogPath()      { return AppDataDir() + L"\\debug.log"; }
std::wstring UserSettingsPath()  { return AppDataDir() + L"\\usersettings.json"; }
std::wstring CrashLogPath()      { return AppDataDir() + L"\\crash.log"; }

static std::mutex g_logMutex;

static std::string NowStamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t  = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%H:%M:%S") << '.'
       << std::setfill('0') << std::setw(3) << ms.count();
    return os.str();
}

void TraceLog(const std::string& tag, const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    try {
        std::string entry = NowStamp() + " [T" +
            std::to_string(static_cast<unsigned long>(GetCurrentThreadId())) +
            "] " + tag + " — " + msg + "\n";
        HANDLE h = CreateFileW(DebugLogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, entry.data(), static_cast<DWORD>(entry.size()), &written, nullptr);
            CloseHandle(h);
        }
    } catch (...) { /* 日志绝不能成为崩溃源 */ }
}

void TraceLogW(const std::string& tag, const std::wstring& msg) {
    TraceLog(tag, WideToUtf8(msg));
}

void WriteCrashLog(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    try {
        std::string entry = "=== Crash " + NowStamp() + " ===\n" + msg + "\n";
        HANDLE h = CreateFileW(CrashLogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, entry.data(), static_cast<DWORD>(entry.size()), &written, nullptr);
            CloseHandle(h);
        }
    } catch (...) {}
}

std::wstring ToLower(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) c = static_cast<wchar_t>(towlower(c));
    return r;
}

bool ContainsI(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    auto h = ToLower(haystack);
    auto n = ToLower(needle);
    return h.find(n) != std::wstring::npos;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string r(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        r.data(), len, nullptr, nullptr);
    return r;
}

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring r(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), len);
    return r;
}

std::wstring IntToWString(long long v) {
    return std::to_wstring(v);
}

std::wstring TrimW(std::wstring_view s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(static_cast<wint_t>(s[a]))) ++a;
    while (b > a && iswspace(static_cast<wint_t>(s[b - 1]))) --b;
    return std::wstring(s.substr(a, b - a));
}

std::wstring ProcessNameFromPid(DWORD pid) {
    if (pid == 0) return L"";
    wchar_t path[MAX_PATH] = {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(h, 0, path, &sz);
    CloseHandle(h);
    if (!ok || sz == 0) return L"";
    std::wstring full(path, sz);
    size_t slash = full.find_last_of(L"\\/");
    std::wstring base = (slash != std::wstring::npos) ? full.substr(slash + 1) : full;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
    return base;
}

std::wstring ProcessPathFromPid(DWORD pid) {
    if (pid == 0) return L"";
    wchar_t path[MAX_PATH] = {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(h, 0, path, &sz);
    CloseHandle(h);
    if (!ok || sz == 0) return L"";
    return std::wstring(path, sz);
}

// ===== DPI 缩放 =====

float GetSystemDpiScale() {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return static_cast<float>(dpi) / 96.0f;
}

float GetDpiScaleForWindow(HWND hwnd) {
    if (!hwnd) return GetSystemDpiScale();
    // 使用 Win10 1607+ API
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) return GetSystemDpiScale();
    return static_cast<float>(dpi) / 96.0f;
}

int ScaleDpi(float scale, int value) {
    return static_cast<int>(static_cast<float>(value) * scale + 0.5f);
}

} // namespace tvl
