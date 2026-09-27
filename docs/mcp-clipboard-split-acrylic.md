# 剪贴板：液态侧边条与原生亚克力面板分离

日期：2026-09-22。

## 改动

- `src/clipboard/panel.cpp`：折叠条和展开面板使用两个持久 HWND。折叠条保留液态轮廓、贴边肩部、拉伸细颈和断开回弹；展开面板保留原生系统圆角与 Acrylic。
- 删除展开/收起的大面板液态形变以及 350ms 后开启 Acrylic、70ms 降低着色不透明度的材质切换。当前试用版直接展开和收起，不附加位移或淡化动画。
- 两个窗口各自保留 DirectComposition 目标，CPU 文本绘制路径继续使用 LumaText。准备目标窗口内容后再显示，后台窗口不处理陈旧的绘制、输入和激活通知。切换目标以不激活方式提升到 topmost 层，避免保留隐藏期间的旧 Z 序而被其他置顶窗口遮挡。
- 原生面板 HWND 同时作为稳定的异步消息、剪贴板监听、快捷键和计时器端点；搜索框始终属于原生面板。隐藏不会中断搜索变更、后台存储或快捷键消息。
- 折叠后的一次性空闲计时释放隐藏面板的合成资源，保留其 HWND 和材质配置。禁用时销毁两个窗口及全部合成资源；没有新增常驻渲染循环或桌面截图。

## 测试范围

- `tests/clipboard_liquid_motion_test.cpp`：继续验证四边、多档 DPI、细颈与回弹；新增两个 HWND 身份、原生面板无异形窗口区域、搜索父窗口、20 次展开收起复用、不主动激活以及禁用释放检查。
- `tests/clipboard_panel_test.cpp`：原有搜索、IME、固定、失焦、原生圆角与资源验证继续使用，旧大面板形变断言替换为独立窗口切换断言。
- 合成测试不使用个人剪贴板、个人偏好或个人文件作夹具。真实 DWM 截图限定在测试窗口覆盖的区域，不放宽原有截图范围及遮挡检查。

## 验证边界

本次消除的是应用主动触发的延迟材质切换，不承诺 Windows 在所有显卡、远程桌面、电源/透明设置下首次显示 Acrylic 都没有系统级过渡。最终手感仍需在用户实际桌面试用。旧 `scripts/preview-clipboard-liquid.ps1` 的完整展开形变合成预览属于旧渲染能力，不再代表生产展开行为；本次以独立窗口的原生外观与交互回归为准。

## 最终验证与交付

- `build/clipboard-split-build-final.log`：完整构建成功，C++20 /W4 /WX。
- `build/clipboard-split-tests-final.log`：相关 CTest **14/14 通过**（83.76 秒），包含 panel、width、header_drag、hotkey_policy、disable_memory、resource_audit（20 轮）、features、panel_pin、appearance、continuous_paste、liquid_motion、shortcut_focus、quick_input、quick_window。
- 已查看最终真实 DWM 合成夹具截图 `build/build/clipboard-acrylic-live-light.png` 和 `build/build/clipboard-split-expanded.png`，深浅主题内容、原生圆角及外缘正常。截图是稳态外观检查，不是对用户桌面展开全过程的录像验收。
- 早期检查发现搜索坐标夹具仍使用折叠窗口作为父坐标、隐藏窗口消息分流以及首次显示的旧 Z 序问题；均已修正。旧中间日志中的失败不代表上述最终回归结果。
- 安装包：`dist/LumaShot-Setup.exe`，**37549423 bytes**；安装器编译成功，检查了 MZ 头及 SHA-256。
- 安装包 SHA-256：`f01fe86bbf2fc887592bdb32ac5a2af8f15a81cb3b44076249f76a278f6efe5a`。
- `build/LumaShot.exe` 与 `dist/LumaShot-setup-payload/LumaShot.exe` 逐字节相同；两者 SHA-256：`5e80c04d444557d61fef055d93a918a3bd06dbdffb917b8e0beb332f7f1723d8`。
- 打包及校验日志：`build/clipboard-split-package.log`、`build/clipboard-split-installer.log`、`build/clipboard-split-installer-verify.log`、`build/clipboard-split-payload-verify.log`。
- **未运行安装器，未替换已安装或正在运行的版本。**
