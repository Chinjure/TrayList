#include "tray_enum.h"
#include "click.h"
#include <wrl/client.h>
#include <uiautomation.h>
#include <uiautomationclient.h>
#include <thread>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using Microsoft::WRL::ComPtr;

namespace tvl {

// ===== COM 辅助 =====
namespace {

thread_local ComPtr<IUIAutomation> t_uia;

ComPtr<IUIAutomation> EnsureUia() {
    if (!t_uia) {
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&t_uia));
    }
    return t_uia;
}

struct BStr {
    BSTR p = nullptr;
    ~BStr() { if (p) SysFreeString(p); }
    std::wstring get() const { return p ? std::wstring(p) : std::wstring(); }
    const wchar_t* c_str() const { return p ? p : L""; }
};

std::wstring GetPropName(ComPtr<IUIAutomationElement>& el) {
    BStr b; el->get_CurrentName(&b.p); return b.get();
}
std::wstring GetPropAutomationId(ComPtr<IUIAutomationElement>& el) {
    BStr b; el->get_CurrentAutomationId(&b.p); return b.get();
}
std::wstring GetPropClassName(ComPtr<IUIAutomationElement>& el) {
    BStr b; el->get_CurrentClassName(&b.p); return b.get();
}
int GetControlType(ComPtr<IUIAutomationElement>& el) {
    CONTROLTYPEID t = 0;
    el->get_CurrentControlType(&t);
    return static_cast<int>(t);
}
DWORD GetProcessId(ComPtr<IUIAutomationElement>& el) {
    int pid = 0; el->get_CurrentProcessId(&pid); return static_cast<DWORD>(pid);
}

std::wstring GetRuntimeId(ComPtr<IUIAutomationElement>& el) {
    SAFEARRAY* psa = nullptr;
    if (FAILED(el->GetRuntimeId(&psa)) || !psa) return L"";
    std::wstring out;
    VARTYPE vt;
    SafeArrayGetVartype(psa, &vt);
    LONG lb = 0, ub = 0;
    SafeArrayGetLBound(psa, 1, &lb);
    SafeArrayGetUBound(psa, 1, &ub);
    int* data = nullptr;
    if (SUCCEEDED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&data)))) {
        for (LONG i = lb; i <= ub; ++i) {
            if (!out.empty()) out += L",";
            out += std::to_wstring(data[i]);
        }
        SafeArrayUnaccessData(psa);
    }
    SafeArrayDestroy(psa);
    return out;
}

bool GetBoundingRect(ComPtr<IUIAutomationElement>& el, RECT& rc) {
    rc = {};
    if (FAILED(el->get_CurrentBoundingRectangle(&rc))) return false;
    // UIA 返回的 RECT 可能是 logical,但坐标点击按 virtual desk;这里仅用于去重与范围判断
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return false;
    return true;
}

// 获取 LegacyIAccessible.Description / Name 作为 tooltip
std::wstring GetLegacyDescription(ComPtr<IUIAutomationElement>& el) {
    ComPtr<IUnknown> unk;
    el->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, &unk);
    if (!unk) return L"";
    ComPtr<IUIAutomationLegacyIAccessiblePattern> p;
    unk.As(&p);
    if (!p) return L"";
    BStr b; p->get_CurrentDescription(&b.p);
    if (!b.p || !*b.p) { BStr n; p->get_CurrentName(&n.p); return n.get(); }
    return b.get();
}

bool IsLegacySupported(ComPtr<IUIAutomationElement>& el) {
    ComPtr<IUnknown> unk;
    el->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, &unk);
    return unk != nullptr;
}

bool InvokeElement(ComPtr<IUIAutomationElement>& el) {
    ComPtr<IUnknown> unk;
    el->GetCurrentPattern(UIA_InvokePatternId, &unk);
    if (unk) {
        ComPtr<IUIAutomationInvokePattern> p; unk.As(&p);
        if (p && SUCCEEDED(p->Invoke())) return true;
    }
    // 回退 LegacyIAccessible.DoDefaultAction
    ComPtr<IUnknown> unk2;
    el->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, &unk2);
    if (unk2) {
        ComPtr<IUIAutomationLegacyIAccessiblePattern> p; unk2.As(&p);
        if (p && SUCCEEDED(p->DoDefaultAction())) return true;
    }
    return false;
}

