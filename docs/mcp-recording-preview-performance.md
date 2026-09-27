# 4K/60 帧预览首帧与保存反馈优化

日期：2026-09-17

## 问题定位

src/recording/preview.cpp 原先一次性生成封面和 8 张缩略图，src/recording/worker.cpp 仅在全部完成后提交给窗口。MP4 面板没有缩略图条，却仍执行整套时间线解码。

旧版相邻目标间隔小于 10 秒时顺序解码；61 秒录制的缩略图间隔约 7.6 秒，因此会扫描大部分视频。4K/60 帧时产生大量无用解码。此外，同步 MFPCreateMediaPlayer(URL) 在窗口线程打开媒体。

保存成功使用 MessageBoxW，视觉风格与自绘面板不一致，也强制用户确认。

## 改动

- src/recording/panel.h：新增 PreviewOptions，默认保留完整缩略图模式，支持解码线程上的渐进发布回调。
- src/recording/preview.cpp：封面完成即发布；MP4 请求仅封面；GIF 逐张补齐 8 张缩略图。超过 2 秒的稀疏目标使用 seek，短间隔继续复用已有解码位置。
- src/recording/worker.cpp：封面消息不再 join 仍工作的缩略图线程；重新录制时停止旧任务，通过代次编号忽略过期消息。已发布封面不会因为后续缩略图失败被丢弃。
- src/recording/preview_player.h：MFPlay 回调与窗口之间使用独立、可脱离的邮箱；异步创建/设置媒体项，不把耗时 URL 打开过程放在窗口线程。关闭时断开通知并释放待处理媒体项。播放器未就绪时可暂存播放请求；失败时明确提示仍可保存。
- src/recording/panel.cpp：添加静态加载状态，以及与深浅主题配套的“视频已保存”/“GIF 已保存”圆角勾选提示；约 4.5 秒自动清除，无成功确认弹窗，不增加常驻渲染循环。
- 保存失败/取消仍保留警告，不能伪装为成功；此前已经成功保存过的录制，不因另一次导出失败被改回未保存。

未修改录制分辨率、帧率、编码器、码率或导出画质参数。预览封面原有 608 像素宽度上限不变，不影响最终视频。

## 合成 4K/60 帧性能验证

同一合成 H.264 文件：3840×2160、60/1 fps、61 秒、3660 帧，无个人桌面/音频数据。使用 FFmpeg testsrc2 + NVENC，GOP 120，不使用 B 帧。原文件 SHA-256：c06e97aa7099a15bbba61e8d9ee55e9a483be1ca645ae296637b697491e778b8。

修改前保留的 preview.cpp 和 panel.h 独立编译为基线程序；与最终版本使用相同合成媒体。每项独立进程运行三次，不进行并行性能测量。

| 指标 | 三次结果（毫秒） |
| --- | --- |
| 旧版封面交付：必须等待全部缩略图 | 67093 / 70047 / 68437 |
| 最终 MP4 封面回调 | 578 / 297 / 313 |
| 最终 MP4 ReadPreview 返回 | 734 / 453 / 453 |
| 最终 GIF 封面回调 | 500 / 266 / 390 |
| 最终 GIF 全部缩略图完成 | 10750 / 10829 / 11375 |
| 实际 Ui::Preview 调用返回 | 78 / 78 / 94 |
| 实际窗口消息泵观察到封面可用 | 1437 / 1515 / 1516 |
| 实际窗口播放器就绪 | 1297 / 1359 / 1375 |

解释：封面解码回调不是屏幕实际呈现时间。窗口测试运行生产 Ui 代码和消息泵，包含播放器并行初始化与界面初始化开销；没有测量显示器实际呈现的帧时间。以上也不是“点击停止”到显示画面的完整延迟，不包括停止录制后的编码封装收尾。

这是本机合成素材的三次测量，不代表冷缓存、多设备、所有编码/GOP、HDR、数小时录制或用户实际素材的保证。真实录制仍需要用户在自己的场景验收。

原始基线与初版结果：build/preview-perf/results.json。最终结果：build/preview-perf/final-results.json。

## 回归与界面验证

- build.bat：最终编译、链接通过，C++20 /W4 /WX。
- 8/8 针对性 CTest 通过，71.50 秒：media_recording、recording_ui、gif_quality、recording_pixels、gif_pipeline、recording_geometry、mp4_quality、mp4_export。
- 另外以详细日志重复运行 media_recording 与 recording_ui：2/2 通过，8.50 秒。
- 已覆盖封面先于缩略图发布、MP4 不生成缩略图、提前取消/封面后取消、过期消息不替换当前画面、发布封面不等待工作线程、真实播放器初始化/播放/暂停、非模态保存提示及清除。
- 通过生产绘制代码生成并目视检查深浅主题截图：build/recording-saved-light.png、build/recording-saved-dark.png。GIF 截图：build/recording-gif-saved-dark.png。
- 当前 src/recording 编辑器诊断为 0 错误、0 警告。47 项 CTest 注册保留，本轮未运行其余 39 项。

日志：build/preview-perf/final-build.log、build/preview-perf/final-tests.log、build/preview-perf/behavior-tests.log。

## 复现与产物管理

从项目根目录在 Bash 运行以下命令可重建同类大体积合成素材；FFmpeg 路径按本机调整：

```bash
/c/ffmpeg/bin/ffmpeg.exe -hide_banner -nostdin -y -f lavfi \
  -i 'testsrc2=size=3840x2160:rate=60' -t 61 \
  -c:v h264_nvenc -preset p1 -rc constqp -qp 30 -g 120 -bf 0 -an \
  build/preview-perf/synthetic-4k60.mp4
```

在 Visual Studio x64 开发命令环境按需构建工具：

```text
cmake --build build --target lumashot_mp4_codec_probe lumashot_recording_ui_test
build\lumashot_mp4_codec_probe.exe preview build\preview-perf\synthetic-4k60.mp4 61 poster
build\lumashot_mp4_codec_probe.exe preview build\preview-perf\synthetic-4k60.mp4 61
build\lumashot_recording_ui_test.exe --preview-perf build\preview-perf\synthetic-4k60.mp4
```

为延续项目精简要求，验证完成后已按大小及 SHA-256 核对并删除本轮生成的 483,195,428 字节合成视频；保留生成方法、源码快照、基线程序、图像和统计。测试/基准工具仍按需使用，没有新增默认 CTest 项。

改动前源码保存在 build/preview-perf/before/。这不是 Git 提交；恢复前必须核对后续改动。

新版录制工作进程位于 build/lumashot_recording_worker.exe，与 build/LumaShot.exe 配套使用。没有重打包 dist/ 安装包，也没有替换已安装程序或更改个人偏好。
