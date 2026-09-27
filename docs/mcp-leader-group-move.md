# 引导线线身拖动：与序号组合移动

日期：2026-09-17

## 问题及修复

用户反馈：二次编辑移动引导线时，引导线与序号分离，没有按组合处理。

原因：`EditMark` 中 `EditPart::Leader` 的线身拖动（handle<0）只平移四个折点并固化 `number_leader`，序号徽标 `a/b` 保持不动，两者随即分离。

本次生产代码仅修改 `src/model/selection.cpp`：

- 拖动引导线线身时，改为对整个标注调用 `Translate`，序号、引导线和目标点作为一个组合整体移动，几何保持既有的连接状态。
- 折点手柄（20–23）仍可独立编辑线形；拖动序号徽标时的重连逻辑（`ReconnectNumberBadge`）不变。
- 移除了原来仅平移折点并固化几何的 `EditPart::Leader` 分支。

## 仅相关验证

在 `tests/number_reconnect_cases.h` 增加线身拖动用例：自动/自定义几何 × 0°/30°/-45° 旋转 × 三个移动方向，断言整组平移、保持连接、一步撤销与重做。

`lumashot_selected_properties_test.exe --number-reconnect` 专项模式共 787 个断言全部通过、0 失败（含既有重连用例）。没有重跑无关旧测试集。

`build.bat` 成功（/W4 /WX），编辑器诊断 0 错误、0 警告。

## 安装包

- 路径：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- 大小：37,360,644 字节，约 35.63 MiB。
- SHA-256：`9C9F81374C916D8FCDBC655615DB85A263427D9B456AE5016C4D1B770CF64222`
- 由 `scripts/build-installer.ps1` 打包，退出码 0；未运行安装程序，未替换已安装应用。