// FindAll(TreeScope, condition=null=所有)
std::vector<ComPtr<IUIAutomationElement>> FindAll(ComPtr<IUIAutomationElement>& root, TreeScope scope) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    if (!root) return out;
    auto uia = EnsureUia();
    ComPtr<IUIAutomationCondition> cond;
    uia->CreateTrueCondition(&cond);
    ComPtr<IUIAutomationElementArray> arr;
    root->FindAll(scope, cond.Get(), &arr);
    if (!arr) return out;
    int n = 0; arr->get_Length(&n);
    for (int i = 0; i < n; ++i) {
        ComPtr<IUIAutomationElement> e; arr->GetElement(i, &e);
        if (e) out.push_back(e);
    }
    return out;
}

// 查找子窗口(按类名,递归)
void FindChildWindowsRecursive(HWND parent, const std::wstring& cls, std::vector<HWND>& out) {
    HWND child = nullptr;
    while (true) {
        child = FindWindowExW(parent, child, nullptr, nullptr);
        if (!child) break;
        wchar_t buf[256] = {};
        GetClassNameW(child, buf, 256);
        if (_wcsicmp(buf, cls.c_str()) == 0) out.push_back(child);
        FindChildWindowsRecursive(child, cls, out);
    }
}
std::vector<HWND> FindChildWindowsOfClass(HWND parent, const std::wstring& cls) {
    std::vector<HWND> out;
    FindChildWindowsRecursive(parent, cls, out);
    return out;
}

bool IsSystemIcon(const std::wstring& name, const std::wstring& processName) {
    // 系统图标名称(英/中)
    static const wchar_t* kSysNames[] = {
        L"Network", L"Volume", L"Battery", L"Action Center", L"Clock", L"IME",
        L"Touch Keyboard", L"Input Indicator", L"Show Desktop", L"Meeting",
        L"Microphone", L"Location", L"Bluetooth", L"Safely Remove", L"OneDrive",
        L"Windows Security", L"Focus Assist", L"Quick Settings",
        L"网络", L"音量", L"电源", L"电池", L"时钟", L"输入指示", L"显示桌面",
        L"蓝牙", L"安全中心", L"安全删除", L"麦克风", L"位置"
    };
    for (auto p : kSysNames) {
        if (!name.empty() && ContainsI(name, p)) return true;
    }

    // 过滤 Shell 体验主机等 Windows 系统进程的非图标元素
    static const wchar_t* kShellProcesses[] = {
        L"explorer", L"ShellExperienceHost", L"SearchHost", L"StartMenuExperienceHost",
        L"Widgets", L"ShellHost"
    };
    if (!processName.empty()) {
        for (auto p : kShellProcesses) {
            if (ContainsI(processName, p)) {
                // 如果是 shell 进程且 name 为空或与进程名雷同，则过滤
                if (name.empty() || ContainsI(processName, name) || ContainsI(name, processName))
                    return true;
                // 检查 name 是否只是进程名变体（如 "explore" vs "explorer"）
                if (name.size() >= 4) {
                    auto lowerName = ToLower(name);
                    auto lowerProc = ToLower(processName);
                    if (lowerProc.find(lowerName) != std::wstring::npos ||
                        lowerName.find(lowerProc) != std::wstring::npos)
                        return true;
                }
            }
        }
    }

    return false;
}

bool IsSelf(const TrayIconInfo& icon) {
    if (ContainsI(icon.processName, L"TrayVerticalList")) return true;
    if (ContainsI(icon.DisplayName(), L"TrayVerticalList")) return true;
    if (ContainsI(icon.tooltip, L"TrayVerticalList")) return true;
    return false;
}

