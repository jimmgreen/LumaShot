# 虚线序号组合：拖动框体时序号跟随

日期：2026-09-18

## 问题与原因

用户反馈虚线序号组合的框体被拖动后，序号停留在原位。

框体命中为 `EditPart::Detail`。`src/model/selection.cpp` 的 `EditMark` 原先仅将文本标签的 Detail 拖动作为组合平移，虚线框落入独立细节移动分支，导致仅更新 `number_detail` / `number_target`，没有移动序号徽章。

新增合成回归在修改生产代码前明确复现该问题，并保存分离的实际渲染结果。用户上传图片仅用于理解反馈，未作为永久测试夹具。

## 修复规则

- 将虚线框 Detail 纳入既有联动编辑路径。
- 拖动框体：序号、框体和目标点按同一物理像素位移整体移动；连续多次拖动仍保持相对位置。
- 调整框体大小：序号跟随原先所在的角，保留原有角位偏移和序号大小，不跟着框体缩放。
- 无位移的点击不固化推导几何、不产生额外撤销记录。
- 保持旋转、100%/150%/200% DPI、负坐标、贴图编辑转换及一次手势一次撤销/重做。
- 直接拖动序号时原有的连接重算行为不变；文本标签、引导线、普通图形及上次的即时属性应用逻辑保留。

## 文件

- `src/model/selection.cpp`：将原文本细节判断推广为文本/虚线框的联动细节判断，复用组合平移和角位保持逻辑。
- `tests/dashed_box_cases.h`：新增模型、真实 PointerDown/Move/Up 路径和渲染回归。
- `tests/selected_properties_test.cpp`：在 `--number-reconnect` 入口执行新增用例。
- `tests/selection_edit_test.cpp`：旧断言明确要求“框移动而序号不移动”，与本次需求相反；更新为对完整组合平移结果的严格比较。

未修改其他序号组合的独立细节编辑语义、录制模块或用户设置。

## 验证结果

- Windows Release 构建成功。
- `lumashot_selected_properties_test.exe --number-reconnect`：11,593 个通过断言，无失败。包含新增虚线框及已有序号、引导线、文本标签连接回归。
- `lumashot_selected_properties_test.exe --number-label`：999 个通过断言，无失败。
- 最终针对性 CTest：4/4 通过，0.71 秒：`pin_annotation`、`selection_edit`、`selected_mark_properties`、`selected_property_controls`。
- 源码及测试诊断：0 个错误/警告。
- 已检查生产 Renderer 的合成输出：修复前框体与徽章分离；修复后拖动和左上角缩放均保持徽章贴合角位。

覆盖四种角位、推导/显式框体、普通序号/自定义标签、0°/30°/-45°旋转、八个缩放控制点、Shift 等比缩放、负坐标、三种 DPI、重复指针事件、直接选择、撤销/重做和贴图缩放保存转换。

### 复现命令

```text
build.bat
build/lumashot_selected_properties_test.exe --number-reconnect
build/lumashot_selected_properties_test.exe --number-label
ctest --test-dir build -R "^(selection_edit|selected_mark_properties|selected_property_controls|pin_annotation)$" --output-on-failure --timeout 30
```

证据目录：`build/dashed-box/`。

- `before/`、`baseline-build.log`、`baseline-tests.log`：修改前备份、编译及失败复现。
- `number-reconnect.log`、`number-label.log`：修复后专用回归。
- `release-build.log`、`release-tests.log`：最终构建与 CTest。
- `before/visuals/dragged.png`：旧逻辑拖动后分离。
- `visuals/dragged.png`、`visuals/resized.png`：修复后的拖动和缩放输出。
- `package.log`、`installer.log`、`package-verification.json`：发布校验。

## 安装包

- 路径：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- 大小：37,374,374 字节。
- SHA256：`6705b39b069a1b3780576b53aa0c689cec45565314f604693066fbc9dca9cdaa`
- 打包及 Inno Setup 编译成功；安装包 MZ/PE 头验证通过，8 个发布载荷文件的 SHA256 与当前构建逐项一致。
- 签名状态：`NotSigned`。未安装，未执行安装/升级测试。

未执行安装或升级操作；未用用户个人文件、首选项或剪贴板作为测试输入。未运行无关历史测试集。
