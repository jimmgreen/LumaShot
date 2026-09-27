# 剪贴板：隐藏亚克力面板在屏幕左上角留下幽灵条

日期：2026-09-22。

## 现象

用户桌面左上角 (0,0) 出现一个约 36×132 px 的浅灰色空白小条，持续存在，结束 LumaShot 进程后消失。用户反馈上一版没有出现。

## 定位

- 枚举顶层窗口：LumaShot 进程内有一个 `LumaShot.Clipboard` HWND 位于 (0,0)、36×132、`IsWindowVisible=false`、未 cloak；另一个可见的 `LumaShot.Clipboard` HWND 在屏幕右侧，即正常的折叠侧边条。
- 该隐藏 HWND 是剪贴板拆分后常驻的原生亚克力面板（`src/clipboard/panel.cpp` `Create()`），在折叠状态下从未显示过，仍停留在创建时的 (0,0)。
- 用 `SetWindowPos` 把该隐藏窗口移到 (600,300)，屏幕上的灰条随之移动，移回后又回到左上角；显示一次再隐藏后灰条消失。说明是 DWM 为这个从未显示过的亚克力窗口合成了背景。
- 独立复现（`build/stray-ghost-repro3.ps1`、`build/stray-ghost-repro4.ps1`、`build/stray-ghost-repro5.ps1`）：对一个从未显示的 `WS_POPUP` + `DWMWA_SYSTEMBACKDROP_TYPE` 窗口逐项开关 owner/topmost、暗色属性、边框颜色、EDIT 子窗口、扩展样式切换、二次设置 backdrop，只有 `SetWindowDisplayAffinity(hwnd, WDA_NONE)` 一项会让隐藏窗口的亚克力背景出现在屏幕上，无论它在设置 backdrop 之前还是之后调用。
- 拆成两个持久 HWND 之前，同一 HWND 创建后很快显示；拆分后原生面板在折叠期间长期隐藏，才暴露了这个系统行为。

## 修复

- `src/clipboard/panel.cpp`：删除 `Create()` 中对原生面板与折叠条的两次 `SetWindowDisplayAffinity(..., WDA_NONE)` 调用。`WDA_NONE` 本就是默认值，剪贴板 UI 仍可被截图；注释记录了原因。
- 未改动其他调用 `SetWindowDisplayAffinity` 的窗口：录制选区/工作窗口使用 `WDA_EXCLUDEFROMCAPTURE` 且创建后即显示，共享下拉/提示框创建后立即显示，托盘菜单在显示前调用但随后立即 `ShowWindow`。它们没有长期隐藏的窗口，本次不做无根据的扩散修改。

## 测试

- `tests/clipboard_panel_test.cpp` `--appearance`：新增回归，把折叠期间隐藏的原生面板移到合成黑色背景内的空白处，`WindowFromPoint` 确认探测点属于夹具，`GetPixel` 必须为纯黑，随后移回 (0,0)。修复后输出 `HIDDEN probe ... = 0`，两处断言 PASS；原有 `GetWindowDisplayAffinity==WDA_NONE` 断言继续通过（`build/stray-ghost-appearance.log`）。
- 相关 CTest 6/6 通过（`build/stray-ghost-tests.log`，74.77 秒）：clipboard_panel、clipboard_liquid_motion、clipboard_resource_audit(20 轮)、clipboard_disable_memory、clipboard_appearance、clipboard_panel_pin。
- 构建：`build/stray-ghost-build.log`，C++20 /W4 /WX，0 warning/error。

## 交付

- 安装包：`dist/LumaShot-Setup.exe`，37554519 bytes，SHA-256 `90ADBB7326697807186E175B4B16FFDDB340BCBF8EB1FE6C0F839B75D21396BD`，MZ 头正常。
- `build/LumaShot.exe` 与 `dist/LumaShot-ghostfix-payload/LumaShot.exe` SHA-256 一致：`586EE7E8D6236A11E63CDAE75AFC6817A7D4B77839D75A413D15595DDC4F4814`。
- 上一版安装包备份：`dist/LumaShot-Setup-pre-ghostfix-20260922.exe`（37552041 bytes，SHA-256 `D1678FB33443DA034D738254D3C4CE7D6242C061B5AB05F8618EBCC0C29D3BC5`）。
- 校验回执：`build/stray-ghost-delivery.json`；打包日志：`build/stray-ghost-package.log`、`build/stray-ghost-installer.log`。
- 未运行安装器，未替换正在运行的已安装版本。本机复现日志：`build/stray-window-observation.json`、`build/stray-ghost-move.txt`、`build/stray-ghost-repro*.txt`。

## 边界

- 复现与回归都在当前机器的 DWM 上验证；这是 Windows 对隐藏 backdrop 窗口的行为，其他系统版本未逐一验证。
- 回归测试只覆盖剪贴板原生面板；不承诺所有隐藏窗口场景。
