# 真实屏幕框选跟随修正（2026-09-15）

## 问题与证据

用户运行 `LumaShot-magnifier-fix`，放大镜正常，但真实屏幕拖动框选不跟手。按用户正常操作取得的日志 `selection-real-before.csv`：3840×2160、60 Hz；16 次框选绘制中位 100.97 ms、P95 147.96 ms。绘制命令 P95 0.43 ms，整屏提交 EndDraw P95 147.64 ms。该记录来自正式入口、真实鼠标操作，不使用真实截图作为自动测试素材。

## 修改

- 将截图覆盖层的旧 HWND 渲染目标替换为 Direct2D 设备上下文、D3D11 和 DXGI Flip Discard 双缓冲。保持原图物理分辨率与原有标注绘制逻辑。
- 每个显示窗口仅允许一帧排队。帧未就绪时不开始绘制，保留当前选区状态；消息循环同时等待输入和帧就绪事件。鼠标松开后即使不再移动，最终选区也会在就绪时刷新。
- 没有待更新画面时，消息循环阻塞等待输入；没有空闲刷新线程、轮询定时器或高精度系统计时器。
- 窗口显示之前完成首次资源准备；设备移除或重置后释放依赖资源并重新创建。没有硬件设备时尝试 Windows WARP 软件设备。
- 放大镜仍使用独立小窗口。PNG 导出、OCR、贴图窗口保持各自原有渲染路径。

实现依据：[Microsoft Flip Model 指南](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)、[Direct2D 设备上下文](https://learn.microsoft.com/en-us/windows/win32/direct2d/devices-and-device-contexts)、[帧就绪事件](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)。这些文档支持实现方式，不代替本机性能测量。

## 验证

`build.bat` 通过，14 项 CTest 全部通过，完整结果见 `selection-follow-tests.txt`。新增瞬时连续投递 40 次拖动的回归：输入中间位置合并成较少的显示帧，但松开后的最终选区仍显示；该轮共提交 3 帧，其中 1 帧为松开后的最终画面。连续跟随、截图到贴图、标注导出和其他现有测试均通过。

## 用户真实操作验收

用户从新便携目录运行正式程序，在相同 3840×2160、60 Hz 环境下完成三次真实截图，明确反馈：**“明显顺滑、跟手了”**。

| 指标 | 修改前 | 修改后 |
|---|---:|---:|
| 框选实际绘制样本数 | 16 | 188 |
| 框选绘制结束与提交调用耗时 P95 | 约 148 ms | 1.07 ms |
| 修改后全部鼠标处理 P95 | — | 1.63 ms，2,203 次 |
| 修改后全部 Present 提交调用 P95 | — | 0.23 ms，711 帧 |

修改前 HWND 目标的 EndDraw 同时包含绘制结束与显示提交；修改后将同一框选帧的 EndDraw、Present 两段相加进行比较。两次真实操作样本量不同，这些结果用于确认本机卡顿修复，不作为跨设备性能承诺，也不是鼠标到显示器的光学延迟。

`paint` 日志包含由于帧未就绪而直接返回的尝试；评估真正渲染与提交应分别查看 `end_draw`、`present`，不能将未绘制的快速返回计入帧率。记录见 `selection-real-before.csv`、`selection-real-after.csv`。验收后临时记录开关已关闭，下次启动不再记录；正在运行的进程保留启动时的记录状态，退出后释放。
