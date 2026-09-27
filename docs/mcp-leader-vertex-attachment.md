# 引导线控制点移动断连修复

日期：2026-09-17

## 根因与更正

用户提供最新版仍断连的截图后，重新审查控制点路径。此前把问题仅归因于已安装旧版是不完整的。已有线身组合移动修复没有覆盖控制点 20–23：src/model/selection.cpp 的 EditMark 直接更新折点，控制点 20 可脱离序号，而 NumberLeaderPoints 会直接使用这份显式几何。

## 修复

- 生产代码只改 src/model/selection.cpp。
- 拖动连接端控制点 20 复用 Badge 移动逻辑，让序号跟随移动、目标保持世界坐标，保留旋转补偿和重连。
- 拖动折点 21/22 或目标端 23 时保留其编辑结果，重新计算贴近序号的起点，目标跨左右侧后也保持连接。
- 控制点没有实际移动时原样返回，避免点击固化自动几何。
- 已有线身整体移动和折点编辑功能保留。

## 验证

新增 tests/number_reconnect_cases.h 控制点模型用例，首次在未修复生产代码上运行得到28个失败断言（连接断开6、序号未随连接端移动6、零移动改数据16）。随后修复，再扩展三个移动方向与跨侧情况。

最终 --number-reconnect 专项模式1253个断言通过、0失败：自动/显式几何、四控制点、0/30/-45度旋转、跨侧移动、撤销重做、零移动以及 tests/selected_properties_test.cpp 中 Application::PointerDown/PointerMove/PointerUp 的合成鼠标路径。没有把这些断言称作1253项独立测试，也没有运行无关旧套件。

build.bat 成功，编辑器诊断为空。合成渲染 build/number-reconnect-preview/vertex-after.png 已读取检查：序号与引导线保持连接，保留原有小间距样式。

## 交付

重新执行 scripts/package.ps1 与 scripts/build-installer.ps1，均成功。打包前比较 build 和 dist/LumaShot-setup-payload 中主程序、三个 worker、onnxruntime.dll、lumatext.dll 的SHA-256，全部一致。

安装包：dist/LumaShot-Setup.exe

SHA-256：21085B61539B65729241BCD980BE05515B3AA5841315D8B988A45A209CA7D171

未运行安装程序，未替换已安装应用。验证为本机构建和合成回归，不冒充用户安装后的实测。

日志：build/leader-vertex-before.log、build/leader-vertex-final-build.log、build/leader-vertex-final.log、build/leader-vertex-package.log、build/leader-vertex-installer.log。
