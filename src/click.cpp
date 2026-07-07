#include "click.h"
#include <wrl/client.h>
#include <uiautomation.h>
#include <uiautomationclient.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace tvl {

// ===== SendInput 绝对坐标点击 =====
static INPUT MakeMouseInput(int x, int y, DWORD flags) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = x;
    in.mi.dy = y;
    in.mi.mouseData = 0;
    in.mi.dwFlags = flags;
    in.mi.time = 0;
    in.mi.dwExtraInfo = 0;
    return in;
}

bool SendClickAt(int screenX, int screenY, bool rightClick) {
    int vscreenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vscreenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vscreenL = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vscreenT = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (vscreenW <= 0) vscreenW = GetSystemMetrics(SM_CXSCREEN);
    if (vscreenH <= 0) vscreenH = GetSystemMetrics(SM_CYSCREEN);

    int absX = static_cast<int>((static_cast<LONG_PTR>(screenX - vscreenL) * 65536L) / vscreenW);
    int absY = static_cast<int>((static_cast<LONG_PTR>(screenY - vscreenT) * 65536L) / vscreenH);

    DWORD down = rightClick ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    DWORD up   = rightClick ? MOUSEEVENTF_RIGHTUP   : MOUSEEVENTF_LEFTUP;
    DWORD absFlag = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    INPUT inputs[2];
    inputs[0] = MakeMouseInput(absX, absY, down | absFlag);
    inputs[1] = MakeMouseInput(absX, absY, up   | absFlag);
    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    TraceLogW("CLICK", L"SendInput sent=" + std::to_wstring(sent) + L"/2 screen=(" +
              std::to_wstring(screenX) + L"," + std::to_wstring(screenY) + L") abs=(" +
              std::to_wstring(absX) + L"," + std::to_wstring(absY) + L")");
    return sent == 2;
}

// ===== UIA 辅助(本模块局部) =====
namespace {

thread_local ComPtr<IUIAutomation> t_uia;
ComPtr<IUIAutomation> LocalUia() {
    if (!t_uia) CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t_uia));
    return t_uia;
}

struct BStr { BSTR p = nullptr; ~BStr() { if (p) SysFreeString(p); } std::wstring get() const { return p ? p : L""; } };

std::wstring ElName(ComPtr<IUIAutomationElement>& e) { BStr b; e->get_CurrentName(&b.p); return b.get(); }
std::wstring ElAutoId(ComPtr<IUIAutomationElement>& e) { BStr b; e->get_CurrentAutomationId(&b.p); return b.get(); }
std::wstring ElRuntimeId(ComPtr<IUIAutomationElement>& e) {
    SAFEARRAY* psa = nullptr; if (FAILED(e->GetRuntimeId(&psa)) || !psa) return L"";
    std::wstring out; int* data = nullptr; LONG lb = 0, ub = 0;
    SafeArrayGetLBound(psa, 1, &lb); SafeArrayGetUBound(psa, 1, &ub);
    if (SUCCEEDED(SafeArrayAccessData(psa, reinterpret_cast<void**>(&data)))) {
        for (LONG i = lb; i <= ub; ++i) { if (!out.empty()) out += L","; out += std::to_wstring(data[i]); }
        SafeArrayUnaccessData(psa);
    }
    SafeArrayDestroy(psa); return out;
}
bool ElBounds(ComPtr<IUIAutomationElement>& e, RECT& rc) {
    rc = {}; if (FAILED(e->get_CurrentBoundingRectangle(&rc))) return false;
    return rc.right - rc.left > 0 && rc.bottom - rc.top > 0;
}

std::vector<ComPtr<IUIAutomationElement>> FindAllChildren(ComPtr<IUIAutomationElement>& root) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    if (!root) return out;
    auto uia = LocalUia();
    ComPtr<IUIAutomationCondition> cond; uia->CreateTrueCondition(&cond);
    ComPtr<IUIAutomationElementArray> arr; root->FindAll(TreeScope_Children, cond.Get(), &arr);
    if (!arr) return out;
    int n = 0; arr->get_Length(&n);
    for (int i = 0; i < n; ++i) { ComPtr<IUIAutomationElement> e; arr->GetElement(i, &e); if (e) out.push_back(e); }
    return out;
}
std::vector<ComPtr<IUIAutomationElement>> FindAllDescendants(ComPtr<IUIAutomationElement>& root) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    if (!root) return out;
    auto uia = LocalUia();
    ComPtr<IUIAutomationCondition> cond; uia->CreateTrueCondition(&cond);
    ComPtr<IUIAutomationElementArray> arr; root->FindAll(TreeScope_Descendants, cond.Get(), &arr);
    if (!arr) return out;
    int n = 0; arr->get_Length(&n);
    for (int i = 0; i < n; ++i) { ComPtr<IUIAutomationElement> e; arr->GetElement(i, &e); if (e) out.push_back(e); }
    return out;
}

bool DoInvoke(ComPtr<IUIAutomationElement>& e, bool rightClick) {
    if (!rightClick) {
        ComPtr<IUnknown> unk; e->GetCurrentPattern(UIA_InvokePatternId, &unk);
        if (unk) { ComPtr<IUIAutomationInvokePattern> p; unk.As(&p); if (p && SUCCEEDED(p->Invoke())) return true; }
    }
    ComPtr<IUnknown> unk2; e->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, &unk2);
    if (unk2) {
        ComPtr<IUIAutomationLegacyIAccessiblePattern> p; unk2.As(&p);
        if (p && SUCCEEDED(p->DoDefaultAction())) return true;
    }
    return false;
}

