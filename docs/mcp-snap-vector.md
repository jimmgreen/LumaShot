# 捕捉标记：单层矢量路径与抗锯齿

用户反馈捕捉效果的描边像锯齿，希望采用 SVG 矢量效果。
原实现已经使用 Direct2D 矢量绘制；问题在于白色粗衬边叠加绿色细线，以及逐边 DrawLine 的端点接合。此次直接修正绘制方式，不引入位图或 SVG 的预栅格化缩放。

## 改动

- 端点方框、中点三角形、最近点沙漏和方向十字改为单个 Direct2D 路径、单次描线。
- 效果等价于 SVG 的 fill="none"、stroke-linejoin="round"、stroke-linecap="round"；没有外加白色衬边。
- 线宽从 1.5 DIP 调整为 1.25 DIP，标记半径 4.5 DIP；明确开启逐图元抗锯齿，并在结束时恢复调用者的抗锯齿模式。
- 已确认顶点改为单层蓝色实心圆，不再叠加白色外圈。
- 捕捉计算、优先级、光标、绘制交互和导出逻辑不变。

## 验证

Windows Release 构建通过；8/8 相关 CTest 通过（7.03 秒）：selection_rotation_cursor、selection_edit、selected_property_controls、pen_polyline_snap、pen_line_mode、selected_mark_properties、unified_toolbar、tool_preferences。IDE diagnostics 为 0。
已检查 100%/125%/150%/200%/300%/400% 明暗背景预览和实际连续绘制场景，白色外圈已消除，端点、中点、最近点及顶点形状可辨。
日志：build/snap-vector-build.log、build/snap-vector-tests.log。
新增测试检查抗锯齿状态恢复、深底上无白色衬边像素，以及边缘存在部分覆盖的平滑像素。
预览：build/polyline/visuals/vector-snap-dpi.png；每个背景分区从左至右为端点、中点、最近点、方向、已确认顶点。
本轮不运行全量测试，不处理此前 annotation_render 的三个标签相关失败。不自动安装。

## 交付

- dist/LumaShot-Setup.exe，37,532,513 字节；打包及安装器编译退出 0，未安装。
- 安装包 SHA256：d7f5b82bd3c7d55aa409ae344b61b04b2ccd171060e97ae6c1e72bb60fa301ec。
- 主程序 SHA256：f5350a98fa4ffd44aa9e0e6e414277b214bb1b5dac7d27eb13e3b22a60cc2bdf。
- Payload 主程序及 recording/OCR/elements worker 与本次 build 逐字节一致（4/4）。
- 打包日志：build/snap-vector-package.log、build/snap-vector-installer.log。
