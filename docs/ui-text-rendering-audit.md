# 全界面文字渲染审计

日期：2026-09-20。范围：src 下的自有窗口、绘制入口、原生控件及系统对话框调用。通过源码调用链确认实际绘制后端，不把链接 lumatext.dll、创建 DirectWrite 字体或仅调用文字测量接口视为已经接入。此表是代码审计，不等同于所有界面的逐帧视觉验证。

## 尚未接入的自有界面

| 界面 | 实际绘制方式 | 证据 |
| --- | --- | --- |
| 设置窗口：标题、说明、快捷键录入显示、按钮、主题选项 | DirectWrite 排版后直接调用 Direct2D DrawText；原生 owner-draw 按钮也回到同一绘制函数 | [settings_dialog.cpp](C:/Users/SS/Desktop/LumaShot/src/app/settings_dialog.cpp:136) |
| 贴图右键菜单：操作项、状态说明 | Direct2D DrawText | [pin_menu.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/pin_menu.cpp:22) |
| 贴图缩放百分比浮层 | Direct2D DrawText | [pin.cpp](C:/Users/SS/Desktop/LumaShot/src/pin/pin.cpp:204) |
| 文字编辑浮层底部 Enter / Shift+Enter / Esc 提示 | Direct2D DrawTextW | [text_editor.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/text_editor.cpp:75) |
| 录屏/窗口选择覆盖层的操作提示 | GDI TextOutW | [worker.cpp](C:/Users/SS/Desktop/LumaShot/src/recording/worker.cpp:89) |
| 截图文字编辑、自定义编号标签的输入正文 | 可见的 Windows EDIT 控件；GDI/native 控件绘制，未由 LumaText 接管 | [text_edit.cpp](C:/Users/SS/Desktop/LumaShot/src/app/text_edit.cpp:88) |

共 5 类自绘文字入口未迁移，另有 1 类自有编辑正文仍使用原生控件绘字。

## 已接入 LumaText 的界面

| 界面 | 调用链 / 证据 |
| --- | --- |
| 截图选区尺寸、状态提示 | Renderer::Text → TextRenderer::Draw → lt_frame_draw_layout；[render.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/render.cpp:52) |
| 截图工具栏、属性栏、工具提示 | PaintContext.text → Renderer::Text；[toolbar_render.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/toolbar_render.cpp:9) |
| 自定义颜色面板，HEX/RGB 字段显示及提示 | Renderer::Text；[color_picker_render.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/color_picker_render.cpp:10) |
| 截图内下拉菜单、独立下拉弹窗 | Renderer::Text / TextRenderer::Draw；[dropdown_window.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/dropdown_window.cpp:24) |
| 文字标注、编号和自定义标签的完成态/导出 | TextRenderer::Draw；[render.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/render.cpp:154)、[number_render.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/number_render.cpp:45) |
| 托盘右键菜单 | TextRenderer::Draw；[tray_menu.cpp](C:/Users/SS/Desktop/LumaShot/src/ui/tray_menu.cpp:59) |
| 贴图 OCR 选中文字的操作工具条 | TextRenderer::Draw；[selection_tools.cpp](C:/Users/SS/Desktop/LumaShot/src/pin/selection_tools.cpp:75) |
| 录屏控制、预览、导出进度等面板文字 | Painter::Label / PaintContext.text → TextRenderer::Draw；[panel.cpp](C:/Users/SS/Desktop/LumaShot/src/recording/panel.cpp:20) |
| 自定义主题确认、提示弹窗 | TextRenderer::Draw；[themed_message.h](C:/Users/SS/Desktop/LumaShot/src/ui/themed_message.h:50) |
| 剪贴板主面板、折叠计数、分类、菜单、状态、记录摘要 | TextRenderer::Draw / DrawLayout；[panel.cpp](C:/Users/SS/Desktop/LumaShot/src/clipboard/panel.cpp:256) |
| 剪贴板搜索框的可见文字、选区文字、组合输入显示 | TextRenderer::DrawLayout；原生 EDIT 保留编辑与输入法行为；[panel.cpp](C:/Users/SS/Desktop/LumaShot/src/clipboard/panel.cpp:393) |
| 剪贴板空格预览、完整文件列表、提示和页脚 | TextRenderer::Draw / DrawLayout；[preview_window.cpp](C:/Users/SS/Desktop/LumaShot/src/clipboard/preview_window.cpp:166) |

## 系统界面及容易误判的情况

- 原生保存文件对话框：截图、贴图、录屏均使用 GetSaveFileNameW，文字由 Windows 绘制。
- 原生 MessageBox：应用通用 Notice、部分贴图错误、启动异常仍使用 MessageBoxW/A；并非所有提示都走已接入的 ShowThemedMessage。
- 托盘悬停提示、快捷键冲突和贴图保存失败通知：Shell_NotifyIcon，由 Windows 外壳绘制。
- Windows 输入法候选窗口、安装向导：不由应用的 TextRenderer 绘制。
- 录屏 worker 创建的四个 EDIT（帧率、尺寸、起止时间）在 LayoutEdits 中统一隐藏，不能把它们算作当前可见的原生输入界面。实际面板字段由 LumaText 绘制。
- text_edit.cpp 的 DrawTextW(...DT_CALCRECT...) 是测量，不是额外的绘制入口；但其可见 EDIT 正文确实仍是原生绘字。
- 放大镜、纸张装饰、视频画面、OCR 选区高亮及选区手柄仅显示像素/几何图形，不是漏接的文字入口。截图原图中的字也不能算作应用重新绘制的文字。
- 所有已接入位置仍可使用 DirectWrite 负责字体、换行和布局，最终经 TextRenderer 进入 LumaText；不能仅凭 DWriteCreateFactory 判断未接入。

## 额外发现

设置页仍显示“仅开启后记录 · 关闭清空 · Ctrl+Shift+V 唤出”，与已实现的本机加密持久保存、关闭后保留历史不符。位置：[settings_dialog.cpp](C:/Users/SS/Desktop/LumaShot/src/app/settings_dialog.cpp:148)。应将说明调整为“仅开启后记录 · 历史保存在本机”等与实际行为一致的文案。

本次审计未将上表中的未接入界面标记为已完成迁移。圆角修复安装包独立包含此前已完成的剪贴板改动。
