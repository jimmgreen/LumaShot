# 画笔属性板直线模式

> 本文记录首版直线模式。后续已加入可关闭的方向/端点捕捉及连续直线，当前行为与交付见 docs/mcp-pen-polyline-snap.md。

## 需求与行为

在画笔属性板直接选择「自由画笔 / 直线」，不再使用 Shift 切换画笔轨迹。
直线按实际拖动方向连接起点与终点，不吸附 45 度；松手位置参与最后一次更新。
轨迹与普通笔 / 荧光笔独立，颜色、线宽、透明度保持现有行为。
直线模式禁用无意义的平滑度控件，切回自由画笔后恢复原平滑度。
画笔轨迹模式随工具偏好保存；旧配置或非法值默认自由画笔。

选中已有画笔标注时，点击「直线」会将其拉直到首尾端点，旋转后的屏幕端点位置保持不变。
该操作可用 Ctrl+Z 恢复完整原始曲线。切回「自由画笔」不会自动恢复被拉直的轨迹；需要撤销。
单击保留原普通笔圆点 / 荧光笔方点语义；直线终点限制在截图选区内。

## 实现范围

- src/model/document.h、src/model/tool_properties.h：增加 pen_straight，默认 false。
- src/app/tool_preferences.cpp：保存和校验轨迹偏好。
- src/ui/toolbar.cpp：属性按钮 83 / 84、选择态、自适应布局和平滑控件可用性。
- src/app/application.cpp：属性命令、提示、按固定轨迹模式更新草稿；移除画笔的 Shift 临时吸附分支。
- src/model/mark_properties.cpp：选中标注属性同步、首尾拉直、旋转中心补偿。
- tests/pen_line_cases.h、tests/selected_properties_test.cpp：真实 PointerDown/Move/Up 路径的专项入口 --pen-line。
- tests/tool_preferences_test.cpp：偏好读写及非法值回退。

不修改 Pulse，不读取个人素材、桌面内容或剪贴板作为测试输入，不运行安装程序。

## 验证

Windows Release 构建成功（build.bat），最终 4/4 相关测试通过（0.80 秒）：

- pen_line_mode：画笔直线实际指针事件与属性板专项。
- tool_preferences：包含新增模式的偏好持久化与非法值回退。
- selected_mark_properties：标注属性映射回归。
- unified_toolbar：工具栏布局与控件回归。

命令：从 build/ 运行 `ctest -R "^(pen_line_mode|tool_preferences|selected_mark_properties|unified_toolbar)$" --output-on-failure`。
专项已注册在 CMakeLists.txt；未运行无关的旧回归测试。
首次专项发现测试错误假设 Undo 保留选择，已根据现有 Document 语义改为撤销后重新选择，再验证模式同步；原曲线几何恢复检查始终保留。

专项覆盖普通笔/荧光笔、100%/150%/200% DPI、任意角度、水平/垂直/反向/零长度、
预览与松手端点、裁剪、自由画笔切换、撤销/重做、旋转标注转换、PNG 像素回读、窄屏按钮命中。
合成预览保存在 build/pen-line/visuals/，深浅主题工具栏、普通笔与荧光笔导出均已进行图像检查。
日志：build/pen-line-build.log、build/pen-line-build-final.log、build/pen-line-tests.log。
本次验证使用合成内容和程序内指针事件，不等同于用户实际桌面上的手工操作验收。

## 安装包

已构建 dist/LumaShot-Setup.exe，37,519,133 字节。Inno Setup 编译成功，耗时 22 秒。
安装包 SHA-256：`48ee2b394a86971f7d4e23893691153f3bfa0510336e81a93c2cd1bf7889e3f9`。
主程序 SHA-256：`5661c196b86fd35354583a5f33723ab1c714d8aa1bb6bb9c4bdd5fe8d81c81e8`。
打包载荷 dist/LumaShot-setup-payload/ 中的主程序与录制、OCR、控件识别三个工作进程均与 build/ 二进制逐字节一致。
打包与安装器日志为 build/pen-line-package.log、build/pen-line-installer.log。
最终 IDE 诊断未报告错误或警告。验证范围为编译、相关自动化/合成视觉检查、打包载荷一致性及安装包哈希；未运行安装器或替换用户现有应用。
