# 连续直线与基础捕捉

后续已取消浮动文字提示，并新增中点/最近点捕捉，详见 [紧凑光标与捕捉改进](mcp-snap-refinement.md)。下文记录首版连续直线功能。

## 使用与约定

画笔属性板保留「自由画笔」「直线」，新增「连续直线」和「捕捉 开/关」。普通笔、荧光笔均适用。

- 连续直线：单击起点，再逐点点击追加线段；不按住鼠标时也预览下一段。
- 双击、Enter、右键：完成已确认线段，不复制截图，也不提交尚未点击的预览点。
- 双击所在点参与完成；重复点击同一点不新增零长度线段。
- Esc：取消本条未完成折线，保留截图和已有标注。没有在画折线时保留原 Esc 退出截图行为。
- Backspace、Ctrl+Z、工具栏撤销：绘制中退回一个顶点；完成后整条折线作为一个标注整体撤销/重做。
- 捕捉到首点并点击，可闭合折线；只有一个起点时完成或取消不产生空标注。
- 切换标注工具或轨迹模式会先保留已确认线段，放弃未确认预览；改变样式作用于当前折线。
- 刚完成折线后在画笔中选择其他轨迹，只改变后续绘制方式，不会把上一条折线压成一条线。需要转换已有标注时先按 V 明确选择。
- 连续直线模式下，点击已有标注位置优先绘制/连接；使用 V 进入选择工具编辑已有标注。
- Enter/Esc 的按键自动重复不继续触发截图复制或退出。

## 捕捉范围

捕捉默认开启，开关和连续直线模式随工具偏好保存，旧配置兼容。
捕捉半径为 8 个界面 DIP；方向辅助还要求偏差不超过 5 度，长度至少 12 DIP，避免短线跳动。

优先级：已有画笔/箭头的端点、连续折线的顶点（含当前折线首点）优先于水平、垂直和正反 45 度方向辅助。
旋转标注按屏幕上的实际端点捕捉。选择区外的端点不参与，捕捉不会把新线段放到选择区外。
未接近捕捉目标时保留自由方向，关闭捕捉时保留实际指针坐标；直线与连续直线共用捕捉。
拖动直线从捕捉端点出发时不会误选或拖走原标注。

捕捉位置显示方框和「端点 / 水平 / 垂直 / 45°」提示，辅助信息仅存在于编辑界面，不进入 PNG 或复制结果。
本次是基础捕捉，不含 CAD 中的中点、交点、网格、数值输入或尺寸约束。

## 实现

- src/model/line_snap.h、src/model/line_snap.cpp：独立可测试的端点/方向捕捉几何。
- src/app/pen_drawing.cpp：确认顶点、悬停预览、结束/取消及撤回顶点。
- src/app/application.cpp、src/app/application.h：指针事件、快捷键和导出生命周期接入。
- src/ui/line_snap_render.cpp：捕捉标记与 LumaText 文字提示，仅在 Chrome 层绘制。
- src/ui/render.cpp：折线不受自由画笔平滑度影响；沿已确认顶点绘制直线段。
- src/ui/toolbar.cpp：三个轨迹按钮、捕捉开关和自适应布局，草稿期间启用撤回点按钮。
- src/model/document.h、src/model/tool_properties.h、src/model/mark_properties.cpp、src/app/tool_preferences.cpp：模式/样式/偏好同步。

## 验证与交付

Windows Release 最终 build.bat 构建成功。最终 6/6 项针对性测试通过，4.16 秒：

- pen_polyline_snap：捕捉几何、实际指针/键盘消息、连续点确认、结束/取消、撤回、闭合、模式切换、整体移动、样式编辑、贴图标注、PNG 和可见捕捉反馈。
- pen_line_mode：上一版拖动直线行为与图像导出。
- tool_preferences：模式和捕捉偏好往返、无配置与非法值兼容。
- selected_mark_properties：标注属性映射。
- selected_property_controls：已有标注属性与选择/编辑行为。
- unified_toolbar：工具栏布局与控件回归。

捕捉测试包含 100%/150%/200% DPI、负坐标、端点优先级、旋转端点、方向阈值、任意角度、开关和选择范围。
实际渲染器生成的浅色/深色合成预览已检查；连续直线和捕捉按钮保持一行紧凑排列，窄屏按已有布局规则换行。
辅助标记仅在 Chrome 层；折线导出与无平滑的折线路径像素一致，PNG 往返像素一致。
最终编辑器诊断无错误或警告。

额外运行 annotation_render，3 项失败：七种便签预设对比度、混合背景可读性、标签文字颜色导出差异。
这与 docs/mcp-optimization-batch1-2026-09-18.md 和 docs/mcp-hand-arrow-terminal-alignment.md 中已有失败记录一致；本次未修改标签配色模块，未隐藏或修改这些断言。
扩展七项执行为 6 通过 / 1 失败，不能表述为全量回归通过。历史基线日志目前不存在，未声称本轮重新做了旧版本二进制对照。

最终日志：build/polyline-build-delivery.log、build/polyline-delivery-tests.log。
扩展检查含失败的原始日志：build/polyline-tests.log。
合成预览：build/polyline/visuals/light.png、build/polyline/visuals/dark.png、build/polyline/visuals/export.png。
专项入口为 tests/polyline_cases.h，由 lumashot_selected_properties_test --polyline 执行；CTest 名为 pen_polyline_snap。
所有交互和预览只使用合成画面，不读取个人桌面、设置或剪贴板作为测试素材。
验证使用程序内指针事件和合成渲染，不替代用户真实桌面、多显示器上的人工操作验收。

## 安装包

已生成 dist/LumaShot-Setup.exe，37,524,060 字节；Inno Setup 编译成功，23.140 秒。
安装包 SHA-256：`ef5fbea4a082648316a859ffc6db99621091c942d621fbb112510d84b38dbf91`。
主程序 SHA-256：`1a7aaa7cf41040df764b847f318bebf9eec47fbc3477ae65b5ccff2ead7329b8`。
dist/LumaShot-setup-payload/ 中主程序、录制、OCR 和控件识别工作进程均与 build/ 最新产物逐字节一致。
打包及安装器日志：build/polyline-package.log、build/polyline-installer.log。
未运行安装器，未替换已安装应用；未进行安装/升级过程验收。
