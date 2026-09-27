# 紧凑属性栏

所有工具共用按内容宽度排列的属性布局，取消固定四块卡片和卡片内的第二层属性。宽屏下每个工具的控件只有一行，标签统一在上方，控件高度为 28 DIP，分组间隔为 14 DIP；窄屏按完整属性组换行。

矩形将线型放在线宽旁，箭头将类型、头部和大小并列，铅笔模式放在颜色前。序号的文本组合将文字颜色和字号并列加入当前属性行。画面绘制、点击、滑块和下拉菜单锚点使用相同的布局数据。纯文字按钮继续居中。

标准宽度下，展开面板高度由 184 DIP 缩短至 122 DIP。所有工具的明暗主题实际渲染图见 `compact-toolbar-gallery.png`。

验证：`build.bat` 成功；`cursor_capture`、`annotation_render`、`unified_toolbar`、`shared_controls`、`capture_to_pin`、`capture_reselect` 六项通过。布局测试覆盖 100%、150%、200% 缩放，480/800/1280 像素屏宽，负坐标屏幕，序号文本附加属性，控件互不重叠及宽屏单行排列。另运行 `lumashot_toolbar_test build/compact-toolbar-preview`，验证 PNG 往返并检查全部工具的明暗主题预览。日志见 `compact-toolbar-tests.txt`。
