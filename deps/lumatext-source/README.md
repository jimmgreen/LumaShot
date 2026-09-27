# LumaText

LumaText 是一个面向 Windows 10/11 x64 的 C++20 灰度字体渲染器。新单行管线使用 HarfBuzz shaping、DirectWrite script/bidi 分析、内置 Unicode 16.0 grapheme 分段和 FreeType 2.13.3 灰度栅格；旧 `IDWriteTextLayout` 绘制路径仍保留用于兼容。

当前版本是 `0.1.0` 阶段 0/1 原型。它用于 Pulse A/B 实机验收，不是方案中的完整 `1.0`：D3D11 atlas、异步冷缓存和单行 TSF 输入尚未实现，相应 API 会明确返回 `LT_E_UNSUPPORTED`。

## 已实现

- 稳定 C ABI 和头文件式 C++ RAII 包装，descriptor 支持尾字段扩展。
- DirectWrite 字体文件流桥接，支持单文件 TrueType/OpenType/TTC face 和 variable axes。
- 每线程独立 FreeType library/face，避免跨线程共享 FreeType 对象。
- 带内存上限的 CPU glyph LRU cache；key 包含 font face 身份、glyph、字号、DPI、子像素相位和校准参数。
- 单行普通文字重绘使用有界整行位图 LRU cache（每 renderer 最多 64 项、8 MiB 像素数据），DPI/设备变化时失效；复用同一 layout 可跳过重复合成和上传。
- glyph provider 支持预加载 `lt_font_face`，避免内存字体逐字请求时反复扫描整个字体文件，旧请求前缀仍兼容。
- 自然灰度轮廓、可调 coverage gamma/contrast 和轻微水平方向字干补偿。
- D2D A8 `FillOpacityMask` 后端、裁剪、下划线/删除线、设备线程检查和 `D2DERR_RECREATE_TARGET` 映射。
- 彩色字体、RTL/sideways 和无法读取的 font run 自动回退 D2D/DirectWrite 绘制，不输出空白 glyph。
- DLL、静态库、安装包 targets、Win32 A/B 画廊、ABI 测试和离屏像素烟雾测试。
- 不依赖 ICU；共享库应用只需部署 `lumatext.dll`，静态库将所需 Unicode grapheme 数据直接链接到最终程序。

## 构建

在 x64 Visual Studio Developer PowerShell 中执行：

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake 默认从带 SHA-256 校验的 FreeType `VER-2-13-3` 和 HarfBuzz `10.4.0` 上游归档构建。也可以设置 `-DLUMATEXT_USE_SYSTEM_FREETYPE=ON` 使用系统包。Unicode grapheme 表固定为 16.0.0，可用 `tools/generate_unicode_grapheme_data.py` 从带哈希校验的官方数据重新生成。

只分发 DLL 时可设置 `-DLUMATEXT_BUILD_STATIC=OFF`；安装目录不会包含 FreeType 开发库或头文件，运行时只需 `bin/lumatext.dll`。同时构建静态库时，安装包会保留静态 consumer 所需的 FreeType 开发文件，但最终应用仍没有 ICU 运行时依赖。

为减小发布体积，script 和 bidi 使用 Windows DirectWrite 的系统 Unicode 数据；极少数新脚本或复杂双向文本的分段结果可能随 Windows 版本变化。Grapheme 边界固定为 Unicode 16.0.0，不随系统变化。

画廊位于 `build/samples/gallery/lumatext_gallery.exe`。按 `Space` 切换 A/B 视图，按 `D` 切换浅色/深色背景，按 `1/2/3` 切换 direct、4x box、4x Mitchell 滤波。画廊分别展示 Microsoft YaHei UI、Segoe UI、Arial、Consolas，以及中文、emoji 和脚本 fallback。

微软雅黑专用实时对照程序为 `lumatext_compare`，支持左右候选比较、主题/字号/DPI/滤波/光学补偿调节及离屏截图，操作与实验范围见 [对照界面说明](docs/compare-ui.md)。候选效果尚未经过同字体 macOS 实机签收。

## 使用

```cmake
find_package(LumaText 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE LumaText::D2D)
```

可用 targets 为 `LumaText::Core`、`LumaText::D2D`、`LumaText::D3D11`、`LumaText::Input`、`LumaText::Shared` 和 `LumaText::Static`。`D3D11` 与 `Input` target 目前只保证接入端无需改 CMake；调用其创建 API 仍返回未实现。

最小 API 使用流程见 [samples/gallery/main.cpp](samples/gallery/main.cpp)。renderer 绑定创建它的设备线程。宿主可通过 `lt_d2d_desc.manage_begin_end_draw` 选择由 LumaText 或宿主管理 `BeginDraw/EndDraw`。

## 验收闸门

在继续 D3D11 和输入模块之前，必须按 [docs/phase-1-acceptance.md](docs/phase-1-acceptance.md) 在 4K/150% 实机对比签收并冻结默认校准参数。当前 macOS-like 灰度候选基线使用未 hint 的自然轮廓，默认值为 `gamma=0.85`、`contrast=1.00`、`stem_strength=0.00px`、`filter=Mitchell`，并按 physical em 与字重执行小字号 optical gamma 校准。该值在 Gallery 的 DirectWrite 对照中降低了低覆盖边缘的增益，避免常规和粗体行出现明显偏粗；仍需在 Microsoft YaHei UI 和 Pulse 4K/150% 实机上签收后才能冻结。

LumaText 自身使用 MIT 许可证。固定依赖 FreeType 使用 FTL，HarfBuzz 使用 MIT，Unicode 数据使用 Unicode License v3；完整许可文本位于 `LICENSES`。本项目不包含任何字体文件。
