# 半透明 Tag 文字样式实施与交付

日期：2026-09-18

## 已实施

- 保留玫红、朱橙、金黄、绿色、蓝色、紫色六个主题色及自定义取色器，不改画笔/荧光笔/图形的色板和填充规则。
- 新增局部外观模块 src/model/tag_appearance.h。浅色底图填充 alpha=31/255（约12%），深色 alpha=46/255（约18%）；同色系弱边框分别71/255和82/255，文字保持不透明。不是将整个标签降低透明度。
- 普通文字新增半透明自动/浅色/深色模式。背景采用内描边、小圆角和随字号/DPI缩放的内边距；自动尺寸文本按实际内容收紧，多行共享一个底框。测量和点击范围包含内边距，手动文本框不擅自重置尺寸。
- 序号说明、序号/自定义胶囊采用相同的半透明配色规则，按截图局部适配深浅，不再固定画浅色胶囊。保留圆、胶囊等既有轮廓语义和点击几何；小圆角主要用于普通文字和说明底框。
- 说明文字去掉旧的预填色叠加，再画一次底色和弱边框，避免两次透明填充累积变浓；说明文字也统一使用 TextRenderer，与普通文字的字体回退/栅格化路径一致。
- 采样包含截图负原点和标注旋转。自动文字对比度按背景合成后的像素估算，考察低分位数而非只比较平均色。自定义说明墨色保持精确，不再用不透明黑白背景兜底。
- 原生文字/说明编辑器保留 EDIT、IME、光标和键盘操作。新增 src/ui/tag_edit_backdrop.h，以底图及前序标注的合成图生成原生图案画刷，避免直接丢弃 alpha 后显示成饱和实色。输入文字不透明；内容修改/高度变化时刷新预览并共享提交时的墨色解析规则。工具编辑面板本身仍沿用应用主题，独立的自定义序号名称输入面板没有改成透明窗口。
- 未全局修改 MixNoteColor/AnnotationTones，因此原有实色背景及无背景样式仍可渲染。文本背景菜单额外保留明确的“增强·浅实底/深实底”选项。

## 兼容与偏好

TextBackground 保留0–6原值，新样式使用7/8/9，不重新解释旧文档/Mark的枚举。保存的工具偏好新增 tag_style_version=1；只对未带版本的工具栏同色浅/深/自动偏好迁移到对应半透明模式。原来明确选择的无背景以及白/黑/黄旧固定背景不被强行切换。新版显式保存的增强实底不会再次迁移。

## 验证

完整 build.bat 构建成功，/W4 /WX；编辑器诊断 error/warning 为0。早期新增测试遇到 WPARAM 有符号转换和 Windows near 宏命名冲突，均已修复并重新构建。

| 专项 | 结果 |
| --- | --- |
| lumashot_selected_properties_test.exe --palette | 683 PASS，0 FAIL |
| lumashot_text_edit_test.exe | 62 PASS，0 FAIL |
| lumashot_tool_preferences_test.exe | 35 PASS，0 FAIL |
| lumashot_selected_properties_test.exe --number-label | 999 PASS，0 FAIL |

共1779条通过的断言。覆盖六色、真实 source-over 像素比对、负原点、浅/深/中灰背景、纹理透出、说明单次填底、自定义墨色、旧枚举、新/旧工具偏好、多行文本、100%/150%/200%几何、重复测量、选中范围、重开编辑、取消/确认、自动收紧、原生控件纹理预览及模拟IME消息处理。没有执行与本次变更无关的录制/OCR等套件，没有使用个人文件或剪贴板作为素材。

额外执行的 lumashot_mark_properties_test.exe 未全通过：6条序号缩放/几何断言失败（日志另有1条FAIL汇总行）。为定位是否本轮引入，在 build/tag-reference 中使用本轮修改前读取的 mark_properties.cpp、text_bounds.cpp 构建独立属性对照可执行文件，与同一测试对象及现有依赖链接，得到完全相同的6条失败；两个日志 cmp 一致。本轮没有改动其序号重连/缩放分支，也未为让旧测试通过而修改这些断言。该对照是属性实现级验证，不声称重新构建了整个历史版本。

## 实际渲染预览

已从真实 Renderer/原生输入窗口生成并读取检查：

- build/tag-preview/light.png
- build/tag-preview/dark.png
- build/tag-preview/texture.png
- build/tag-preview/gradient.png
- build/tag-preview/editor.png
- build/tag-preview/toolbar.png（生成用于色板回归；本轮主要视觉检查集中在前五张）

这些是合成浅色、深色、棋盘和亮度渐变素材，不是用户桌面截图，也不是AI绘图。纹理图可清楚看到背景穿过色底；原生输入框同样保留纹理，字没有整体褪色。

限制：任意高反差照片/纹理无法靠固定12%/18%透明底保证每个字下方都达到4.5:1。此时应手动选择更合适的颜色，或对普通文字使用明确的增强实底；不会悄悄改成不透明黑白块。输入面板使用原生GDI字体和工具面板布局，非承诺与导出逐像素完全相同；合成底色及自动墨色使用共同规则。未宣称完成真实输入法候选窗的人工验收或任意照片的普适可读性验证。

## 交付

scripts/package.ps1 -PackageName LumaShot-setup-payload 与 scripts/build-installer.ps1 均成功。主程序、三个worker、onnxruntime.dll、lumatext.dll、ffmpeg.exe共七个二进制与构建目录逐项cmp一致。

安装包：C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe

SHA-256：9ad16a20ef84d02347e00c20bb01b2379bf8b62866c5d12ca8425932545de358

大小：37,374,709字节。未运行安装程序，未替换已安装应用。保留此前已完成的贴图缩放/Esc关闭及工具记忆等改动。

日志：tag-style-build-final.log、tag-style-palette-tests.log、tag-style-editor-tests.log、tag-style-preferences-tests.log、tag-style-label-tests.log、tag-style-properties-tests.log、tag-style-reference-build.log、tag-style-reference-tests.log、tag-style-package.log、tag-style-installer.log。
