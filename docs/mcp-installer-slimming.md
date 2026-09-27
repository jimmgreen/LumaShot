# 精简安装包：保留现有 MP4 导出结果

日期：2026-09-17。用户授权裁掉未使用组件。本轮实际替换依赖、重建并核对安装包；没有安装软件或修改用户视频。

## 1. 实际交付与体积

安装包：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`

| 项目 | 原版 | 精简后 |
|---|---:|---:|
| 安装 EXE | 77,155,922 字节 / 73.58 MiB | 37,352,446 字节 / 35.62 MiB |
| FFmpeg EXE | 144,177,152 字节 / 137.50 MiB | 36,424,704 字节 / 34.74 MiB |
| ONNX Runtime DLL | 12,418,080 字节 / 11.84 MiB | 8,611,328 字节 / 8.21 MiB |
| 打包目录原始文件合计 | 195,043,434 字节 / 186.01 MiB | 83,636,093 字节 / 79.76 MiB |

安装 EXE 实际减少 **51.59%**。这是本次 Inno Setup 6.7.3 的实测结果，不是把独立 ZIP 条目压缩量相加得出的估计。之前 35–45 MiB 的工程目标现已有本次实测值。

新安装包 SHA-256：

```text
5B98F8BBECEDC12B870324B91FFDF84E4B135D3D63D4550A61EAAB18333F7EF1
```

旧安装包 SHA-256：`1e92d3c5c0cf1ea19fc5f064bf9bef21ad40ddc4d2a380feaacfd629f8eddef7`。备份保留于 `build/ffmpeg-slim-validation/before/LumaShot-Setup.exe`，没有执行该备份或新安装包。

## 2. 裁剪范围与不变项

- FFmpeg 从源码专门构建，删除未用的外部编解码库、网络协议、字幕栈、采集设备和 GPU 编码后端；不构建或打包 ffplay/ffprobe（此前安装包也未包含这两个独立工具）。
- 保留 libsvtav1、libx264、libdav1d、H.264/AAC 解码和必要解析器；保留 MOV/MP4、file/pipe、缩放/格式转换、时间轴与 SSIM 校验链。
- 特别保留 `null` muxer 和 `wrapped_avframe` encoder，防止缺少校验输出组件而悄悄回退原文件。
- 保留 CPU SIMD。没有通过降低分辨率、帧数、码率质量参数、删音轨或放宽画质门槛换取体积。
- 应用的 Media Foundation 采集和预览独立于被裁掉的 FFmpeg 设备/GPU 库。该运行时面向 LumaShot 录屏，不是通用 FFmpeg 替代品。
- `LumaShot.exe` 及录制、OCR、元素三个 worker 的 SHA-256 与上一安装载荷完全相同，原设置共存、首帧渲染和五种桌面贴图样式代码未改。

保持的生产参数：SVT-AV1 preset 6 / CRF 36 / `tune=0:lp=2:lookahead=16`，未通过时 CRF 28，再回退 x264 slow / CRF 18 / threads 4；原尺寸、yuv420p、`fps_mode passthrough`、`enc_time_base demux`、AAC 包复制、MP4 faststart。

仍要求候选更小，尺寸/时长/逐帧时间戳/音频完整性通过；SSIM 全帧均值 ≥0.99、最低值 ≥0.96，低于0.98的帧数不超过整数1%。否则保留原文件。

## 3. 同源码版本与构建来源

| 组件 | 固定提交 |
|---|---|
| FFmpeg | `494c96137916e0e61f17c439f8f5be13b27fc592` |
| SVT-AV1 | `8f1f1b0dc52b063264838b51bc2299a5e44e31d5` |
| x264 | `da14df5535fd46776fb1c9da3130973295c87aca` |
| dav1d | `d242c47b437c950b545e96e7872aa914edc50be5` |

上述提交与替换前完整运行时对应。源码来自各项目官方归档并固定 SHA-256。完整 URL、归档哈希、配置参数、编译器包版本在 `deps/ffmpeg/` 的 source-lock、manifest、构建脚本及 BUILD-README 中。

在 Linux 构建沙箱使用 MinGW-w64/GCC 14.2.0 交叉编译，没有向 Windows 产品机器安装交叉编译工具。静态 EXE 只导入 Windows 自带的 bcrypt、KERNEL32、msvcrt、SHELL32，无新增第三方 DLL 依赖。

必要构建修复仅为：补上该 FFmpeg 提交在关闭原生 AV1 解码器后漏列的共享 H.264 SEI 清理对象 `aom_film_grain.o`；恢复源码归档缺少的 x264 `x264_config.h` 版本字符串。没有修改编解码算法。首次 H.264 对照因少了14字节版本后缀被严格比较拒绝，补齐后重新编译、复测，没有事后修改输出视频。

最终 FFmpeg SHA-256：`f0ae86e685f9167f171b0561f7909588d3f2e7e21e4cbf03ae30a51828471059`。

## 4. MP4 严格 A/B 结果

调用生产 `ExportMp4WithTool` 入口，仅在测试工具中显式指定完整/精简运行时。相同输入分别导出，以下 **四组最终文件全部逐字节一致（整文件 SHA-256 相同）**，不是仅凭相近的 SSIM 或进程退出码作判断。

| 合成用例 | 帧数 | 新旧版本相同输出字节 | 全帧 SSIM 均值 | 最低 SSIM |
|---|---:|---:|---:|---:|
| 原生1080p、20秒、文字/滚动/棋盘/渐变、AAC，AV1 | 600 | 681374 | 0.99911951 | 0.998114 |
| 同一1080p输入，强制兼容 H.264 | 600 | 2264717 | 0.99962399 | 0.998797 |
| Media Foundation 合成变帧时间/采集间隔，AV1 | 600 | 439457 | 0.99993773 | 0.999911 |
| 1秒合成放大4K60、AAC，AV1 | 60 | 298124 | 0.99961710 | 0.999502 |

指标参考是各自输入的已编码录像，不是未压缩桌面；这是保留原版有损输出，不是无损压缩承诺。VFR 正向用例采用已有 MF 合成测试并附加 free box，以稳定验证候选采用，不作为实际压缩率基准。初始独立重封装的 VFR 样例连原完整运行时也会保留原文件，未通过修改生产校验门槛强行采用。

独立使用完整开发版 FFmpeg/ffprobe 检查所有解码帧、整画面/文字半幅 SSIM、尺寸、时长及音频包。视频时间戳相对输入的最大差异：VFR 0.8 ms，其余为0，符合原有1 ms容差；音频包数分别938/938/938/47，各包 SHA-256 和时间戳保持。新旧导出文件本身完全相同。

四组均真实采用预期 AV1/H.264 编码，没有静默保留原文件。生产3 GiB作业限制保持。4K60是短、放大的功能/资源检查，不代表长时原生4K压力测试；不承诺所有素材都缩小或跨所有机器字节一致。单轮用时受缓存与调度影响，不作为普遍加速承诺。

证据：`scripts/validate-ffmpeg-runtime.py`、`build/ffmpeg-slim-validation/{full-results,slim-results,comparison}.json`、各导出/指标日志。完整 ffmpeg/ffprobe 仅为开发基线，未打包。

## 5. 回归：通过项与未通过项分开记录

- `build.bat` 成功；主程序无源码变动，Ninja 报无待编译任务，依赖复制仍由 CMake 更新。修改过的 codec probe 单独编译成功。
- 首轮扩展 CTest 为 **14/17通过**，不是17/17：MP4导出/质量、OCR文本/服务/表格/选项、设置与捕获共存、贴图样式、截图到贴图、录制像素/UI/共存等通过。
- MP4 导出回归包含真实 AV1 采用、H.264 路径、VFR/AAC、长GOP预览、错误/非法/变大/尺寸不符/质量不足/日志不完整/超时/内存限制、取消与临时目录清理，精简依赖下通过。
- 首轮 `media_recording` 的 GIF 帧数/时长断言失败。随后固定工作目录交替两轮，新旧依赖均通过；每次合成 MP4 为45帧/约1.5秒，GIF 为15帧/1秒。没有修改 GIF 代码或放宽断言。
- 首轮 `pin_interaction` 遇到剪贴板占用。受控复测中精简依赖连续两轮通过，原依赖反而出现剪贴板占用/TSV断言失败；不能把这类测试描述为始终稳定。首次隔离补测缺少相对输出目录造成的 Windows imaging 错误已排除，不作为产品回归结论。
- **仍未通过：`recording_window` 的 WGC 合成窗口捕获报 `0x80070057`。使用原完整 FFmpeg + 官方 OCR DLL 的隔离基线同样失败。本轮未修改或修复这条采集路径，未把它算作通过。**

证据：`build/slim-final-tests.log`、`build/slim-controlled-recheck.log`、`build/ffmpeg-slim-validation/extended-baseline-comparison.json` 和 `controlled-recheck.json`。原依赖隔离目录来自替换前安装载荷，哈希有记录；主程序与 worker 位串未变。

## 6. OCR 精简

新一轮独立表格/真实模型、反复冷启动、取消旧版本、工作进程崩溃与重试测试通过；记录于 `build/slim-ocr-verification.log`。冷1080p/20行测试 P95 为2255.41 ms，不作为新旧性能对比。

两份模型未量化、未替换、哈希不变。实际选用 DLL 为 `deps/ocr-runtime-reduced/onnxruntime.dll`，SHA-256 `f98dd40890987f103df3c0a8d135d64683d03e73a9e9d1764d395ccd9f85cbd5`。CMake 新默认选择它并验证 DLL/模型清单哈希；可显式选择官方库回退。已有缓存用 `build.bat -ULUMASHOT_OCR_RUNTIME` 切到新默认。

## 7. 打包与交付校验

- 使用原离线打包流程与固实 LZMA2 ultra64，没有改成联网安装器。
- `package.ps1` 增加完整编码/解码/格式/过滤器/协议能力检查及运行时哈希门禁；安装编译脚本再次核对载荷 FFmpeg 哈希。
- 载荷共 80 个文件，原始总量 83,636,093 字节；按现有文档排除规则进入 Inno 的文件合计 83,001,026 字节。
- 载荷二进制、模型与构建/来源清单核对；原四个应用 EXE 哈希不变。打包阶段检查直接与延迟导入，部署所需 CRT。
- 实际运行打包目录里的 FFmpeg 成功。便携 ZIP 全量 CRC 检查通过，关键二进制/模型/清单解压数据的 SHA-256 与载荷一致。
- 编译后的安装 EXE 大小、SHA-256 再次核对。**未解包安装 EXE，未安装、卸载、升级或在干净系统验收**，不把源载荷核对等同于这些步骤。
- 构建期间临时文件传输通道已关闭。

对应源代码另行交付：`dist/LumaShot-ffmpeg-source.zip`，29,783,172 字节 / 28.40 MiB，SHA-256 `615a16364f3bf9090bc322b8573f90f722ebc513e9c8c1982fd0482a58666952`。包含四份锁定上游源码归档、许可证及构建控制脚本，完整性和归档内源码哈希已验证。它不嵌入安装 EXE，不是应用运行依赖；再分发二进制时应在同一发布位置提供等价的对应源码访问。

最终机器可读校验：`build/slim-delivery-check.json`。构建/打包日志：`build/slim-final-build.log`、`build/slim-package.log`、`build/slim-installer.log`。未宣称安装包已签名或已安装。
