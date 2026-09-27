# LumaText 独立字体渲染器实施计划

## 总结

在 `C:\Users\SS\Desktop\lumatext` 建立独立 MIT 开源 C++20 项目。首版支持 Windows 10/11 x64，以 Pulse 为首个宿主：

- DirectWrite继续负责 shaping、字体 fallback、双向文字、换行、测量和命中测试。

- FreeType负责灰度字形栅格化，采用自然轮廓、轻微字干补偿和可校准覆盖曲线。

- LumaText负责字体桥接、R8 glyph atlas、D3D11 批量绘制、D2D 兼容后端及设备恢复。

- 不包含字体文件，不使用 Apple 字体，不复制 MacType GPL 源码。

- 先完成 2–3 周真实 Pulse A/B 原型，用户实机验收后才继续完整 1.0。

最终效果目标是右侧方向样张的进一步校准版：小字号中文笔画更稳定、更实、无彩边，浅色与深色背景字重一致；字形仍是微软雅黑 UI，不承诺与苹方/CoreText 像素级相同。

## 项目架构

- `lumatext_core`：DirectWrite glyph run 收集、字体文件流桥接、FreeType face 管理、栅格任务、缓存和渲染策略。

- `lumatext_d2d`：使用 A8 mask 和 `FillOpacityMask` 的即时兼容后端，保证透明窗口、菜单、WARP 和故障回退可用。

- `lumatext_d3d11`：R8 atlas、矩形分配、LRU 淘汰、实例化 glyph quad 和批量提交，供高频文件列表使用。

- `lumatext_input_win32`：可选的单行输入组件，覆盖搜索、地址、重命名和数值输入，支持 TSF/中文 IME、光标、选区、剪贴板、撤销及 UI Automation。

- `samples` 提供字体画廊、透明窗口、文件列表和输入法示例`tests/bench` 提供图像回归、ABI、性能和设备恢复测试。

字体缓存键包含字体文件身份、TTC face index、variable axes、glyph ID、物理字号、子像素相位、模拟样式和渲染配置。FreeType实例与 face 按工作线程隔离，避免跨线程共享非线程安全状态。

系统字体优先通过 DirectWrite font stream 读取；支持 TTC、TrueType、OpenType 和 variable font。彩色字体、远程字体、无法读取的自定义字体及不支持的 glyph run 自动回退 DirectWrite，不能显示空白字形。

## 公共接口

发布稳定 C ABI 和头文件式 C++ RAII 包装，不跨 DLL 边界传递 STL、异常或由另一侧释放的内存。所有描述结构以 `struct_sizeabi_version` 开头。

主要 C API：

```cpp

lt_result lt_context_create(const lt_context_desc*, lt_context**);

lt_result lt_d2d_renderer_create(lt_context*, const lt_d2d_desc*, lt_renderer**);

lt_result lt_d3d11_renderer_create(lt_context*, const lt_d3d11_desc*, lt_renderer**);

lt_result lt_frame_begin(lt_renderer*, const lt_frame_desc*, lt_frame**);

lt_result lt_frame_draw_layout(

    lt_frame*, IDWriteTextLayout*, const lt_draw_text_desc*);

lt_result lt_frame_flush(lt_frame*);

lt_result lt_frame_end(lt_frame*);

lt_result lt_input_create(const lt_input_desc*, lt_input**);

bool lt_input_handle_message(lt_input*, HWND, UINT, WPARAM, LPARAM);

lt_result lt_input_get_visual_state(lt_input*, lt_input_visual_state*);

void lt_release(void*);

```

- `lt_draw_text_desc`包含位置、裁剪、前景色、背景类型和渲染配置。

- 默认配置为对透明背景安全的校准灰度覆盖；已知纯色背景可选择精确线性光混合。

- `lt_context_desc`提供日志、异步 glyph 就绪通知、内存限制和 DirectWrite factory。

- 渲染器绑定创建它的设备线程；context、字体缓存和栅格队列支持并发。

