# 铅笔荧光笔模式

铅笔属性区提供「普通 / 荧光笔」。荧光笔默认黄色、24 DIP 线宽、60% 透明度，支持黄、绿、粉、青及自定义颜色，线宽最大 64 DIP；绘制时按当前屏幕缩放转换为物理像素。透明度沿用现有语义：100% 完全透明。普通笔与荧光笔分别保存本次截图会话内的颜色、线宽、透明度；平滑度共用，其他工具继续使用原有样式。

荧光笔使用平头路径，单击绘制方形印记。每条路径一次绘制，同一笔内回描不叠加透明度；独立笔迹正常叠加。标注记录保存模式，预览和导出共用绘制逻辑，撤销重做保留完整样式。

验证：`build.bat` 成功；`cursor_capture`、`annotation_render`、`unified_toolbar`、`shared_controls`、`capture_reselect` 共 5 项通过。覆盖平头、透明度、回描、单击、模式样式隔离、菜单宽度、实际指针输入、DPI 换算、撤销重做、窄屏及负坐标布局。`lumashot_toolbar_test build/highlighter-preview` 生成合成内容的明暗主题预览，并验证 PNG 像素往返；两套预览均已人工检查。

验证日志：`highlighter-tests.txt`。实际渲染图：`highlighter-light.png`、`highlighter-dark.png`。
