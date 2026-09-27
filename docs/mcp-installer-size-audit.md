# 安装包体积分析：保留现有 MP4 效果

日期：2026-09-17。本轮只读取程序、载荷、压缩包目录和构建元数据，并生成本报告；未替换依赖、修改编码参数、重新编码用户视频或重建安装包。

## 1. 实测构成

当前 `dist/LumaShot-Setup.exe` 为 77,155,922 字节（73.58 MiB）。

| 打包目录分类 | 未压缩字节 | MiB |
|---|---:|---:|
| FFmpeg | 144177152 | 137.50 |
| OCR 模型、ONNX Runtime、LumaText、OCR worker 及 OCR 目录附件 | 45806028 | 43.68 |
| 应用、其他 worker、gifsicle、VC Runtime、文档和许可证等 | 5060254 | 4.83 |
| 打包目录总计 | 195043434 | 186.01 |

FFmpeg 占打包目录未压缩字节的 73.92%。按 `scripts/installer.iss` 排除开发文档的 CSV/TXT/JSON 后，完整离线组件的源文件合计约 194,408,367 字节（185.40 MiB）；这仍不是安装 EXE 的压缩体积。

已有 ZIP 中 FFmpeg 条目的 Deflate 压缩体积为 58,781,598 字节；ZIP 使用的压缩方法与 Inno Setup 的固实 LZMA2 不同，不能把它当作安装 EXE 中 FFmpeg 的精确占比，也不能直接从安装包总大小中相减。固实压缩各文件并不具有简单可加的独立占比。

## 2. 大头不是当前功能所必需

`deps/ffmpeg/README.txt` 及运行时 `-version` 确认，目前使用 gyan.dev 的 Windows x64 **full build**，版本 `2024-12-19-git-494c961379`，单文件静态链接了大量外部库。

PE 检查显示 `.text` 为 103,603,200 字节、`.rdata` 为 35,008,000 字节；未发现 `.debug*` 节。构建说明也标记已 strip。再删除符号或调高安装压缩级别不是主要收益来源。

`src/recording/mp4_export.cpp` 的生产使用范围明显小于完整 FFmpeg：

1. 首选 SVT-AV1：preset 6 / CRF 36 / tune=0 / lp=2 / lookahead=16。
2. 未通过时重试同设置 CRF 28。
3. 再回退 x264 slow / CRF 18 / threads 4。
4. 原始尺寸、yuv420p、实际帧数、`fps_mode passthrough` 和 `enc_time_base demux` 保持。
5. AAC 使用 `-c:a copy`，不重新编码声音；验证音频包 SHA-256、时间戳及声音参数。
6. 候选必须更小、元数据/时间轴一致，并经全帧 SSIM 验证后才采用，否则保留原片。
7. SSIM 门槛不改变：均值至少 0.99；低于 0.98 的帧数至多为总帧数的整数 1%；任一帧不得低于 0.96。它们是新增失真的验收门槛，不是无损或主观画质百分比。

实时录制由 `src/recording/encoder.cpp` 的 D3D11 / Media Foundation H.264 + AAC 完成。应用内预览使用原始 H.264 缓存和 Media Foundation，不依赖 FFmpeg 的播放设备、字幕渲染、网络协议或 GPU 编码插件。

## 3. 首选：重新编译专用 FFmpeg，保持编码策略

### 最小功能边界

| 类别 | 首轮需要保留的能力 |
|---|---|
| 编码 | `libsvtav1`、`libx264`；质量校验 null 输出所需的 `wrapped_avframe` |
| 解码 | H.264、`libdav1d` AV1 软件解码；AAC 解析/探测能力，首轮可保留其解码器以降低精简风险 |
| 容器 | MOV/MP4 解复用、MP4 复用、null 复用 |
| 本地 I/O | file、pipe，包括 progress 文件、SSIM 日志和 `-f null -` |
| 过滤器 | `ssim`、`settb`、`setpts`、`format`、`scale`/swscale，以及过滤图隐式依赖 |
| 辅助 | 对应解析器、extradata/bitstream 处理、线程、CPU 运行时分派及所需标准库 |

特别注意：本机 `-h muxer=null` 显示默认视频编码器是 `wrapped_avframe`。只保留 AV1/x264 编码器而误删它，会让 `-f null -` 的 SSIM 校验失败，程序可能仍保存成功但退回未压缩原文件，失去当前 MP4 体积效果。

这是一份功能边界，不是未经编译验证即可直接使用的完整 configure 命令。以 disable-everything/disable-autodetect 为起点启用白名单，并通过实际命令补齐隐式依赖。

### 可剔除的候选

- x265、VP8/VP9、Xvid 等未使用的编码器，AV1 的 libaom/rav1e 冗余编码器。
- Vulkan/libplacebo/shaderc、CUDA/NVENC/QSV/AMF 等未被当前 FFmpeg 导出命令调用的 GPU 后端；应用自己的 D3D11 硬件录制不受此类 FFmpeg 裁剪影响。
- 网络流媒体/TLS/SSH/SRT/RIST 等协议依赖、设备采集、SDL 播放支持。
- libass/字幕/字体渲染、音频特效、唱歌语音/音乐解码器等未使用模块。
- libvmaf 可留作开发侧独立评估工具，不必随正式运行时分发；生产验收实际使用 SSIM，不应删除 SSIM。

