# 项目精简记录

日期：2026-09-17

## 范围与结果

用户选择进一步精简测试与样例。本轮未修改 src/、运行依赖、现有发行包或相邻 Pulse 项目。

- 删除 132 个已核对用途并逐文件校验 SHA-256 的旧生成文件，总计 162,629,918 字节（约 155.10 MiB）。这是删除时的总量；后续验证重新生成了少量 build/ 内测试输出，不等同于最终磁盘净减少量。
- 清理项目根目录的 gif-quality-fixture/、mp4-export-fixture/、mp4-quality-fixture/、mp4-quality-baseline/ 和 10 张自动生成的预览图。
- 清理 build/ 内的 1080p/4K GIF 基准媒体、旧质量及导出测试产物、录制测试产物，以及 scripts/__pycache__/。
- build/mp4-validation/ 只删除生成的 MP4 视频，保留原始 JSON、日志、对比图和脚本。build/gif-baseline/、build/mp4-study/ 等原始源码快照与历史证据保留。
- 保留 docs/ 中的设计图、历史报告与证据；保留 icon.png、deps/ 和 dist/。

完整删除清单、字节数和原文件哈希：build/cleanup-files.json。

## 测试与构建精简

删除两个未注册到 CTest 的一次性实验程序及其 CMake 目标：

- tests/gif_4k_benchmark.cpp：旧的固定 4K 长运动压力实验，不含完整功能回归断言。现行导出性能入口保留 tests/gif_export_benchmark.cpp，支持 1080p/4K；两者素材不同，不声称压力覆盖完全相同。
- tests/gif_candidate_benchmark.cpp：旧版有损颜色容差研究工具，仅接受受限的全局调色板素材。当前生产默认使用精确帧复用和无损后处理，相关功能仍由 tests/gif_pipeline_test.cpp 回归验证。docs/gif-temporal-reuse.md 已标明退役状态。

以下 6 个手动性能/诊断目标改为 EXCLUDE_FROM_ALL，但源码和显式构建能力保留：

- lumashot_selection_perf_test
- lumashot_clipboard_perf_test
- lumashot_encoding_perf_test
- lumashot_ocr_benchmark
- lumashot_profile
- lumashot_profile_real

按需构建：`cmake --build build --target <目标名>`。

47 项已注册 CTest 测试全部保留；tests/ 的 C++ 文件由 54 个减至 52 个。
特别保留 tests/gif_optimizer_fixture.cpp 与 tests/mp4_optimizer_fixture.cpp：它们是子进程故障、超时、取消和回退测试的必要辅助程序，并非无用样例。

.gitignore 已补充根目录测试媒体/预览输出及 Python 字节码规则，README.md 已更新使用方法。

## 验证

- build.bat：CMake 重新配置与生成成功，增量构建通过（ninja: no work to do）；未执行全量干净重编译。
- 当前编辑器诊断：0 条错误或警告；诊断结果不能代替编译和测试。
- 构建图检查：全部 CMake 测试源码存在，删除目标不再引用；6 个手动目标不属于默认 all，但仍可显式构建。
- CTest 注册数量：47，清理前后不变。
- 7/7 针对性回归通过，总计 21.05 秒：cursor_capture、annotation_render、gif_quality、recording_pixels、gif_pipeline、mp4_quality、mp4_export。
- 未运行其余 40 项测试，未重新打包、安装或进行额外手动视觉验收。

构建日志：build/cleanup-build.log。测试日志：build/cleanup-tests.log。

历史报告中指向已清理生成媒体的路径仅为历史运行位置，不再保证文件存在；保留生成工具可重建新的实验产物，旧基准仍需对应的历史源码快照。

## 备份与恢复

工作区没有 Git 提交，不能依赖 Git 恢复。本轮改动前的 4 个文本文件和删除的 2 个 C++ 文件已保存到 build/cleanup-backup/，另在 Agent 工作区保留一份。

备份按原仓库相对路径组织；versions.json 保存读取时的 SHA-256 版本。备份内容来自文件读取结果，统一使用 UTF-8/LF，哈希记录对应原文件字节，不保证备份换行格式与原文件字节一致。

恢复前先核对后续改动，再将所需备份文件复制回对应位置并运行 build.bat。不要整批覆盖后续工作。已清理的生成媒体未另作备份，可通过保留的测试或生成工具重新生成；现有发布包没有删除。
