#pragma once
#include "common.h"
#include "settings.h"
#include <map>
#include <functional>

namespace tvl {

// 全局热键管理服务 — RegisterHotKey/UnregisterHotKey 封装
// 回调在主线程(WndProc 处理 WM_HOTKEY 时)被调用
class GlobalHotkeyService {
public:
    using Callback = std::function<void()>;
    using NumberCallback = std::function<void(int)>;

    void Initialize(HWND hwnd) { hwnd_ = hwnd; }
    HWND Hwnd() const { return hwnd_; }

    // 注册单个热键,返回 id(>0 成功,-1 失败)
    int Register(const HotkeyConfig& cfg, Callback cb);

    // 注册数字热键 0-9(使用 prefix 的修饰键)
    std::vector<int> RegisterNumbers(const HotkeyConfig& prefix, NumberCallback cb);

    bool Unregister(int id);
    void UnregisterAll();

    // 处理 WM_HOTKEY 的 wParam,返回是否消费
    bool HandleWParam(WPARAM wParam);

    // 暂停(注销但保留配置)/ 恢复(重新注册)
    void SuspendAll();
    void ResumeAll();

    // 探测某热键是否可注册(注册后立即注销),失败给出原因
    bool CanRegister(const HotkeyConfig& cfg, std::wstring* reason = nullptr);

private:
    struct Reg { int id = 0; HotkeyConfig cfg; UINT mod = 0; UINT vk = 0; Callback cb; };
    HWND hwnd_ = nullptr;
    int nextId_ = 1;
    std::map<int, Reg> regs_;
};

} // namespace tvl
