# 剪贴板液态脱离：细颈、断开与回弹

日期：2026-09-22。承接 `docs/mcp-clipboard-edge-shoulders.md` 的静止贴边轮廓修正。

## 目标

补齐用户截图中的脱离阶段：主体离开边缘后仍通过两端较宽、中间较窄的液态细颈相连，继续拉远时收细、断开并回弹。保留现有竖向内容布局和主题；本轮不把控件重做为参考图中的横向黑色胶囊。

## 实现

- `src/clipboard/liquid_neck.h`：纯几何双三次贝塞尔轮廓。零间距时精确细分上一轮的凹圆角，保证静止贴边造型不退回旧样式；0–14 DIP 平滑转为沙漏形细颈；28–44 DIP 将细颈及两端连接口一起收细，达到断开范围后移除连接。
- `src/clipboard/liquid_surface.cpp`：细颈主体端跟随伸缩、倾斜、滞后变换，屏幕端保持在工作区边界；与主体进行布尔合并，避免缝隙、重复透明度和内部描边。上下/左右镜像共用同一轮廓。
- `src/clipboard/liquid_motion.h`：连接可见范围由约 26 DIP 调整为约 44 DIP，视口和命中轮廓使用同一范围；补充有限幅度的弹簧速度冲量。
- `src/clipboard/panel.cpp`：跟踪拖动中的连接边，检测连接断开后立即施加向离边方向回弹的形变冲量，无需等待鼠标松开。停止、禁用、重排及工作区变更重置连接状态；继续使用已有短期计时器，静止后不持续重绘。
- 文本与图标仍采用原有缓存绘制路径，不随着细颈非等比拉伸。系统关闭动画时仍走原来的即时定位路径。

## 验证

- `build/clipboard-detach-motion.log`：专项 **800/800 通过**。覆盖原有贴边肩部、四边连接、两主题、多档 DPI、细颈双端较宽/腰部较窄、断开收细、带拖动形变时不断缝、断开后无残余命中、方向性回弹及生产拖动路径在鼠标松开前回弹、静止计时器停止、释放后状态清理。
- `build/clipboard-detach-build.log`：生产代码完整构建通过；`build/clipboard-detach-build-final.log` 记录测试夹具调整后的构建。
- 首轮相关回归中，appearance 的合成面板距离真实屏幕边缘仅 40px，扩大连接范围后液态视口超出合成背景。测试正确拒绝截取背景以外的桌面，没有放宽安全检查。已将此非贴边 DWM 圆角测试夹具移出连接范围，边缘效果由专用生产渲染器测试覆盖。
- 最终相关回归 **6/6 通过**：panel、width、header_drag、disable_memory、appearance、shortcut_focus，见 `build/clipboard-detach-regression-final.log`。
- 修改区域当前编辑器诊断未报告 warning/error；诊断不替代构建或视觉检查。

## 视觉预览

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/preview-clipboard-liquid.ps1 -Detach
```

- `build/lumashot_clipboard_liquid_motion_test.exe --detach-preview` 生成 150 个生产渲染器帧，30fps、五秒，覆盖贴边、拉开、细颈、收细、断开回弹和贴回。
- 原始帧：`build/clipboard-liquid/frames/frame-1000.png` 至 `frame-1149.png`；图集：`build/clipboard-liquid/liquid-frames.png`；生成日志：`build/clipboard-detach-preview.log`。
- 已检查关键阶段帧并生成放大裁切 GIF 展示。预览是合成轨迹与合成剪贴内容，不是真实鼠标录像，不读取个人剪贴板，也不抓取个人桌面。
- 未声称已在所有真实混合 DPI 多显示器组合上手工验收。

## 交付

- 安装包：`dist/LumaShot-Setup.exe`，37539287 bytes。
- SHA-256：`88a884c0bcc4c4c2fe63e887283093c4988c08626ea1c1621a5c933bab49ff39`。
- 主程序及 payload 主程序 SHA-256 均为 `0bc99da33547305b7d44b043a6e3f80a81d05cfe830561f066ab0fd5747becb4`，逐字节核对一致。
- 安装器编译成功，已检查 MZ 文件头、大小与哈希；日志：`build/clipboard-detach-package.log`、`build/clipboard-detach-installer.log`、`build/clipboard-detach-delivery.log`。
- 没有运行安装器，没有替换已安装应用。