std::wstring MakeDedupKey(const TrayIconInfo& icon) {
    if (!icon.runtimeId.empty()) return icon.runtimeId;
    if (!icon.processName.empty() && !icon.automationId.empty())
        return icon.processName + L"|" + icon.automationId;
    if (!icon.DisplayName().empty()) return ToLower(icon.DisplayName());
    // 兜底:用进程 id + bounds
    return L"p" + std::to_wstring(icon.processId) + L"_" +
           std::to_wstring(icon.bounds.left) + L"," + std::to_wstring(icon.bounds.top);
}

// 从 UIA 元素提取图标信息(对齐 ExtractIconInfo)
bool ExtractIconInfo(ComPtr<IUIAutomationElement>& el, int index, TrayIconInfo& out) {
    try {
        out.index = index;
        out.name = GetPropName(el);
        out.automationId = GetPropAutomationId(el);
        out.runtimeId = GetRuntimeId(el);

        // tooltip:优先 Legacy.Description,其次 Legacy.Name,最后 = name
        std::wstring desc = GetLegacyDescription(el);
        if (!desc.empty()) out.tooltip = desc;
        else out.tooltip = out.name;

        RECT rc{};
        if (GetBoundingRect(el, rc)) {
            out.bounds = rc;
            out.hasValidBounds = true;
        }

        out.processId = GetProcessId(el);
        if (out.processId > 0) out.processName = ProcessNameFromPid(out.processId);
        out.executablePath = ProcessPathFromPid(out.processId);
        out.isSystemIcon = IsSystemIcon(out.name, out.processName);
        return true;
    } catch (...) {
        return false;
    }
}

void DumpWindowHierarchy() {
    HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
    TraceLogW("ENUM", std::wstring(L"Shell_TrayWnd=0x") +
              std::to_wstring(reinterpret_cast<uintptr_t>(hTray)));
    // 检查溢出窗口
    HWND hOvf = FindWindowW(kOverflowXamlIsland, nullptr);
    TraceLogW("ENUM", std::wstring(L"OverflowXamlIsland=0x") +
              std::to_wstring(reinterpret_cast<uintptr_t>(hOvf)) + (hOvf ? (IsWindowVisible(hOvf) ? L" visible" : L" hidden") : L""));
}

} // namespace

