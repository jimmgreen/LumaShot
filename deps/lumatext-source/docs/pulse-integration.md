# Pulse 原型接入

Pulse 开发构建可以通过 `LUMATEXT_SOURCE_DIR` 引入同级源码：

```cmake
if(DEFINED LUMATEXT_SOURCE_DIR)
  add_subdirectory("${LUMATEXT_SOURCE_DIR}" lumatext)
else()
  find_package(LumaText 0.1 CONFIG REQUIRED)
endif()
target_link_libraries(pulse PRIVATE LumaText::D2D)
```

复用 Pulse 已创建的 `IDWriteFactory` 与 `ID2D1RenderTarget`。每帧仍由 Pulse 创建和持有 `IDWriteTextLayout`；A/B 两条路径必须绘制同一个 layout，避免把 shaping 或测量差异混入视觉比较。

原型开关仅放在开发构建中：关闭时调用 Pulse 原有 `DrawTextLayout`，开启时用 `lt_frame_draw_layout`。若 API 返回 `LT_E_DEVICE_LOST`，先销毁 frame/renderer，再跟随 Pulse 原有设备恢复流程重建 D2D target 和 LumaText renderer。任何其他绘制失败都应在当帧回退 Pulse 原有 DirectWrite 路径。

当前 renderer 绑定创建线程；不要从后台 raster 线程调用 frame API。透明窗口可使用默认透明背景配置。`LT_RENDER_CONFIG_LINEAR_BLEND` 和已知纯色背景精确线性混合尚未在 `0.1` 实现，不应在验收记录中标为已覆盖。

