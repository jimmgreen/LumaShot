# 旋转对象的缩放光标方向修复

**后续纠正：**本页记录的“最近系统方向”方案不能满足任意旋转角度的严格对齐，已由 [精确坐标系旋转](mcp-exact-resize-cursor.md) 替代，请勿以本页的角度量化作为当前行为。

## 问题与修复

原来的编辑框缩放光标只依据控制点编号选择，没有叠加对象的旋转角度，导致水平/竖直对象正确、旋转后方向不匹配。

- 对四个边点和四个角点，先取其局部缩放轴，再叠加对象旋转角度。
- 双向箭头以 180° 为周期，按最近方向匹配 Windows 原生水平、垂直、两种对角缩放光标，分区边界为 22.5°。
- 继续使用系统光标，尺寸/样式不另行绘制。任意角度匹配最近的系统方向，不引入连续角度自定义光标。
- 按下、捕获拖动和 WM_SETCURSOR 使用同一套映射；拖到编辑框外或工具栏上仍保持当前缩放控制点的方向。
- 松开后重新计算悬停光标，避免留下拖动状态的方向。
- 顶部旋转光标、对象移动、圆角及引导线/箭头端点的十字光标不变；不修改几何缩放逻辑。

## 验证

Windows Release 构建通过；8/8 相关 CTest 通过（7.40 秒）：selection_rotation_cursor、selection_edit、selected_property_controls、pen_polyline_snap、pen_line_mode、selected_mark_properties、unified_toolbar、tool_preferences。IDE diagnostics 为 0。
扩充 selection_rotation_cursor：八个缩放控制点，0°/35°/45°/90°/135°/180°/270°、负角度、多圈角度，22.5°/67.5°分区边界，100%/125%/150%/200% DPI，负屏幕坐标。
实际应用指针事件覆盖 35°/45°/90°下全部八个控制点的按下、拖动、释放、撤销与重做。
修正旧测试中错误要求旋转后仍保持原始方向的断言。
日志：build/resize-cursor-build.log、build/resize-cursor-tests.log。
仅使用合成对象；不读写个人剪贴板/偏好，不移动用户指针，不自动安装。
本次不运行全量测试，此前 annotation_render 的三个标签相关失败不在修复范围内。

## 安装包

- dist/LumaShot-Setup.exe：37,518,506 字节；打包及安装器编译退出 0，未安装。
- SHA256：e5e18a9c4a00621898ae0494928b6d4fc658188305fcac364aeb6a85be596099。
- 主程序 SHA256：41ee1dd562b7bd04abf569dc237766a6c70316e873357186aa865c42c16c72db。
- Payload 主程序及 recording/OCR/elements worker 与本次 build 逐字节一致（4/4）。
- 打包日志：build/resize-cursor-package.log、build/resize-cursor-installer.log。
