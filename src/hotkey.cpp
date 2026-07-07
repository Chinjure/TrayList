#include "hotkey.h"

namespace tvl {

int GlobalHotkeyService::Register(const HotkeyConfig& cfg, Callback cb) {
    if (!hwnd_) return -1;
    int id = nextId_++;
    UINT mod = cfg.modifiers.Flags();
    UINT vk  = cfg.key;

    if (RegisterHotKey(hwnd_, id, mod, vk)) {
        Reg r; r.id = id; r.cfg = cfg; r.mod = mod; r.vk = vk; r.cb = std::move(cb);
        regs_[id] = std::move(r);
        TraceLogW("HOTKEY", L"registered id=" + std::to_wstring(id) + L" " + cfg.Display());
        return id;
    }
    DWORD err = GetLastError();
    TraceLogW("HOTKEY", L"register FAILED id=" + std::to_wstring(id) + L" err=" + std::to_wstring(err) +
              L" " + cfg.Display());
    return -1;
}

std::vector<int> GlobalHotkeyService::RegisterNumbers(const HotkeyConfig& prefix, NumberCallback cb) {
    std::vector<int> ids;
    for (int d = 0; d <= 9; ++d) {
        HotkeyConfig cfg;
        cfg.modifiers = prefix.modifiers;
        cfg.key = static_cast<UINT>('0' + d); // VK_0..VK_9
        cfg.description = L"数字快捷键 " + std::to_wstring(d);
        int digit = d;
        int id = Register(cfg, [cb, digit]() { if (cb) cb(digit); });
        if (id > 0) ids.push_back(id);
    }
    TraceLogW("HOTKEY", L"registered " + std::to_wstring(ids.size()) + L" number hotkeys");
    return ids;
}

bool GlobalHotkeyService::Unregister(int id) {
    auto it = regs_.find(id);
    if (it == regs_.end()) return false;
    bool ok = UnregisterHotKey(hwnd_, id) != 0;
    regs_.erase(it);
    return ok;
}

void GlobalHotkeyService::UnregisterAll() {
    for (auto& kv : regs_) UnregisterHotKey(hwnd_, kv.first);
    regs_.clear();
}

bool GlobalHotkeyService::HandleWParam(WPARAM wParam) {
    int id = static_cast<int>(LOWORD(wParam)); // WM_HOTKEY wParam 的低字为 id
    auto it = regs_.find(id);
    if (it != regs_.end()) {
        if (it->second.cb) it->second.cb();
        return true;
    }
    return false;
}

void GlobalHotkeyService::SuspendAll() {
    for (auto& kv : regs_) UnregisterHotKey(hwnd_, kv.first);
}

void GlobalHotkeyService::ResumeAll() {
    auto copy = regs_;
    for (auto& kv : copy) {
        RegisterHotKey(hwnd_, kv.first, kv.second.mod, kv.second.vk);
    }
}

bool GlobalHotkeyService::CanRegister(const HotkeyConfig& cfg, std::wstring* reason) {
    if (!hwnd_) { if (reason) *reason = L"服务未初始化"; return false; }
    UINT mod = cfg.modifiers.Flags();
    UINT vk = cfg.key;
    int testId = -1;
    for (int i = 10000; i < 10100; ++i) { if (regs_.find(i) == regs_.end()) { testId = i; break; } }
    if (testId < 0) { if (reason) *reason = L"无法分配测试 ID"; return false; }
    if (RegisterHotKey(hwnd_, testId, mod, vk)) {
        UnregisterHotKey(hwnd_, testId);
        return true;
    }
    DWORD err = GetLastError();
    if (reason) *reason = (err == 1409) ? L"热键已被其他应用占用" : (L"注册失败 (错误代码: " + std::to_wstring(err) + L")");
    return false;
}

} // namespace tvl