void FindBridgeWindows(HWND parent, std::vector<HWND>& out) {
    HWND child = nullptr;
    while (true) {
        child = FindWindowExW(parent, child, nullptr, nullptr);
        if (!child) break;
        wchar_t buf[256] = {}; GetClassNameW(child, buf, 256);
        if (_wcsicmp(buf, kDesktopContentBridge) == 0) out.push_back(child);
        FindBridgeWindows(child, out);
    }
}

// 在任务栏可见区按 RuntimeId 查找并点击
bool ClickVisibleByRuntimeId(const std::wstring& runtimeId, bool rightClick) {
    HWND hTray = FindWindowW(kShellTrayWnd, nullptr);
    if (!hTray) return false;
    HWND hNotify = FindWindowExW(hTray, nullptr, kTrayNotifyWnd, nullptr);
    std::vector<HWND> bridges;
    FindBridgeWindows(hTray, bridges);
    FindBridgeWindows(hNotify, bridges);
    std::sort(bridges.begin(), bridges.end());
    bridges.erase(std::unique(bridges.begin(), bridges.end()), bridges.end());

    auto uia = LocalUia();
    for (HWND hb : bridges) {
        ComPtr<IUIAutomationElement> root; uia->ElementFromHandle(hb, &root);
        if (!root) continue;
        auto desc = FindAllDescendants(root);
        for (auto& child : desc) {
            if (ElRuntimeId(child) == runtimeId) {
                TraceLogW("CLICK", L"found by RuntimeId, invoking");
                return DoInvoke(child, rightClick);
            }
        }
    }
    return false;
}

// 在溢出窗口按显示名查找并点击
bool FindAndClickInOverflow(const std::wstring& displayName, bool rightClick) {
    HWND hOuter = FindWindowW(kOverflowXamlIsland, nullptr);
    if (!hOuter) return false;
    HWND hInner = FindWindowExW(hOuter, nullptr, kDesktopContentBridge, nullptr);
    if (!hInner) return false;
    auto uia = LocalUia();
    ComPtr<IUIAutomationElement> inner; uia->ElementFromHandle(hInner, &inner);
    if (!inner) return false;
    auto children = FindAllChildren(inner);
    for (auto& child : children) {
        std::wstring name = ElName(child);
        std::wstring desc;
        ComPtr<IUnknown> unk; child->GetCurrentPattern(UIA_LegacyIAccessiblePatternId, &unk);
        if (unk) { ComPtr<IUIAutomationLegacyIAccessiblePattern> p; unk.As(&p); if (p) { BStr d; p->get_CurrentDescription(&d.p); desc = d.get(); } }
        if (ContainsI(name, displayName) || ContainsI(desc, displayName)) {
            RECT rc{};
            if (ElBounds(child, rc)) {
                int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
                return SendClickAt(cx, cy, rightClick);
            }
            return DoInvoke(child, rightClick);
        }
    }
    return false;
}

} // namespace

// ===== 主点击逻辑(对齐 C# IconActionService.ClickAt) =====
bool ClickIcon(const TrayIconInfo& icon, bool rightClick) {
    try {
        bool hasValid = icon.hasValidBounds &&
            icon.bounds.right > icon.bounds.left && icon.bounds.bottom > icon.bounds.top;

        // 溢出图标 — 打开溢出窗口后用 UIA 点击
        if (icon.automationId == L"NotifyItemIcon" && !hasValid) {
            HWND hOvf = FindWindowW(kOverflowXamlIsland, nullptr);
            if (hOvf) {
                bool wasVisible = IsWindowVisible(hOvf) != 0;
                if (!wasVisible) { ShowWindow(hOvf, SW_SHOWNOACTIVATE); Sleep(400); }
                bool ok = FindAndClickInOverflow(icon.DisplayName(), rightClick);
                if (!wasVisible) ShowWindowAsync(hOvf, SW_HIDE);
                return ok;
            }
            return false;
        }

        // 可见图标 — 优先 UIA Invoke
        if (hasValid) {
            if (!icon.runtimeId.empty()) {
                if (ClickVisibleByRuntimeId(icon.runtimeId, rightClick)) return true;
            }
            int cx = (icon.bounds.left + icon.bounds.right) / 2;
            int cy = (icon.bounds.top + icon.bounds.bottom) / 2;
            return SendClickAt(cx, cy, rightClick);
        }

        // 可见图标但无坐标 — UIA Invoke
        if (!icon.runtimeId.empty())
            return ClickVisibleByRuntimeId(icon.runtimeId, rightClick);
        return false;
    } catch (const std::exception& e) {
        TraceLog("CLICK", std::string("EXCEPTION ") + e.what());
        return false;
    }
}

bool ClickIconAsync(const TrayIconInfo& icon, bool rightClick) {
    bool ok = false;
    TrayIconInfo copy = icon;
    RunOnSta([&]() { ok = ClickIcon(copy, rightClick); });
    return ok;
}

} // namespace tvl
