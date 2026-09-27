# 截图工具栏新增录制入口

> 历史初版记录：下拉方案已被后续的独立录制图标替代。当前行为与安装包见 [独立录制图标](mcp-recording-icon.md)。

日期：2026-09-17

## 功能与范围

- 按“框选截图区域后出现的工具栏”实现入口；位置澄清被跳过后，已说明这一实现假设。
- 在 OCR 与取消按钮之间新增“录制”下拉按钮，菜单为“录制视频”和“录制 GIF”。
- 新建录制窗口时携带截图选区的桌面物理像素坐标，支持负坐标；直接进入准备界面，不启动倒计时、屏幕采集或编码。
- 沿用现有录制器的单显示器范围：跨屏选区裁剪到所选显示器，并在准备界面明确提示，可重新选择区域。显示器变化导致选区不可用时提示重新选择。
- 已有录制进程时只调起原窗口，不发送新的模式或选区设置，不重建进程。
- Esc、Tab、点击菜单外侧关闭菜单并保留截图；方向键、Home/End、Enter/Space 沿用现有菜单交互。
- 截图处理忙碌时、无有效选区或选区小于 8×8 时不允许进入；桌面贴图的图片标注界面不显示此入口。
- 先启动录制进程，再结束截图界面。录制组件缺失、CreateProcess 启动失败时显示错误并保留截图；这不等同于承诺处理所有启动后的工作进程崩溃。
- 没有修改 MP4/GIF 导出实现或编码参数，没有新增后台持续采集或渲染循环，保留之前的运行时瘦身。

## 实现

- `src/ui/toolbar.cpp`、`render.h`：录制按钮、加权动作布局及窄屏缩放约束。
- `src/ui/dropdown.cpp`、`toolbar_render.cpp`：复用现有下拉菜单、提示与主题绘制。
- `src/app/application.cpp`：入口交互、选区交接、贴图及忙碌保护、启动失败保留截图。
- `src/recording/launch_options.h`、`process.h`、`worker.cpp`：可选 `--region=left,top,right,bottom` 参数，严格整数/尺寸校验及准备态初始化。
- `src/recording/panel.cpp`：准备界面的选区提示行。
- 新增 `tests/recording_entry_test.cpp`，扩展工具栏与录制 UI 测试，注册 `screenshot_recording_entry` CTest。
- 使用本次读取的现有代码增量修改，未回退已有的设置进程、LumaText、贴图和运行时优化。

## 构建和验证

- `build.bat` 成功，C++20、`/W4 /WX`。最终构建仅重新编译修正后的测试确认器；生产程序与已通过回归的二进制一致。
- 3/3 核心 CTest：`recording_ui`、`unified_toolbar`、`screenshot_recording_entry`。
- 18/18 回归 CTest：`media_recording`、`shared_controls`、`selection_tools`、`pin_styles`、`capture_reselect`、`pin_menu`、`pin_annotation`、`selection_edit`、`text_edit`、`tool_preferences`、`settings_dialog`、`ocr_option`、`selected_property_controls`、`recording_coexist`、`gif_pipeline`、`recording_geometry`、`mp4_export`、`settings_capture_coexist`。
- 工具栏测试覆盖 144 组工具/组合、缩放输入和显示器宽度布局，包含负原点；检查按钮命中、顺序、边界和重叠。
- 实际入口测试覆盖鼠标视频跳转、键盘 GIF 跳转、菜单取消、忙碌/贴图/小选区保护、窗口复用与退出。
- 录制 UI 测试覆盖有符号坐标往返、非法/溢出参数、跨屏裁剪、准备态无采集会话/倒计时、GIF 默认帧率和选区边框排除采集。
- 隔离目录故意不放录制组件，`--missing-worker` 测试通过，确认错误提示后截图仍在。首次使用定时器自动确认的测试运行超时；改为仅查找本测试进程对话框的确认线程后通过，未修改生产失败处理或放宽断言。首次日志保留为 `build/capture-record-missing-worker-first.log`。
- 生成 49 张工具栏预览；检查浅色/深色录制菜单、8 种工具栏合集，以及视频/GIF 准备态和跨屏提示。使用合成场景，不以个人图片、偏好或剪贴板内容为测试素材。
- 包内录制工作进程及 DLL 的独立启动冒烟测试通过，包含两种入口与窗口复用；没有点击开始录制。
- 包中 8 个主要 EXE/DLL/媒体组件与当前构建 SHA-256 一致；运行时元数据哈希一致。便携 ZIP 全部 80 个文件的 CRC、大小和 SHA-256 与目录内容一致；独立 FFmpeg 源码 ZIP 的哈希与 CRC 通过。
- 核对 14 个本次修改文件的版本与已测试源码一致。编辑器诊断为 0 错误、0 警告。
- 这是针对性验证，不是完整测试集、安装/升级测试或长时间真实桌面录制测试。历史 `recording_window` / WGC 问题不在本次修复和复测范围内。

## 交付

安装包：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`

- 大小：37,357,987 字节，约 35.63 MiB。
- SHA-256：`6af702f1b39e3448e2a2af95f10f760e8b25b3454640acaba651c492a079d7f6`
- 载荷：80 个文件，83,645,309 字节。
- 已通过 `scripts/package.ps1 -PackageName LumaShot-setup-payload` 和 `scripts/build-installer.ps1` 构建，未运行安装程序或替换已安装应用。

主要程序 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| LumaShot.exe | `c07d0ba5158788661182fbd8933389b98936300cdc1c3695652995718320176a` |
| lumashot_recording_worker.exe | `a1d419d9c8f406a15d23e0cd067de413eb9ea65610552418cbe7819071826002` |
| ffmpeg.exe（保持瘦身版本） | `f0ae86e685f9167f171b0561f7909588d3f2e7e21e4cbf03ae30a51828471059` |
| onnxruntime.dll（保持 reduced 版本） | `f98dd40890987f103df3c0a8d135d64683d03e73a9e9d1764d395ccd9f85cbd5` |

单独的对应源码包保持不变：`dist/LumaShot-ffmpeg-source.zip`，29,783,172 字节，SHA-256 `615a16364f3bf9090bc322b8573f90f722ebc513e9c8c1982fd0482a58666952`。对外分发时继续一并提供。

## 证据

- 构建：`build/capture-record-build.log`、`build/capture-record-build-tests.log`、`build/capture-record-final-build.log`
- 测试：`build/capture-record-focused.log`、`build/capture-record-focused-details.log`、`build/capture-record-regression.log`、`build/capture-record-regression-details.log`
- 补充测试：`build/capture-record-missing-worker.log`、`build/capture-record-packaged-smoke.log`
- 视觉：`build/capture-record-toolbar/`、`build/recording-region-video.png`、`build/recording-region-gif.png`、`build/recording-region-clipped.png`
- 打包：`build/capture-record-package.log`、`build/capture-record-installer.log`
- 身份与内容核对：`build/capture-record-delivery.json`、`build/capture-record-delivery.log`、`build/capture-record-source-versions.json`