- DLL 与静态库同时发布；提供 `LumaText::CoreLumaText::D2DLumaText::D3D11LumaText::Input` CMake targets。

## 实施流程

| 阶段 | 时间 | 交付与闸门 |

|---|---:|---|

| 0. 工程基线 | 2–3 天 | CMake、MIT/FTL 许可、固定 FreeType 版本、CI、画廊与基准框架 |

| 1. 真实效果原型 | 2–3 周 | DirectWrite-to-FreeType 桥接、D2D mask 后端、Pulse 文件列表 A/B 开关、100%/125%/150% 样张 |

| 2. 生产渲染核心 | 3–5 周 | D3D11 atlas、批处理、异步冷缓存、内存上限、设备丢失、DirectWrite fallback |

| 3. 单行输入模块 | 3–5 周 | TSF/IME、composition、光标、选区、剪贴板、撤销、候选窗定位和 UIA |

| 4. Pulse 全量接入 | 2–4 周 | 替换 Pulse 自有文字路径，接入菜单、对话框、Quick Look 外壳和输入框，完整回归 |

| 5. 1.0 发布 | 1 周 | ABI 检查、文档、示例、NuGet/ZIP/CMake 包、PDB、SHA-256、SBOM 和许可证清单 |

阶段 1 必须由用户在 4K、150% 实机对比签收。验收不过即停止，不投入后续工程化；验收通过后冻结默认灰度曲线和字干补偿基线。

总投入约 10–18 个高级工程师周。单人预计 3–4.5 个月；两人并行预计 7–11 周，但原型验收和 Pulse 最终接入仍是串行路径。

## 验证标准

- 画廊覆盖 12/13/14/16 DIP、中英文、数字、标点、混合文件名、粗体、variable font 和字体 fallback。

- 在 100%、125%、150%、200%、300% DPI 下验证基线、裁切和布局；LumaText不得改变 DirectWrite 的换行、命中测试或文本宽度语义。

- 浅色、深色、高对比度、透明材质和纯色背景均无彩边、暗边、光晕或明显字重跳变。

- 4K/150%、5,000 个可见 glyph 的热缓存文字提交目标为 p95 不超过 1.5 ms；默认 GPU atlas 限制为 16 MiB，可配置上限 24 MiB。

- 冷缓存不得造成持续白字或闪烁；超出单帧栅格预算时暂用 DirectWrite绘制该 glyph run，缓存完成后通知宿主重绘。

- 覆盖硬件 D3D11、WARP、设备丢失、窗口跨显示器、DPI 切换、atlas 淘汰和低显存状态。

- 输入模块覆盖微软拼音 composition、候选窗位置、英文输入、代理对、emoji、选择、撤销、剪贴板、焦点切换和 Narrator。

- ABI 测试验证旧版本头文件可加载同一 major 版本的新 DLL，未知结构尾字段可忽略，错误不跨边界抛异常。

## 发布与 Pulse 接入

- 版本采用 SemVer`0.1` 为画廊原型`0.3` 为 Pulse A/B`0.8` 为完整候选版，Pulse 全量回归后发布 `1.0.0`。

- Release包包含 x64 DLL、静态库、导入库、头文件、CMake config、PDB、许可证、SBOM 和哈希清单。

- Pulse开发构建通过 `LUMATEXT_SOURCE_DIR` 使用同级源码；正式构建固定到 LumaText 的版本和提交哈希。

- Pulse保留 DirectWrite故障回退，但正式版本不提供面向用户的渲染器开关。

- Office/XLSX、Shell preview handler及其他第三方宿主内部文字保持原样。

## 默认边界

- 1.0 不支持 ARM64、macOS、Linux、HDR/scRGB、竖排文字编辑或多行富文本编辑器。

- 不打包微软雅黑、Segoe UI、SF Pro、苹方或其他字体。

- FreeType采用 FTL 许可并保留署名；LumaText自身采用 MIT。

- 维护成本包括 FreeType安全更新、Windows/显卡驱动回归、每个 Windows功能更新的兼容测试，以及未来新增 ARM64 时的独立性能验收。

