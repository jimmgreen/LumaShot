# 选择编辑框的旋转光标

后续已缩小图形并改为多尺寸 CUR 资源，最新实现和交付见 [紧凑光标与捕捉改进](mcp-snap-refinement.md)。下文记录首次旋转光标交付。

旋转后的边角缩放方向另见 [缩放光标方向修复](mcp-rotated-resize-cursor.md)。

## 改动

用户希望选中对象编辑框顶部的旋转控制点显示明确的旋转鼠标手势，而不是手形。

- 选择工具悬停旋转控制点（handle 8）时，使用专用顺时针环形箭头光标。
- 光标带黑色轮廓、白色内芯和透明背景，适配明暗画面；中心热点对应实际控制点。
- 根据窗口 DPI 生成光标并缓存，重复悬停不反复创建 GDI/USER 资源；退出时释放自有句柄。
- 按下和拖动旋转时持续使用旋转光标，即使鼠标离开控制点；释放后重新判断当前位置，避免光标停留在旋转状态。
- 保留现有旋转计算、Shift 角度约束、撤销/重做，以及边角缩放和对象移动光标。
- 工具栏、颜色选择器、下拉框和忙碌状态优先，不被底层旋转控制点覆盖。

## 文件

- src/ui/selection_cursor.h、src/ui/selection_cursor.cpp：DPI 光标资源与选择编辑光标路由。
- src/app/application.cpp：WM_SETCURSOR、旋转按下/拖动/释放时使用新光标。
- tests/rotation_cursor_cases.h：专项合成测试。
- tests/selected_properties_test.cpp、CMakeLists.txt：--rotation-cursor / selection_rotation_cursor 测试入口。

## 验证

build.bat Windows Release 构建通过；IDE diagnostics 为 0。
两组定向 CTest 共 8/8 通过：selection_rotation_cursor、selection_edit、selected_property_controls、pen_polyline_snap（5.79 秒），以及 unified_toolbar、tool_preferences、selected_mark_properties、pen_line_mode（0.82 秒）。
实际 HCURSOR 合成的 build/rotation-cursor/dpi-gallery.png 已检查，六档缩放在明暗底图上均能辨识环形箭头。
本次未重跑全量测试；此前 annotation_render 的三个标签相关失败不在本次改动范围内。
专项覆盖 100%/125%/150%/200%/300%/400% 光标尺寸和热点、明暗底图、缓存句柄复用、负坐标和已旋转对象、实际拖动和撤销重做。
测试使用合成对象，不读取个人桌面、设置或剪贴板，也不移动用户鼠标位置；测试结束恢复原光标。
日志：build/rotation-cursor-build.log、build/rotation-cursor-tests.log、build/rotation-cursor-regressions.log。

## 交付

已重新生成 dist/LumaShot-Setup.exe，37,528,312 字节；打包和安装器编译均退出 0，未安装或替换运行中的程序。
安装包 SHA256：3854b719e197d05e46c1b3b650240788b1253f5b32cd2cecae04c76d14ec2955。
主程序 SHA256：742b174e426e5997d64c05d886d16fe1c511524d28caadac806b817faab690c3。
payload 主程序及 recording/OCR/elements 三个 worker 与本次 build 逐字节比较 4/4 一致。
打包日志：build/rotation-cursor-package.log、build/rotation-cursor-installer.log。
