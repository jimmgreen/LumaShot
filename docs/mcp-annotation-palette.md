# 六色标注预设与同色系文字配色

日期：2026-09-17

## 范围

按用户确认，仅保留六个代表色：玫红、朱橙、金黄、绿色、蓝色（青蓝色系代表）和紫色。覆盖属性栏中的线条、画笔、荧光笔、填充、文字及序号预设。工具栏面板本身的主题背景和整体布局算法不改。

色值依次为 #E0529C、#E87040、#E8B339、#6ABE39、#3B85E1、#9859D6。各色板采用相同顺序，自定义取色器保留。没有加入十一种近似色。

## 实现

- 新增 src/model/annotation_palette.h，统一六色常量及生成配色逻辑；使用相对亮度与对比度计算，将文字调至相同色系的可读亮度，目标对比度至少4.5:1。
- src/ui/toolbar.cpp 和 src/app/application.cpp 统一六色色块及点击处理；第六个填充色使用独立属性ID82。荧光笔也不再使用单独的荧光黄/粉/青色板。
- src/model/note_color.h 增加普通文字自动背景处理，并复用同色系配色处理说明文字。深浅背景根据截图区域的抽样亮度判断，不是根据工具栏主题判断。
- 普通文字背景选项为无背景、自动配色、同色浅底、同色深底。色板在带背景模式下选择配色色系，无背景时保留选定文字颜色。
- 序号/胶囊标注改用同色系浅底与高对比文字，不再使用固定白字或固定深色字。描边序号保留无填充的描边样式。
- 说明文字默认自动；明确自定义文字颜色保持原色，必要时调整配对底色，而不是强制覆盖自定义墨色。
- 普通文字旧背景枚举1/2/3和旧说明文字预设索引仍可读取，不重新解释成新的颜色。新背景枚举为4/5/6，持久化校验已扩展。没有重置用户个人偏好，也没有读取用户个人配置作为测试素材。
- src/app/text_edit.cpp 让普通文字的自动背景编辑预览与输出配色一致，同时保持原始色系值存储，避免打开编辑器后颜色逐次漂移。

## 验证

- build.bat 完整构建成功（/W4 /WX）。编辑器诊断无错误或警告。
- --palette 新专项：481个断言通过，0失败。覆盖六色映射、深浅底对比度、黑白灰/高亮颜色边界、负原点、普通文字和说明文字、保留自定义色，以及400/800/1320宽度与高DPI下的属性栏水平边界。
- lumashot_tool_preferences_test.exe：10个断言通过，0失败，包含新增三种背景模式的保存/读取。
- --number-label 专项999个断言通过，0失败，覆盖序号和自定义标签相关行为。未运行录制、OCR、设置等无关旧测试套件。
- 从真实渲染器生成并读取检查 build/palette-preview/light.png、dark.png、toolbar.png。六色和自动文字/说明背景输出正常；标准宽度矩形属性栏仍为一排。
- 预览使用合成背景，不使用个人桌面或剪贴板内容。

## 交付

scripts/package.ps1 和 scripts/build-installer.ps1 均成功。完整安装包包含此前引导线连接修复，没有回退该逻辑。
主程序、三个worker、onnxruntime.dll、lumatext.dll、ffmpeg.exe共七个二进制的打包目录SHA-256与构建目录逐项一致；校验结果见build/palette-delivery.json。安装包大小37,364,881字节。

安装包路径：dist/LumaShot-Setup.exe

SHA-256：2EBD485F77936FD1E8BF53377EDC0CA6D1455DC6BDAD4E6CBB23C84AEF4F6D36

未运行安装程序，未替换已安装应用。

日志与产物：build/palette-final-build.log、build/palette-tests.log、build/palette-preferences.log、build/palette-labels.log、build/palette-package.log、build/palette-installer.log、build/palette-preview/。
