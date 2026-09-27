# GIF / MP4 录制性能审查

日期：2026-09-21

只读审查，无生产代码改动。范围：`src/recording/` 采集编码主循环、GIF 导出管线、MP4 导出管线与导出期 UI。已对照既有优化记录（`docs/mcp-mp4-export-performance.md`、`docs/mcp-recording-preview-performance.md`、`docs/gif-optimization.md`），下述建议均为其未覆盖的剩余空间；所有收益判断来自代码阅读，未经本轮实测，落地前需按仓库规则用专项基准验证。

## 已经做得好的部分（无需再动）

- 录制路径全 GPU：WGC 捕获 → 视频处理器裁剪/缩放/NV12 → 硬件 H.264，8 槽有界纹理池 + IMFTrackedSample 回收提供背压（`encoder.cpp`）。
- 慢编码器时跳帧而非积压（`core.cpp` 第 65 行）；同步 SinkWriter 节流。
- 预览封面优先发布、MP4 不再解码整条时间线（前轮已优化，实测 67s → 0.3s 量级）。
- MP4 导出首轮 preset 8、阶段化进度（前轮已优化，39.2s → 28.7s）。
- GIF 全局自适应调色板 + 增量矩形 + gifsicle 无损后处理，质量护栏完整。

## 高收益候选

### 1. GIF 导出：解码即缩放，去掉每帧全分辨率 WIC 缩放（预计收益最大）

`gif.cpp` 第 16 行只开启基础 `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING`，第 22–26 行对每一帧：全分辨率 RGB32 解码 → `CreateBitmapFromMemory`（内部整帧拷贝）→ WIC Fant CPU 缩放。4K 录制导出 640px GIF 时，每帧约 33 MB 解码 + 拷贝 + CPU 缩放全部浪费。

`preview.cpp` 第 9–16 行已示范正确做法：`MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` + 在媒体类型上直接设定目标 `MF_MT_FRAME_SIZE`，由解码链路完成缩放（含失败回退）。迁移到 `gif.cpp` 后，调色板采样按原始分辨率像素采样的语义需要保留或显式重新评估；缩放算法从 Fant 变为视频处理器，必须用 `gif_quality_test` 和现有合成素材核对画质回归。

### 2. GIF 量化并行化

`gif_quantizer.h` `Map()` 是单线程逐像素 kd-树最近色搜索（带前色缓存）。行与行完全独立，可按行条带切给 `std::for_each(std::execution::par)` 或固定线程池，每条带各自维护 previous-color 缓存，结果逐字节确定不变。渐变/照片类内容（前色缓存命中率低）预计接近线性加速；纯 UI 内容收益小但无害。

### 3. MP4 导出：源检查与首轮编码并行

`mp4_export.cpp` 第 115 行 `Inspect(source)` 串行读完源文件全部视频/音频包并做 AAC 哈希，之后才启动第一轮 ffmpeg（第 131 行起）。两者无数据依赖：Inspect 用独立 SourceReader 只读源文件，编码是独立子进程。将 Inspect 放入 `std::jthread`，在首个候选完成后 join 再做 `Same()` 比较，可把整段源读取时间（长片段可达数秒）从关键路径上移除。取消语义不变：两边都已接受 stop_token。

### 4. 录制主循环：只拷贝最新捕获帧

`core.cpp` 第 59 行的排空循环对池中每一帧都执行 `CopyResource(latest, source)`。显示器 120/144 Hz 而录制 15–30 fps 时，绝大多数拷贝立即被覆盖。改为：循环内仅保留最后一个非空 frame（前一个直接 Close），循环后拷贝一次。省下的是全监视器分辨率的 GPU 带宽，高刷屏 + 4K 下每秒可减少上百次无效整幅拷贝。

## 中收益候选

### 5. Encoder::Frame 缓存恒定对象

`encoder.cpp` 第 91–99 行每帧重建 `VideoProcessorInputView`（源纹理恒为 `latest`）、`VideoProcessorOutputView`（目标恒为 `output_`），并重设恒定的 source/dest rect 与色彩空间。四者在会话内不变，可在构造或首帧时创建缓存。单次开销小，但 60 fps 长录制下是纯浪费，且减少驱动层对象翻腾。

