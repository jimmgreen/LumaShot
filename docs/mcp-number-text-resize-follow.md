# 序号说明框缩放时联动序号

日期：2026-09-18

## 最新要求

调整“序号＋文本标签”的矩形说明框大小时，序号应跟随并保持相对位置。这一要求更新了上一轮 docs/mcp-number-text-group-move.md 中控制点只调整说明框、序号不动的行为。说明主体整体平移、双击编辑说明仍保留。

## 实现与位置规则

src/model/selection.cpp 的 Text/Detail 控制点缩放分支新增序号跟随：

- 以本次按下鼠标时的原始几何确定序号靠近说明框的哪个边角；序号中心相对说明框中心所在的左右/上下方向确定对应边角。
- 调整矩形时，序号平移该边角的坐标变化量。维持序号与这个边角的水平/垂直间距，不按说明框宽高比例放大间距，不改变序号自身尺寸或文字。
- 例如右上方序号跟随说明框右上角；若只调整不影响该边角的对侧边，则序号不必移动，原有间距自然保持。
- 当前手势内始终使用原始几何确定的边角，不在鼠标移动过程中切换依附边角。已有 preserve() 补偿整体旋转后，仍保持相应的世界坐标关系。
- 零位移点击直接返回原始标注，避免把自动推导说明框意外转成显式尺寸。
- Ctrl整体操作、说明主体组合平移、双击编辑、单步撤销/重做不变；其他序号组合的控制点行为不改。
- 不擅自重新吸附此前已经手动分开的位置；新缩放保持当前偏移。

## 验证

完整 build.bat 构建通过（/W4 /WX），最终增量构建及四组测试再次执行，全部 exit 0。编辑器诊断 error/warning 为0。

| 专项 | PASS | FAIL |
| --- | ---: | ---: |
| selected_properties --number-reconnect | 5712 | 0 |
| selection_edit | 52 | 0 |
| selected_properties --number-label | 999 | 0 |
| text_edit | 81 | 0 |

共6844条通过。新增矩阵覆盖四个依附边角、八个控制点、放大/缩小、Shift比例调整、0/30/-45度旋转、负坐标、零位移、连续缩放、自动推导说明框、序号尺寸不变、固定对侧控制点、撤销/重做。真实PointerDown/Move/Up回归覆盖100%/150%/200%控制点尺度，验证多事件手势只有一个撤销记录。

通过真实 Renderer 生成并读取查看 build/note-resize-preview/before.png、after.png：右上方“2”在矩形右上角向右上扩大后同步移动，保持原来的间距与大小。图片是合成暗色素材，不使用个人桌面或剪贴板。测试包含上一轮主体拖动以及移动后双击编辑说明的回归。

过程中曾遇到远程隧道502/530和PTY启动延迟，首次测试调用未取回完成状态；连接恢复后重新完成构建与四组测试，并明确核对全部退出码。没有把连接失败当作测试通过，也没有运行未验证的安装包。

## 交付

package.ps1 与 build-installer.ps1 成功，主程序、三个worker、onnxruntime.dll、lumatext.dll、ffmpeg.exe共七个二进制与构建目录逐项cmp一致。安装器脚本输出与独立sha256sum核对一致。

安装包：C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe

SHA-256：ef00ac1185f8d9361b04a569c06d3c574455335c0aa4328d0203cb425f0ebdbb

大小：37,374,502字节。未运行安装器，未替换已安装应用。之前的输入框主题实底、默认选择工具与半透明Tag输出等行为保留。

日志：note-resize-build.log、note-resize-build-final.log、note-resize-reconnect-tests.log、note-resize-selection-tests.log、note-resize-label-tests.log、note-resize-editor-tests.log、note-resize-package.log、note-resize-installer.log、note-resize-installer-sha.txt、note-resize-installer-size.txt。
