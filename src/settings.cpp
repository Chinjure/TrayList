#include "settings.h"
#include <fstream>
#include <sstream>
#include <map>
#include <variant>
#include <cctype>

namespace tvl {

// ============ ModifierSet ============
UINT ModifierSet::Flags() const {
    UINT f = 0;
    if (ctrl)  f |= MOD_CONTROL;
    if (alt)   f |= MOD_ALT;
    if (shift) f |= MOD_SHIFT;
    if (win)   f |= MOD_WIN;
    f |= MOD_NOREPEAT;
    return f;
}

std::wstring ModifierSet::Display() const {
    std::wstring s;
    auto add = [&](const wchar_t* p) { if (!s.empty()) s += L"+"; s += p; };
    if (ctrl)  add(L"Ctrl");
    if (alt)   add(L"Alt");
    if (shift) add(L"Shift");
    if (win)   add(L"Win");
    return s;
}

ModifierSet ModifierSet::FromFlags(UINT mod) {
    ModifierSet m;
    m.ctrl  = (mod & MOD_CONTROL) != 0;
    m.alt   = (mod & MOD_ALT) != 0;
    m.shift = (mod & MOD_SHIFT) != 0;
    m.win   = (mod & MOD_WIN) != 0;
    return m;
}

// ============ HotkeyConfig ============
std::wstring HotkeyConfig::VkToName(UINT vk) {
    // 常用键名映射
    switch (vk) {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PgUp";
    case VK_NEXT: return L"PgDn";
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
        const wchar_t* names[] = {L"Left", L"Up", L"Right", L"Down"};
        return names[vk - VK_LEFT];
    }
    }
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    wchar_t buf[32];
    swprintf_s(buf, L"VK_0x%X", vk);
    return buf;
}

UINT HotkeyConfig::NameToVk(const std::wstring& name) {
    if (name.empty()) return 0;
    auto lower = ToLower(name);
    if (lower == L"space") return VK_SPACE;
    if (lower == L"enter" || lower == L"return") return VK_RETURN;
    if (lower == L"tab") return VK_TAB;
    if (lower == L"esc" || lower == L"escape") return VK_ESCAPE;
    if (lower == L"backspace" || lower == L"back") return VK_BACK;
    if (lower == L"insert") return VK_INSERT;
    if (lower == L"delete" || lower == L"del") return VK_DELETE;
    if (lower == L"home") return VK_HOME;
    if (lower == L"end") return VK_END;
    if (lower == L"pgup" || lower == L"pageup") return VK_PRIOR;
    if (lower == L"pgdn" || lower == L"pagedown") return VK_NEXT;
    if (lower == L"left") return VK_LEFT;
    if (lower == L"up") return VK_UP;
    if (lower == L"right") return VK_RIGHT;
    if (lower == L"down") return VK_DOWN;
    if (lower.size() == 1) {
        wchar_t c = lower[0];
        if (c >= '0' && c <= '9') return static_cast<UINT>(c);
        if (c >= 'a' && c <= 'z') return static_cast<UINT>(c - 'a' + 'A');
    }
    if (lower.size() >= 2 && lower[0] == L'f') {
        try {
            int n = std::stoi(lower.substr(1));
            if (n >= 1 && n <= 24) return static_cast<UINT>(VK_F1 + n - 1);
        } catch (...) {}
    }
    if (lower.rfind(L"vk_0x", 0) == 0) {
        try { return static_cast<UINT>(std::stoul(lower.substr(5), nullptr, 16)); } catch (...) {}
    }
    return 0;
}

std::wstring HotkeyConfig::Display() const {
    std::wstring s = modifiers.Display();
    if (!s.empty()) s += L"+";
    s += VkToName(key);
    return s;
}

AppSettings AppSettings::Default() {
    AppSettings s;
    s.toggleMenu.modifiers.ctrl = true;
    s.toggleMenu.modifiers.shift = true;
    s.toggleMenu.key = VK_SPACE;
    s.toggleMenu.description = L"展开/收起垂直列表菜单";

    s.numberPrefix.modifiers.ctrl = true;
    s.numberPrefix.modifiers.shift = true;
    s.numberPrefix.key = VK_SPACE; // 前缀仅用其修饰键,主键由 0-9 占位
    s.numberPrefix.description = L"数字快捷键前缀(配合 0-9 使用)";
    return s;
}

