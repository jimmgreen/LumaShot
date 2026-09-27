# 录制预览与 GIF 斜向错位修复

## 原因与复现

解码器会在首帧 `ReadSample` 返回时报告 `MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED`。原实现提前缓存 RGB32 行跨度，忽略了随后协商出的缓冲区宽高；对于不对齐的框选尺寸，使用旧跨度读取线性缓冲区会逐行偏移，预览、缩略图、GIF 都受影响。

本机真实 MP4 编码/解码复现（CPU 与硬件编码均发生）：

| 可见图像 | 实际解码缓冲区 | 旧跨度 → 新跨度（字节） |
|---|---|---|
| 638×358 | 640×368 | 2552 → 2560 |
| 1918×1078 | 1920×1088 | 7672 → 7680 |
| 3838×2158 | 3840×2160 | 15352 → 15360 |

这些样本输出的是普通 IMFMediaBuffer，没有 IMF2DBuffer 接口。此前仅验证 IMF2DBuffer 正负行跨度的用例未覆盖此路径；此前 1920/3840 整齐尺寸的基准也未暴露宽度变化。

## 修改

- 预览、缩略图和 GIF 的采色/编码共用 CopyDecodedRgb32，在实际读取帧后查询当前媒体类型，包含 seek 后的重新协商。
- 区分存储宽高、带符号行跨度、可见区域，按最小显示区域（其次几何区域）裁去编码补齐边缘。
- 原始可见尺寸保持不变；拒绝不合法缓冲区、意外像素格式变化或可见尺寸变化。
- 线性倒置缓冲区的起点按完整存储高度计算，再裁取可见行；仍优先使用 IMF2DBuffer 提供的实际行跨度。
- 不改变捕获、视频编码、播放逻辑或 GIF 压缩策略。

## 验证

新增 recording_geometry：三种非对齐宽度 × CPU/硬件编码，每种生成六帧合成渐变 MP4；验证海报、八张缩略图、GIF 所有合成帧的行列关系，GIF 可见尺寸和 40 厘秒时长。素材全部由程序生成。

修复前六种组合全部失败。1918 宽硬件样本的预览平均通道误差由 18.9823 降至 0.650844；3838 宽硬件样本由 18.8953 降至 0.5422。修复后的预览与导出关键帧另做视觉检查。

recording_pixels 补充带填充的线性正/负跨度、非零裁剪偏移、截断缓冲区拒绝测试。

产物：`build/recording-geometry-fixture` 下的合成 MP4、GIF、预览 PNG 和 GIF 首帧 PNG。日志见 `geometry-before.txt`、`geometry-after.txt`、`geometry-tests.txt`。

最终验证：`build.bat` 通过；cursor_capture、media_recording、recording_pixels、gif_quality、gif_pipeline、recording_geometry 共 6/6 通过（76.84 秒）。geometry 用例同时验证 CPU/硬件编码、预览、八张缩略图及 GIF 导出，耗时 64.72 秒。未改动个人录制或读取其内容。