保留编码器/解码器的 SIMD 和 CPU 分派，优先删除无用模块、启用链接死代码消除和经过验证的 LTO，不以禁用汇编或 `-march=native` 牺牲导出速度、旧 CPU 兼容性换体积。

### 一致性边界

优先锁定当前 FFmpeg 源码提交与编码器依赖版本。仓库明确记录 SVT-AV1 为 `v2.3.0-72-g8f1f1b0d`；x264、dav1d 的对应源码/版本需在正式可复现构建时补齐。不要借瘦身顺便升级编码器或调整 preset/CRF。

相同功能、参数和质量门槛可以保留，但重新编译不自动证明输出逐字节相同。必须对照完整旧运行时确认画质、输出大小、时间轴、声音和性能没有不可接受退步；若要求字节级相同，还需单独验证码流确定性。

**未经构建的工程目标**：先争取 FFmpeg 本体约 15–35 MiB，完整离线安装包约 35–45 MiB。此范围仅用于排优先级，依赖版本、静态链接、SIMD、编译器和实际固实压缩结果均会改变数值；本轮没有生成精简版，不能当作已实现收益或承诺。

旧 x264-only FFmpeg 备份在 `build/mp4-size/before/deps/ffmpeg/ffmpeg.exe`，大小 5,335,552 字节（5.09 MiB）。**不能直接换回它**：会丢失当前默认 AV1 压缩能力。

## 4. 次优先级：已有 OCR 精简库未被当前包采用

- 当前 CMake 缓存仍指向 `deps/ocr/runtimes/win-x64/native/onnxruntime.dll`。
- 当前 build 和 payload 的 ONNX Runtime 均为 12,418,080 字节，SHA-256 `579b636403983254346a5c1d80bd28f1519cd1e284cd204f8d4ff41f8d711559`。
- 已有 `deps/ocr-runtime-reduced/onnxruntime.dll` 为 8,611,328 字节，SHA-256 与 manifest 一致：`f98dd40890987f103df3c0a8d135d64683d03e73a9e9d1764d395ccd9f85cbd5`。
- 打包的 `det.onnx`、`rec.onnx` 哈希均匹配该精简库 manifest 的模型哈希。
- 恢复选择该库可减少未压缩体积 3,806,752 字节（3.63 MiB）。`docs/ocr-runtime-size.md` 历史独立 LZMA9 数据显示压缩后约减少 0.95 MiB；这不是本轮重新打包的实际安装包差值。
- 不改变 MP4；OCR 本身仍须重跑识别、表格、动态尺寸和服务生命周期回归后才能启用。不量化、不更换 OCR 模型。

## 5. 容易误判的方案

- 当前安装器已经使用 `Compression=lzma2/ultra64` 和 `SolidCompression=yes`，更换压缩等级不应作为主方案。
- `ffmpeg.exe` 在 deps、build、payload 中的多份副本是开发工作区拷贝，并没有都被安装器递归装进去；当前载荷只有一份，删除开发拷贝不缩小用户下载。
- 给 OCR/FFmpeg 加安装时复选框但仍把文件嵌入同一 EXE，只能减少选装后的磁盘占用，不能缩小安装包下载；当前 OCR 选装已如此。
- 若允许改变分发方式，可提供完整离线版与按需下载版。按需下载不降低模块到位后的 MP4 效果，但首次导出/识别需要网络；不得把它宣传为同样的开箱离线体验，模块总下载量也不能忽略。
- 不建议首选 UPX、直接删 FFmpeg、退回旧 x264-only、改成纯系统编码器、提高 CRF、降低帧率/分辨率、放宽 SSIM。它们不能同时保证当前效果与行为。
- 不随意删除许可与源代码提供材料。现有 FFmpeg 标记 GPLv3，x264 等依赖须保留相应合规分发及源码/构建信息。

## 6. 建议执行顺序与验收

1. 固定旧 full build 作为开发侧参考，不放入新安装包；建立版本、能力和包体基线。
2. 在独立目录构建白名单 FFmpeg，保持当前导出源码参数；通过实际生产转码命令与 SSIM 命令，而非仅检查 `-encoders`。
3. 对同一套合成/另行授权样本进行 A/B：细小文字、滚动、运动、渐变、1080p/4K、30/60 fps、VFR、静音/有声及长片；比较输出大小、全帧/文字区域指标、最差帧、帧数、时间戳和 AAC 包哈希。长片还要验证时间/内存限制。
4. 必須确认 `Mp4Encoding::Av1` 正常被采用，H.264 回退仍可用；不能仅以“保存成功”放行。沿用 `tests/mp4_export_test.cpp` 的真实 AV1、H.264、VFR、错误/超时/内存/取消/原片回退测试。
5. 可随后切换已验证 OCR 精简库并回归，避免把两类库问题混在首次排查中。
6. 重新构建完整安装包，实测 EXE 字节数、全组件磁盘大小与依赖哈希；未经用户要求不运行安装程序。

结论：优先做**专用 FFmpeg + 原算法/原验收保持不变**，其次恢复已验证 OCR 小运行库。这比删功能或反复调安装压缩等级更符合“保持当前 MP4 效果”的要求。
