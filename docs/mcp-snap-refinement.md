# 紧凑旋转光标与 CAD 式线段捕捉

后续已去除捕捉符号及顶点的白色衬边，改为单层圆角矢量路径，见 [矢量捕捉标记改进](mcp-snap-vector.md)。

## 本次要求

旋转光标需与箭头、移动、缩放光标视觉尺寸协调；连续绘制不再显示“端点”等浮动文字，使用小顶点和几何捕捉符号。中点、最近点先覆盖直线、折线和箭头，不扩展到矩形或圆的轮廓。

## 实现

- 旋转光标改为构建时生成的多分辨率 Windows CUR 资源，嵌入程序并通过 LoadImage 加载，不再在运行时绘制像素。32 DIP 透明画布上的图形缩小为原来的 75%，略微减细轮廓；中心热点不变。
- 资源含 100%、125%、150%、200%、300%、400% 六档，其他 DPI 由系统加载器缩放；缓存与释放逻辑保留。
- scripts/compile-rotation-cursor.py 使用 Python 标准库生成资源，CMake 将资源嵌入使用 UI 库的目标，安装时不依赖外部光标文件。
- 小蓝点表示已确认的折线顶点，不标记未确认的预览端点。撤回顶点、取消或结束绘制会同步清理。
- 捕捉标记：端点为小方框，中点为三角形，最近点为沙漏形；使用绿色轮廓与白色衬边，不再出现悬浮文字框。方向捕捉保留细蓝辅助线和十字。
- 捕捉优先级：端点 > 中点 > 线段最近投影点 > 水平/垂直/45°。半径保持 8 DIP，中点优先于附近的投影点，避免难以捕获中点。
- 中点及最近点支持已完成直线、折线每段、直线型箭头主轴以及当前折线已确认的线段，支持旋转与负屏幕坐标。最近点限制在线段内，不延长线段；自由画笔采样点和未确认预览段不参与新增几何捕捉。弯曲/手绘箭头保留原有端点捕捉，不把不可见的首尾连线当成捕捉目标。
- 顶点与捕捉符号只属于编辑界面，不写入导出的图片。

## 验证与交付

Windows Release 全构建通过；8/8 相关 CTest 通过（4.76 秒）：selection_rotation_cursor、selection_edit、selected_property_controls、pen_polyline_snap、pen_line_mode、selected_mark_properties、unified_toolbar、tool_preferences。
最终日志：build/snap-refinement-build-final.log、build/snap-refinement-tests-final.log。
实际资源通过 Windows LoadImage 加载为 HCURSOR 后合成预览，已检查 build/rotation-cursor/dpi-gallery.png；每个明暗分区从左至右为箭头、移动、对角缩放、旋转，自上而下为六档 DPI。系统对照从 LoadCursor 得到并以 CopyImage 复制到相同尺寸，未更改系统鼠标设置。
build/polyline/visuals/mid-light.png、nearest-dark.png 已检查：已确认顶点清晰，捕捉标记局部且不带浮动文字。专项像素比较也验证了中点/最近点反馈的变化仅在捕捉点附近 8 像素范围内。
首次对照测试使用不带共享标志的 LoadImage 加载系统光标失败；测试工具改为 LoadCursor + CopyImage，并增加空句柄保护后，上述最终测试全部通过。
本次未运行全量测试；此前 annotation_render 的三个标签相关失败未在本次处理。
测试为合成内容，不读取用户截图或剪贴板，不改用户偏好，不自动安装。

## 安装包

- dist/LumaShot-Setup.exe：37,529,888 字节，打包和编译安装器退出 0，未自动安装。
- SHA256：2908eb0b16b8001c84b28ed9e29535691522c952fb687b5f9b4d504973cad97b。
- 主程序 SHA256：2c5d1ff82b467acdd1156615dfd5653577105b0613512bad227abc52219434c1。
- Payload 主程序和 recording/OCR/elements 三个 worker 与最终 build 逐字节核对 4/4 一致。
- 日志：build/snap-refinement-package.log、build/snap-refinement-installer.log。最终 IDE diagnostics 为 0。