// ====================================================================
// 极简 JSON:仅支持本配置 schema 所需子集
// ====================================================================
namespace json {

struct Value;
using Object = std::map<std::wstring, Value, std::less<>>;
using Array  = std::vector<Value>;

struct Value {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::wstring str;
    Array arr;
    Object obj;
};

struct Parser {
    const std::wstring& s;
    size_t i = 0;
    explicit Parser(const std::wstring& src) : s(src) {}

    void skipWs() {
        while (i < s.size() && iswspace(static_cast<wint_t>(s[i]))) ++i;
    }
    bool match(wchar_t c) { skipWs(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    void expect(wchar_t c) { skipWs(); if (i >= s.size() || s[i] != c) throw std::runtime_error("json expect"); ++i; }

    Value parseValue() {
        skipWs();
        if (i >= s.size()) throw std::runtime_error("eof");
        wchar_t c = s[i];
        if (c == L'{') return parseObject();
        if (c == L'[') return parseArray();
        if (c == L'"') { Value v; v.type = Value::Str; v.str = parseString(); return v; }
        if (c == L't' || c == L'f') return parseBool();
        if (c == L'n') { i += 4; Value v; v.type = Value::Null; return v; }
        return parseNumber();
    }

    Value parseObject() {
        Value v; v.type = Value::Obj;
        expect(L'{');
        skipWs();
        if (i < s.size() && s[i] == L'}') { ++i; return v; }
        while (true) {
            skipWs();
            std::wstring key = parseString();
            expect(L':');
            v.obj[key] = parseValue();
            if (match(L',')) continue;
            expect(L'}');
            break;
        }
        return v;
    }

    Value parseArray() {
        Value v; v.type = Value::Arr;
        expect(L'[');
        skipWs();
        if (i < s.size() && s[i] == L']') { ++i; return v; }
        while (true) {
            v.arr.push_back(parseValue());
            if (match(L',')) continue;
            expect(L']');
            break;
        }
        return v;
    }

    std::wstring parseString() {
        expect(L'"');
        std::wstring out;
        while (i < s.size() && s[i] != L'"') {
            wchar_t c = s[i++];
            if (c == L'\\' && i < s.size()) {
                wchar_t e = s[i++];
                switch (e) {
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'n': out += L'\n'; break;
                case L't': out += L'\t'; break;
                case L'r': out += L'\r'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'u': {
                    if (i + 4 <= s.size()) {
                        unsigned code = std::stoul(std::wstring(s.substr(i, 4)), nullptr, 16);
                        i += 4;
                        if (code >= 0xD800 && code <= 0xDBFF && i + 6 <= s.size() && s[i] == L'\\' && s[i+1] == L'u') {
                            unsigned lo = std::stoul(std::wstring(s.substr(i + 2, 4)), nullptr, 16);
                            i += 6;
                            code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        if (code <= 0xFFFF) out += static_cast<wchar_t>(code);
                        else { out += static_cast<wchar_t>(0xD800 + ((code - 0x10000) >> 10));
                               out += static_cast<wchar_t>(0xDC00 + ((code - 0x10000) & 0x3FF)); }
                    }
                    break;
                }
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        if (i < s.size()) ++i; // 跳过结束引号
        return out;
    }

    Value parseBool() {
        Value v; v.type = Value::Bool;
        if (s.compare(i, 4, L"true") == 0) { v.b = true; i += 4; }
        else if (s.compare(i, 5, L"false") == 0) { v.b = false; i += 5; }
        else throw std::runtime_error("bool");
        return v;
    }

    Value parseNumber() {
        size_t start = i;
        while (i < s.size()) {
            wchar_t c = s[i];
            if ((c >= L'0' && c <= L'9') || c == L'-' || c == L'+' || c == L'.' || c == L'e' || c == L'E') ++i;
            else break;
        }
        Value v; v.type = Value::Num;
        v.num = std::stod(std::wstring(s.substr(start, i - start)));
        return v;
    }
};

const Value* find(const Value& v, std::wstring_view key) {
    if (v.type != Value::Obj) return nullptr;
    auto it = v.obj.find(key);
    return (it == v.obj.end()) ? nullptr : &it->second;
}

// JSON 写出器(带缩进,字符串转义)
struct Writer {
    std::wstring out;
    int indent = 0;
    void pad() { for (int k = 0; k < indent; ++k) out += L"  "; }
    void esc(const std::wstring& s) {
        out += L'"';
        for (wchar_t c : s) {
            switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\t': out += L"\\t"; break;
            case L'\r': out += L"\\r"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            default:
                if (c < 0x20) { wchar_t buf[8]; swprintf_s(buf, L"\\u%04x", c); out += buf; }
                else out += c;
            }
        }
        out += L'"';
    }
};

} // namespace json

// ============ Load / Save ============

static ModifierSet ParseModifiers(const json::Value* v) {
    ModifierSet m;
    if (!v || v->type != json::Value::Str) return m;
    auto lower = ToLower(v->str);
    auto has = [&](const wchar_t* p) { return lower.find(p) != std::wstring::npos; };
    m.ctrl  = has(L"control") || has(L"ctrl");
    m.alt   = has(L"alt");
    m.shift = has(L"shift");
    m.win   = has(L"win") || has(L"windows");
    return m;
}

static std::wstring ModifiersToString(const ModifierSet& m) {
    std::wstring s;
    auto add = [&](const wchar_t* p) { if (!s.empty()) s += L","; s += p; };
    if (m.ctrl)  add(L"Control");
    if (m.alt)   add(L"Alt");
    if (m.shift) add(L"Shift");
    if (m.win)   add(L"Windows");
    return s;
}

AppSettings LoadSettings() {
    AppSettings s = AppSettings::Default();
    try {
        std::ifstream f(UserSettingsPath(), std::ios::binary);
        if (!f) return s;
        std::stringstream ss;
        ss << f.rdbuf();
        std::wstring w = Utf8ToWide(ss.str());

        json::Parser p(w);
        json::Value root = p.parseValue();

        if (const json::Value* hk = find(root, L"Hotkeys")) {
            if (const json::Value* t = find(*hk, L"ToggleMenu")) {
                if (const json::Value* m = find(*t, L"Modifiers")) s.toggleMenu.modifiers = ParseModifiers(m);
                if (const json::Value* k = find(*t, L"Key")) {
                    if (k->type == json::Value::Str) s.toggleMenu.key = HotkeyConfig::NameToVk(k->str);
                }
                if (const json::Value* d = find(*t, L"Description")) s.toggleMenu.description = d->str;
                if (const json::Value* e = find(*t, L"Enabled")) s.toggleMenu.enabled = e->b;
            }
            if (const json::Value* n = find(*hk, L"NumberPrefix")) {
                if (const json::Value* m = find(*n, L"Modifiers")) s.numberPrefix.modifiers = ParseModifiers(m);
                if (const json::Value* d = find(*n, L"Description")) s.numberPrefix.description = d->str;
                if (const json::Value* e = find(*n, L"Enabled")) s.numberPrefix.enabled = e->b;
            }
        }
        if (const json::Value* ap = find(root, L"Appearance")) {
            if (const json::Value* v = find(*ap, L"Theme")) s.appearance.theme = v->str;
            if (const json::Value* v = find(*ap, L"MaxVisibleItems")) s.appearance.maxVisibleItems = static_cast<int>(v->num);
            if (const json::Value* v = find(*ap, L"ItemHeight")) s.appearance.itemHeight = static_cast<int>(v->num);
            if (const json::Value* v = find(*ap, L"WindowWidth")) s.appearance.windowWidth = static_cast<int>(v->num);
            if (const json::Value* v = find(*ap, L"ShowSearchBox")) s.appearance.showSearchBox = v->b;
            if (const json::Value* v = find(*ap, L"ShowNumberHints")) s.appearance.showNumberHints = v->b;
        }
        if (const json::Value* bh = find(root, L"Behavior")) {
            if (const json::Value* v = find(*bh, L"AutoStart")) s.behavior.autoStart = v->b;
            if (const json::Value* v = find(*bh, L"CloseOnClick")) s.behavior.closeOnClick = v->b;
            if (const json::Value* v = find(*bh, L"CloseOnFocusLost")) s.behavior.closeOnFocusLost = v->b;
            if (const json::Value* v = find(*bh, L"RefreshIntervalSeconds")) s.behavior.refreshIntervalSeconds = static_cast<int>(v->num);
        }
    } catch (const std::exception& e) {
        TraceLog("SETTINGS", std::string("load failed: ") + e.what());
    }
    return s;
}

bool SaveSettings(const AppSettings& s) {
    try {
        json::Writer w;
        w.out = L"{\n";
        // Hotkeys
        w.indent = 1; w.pad(); w.out += L"\"Hotkeys\": {\n";
        w.indent = 2; w.pad(); w.out += L"\"ToggleMenu\": {\n";
        w.indent = 3; w.pad(); w.out += L"\"Modifiers\": "; w.esc(ModifiersToString(s.toggleMenu.modifiers)); w.out += L",\n";
        w.pad(); w.out += L"\"Key\": "; w.esc(HotkeyConfig::VkToName(s.toggleMenu.key)); w.out += L",\n";
        w.pad(); w.out += L"\"Description\": "; w.esc(s.toggleMenu.description); w.out += L",\n";
        w.pad(); w.out += L"\"Enabled\": "; w.out += (s.toggleMenu.enabled ? L"true" : L"false"); w.out += L"\n";
        w.indent = 2; w.pad(); w.out += L"},\n";
        w.pad(); w.out += L"\"NumberPrefix\": {\n";
        w.indent = 3; w.pad(); w.out += L"\"Modifiers\": "; w.esc(ModifiersToString(s.numberPrefix.modifiers)); w.out += L",\n";
        w.pad(); w.out += L"\"Key\": "; w.esc(HotkeyConfig::VkToName(s.numberPrefix.key)); w.out += L",\n";
        w.pad(); w.out += L"\"Description\": "; w.esc(s.numberPrefix.description); w.out += L",\n";
        w.pad(); w.out += L"\"Enabled\": "; w.out += (s.numberPrefix.enabled ? L"true" : L"false"); w.out += L"\n";
        w.indent = 2; w.pad(); w.out += L"}\n";
        w.indent = 1; w.pad(); w.out += L"},\n";
        // Appearance
        w.pad(); w.out += L"\"Appearance\": {\n";
        w.indent = 2;
        w.pad(); w.out += L"\"Theme\": "; w.esc(s.appearance.theme); w.out += L",\n";
        w.pad(); w.out += L"\"MaxVisibleItems\": "; w.out += std::to_wstring(s.appearance.maxVisibleItems); w.out += L",\n";
        w.pad(); w.out += L"\"ItemHeight\": "; w.out += std::to_wstring(s.appearance.itemHeight); w.out += L",\n";
        w.pad(); w.out += L"\"WindowWidth\": "; w.out += std::to_wstring(s.appearance.windowWidth); w.out += L",\n";
        w.pad(); w.out += L"\"ShowSearchBox\": "; w.out += (s.appearance.showSearchBox ? L"true" : L"false"); w.out += L",\n";
        w.pad(); w.out += L"\"ShowNumberHints\": "; w.out += (s.appearance.showNumberHints ? L"true" : L"false"); w.out += L"\n";
        w.indent = 1; w.pad(); w.out += L"},\n";
        // Behavior
        w.pad(); w.out += L"\"Behavior\": {\n";
        w.indent = 2;
        w.pad(); w.out += L"\"AutoStart\": "; w.out += (s.behavior.autoStart ? L"true" : L"false"); w.out += L",\n";
        w.pad(); w.out += L"\"CloseOnClick\": "; w.out += (s.behavior.closeOnClick ? L"true" : L"false"); w.out += L",\n";
        w.pad(); w.out += L"\"CloseOnFocusLost\": "; w.out += (s.behavior.closeOnFocusLost ? L"true" : L"false"); w.out += L",\n";
        w.pad(); w.out += L"\"RefreshIntervalSeconds\": "; w.out += std::to_wstring(s.behavior.refreshIntervalSeconds); w.out += L"\n";
        w.indent = 1; w.pad(); w.out += L"}\n";
        w.out += L"}\n";

        std::string utf8 = WideToUtf8(w.out);
        std::ofstream f(UserSettingsPath(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        return true;
    } catch (const std::exception& e) {
        TraceLog("SETTINGS", std::string("save failed: ") + e.what());
        return false;
    }
}

} // namespace tvl
