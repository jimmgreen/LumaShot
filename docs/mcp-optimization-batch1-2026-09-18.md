# LumaShot 性能与对话框优化 · 第一批（2026-09-18）

依据 docs/mcp-review-performance-dialogs-2026-09-18.md，本轮落地全部 3 项 P1 与模糊缓存相关的渲染改动；其余 P2 项（设置写盘线程、贴图标注入口同步处理、预览取消等待、MP4 进度轮询、文本布局缓存、重绘范围收敛）留待后续批次。除下述文件与测试外未改动其他业务代码。

## 改动明细

### 1) 导出/重绘不再复制撤销历史（P1：整史复制）
- src/model/document.h、src/model/document.cpp：新增 `Document::Snapshot()`，仅复制 marks、selected、selected_part；undo_/redo_ 不进入导出与重绘路径。
- src/app/application.cpp：导出任务改为捕获 `document_.Snapshot()` 并按引用移动进入线程，消除完成/失败两条路径上的全量复制；文字编辑重绘改为借用 `document_`，由渲染层做局部替换。

### 2) 编辑期重绘零克隆（P1 关联）
- src/ui/render.h、src/ui/render.cpp：`Paint/Chrome/Marks` 增加 `editing_text` 参数。渲染时按索引跳过正在编辑的元素；仅 Number 组合的说明文字覆盖需要复制单个 Mark。绘制期间不再构造整份 Document（含历史），未编辑元素纹理索引保持稳定。

### 3) 模糊马赛克计算与位图缓存（P1：UI 线程模糊）
- src/model/mosaic.cpp：抽出共享的 `MosaicCoverage`；矩形模糊直接复制裁剪区域，不再构建 4px 网格与颜色统计；笔刷模式仍用 4px 网格掩膜。与冻结的旧实现逐位一致（见测试）。
- src/ui/render.cpp：模糊缓存新增目标绑定的 `ID2D1Bitmap`；CPU 像素不变时同帧/跨帧重绘直接复用，不再每帧重新上传。渲染目标创建、设备丢失重置、Flatten、Demo 路径均先丢弃缓存位图，避免跨目标复用。

### 4) 贴图保存完成的安全处理（P1：模态重入/迭代器失效）
- src/pin/pin.h、src/pin/pin.cpp：`Saved()` 改为两段式——先以 `wait_for(0)` 排干所有就绪结果并移出队列，再统一执行剪贴板发布与错误弹窗；`processing_saves_` 防护嵌套消息循环中的重入；全程不跨对话框保留 `saves_` 迭代器。审查报告中“旧慢任务+新失败任务”的失效顺序不再可达。

## 验证

新增测试（CMake 注册：render_optimization、pin_save_reentry）：
- tests/performance_batch1_test.cpp + tests/mosaic_reference.h：冻结的优化前模糊/瓦片实现作为逐位基准；覆盖负原点、退化选区、越界、单点/多段笔刷、半径 2–128。
- 断言组：Snapshot 语义（隔离性、原对象撤销/重做不受影响）；编辑渲染与旧“整份克隆”像素逐位一致（含旋转文字、Number 说明、模糊、尺寸角点、工具栏）；缓存身份（同输入复用同一 GPU 位图；参数变化、目标切换、设备重置后正确失效）；矩形模糊在覆盖全部基准后逐位一致。
- tests/pin_test.cpp：新增 `--save-reentry` 注入式重入回归（旧实现下该序列会破坏遍历），不依赖平台弹窗自动化。
- 视觉抽查：build/optimization-batch1/visuals/editing-{0,1}.png。

结果：
- render_optimization：177 项断言全部通过（两轮）。
- pin_save_reentry：全部通过。
- 相关回归 16 项：14 项通过；annotation_render、pin_interaction 失败——经基线对照（build/optimization-batch1/baseline-render.log、baseline-pin.log，未修改代码）确认失败集合与失败点完全相同，属既有问题，与本次改动无关（见下文）。
- 全量构建 build.bat 通过（/W4 /WX），LSP 诊断 0。

## 组件基准（合成输入，非整机帧率；两轮中位值）

| 项目 | 优化前（审查探针） | 优化后（本轮） |
| --- | --- | --- |
| 100 条复杂历史的 Document 复制 | 88.77 / 89.79 ms | 路径已消除；Snapshot 0.054 / 0.055 ms |
| 1920×1080 矩形模糊 | 125.51 / 135.41 ms | 74.08 / 73.47 ms |
| 1280×720 矩形模糊 | 50.91 / 50.98 ms | 31.81 / 33.97 ms |
| 640×360 矩形模糊 | 10.30 / 13.05 ms | 6.03 / 5.89 ms |

说明：前后两组数字来自不同的合成驱动（审查探针 vs build/optimization-batch1/benchmark.log），绝对值不可直接横比；同驱动前后对比（benchmark.log 内 before/after）显示模糊约 1.5–1.9 倍加速。1080p 大面积模糊仍为几十毫秒级 UI 线程计算，增量/异步化留给后续批次。

## 交付

- 安装包：dist/LumaShot-Setup.exe，37,377,844 字节，SHA256 856841140c578503e66d55a3ba73bd12f63692b17f7f7640c2c9633590a00869，未签名、未安装。
- 载荷校验：8/8 与 build/ 一致（build/optimization-batch1/package-verification.json）。

## 发现的既有问题（基线即失败，未在本轮处理）

1. annotation_render 三项断言失败：七种便签预设对比度、混合背景可读性、标签文字颜色导出差异（note_color/标签渲染相关，与本轮文件无交集）。
2. pin_interaction：在文字上双击会关闭贴图（期望不关闭）并随后段错误；基线相同，时序敏感。
3. 审查报告剩余 P2 项如上，建议作为第二批处理。
