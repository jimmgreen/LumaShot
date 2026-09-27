# 缩放光标精确跟随对象坐标系

## 对上次修复的纠正

上次采用“最近系统方向”的量化是不正确的：系统只有四个双向轴，但对象可取任意角度。用户要求的是水平/竖直时的同一套局部坐标逻辑整体旋转，而不是把方向取整到 45°。

本次规则：边中点的局部方向为 0°/90°，角点为 45°/135°；加上对象旋转角，双向箭头以 180°为周期。例：对象旋转 30°，左右边点为 30°、上下边点为 120°、两组角点为 75°/165°。

## 实现

- 删除 22.5°分区、最近方向取整逻辑。
- 在方向和原始光标类型均匹配时复用系统光标（边点的水平/竖直、角点的两种对角）；其余情况使用精确旋转的黑色双箭头与细白边。
- 分别测量系统边点与角点光标的可见尺寸，再按各自的原始轴向校准矢量轮廓，避免旋转过程中边点和角点光标互换尺寸。不是只比较透明位图画布大小。
- 非标准角度按实际角度变换矢量轮廓，再以 4×覆盖采样生成 Win32 HCURSOR，不旋转低分辨率位图，不对角度做离散分档。
- DPI 与中心热点保持一致；采用最多 64 项的缓存，不回收当前显示的光标，避免遍历任意角度造成资源持续增长。
- 悬停、捕获拖动和松开使用同一方向逻辑；顶部旋转图标、移动及特殊端点十字光标保持不变。

## 验证

Windows Release 构建通过，8/8 相关 CTest 通过（6.49 秒）：selection_rotation_cursor、selection_edit、selected_property_controls、pen_polyline_snap、pen_line_mode、selected_mark_properties、unified_toolbar、tool_preferences；IDE diagnostics 为 0。
这次不再仅断言返回了哪个系统光标：对真正的 HCURSOR 进行 DrawIconEx 合成，按黑色箭头像素的二阶矩测量主轴方向，与“局部轴 + 对象角度”比较。小尺寸栅格误差允许 2°，没有人为角度量化。
覆盖 15°/30°/35°/60°、负角度、多圈角度、以前的分区边界、多档 DPI 和负屏幕坐标；保留实际拖动、撤销重做及缓存边界测试。
合成预览 build/rotation-cursor/exact-resize-gallery.png：0°、15°、30°、45°、60°、-20°编辑框；蓝线为框边，淡红线为正确缩放轴，叠加实际光标像素。
最终预览已人工检查；合成测试使用系统原生光标实际像素尺寸对应的 DPI，避免把 96-DPI 自定义光标与桌面较高 DPI 的系统光标混在一起比较。
最终日志：build/exact-resize-delivery-build.log、build/exact-resize-delivery-tests.log。
只使用合成数据，不自动安装，不运行全量测试；此前标签相关的 annotation_render 三项失败不在本次范围内。

## 交付

- dist/LumaShot-Setup.exe：37,536,964 字节；打包及安装器编译退出 0，未安装。
- 安装包 SHA256：722bc56cb6ddc12b43a014d98a49ad87151a4a2b35b46f9969d132d745514b61。
- 主程序 SHA256：d046a75274820c20290794e40bcf333ba3d3511e703079ce6a85aa1de30a5f3d。
- Payload 主程序及 recording/OCR/elements worker 与最终 build 逐字节匹配（4/4）。
- 打包日志：build/exact-resize-package.log、build/exact-resize-installer.log。