// ===== 可见区枚举 =====
static std::vector<TrayIconInfo> EnumerateVisible(HWND hTray, HWND hNotify) {
    std::vector<TrayIconInfo> icons;
    if (!hTray || !hNotify) return icons;

    RECT trayArea{};
    GetWindowRect(hNotify, &trayArea);

    auto bridges = FindChildWindowsOfClass(hTray, kDesktopContentBridge);
    auto notifyBridges = FindChildWindowsOfClass(hNotify, kDesktopContentBridge);
    for (HWND b : notifyBridges) bridges.push_back(b);
    // 去重
    std::sort(bridges.begin(), bridges.end());
    bridges.erase(std::unique(bridges.begin(), bridges.end()), bridges.end());

    auto uia = EnsureUia();
    int index = 0;
    std::unordered_set<std::wstring> seen;

    for (HWND hwndBridge : bridges) {
        ComPtr<IUIAutomationElement> root;
        uia->ElementFromHandle(hwndBridge, &root);
        if (!root) continue;
        auto descendants = FindAll(root, TreeScope_Descendants);
        for (auto& child : descendants) {
            int ct = GetControlType(child);
            if (ct != UIA_ButtonControlTypeId && ct != UIA_ListItemControlTypeId && ct != UIA_ImageControlTypeId)
                continue;

            std::wstring childName = GetPropName(child);
            RECT br{};
            bool hasValid = GetBoundingRect(child, br);
            if (hasValid) {
                bool inH = br.left >= trayArea.left - 8 && br.right <= trayArea.right + 8;
                bool inV = br.top >= trayArea.top - 8 && br.bottom <= trayArea.bottom + 8;
                if (!inH || !inV) {
                    TraceLogW("ENUM", L"VIS REJECT name=" + childName);
                    continue;
                }
            }

            TrayIconInfo info;
            if (!ExtractIconInfo(child, index, info)) continue;

            // 跳过溢出 ^ 按钮
            bool isOverflowToggle = (hasValid && br.left < trayArea.left) ||
                ContainsI(childName, L"隐藏") || ContainsI(childName, L"hidden");
            if (isOverflowToggle) { TraceLogW("ENUM", L"VIS SKIP overflow-toggle " + childName); continue; }
            // 跳过自己
            if (IsSelf(info)) { TraceLogW("ENUM", L"VIS SKIP self " + childName); continue; }
            // 跳过幻影(空 name + 空 autoId)
            if (info.name.empty() && info.automationId.empty()) continue;

            auto key = MakeDedupKey(info);
            if (seen.insert(key).second) {
                if (hasValid) { info.bounds = br; info.hasValidBounds = true; }
                icons.push_back(info);
                ++index;
            }
        }
    }

    // 桥窗口方式不足时,直接遍历 TrayNotifyWnd
    if (icons.size() <= 1) {
        TraceLog("ENUM", "VIS bridge<=1, trying direct TrayNotifyWnd");
        ComPtr<IUIAutomationElement> notifyEl;
        uia->ElementFromHandle(hNotify, &notifyEl);
        if (notifyEl) {
            auto desc = FindAll(notifyEl, TreeScope_Descendants);
            for (auto& child : desc) {
                int ct = GetControlType(child);
                if (ct != UIA_ButtonControlTypeId && ct != UIA_ListItemControlTypeId && ct != UIA_ImageControlTypeId)
                    continue;
                TrayIconInfo info;
                if (!ExtractIconInfo(child, index, info)) continue;
                if (IsSelf(info)) continue;
                auto key = MakeDedupKey(info);
                bool dup = false;
                for (auto& e : icons) if (MakeDedupKey(e) == key) { dup = true; break; }
                if (dup) continue;
                icons.push_back(info); ++index;
            }
        }
    }
    return icons;
}

// ===== 溢出区枚举 =====
static std::vector<TrayIconInfo> EnumerateOverflow() {
    std::vector<TrayIconInfo> icons;
    HWND hWndOuter = FindWindowW(kOverflowXamlIsland, nullptr);
    if (!hWndOuter) {
        // 传统溢出窗口
        HWND hLegacy = FindWindowW(kNotifyIconOverflowWindow, nullptr);
        if (hLegacy) {
            // ToolbarWindow32 fallback 在 EnumerateLegacyToolbar 中处理
        }
        return icons;
    }
    HWND hWndInner = FindWindowExW(hWndOuter, nullptr, kDesktopContentBridge, nullptr);
    if (!hWndInner) return icons;

    bool wasVisible = IsWindowVisible(hWndOuter) != 0;
    if (!wasVisible) {
        ShowWindowAsync(hWndOuter, SW_SHOWNOACTIVATE);
        Sleep(150);
    }
    auto uia = EnsureUia();
    ComPtr<IUIAutomationElement> inner;
    uia->ElementFromHandle(hWndInner, &inner);
    if (inner) {
        auto children = FindAll(inner, TreeScope_Children);
        int index = 0;
        for (auto& child : children) {
            TrayIconInfo info;
            if (!ExtractIconInfo(child, index, info)) continue;
            auto key = MakeDedupKey(info);
            bool dup = false;
            for (auto& e : icons) if (MakeDedupKey(e) == key) { dup = true; break; }
            if (dup) continue;
            icons.push_back(info); ++index;
        }
    }
    if (!wasVisible) ShowWindowAsync(hWndOuter, SW_HIDE);
    return icons;
}

// ===== 传统 ToolbarWindow32 枚举(回退) =====
// 使用 UIA 从 toolbar 句柄获取子元素(等价 C# AddIconsFromToolbar)
static void AddIconsFromToolbar(HWND hToolbar, std::vector<TrayIconInfo>& icons) {
    auto uia = EnsureUia();
    ComPtr<IUIAutomationElement> tb;
    uia->ElementFromHandle(hToolbar, &tb);
    if (!tb) return;
    auto children = FindAll(tb, TreeScope_Children);
    for (auto& child : children) {
        TrayIconInfo info;
        if (!ExtractIconInfo(child, static_cast<int>(icons.size()), info)) continue;
        auto key = MakeDedupKey(info);
        bool dup = false;
        for (auto& e : icons) if (MakeDedupKey(e) == key) { dup = true; break; }
        if (dup) continue;
        icons.push_back(info);
    }
}

