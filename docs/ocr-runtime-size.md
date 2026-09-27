# 安装包与 OCR 运行库精简

保留原来的 det.onnx 与 rec.onnx 文件，不做量化、模型替换或联网识别。运行库使用官方 ONNX Runtime v1.22.0 提交 f217402897f40ebba457e2421bc0a4702771968e，收集原模型及 Basic、Extended、All 优化图的算子并集，仅移除无用算子注册与传统 ML 模块。保留完整 ONNX 加载、动态输入尺寸、融合运算、MLAS CPU 分派及现有线程限制。

运行库从 12,418,080 字节缩小到 8,611,328 字节，约减少 30.7%。独立 LZMA9 压缩从 3,351,116 字节降至 2,355,956 字节。安装包使用固实 LZMA2 ultra64，排除开发 CSV、TXT、JSON 测试记录，并只保留实际需要的四份 VC 运行库。用户说明与第三方许可仍包含在内。

## 验证

两份模型 SHA256 与原版完全相同。三轮交替测试的非计时输出逐字一致；加载中位数原版 281.617 ms、裁剪版 273.031 ms，识别中位数 1019.980 ms、886.321 ms。本轮未见速度退步，样本与轮次有限，不作为所有图片的加速承诺。

真实表格识别、空单元格 TSV、服务反复冷启、取消旧版本、工作进程崩溃及重试均通过。正式构建后 7 项相关回归通过。截图转贴图测试增加了有界异步绘制等待；原版与裁剪版对照通过，产品绘制逻辑未改。

## 重建与切换

运行 `scripts/build-ocr-runtime.ps1` 重建；脚本自动检测 Python 3.12，也可用 `-Python` 指定，使用 Git、CMake 与 MSVC，最多 4 个编译任务。源码及依赖首次下载需要网络，应用使用 OCR 不需要网络。Eigen 保持官方固定提交，VS18 的一项弃用警告作特定抑制，不改上游数值计算实现。

当前已验证文件在 `deps/ocr-runtime-reduced/onnxruntime.dll`；同目录保存来源、模型哈希和所需算子。它只适用于这些固定模型，模型更新时必须重新生成算子配置并验证。

2026-09-17 新一轮独立验证通过后，新构建目录默认使用这份精简运行库；CMake 配置时检查 DLL 和两份模型的清单哈希。已有构建目录可运行 `build.bat -ULUMASHOT_OCR_RUNTIME` 清除旧选择并采用新默认值，或用 `-DLUMASHOT_OCR_RUNTIME:FILEPATH=完整DLL路径` 显式选择。此选项仍保留在 CMake 缓存，也可显式设回 `deps/ocr/runtimes/win-x64/native/onnxruntime.dll` 的完整路径恢复官方库。

用 `scripts/verify-ocr-runtime.ps1 -RuntimePath 完整DLL路径 -Label reduced` 在独立测试目录验证表格和工作进程生命周期，不使用个人图片、偏好或剪贴板数据。
