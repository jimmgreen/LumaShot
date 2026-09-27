# 截图工具栏：独立录制图标

日期：2026-09-17。此版本替代此前的录制下拉方案。

## 改动

- 截图工具栏只保留一个摄像机形状的录制图标，与其他动作图标同宽；没有文字按钮、下拉箭头或 GIF 图标。悬停提示“录制”。
- 一次点击直接进入视频录制准备界面，沿用当前选区，不自动开始采集。GIF 切换仍在原录制界面内。
- 保留已有录制窗口复用、贴图标注隐藏入口、忙碌/无效选区保护，以及录制组件缺失时保留截图的行为。
- 移除录制下拉菜单及其选择分支，缩短动作区；没有修改录制工作进程、选区参数协议和 MP4/GIF 编码实现。
- 按用户要求，将“只运行与当前改动相关的回归测试，不默认重跑无关旧测试集”写入 AGENTS.md。

## 本次验证范围

- `build.bat` 增量构建成功；构建产生的旧测试可执行文件没有因此自动运行。
- 仅运行 `lumashot_toolbar_test.exe --record-icon`：单图标控件、无下拉、单槽宽度、命中位置、DPI/负原点、OCR 可用性和禁用条件。专项模式提前返回，没有执行后面的旧属性测试或完整预览图库。
- 仅运行更新后的 `lumashot_recording_entry_test.exe`：一次点击打开准备界面、无菜单、截图释放、已有窗口复用及受限状态保护。
- 在不放置录制组件的隔离目录运行同一入口测试的 `--missing-worker` 分支，确认失败后截图仍保留。
- 生成四张相关预览，检查浅色、深色下的图标及悬停提示。测试均使用合成界面。
- 打包后仅再次验证包内录制工作进程的入口启动；没有重跑上次的 21 项回归，也没有运行 MP4、GIF、OCR 或设置旧回归集。
- 校验包内主要组件与本次构建的 SHA-256、运行时元数据，以及便携 ZIP 全部 80 个文件的 CRC/大小/SHA-256。FFmpeg 和 reduced OCR DLL 哈希不变。
- 编辑器诊断 0 错误、0 警告。未执行安装或升级测试，未自动安装。

## 新安装包

路径：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`

大小：37,353,082 字节，约 35.62 MiB。

SHA-256：`00f3f0416c2c14ae3d49592d211bea0fb0d0437fd7f9a2bb07684225cae96a00`

证据：`build/record-icon-build.log`、`build/record-icon-toolbar.log`、`build/record-icon-entry.log`、`build/record-icon-missing-worker.log`、`build/record-icon-toolbar/`、`build/record-icon-package.log`、`build/record-icon-installer.log`、`build/record-icon-packaged-smoke.log`、`build/record-icon-delivery.json`。
