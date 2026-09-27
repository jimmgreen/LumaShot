# LumaText DLL 接入

截图标注、截图工具栏和 OCR 选择工具栏的文字通过预编译 DLL 绘制。
DirectWrite 负责布局、换行、对齐和测量，LumaText 使用 FreeType 灰度栅格化。
图片与图形继续使用 Direct2D。

依赖在 `deps/lumatext`，构建不访问桌面的 lumatext 项目。
`.lib` 是 DLL 导入库。`build.bat` 自动复制 DLL 到输出目录；
`scripts/package.ps1` 部署 DLL 和许可证。移动应用时保留同目录的 DLL。

适配器在 `src/ui/text_renderer.*`，上下文缓存上限 16 MiB，按所有者/线程
隔离。每次绘制的目标与帧由 RAII 释放，不持有旧画布；宿主管理绘制帧，
没有新增常驻渲染循环。

## 验证（2026-09-15）

- `build.bat`：C++20、/W4、/WX 构建通过。
- capture、render、toolbar、selection_tools、color_picker、controls：全部通过。
- 新增离屏测试确认 FreeType glyph 计数非零、像素非空，并检查目标重建、
  平移和 96/144/192 DPI 下的裁剪。
- `build/lumatext-preview.png` 使用实际 Demo 路径生成，已检查中文、英文、
  多行文字、标注和工具栏；测试内容均为合成数据。

2026-09-20：预编译 SDK 与本地源码快照已更新至当前 LumaText 工作区版本。保持 coverage_gamma=0.85、contrast=1，不启用 KNOWN_BACKGROUND 或光学补偿。
真实多显示器混合 DPI 外观仍需实机验收。


2026-09-22：同步优化后的 x64 Release /MT 预编译 SDK（含 manifest.json），DLL 1,648,128 字节，SHA256：1EAE7113B138FB26C9D47234D954CB53DABFD1FE0824EB10B41F831781D10535。应用仅链接 deps/lumatext；历史源码快照未改动。保留其他组件所需 MSVC 运行库。
