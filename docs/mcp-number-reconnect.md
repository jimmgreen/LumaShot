# 序号再次编辑：组合框和引导线重连

日期：2026-09-17

## 问题及修复

用户反馈：移动已有序号后，组合引导线留在旧位置，与序号分离；组合框也需要重新连接。

原因：`EditMark` 对单个组件移动/缩放时，无条件把推导出的引导线或框固化为 `number_leader` / `number_detail`。随后只更新数字标记位置，附件继续使用旧坐标。

本次生产代码仅修改 `src/model/selection.cpp`：

- 移动或缩放 Badge 时，不再把自动组合几何固化；保留自动重新计算。
- 对已经手动调整或由旧版本固化的引导线，更新起点及首段。保留远端折点和目标点；左右换位后连接侧随目标方向调整。
- 虚线框、高亮框更新靠近序号的角，另一端的目标角保留；文字说明框重新靠齐序号，显式调整过的宽高保持不变。
- 零移动返回原对象，不因选择操作修改几何；继续使用现有旋转补偿、整体移动及撤销机制。
- 仍允许独立调整线条折点或附件。此修复针对随后移动/缩放序号时的重连，不强制取消独立编辑能力。
- 旧会话内已分离的可编辑标注，在下一次拖动序号时也会重新连接；不是从已扁平化的 PNG 恢复编辑对象。

保留之前的独立录制图标、设置及贴图功能。本次没有修改录制/MP4/GIF/OCR 实现。

## 仅相关验证

新增 `tests/number_reconnect_cases.h`，通过 `lumashot_selected_properties_test.exe --number-reconnect` 专项模式执行，提前返回，不运行该程序的旧属性测试集。

先在未修复生产代码上运行同一专项用例，复现断开问题：345 个断言通过、370 个失败。修复后同样的 715 个专项断言全部通过。这里是一个专项模式中的断言计数，不是重新运行 715 项旧测试。

覆盖：

- 自动和显式几何的引导线、虚线框、高亮框、文字说明框。
- 不同移动方向、跨到目标另一侧、负坐标、0°/30°/-45° 旋转及八个缩放控制点。
- 目标点的世界坐标保持、远端手动折点保持、说明框自定义尺寸保持。
- 单次点击不改对象、连续移动形成一个撤销步骤、重做和第二次拖动。
- 真实 `Application::PointerDown/PointerMove/PointerUp` 编辑路径。
- 贴图编辑的缩放与负原点坐标往返、旧版分离对象的修复。
- 合成场景的导出图：分离前后引导线、虚线框、高亮框、文字说明框，共五张；已检查视觉连接。

另将 `tests/selection_edit_test.cpp` 中“移动序号仍固定整个框”的旧断言更新为新的连接语义，但本次没有运行其完整旧测试集。没有重跑录制、设置、OCR 等无关回归。

`build.bat` 最终成功，`/W4 /WX`；中间遇到 Windows `far` 宏与局部变量重名，已改名并重新构建。最终编辑器诊断为 0 错误、0 警告。

## 安装包

- 路径：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- 大小：37,358,475 字节，约 35.63 MiB。
- SHA-256：`e52e7b7b0a69fdef326d7b11886cac179b03a7cdd53647872ad6ec7ba3fec5b2`
- 主程序 SHA-256：`162b3d107f90cd37942d3a8bd422eaac53a4762b651acb56c6bf2fcfc2297043`
- 包内主要组件与构建目录 SHA-256 一致，运行时元数据一致，便携 ZIP 全部 80 个文件的 CRC、大小及 SHA-256 核对通过。
- FFmpeg 和 reduced OCR DLL 保持原瘦身版本；独立 FFmpeg 对应源码 ZIP 的哈希及 CRC 通过。
- 未运行安装程序、未自动安装，也未执行安装升级测试。

## 证据

- `build/number-reconnect-baseline-build.log`、`build/number-reconnect-before.log`
- `build/number-reconnect-final-build.log`、`build/number-reconnect-after.log`、`build/number-reconnect-tests.json`
- `build/number-reconnect-preview/`
- `build/number-reconnect-package.log`、`build/number-reconnect-installer.log`
- `build/number-reconnect-delivery.json`、`build/number-reconnect-delivery.log`
