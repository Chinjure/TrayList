#include "app.h"
#include "common.h"

// Win32 GUI 入口 — 无控制台
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        tvl::App app;
        return app.Run();
    } catch (const std::exception& e) {
        tvl::WriteCrashLog(std::string("wWinMain exception: ") + e.what());
        return 1;
    } catch (...) {
        tvl::WriteCrashLog("wWinMain unknown exception");
        return 1;
    }
}

// 提供控制台入口便于调试(可链接为 CONSOLE 子系统时使用)
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOW);
}
