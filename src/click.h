#pragma once
#include "common.h"
#include "tray_enum.h"

namespace tvl {

// 在屏幕坐标发送鼠标点击(绝对坐标 + 虚拟桌面)
bool SendClickAt(int screenX, int screenY, bool rightClick = false);

// 对图标执行左键点击 / 右键菜单。会自动处理溢出窗口与 UIA Invoke 回退。
// 必须在 STA 线程上调用(内部使用 COM UIA)。
bool ClickIcon(const TrayIconInfo& icon, bool rightClick);
bool ClickIconAsync(const TrayIconInfo& icon, bool rightClick); // 内部 RunOnSta,阻塞

} // namespace tvl
