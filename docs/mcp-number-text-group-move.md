# 序号文本标签：拖动说明主体时整体移动

日期：2026-09-18

## 问题与原因

用户反馈“序号”的文本标签组合中，拖动“双击输入说明”区域后说明与序号分离。HitMarkPart 正确地把说明命中为 Detail（双击编辑和控制点操作需要这一分类），但 EditMark 在无控制点的拖动分支中将 Detail 当作独立几何，只移动说明框及目标点，没有移动序号。

## 修复边界

仅修改 src/model/selection.cpp 的无控制点移动分支：当 mark.tool==Number、number_combo==Text 且 part==Detail 时，按鼠标的屏幕/图像坐标增量调用 Translate 移动整个 Mark，与现有引导线主体组合移动一致。

- 拖动说明正文、空说明占位区，会连同序号或“看这里”等自定义名称标签一起移动，无需按Ctrl。
- 保持当前相对布局、字体、颜色、旋转、说明框尺寸，以及推导/显式几何的存储形式，不在拖动过程中新建独立说明框。
- 命中分类仍为 Detail，因此双击仍进入说明编辑器，不会误打开序号名称输入框。
- 控制点缩放说明框、原有序号拖动/重连行为、引导线顶点、其他组合的编辑行为不改。
- 此修复防止说明主体拖动继续拆散组合；不会擅自把已手动摆放或此前已经分开的部件吸附回默认位置，整体平移保留其当前相对位置。
- 保留前一轮要求：输入框使用主题实底、已提交标注仍采用半透明Tag、每次新会话默认选择工具。

## 验证

build.bat 完整构建成功（/W4 /WX），编辑器错误/警告诊断为0。

| 专项 | 结果 |
| --- | --- |
| selected_properties --number-reconnect | 2063 PASS，0 FAIL |
| selection_edit | 52 PASS，0 FAIL |
| selected_properties --number-label | 999 PASS，0 FAIL |
| text_edit | 81 PASS，0 FAIL |

共3195条通过。新增覆盖：普通序号与自定义名称、空占位与正文、推导/显式说明框、负坐标、0/30/-45度旋转、多个拖动方向、零位移、100%/150%/200%控制点尺度、真实PointerDown/Move/Up多事件拖动、再次拖动、单次撤销/重做、独立调整说明框、拖动后双击并确认说明文本。

测试直接复现“看这里＋空说明占位区”，断言整个Mark等于对原对象进行同一增量的Translate。双击后确认编辑的是说明而不是名称，且提交不改变名称和序号位置。使用合成数据和原生测试窗口，没有读取个人桌面、文件或剪贴板作为素材；未运行无关的录制/OCR等测试。

## 交付

package.ps1 与 build-installer.ps1 均成功。主程序、三个worker、onnxruntime.dll、lumatext.dll、ffmpeg.exe共七个二进制与构建目录逐项cmp一致。

安装包：C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe

SHA-256：8e0670fed4e196310a5555e4c24cd42084e5d38cd303d3a9f3e181fd4f4323c1

大小：37,364,704字节。未运行安装程序，未替换已安装应用。

日志：text-group-build.log、text-group-reconnect-tests.log、text-group-selection-tests.log、text-group-label-tests.log、text-group-editor-tests.log、text-group-package.log、text-group-installer.log。