### 6. 音频缓冲复用

`core.cpp` 第 67 行每视频帧新建 `std::vector<short> samples`；`encoder.cpp` `AudioFrame` 每次 `MFCreateMemoryBuffer` + 拷贝。前者提为循环外复用缓冲即可；后者可维护 2–3 个 IMFMediaBuffer 轮换（注意 WriteSample 异步持有，需按当前同步节流语义确认安全后再做）。

### 7. 兼容（软件）编码路径的同步停顿

`encoder.cpp` 第 101–105 行单一 staging 纹理 `CopyResource` 后立即 `Map(READ)`，强制等待 GPU 完成本帧转换，采集线程被阻塞。双 staging 乒乓（本帧 Copy 到 A，Map 上一帧的 B）可将等待与下一帧捕获重叠。软件路径本就是兼容兜底，优先级视用户使用比例决定。

### 8. GIF 导出解码/量化流水线重叠

`gif.cpp` 第 47–54 行解码、缩放、量化、WIC 写入全部串行在一个线程。单生产者（解码+缩放）/单消费者（量化+GifFrames::Add）双缓冲即可让两段耗时重叠；与候选 2 叠加时注意总线程数。GifFrames 内部有序写文件的约束天然满足（消费者单线程）。

## 低收益 / 明确取舍项

- **导出进度消息风暴**：`worker.cpp` 第 179–191 行进度回调每帧加锁并 `PostMessageW` + 整面板重绘，GIF 长片段导出会发出数千条消息。仅在百分比或阶段变化时发布即可。
- **x264 后备线程数**：`mp4_export.cpp` 第 128 行 `-threads 4` 固定；多核机器可试 `-threads 0`，但受 3 GiB 作业内存上限约束，需实测不回退。
- **>1080p 的 AV1 并行度**：`lp=2` 是前轮 4K 实测内存回退后的保守选择（文档已记录）。若想放开到 lp=4，应同步提高 `memory_bytes` 并重跑 4K 边界用例，风险自负，不建议默认改。
- **调色板采样阶段的 24 次 seek**：每次 seek 后可能解码整个 GOP 才到目标时间。录制端 GOP 由硬件编码器默认值决定；可评估录制时显式设短 GOP（会略增码率），或采样时容忍关键帧对齐。属于精度/体积/速度三方权衡，非纯优化。
- **gifsicle 后处理**：串行于编码之后是必须的（需要完整文件），已限时限内存，无进一步空间。

## 建议的验证方式（若实施）

- GIF：`lumashot_gif_export_benchmark`（1080p/4K 合成素材）对比总耗时与输出字节；`gif_quality_test` + `gif_pipeline_test` 守画质与结构。候选 1 属于画质敏感改动，需按 quality-first 原则用逐帧比对确认。
- MP4：`tests/mp4_codec_probe.cpp` 生成合成 1080p/4K 素材走生产 `ExportMp4ToFile` 计时（同 `docs/mcp-mp4-export-performance.md` 方法）；SSIM 护栏与 `Same()` 全量校验不得放宽。
- 录制：`recording_perf_test` / `encoding_perf_test`（按需构建目标）对比采集循环 CPU/GPU 占用与丢帧计数；候选 4、5 需确认高刷屏场景丢帧数不升。
- 全部改动按仓库规则只跑相关专项，构建 /W4 /WX，改动生产代码后打包安装器并报告 SHA-256。

## 优先级建议

1. 候选 1（GIF 解码即缩放）：4K→小尺寸 GIF 场景收益最大，改动集中在 `gif.cpp` 单文件。
2. 候选 3（MP4 Inspect 并行）：实现简单，收益随片长线性增长，不触碰任何质量护栏。
3. 候选 4 + 5（录制循环拷贝与视图缓存）：降低录制期 GPU/CPU 占用，尤其高刷屏。
4. 候选 2 / 8（GIF 量化并行与流水线）：在 1 之后再测，避免相互掩盖收益。
5. 其余按需，取舍项默认不动。