static std::vector<TrayIconInfo> EnumerateLegacyToolbar() {
    std::vector<TrayIconInfo> icons;
    HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
    HWND hNotify = FindWindowExW(hTray, nullptr, kTrayNotifyWnd, nullptr);
    HWND hPager = FindWindowExW(hNotify, nullptr, kSysPager, nullptr);
    HWND hToolbar = FindWindowExW(hPager, nullptr, kToolbarWindow32, nullptr);
    if (hToolbar) AddIconsFromToolbar(hToolbar, icons);

    HWND hOvf = FindWindowW(kNotifyIconOverflowWindow, nullptr);
    if (hOvf) {
        HWND hOvfTb = FindWindowExW(hOvf, nullptr, kToolbarWindow32, nullptr);
        if (hOvfTb) AddIconsFromToolbar(hOvfTb, icons);
    }
    return icons;
}

// ===== 直接 UIA 树遍历(最后手段) =====
static void WalkTreeForIcons(ComPtr<IUIAutomationElement>& el, const RECT& trayArea,
                             std::vector<TrayIconInfo>& icons, int& index,
                             std::unordered_set<std::wstring>& seen, int depth) {
    if (depth > 20 || !el) return;
    int ct = GetControlType(el);
    if (ct == UIA_ButtonControlTypeId || ct == UIA_ListItemControlTypeId || ct == UIA_ImageControlTypeId) {
        RECT br{};
        if (GetBoundingRect(el, br)) {
            bool inTray = br.left >= trayArea.left - 8 && br.right <= trayArea.right + 8 &&
                          br.top >= trayArea.top - 8 && br.bottom <= trayArea.bottom + 8;
            int w = br.right - br.left, h = br.bottom - br.top;
            if (inTray && w >= 8 && w <= 80 && h >= 8 && h <= 56) {
                TrayIconInfo info;
                if (ExtractIconInfo(el, index, info)) {
                    auto key = MakeDedupKey(info);
                    if (seen.insert(key).second) {
                        info.bounds = br; info.hasValidBounds = true;
                        icons.push_back(info); ++index;
                    }
                }
            }
        }
    }
    auto children = FindAll(el, TreeScope_Children);
    for (auto& c : children) WalkTreeForIcons(c, trayArea, icons, index, seen, depth + 1);
}

static std::vector<TrayIconInfo> EnumerateViaDirectUIA() {
    std::vector<TrayIconInfo> icons;
    HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
    HWND hNotify = FindWindowExW(hTray, nullptr, kTrayNotifyWnd, nullptr);
    if (!hTray || !hNotify) return icons;
    auto uia = EnsureUia();
    ComPtr<IUIAutomationElement> trayEl;
    uia->ElementFromHandle(hTray, &trayEl);
    if (!trayEl) return icons;
    RECT trayArea{}; GetWindowRect(hNotify, &trayArea);
    int index = 0;
    std::unordered_set<std::wstring> seen;
    WalkTreeForIcons(trayEl, trayArea, icons, index, seen, 0);
    return icons;
}

// ===== 主入口 =====
std::vector<TrayIconInfo> EnumerateTrayIcons() {
    std::vector<TrayIconInfo> result;
    std::unordered_map<std::wstring, TrayIconInfo> all;

    try {
        if (!EnsureUia()) { TraceLog("ENUM", "IUIAutomation create failed"); return result; }
        TraceLog("ENUM", "===== ENUM START =====");
        DumpWindowHierarchy();

        HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
        HWND hNotify = FindWindowExW(hTray, nullptr, kTrayNotifyWnd, nullptr);

        auto visible = EnumerateVisible(hTray, hNotify);
        TraceLogW("ENUM", L"visible=" + std::to_wstring(visible.size()));
        for (auto& ic : visible) {
            if (IsSelf(ic) || ic.isSystemIcon) continue;
            auto key = MakeDedupKey(ic);
            if (!all.count(key)) all[key] = ic;
        }

        auto hidden = EnumerateOverflow();
        TraceLogW("ENUM", L"hidden=" + std::to_wstring(hidden.size()));
        for (auto& ic : hidden) {
            if (IsSelf(ic) || ic.isSystemIcon) continue;
            auto key = MakeDedupKey(ic);
            if (!all.count(key)) all[key] = ic;
        }

        if (all.size() <= 2) {
            TraceLog("ENUM", "trying legacy ToolbarWindow32");
            auto legacy = EnumerateLegacyToolbar();
            for (auto& ic : legacy) {
                if (IsSelf(ic) || ic.isSystemIcon) continue;
                auto key = MakeDedupKey(ic);
                if (!all.count(key)) all[key] = ic;
            }
        }
        if (all.size() <= 2) {
            TraceLog("ENUM", "trying direct UIA tree walk");
            auto direct = EnumerateViaDirectUIA();
            for (auto& ic : direct) {
                if (IsSelf(ic) || ic.isSystemIcon) continue;
                auto key = MakeDedupKey(ic);
                if (!all.count(key)) all[key] = ic;
            }
        }

        result.reserve(all.size());
        for (auto& kv : all) result.push_back(kv.second);
        // 重新编号
        for (int i = 0; i < (int)result.size(); ++i) result[i].index = i;

        TraceLogW("ENUM", L"===== ENUM DONE total=" + std::to_wstring(result.size()) + L" =====");
    } catch (const std::exception& e) {
        TraceLog("ENUM", std::string("FAILED: ") + e.what());
    }
    return result;
}

// ===== 溢出窗口控制 =====
bool OpenOverflowWindow() {
    HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
    HWND hNotify = FindWindowExW(hTray, nullptr, kTrayNotifyWnd, nullptr);
    if (!hNotify) return false;
    auto uia = EnsureUia();
    if (!uia) return false;
    ComPtr<IUIAutomationElement> notifyEl;
    uia->ElementFromHandle(hNotify, &notifyEl);
    if (!notifyEl) return false;
    auto buttons = FindAll(notifyEl, TreeScope_Children);
    for (auto& btn : buttons) {
        std::wstring name = GetPropName(btn);
        std::wstring autoId = GetPropAutomationId(btn);
        if (ContainsI(name, L"hidden") || ContainsI(name, L"overflow") || ContainsI(name, L"Show") ||
            ContainsI(autoId, L"Overflow") || ContainsI(autoId, L"Notify")) {
            TraceLogW("ENUM", L"OpenOVF click " + name);
            // 用 Invoke,失败则坐标点击
            if (!InvokeElement(btn)) {
                RECT rc{}; if (GetBoundingRect(btn, rc)) {
                    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
                    SendClickAt(cx, cy, false);
                }
            }
            return true;
        }
    }
    return false;
}

void CloseOverflowWindow() {
    HWND h = FindWindowW(kOverflowXamlIsland, nullptr);
    if (h) ShowWindowAsync(h, SW_HIDE);
    HWND h2 = FindWindowW(kNotifyIconOverflowWindow, nullptr);
    if (h2) ShowWindowAsync(h2, SW_HIDE);
}

// ===== STA 线程运行 =====
void RunOnSta(const std::function<void()>& fn) {
    std::thread([fn]() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try { fn(); } catch (...) {}
        if (SUCCEEDED(hr)) CoUninitialize();
        // 释放本线程的 UIA 缓存
        t_uia.Reset();
    }).join();
}

std::vector<TrayIconInfo> EnumerateTrayIconsAsync() {
    std::vector<TrayIconInfo> result;
    RunOnSta([&]() { result = EnumerateTrayIcons(); });
    return result;
}

} // namespace tvl
