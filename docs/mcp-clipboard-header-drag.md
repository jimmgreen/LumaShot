# 剪贴板面板：全宽标题栏拖动

## 需求与范围

允许从剪贴板面板顶部标题栏的空白、快捷键提示和品牌区域拖动窗口，而不只限于左侧图标与标题。搜索框、分类、内容区及置顶、设置、更多按钮保留原交互。窗口外缘仍用于原生缩放，不改成拖动区。

## 原因与实现

原来的 `WM_LBUTTONDOWN` 使用 `y < 65 && x < 154` 判定展开面板的拖动区域，导致右侧大部分标题栏无法拖动。

- `src/clipboard/panel.cpp`：增加 `HeaderDragHit`，以当前面板宽度和搜索框上沿（76 DIP）限定标题栏，并排除现有控件的完整点击矩形。
- 搜索框上沿与拖动判定共用 `HeaderBottom`，不改变原有绘制位置。
- 仍使用既有 `BeginDrag` / `Drag` / `EndDrag`，保留移动阈值、鼠标捕获、位置记忆、工作区边界约束及折叠侧栏吸边。
- 置顶时不抢焦点、菜单关闭不穿透点击、搜索输入、分类切换及边缘缩放保持原逻辑。
- `tests/clipboard_header_drag_test.cpp`：新增独立的隐藏原生窗口消息回归。
- `CMakeLists.txt`：注册 `clipboard_header_drag`，测试工作目录为 `build/clipboard-header-drag/`。

未修改兄弟项目、用户设置、真实剪贴板内容或已安装应用。

## 验证结果（2026-09-21）

### 本次变更的直接验证

- 先增加测试再修复：原来的拖动逻辑在 1147 项检查中失败 410 项；修复后同一测试为 **1147 项检查、0 失败**。
- 合成缩放参数：100%、125%、150%、200%；面板宽度：400、560 DIP。
- 覆盖标题栏横向空白、快捷键区域、两侧留白、按钮间隙、按钮上下空白、实际窗口位移、移动阈值、捕获释放与取消、位置锚点、置顶不激活、菜单关闭、搜索、分类和边缘缩放。
- 最终定向 CTest：`clipboard_header_drag`、`clipboard_panel_pin`，**2/2 通过**。
- `build.bat`：Release / C++20 / `/W4 /WX` 构建成功。
- 编辑器诊断：返回 0 条 error/warning；不将空诊断代替编译或功能测试。
- 测试使用隐藏窗口、合成输入状态和窗口消息，不注入系统鼠标输入，不以个人剪贴板或设置为测试素材。

### 额外回归的未通过项

同时运行的 `clipboard_panel` 总回归未通过，唯一报告的失败断言为：

```text
FAIL clipboard shortcut dispatch resumes when allowed
```

该断言位于 `tests/clipboard_panel_test.cpp` 的快捷键检查中，要求 `p.expanded` 为真；本次修改前读取到的 `WM_HOTKEY` 已调用 `ShowQuick()`，本次没有修改这条快捷键路径。没有删除、跳过或修改该断言来制造全绿结果；总回归不能记为通过。

曾在 `build/clipboard-header-drag-baseline/` 中生成撤回本次拖动改动的隔离代码副本以尝试对照。该副本经过编码/换行重建，不是原始字节归档；隔离测试进程在产生测试输出前以 `0xC0000409` 退出。因此这次隔离探测没有有效行为结果，不能用于证明原版本测试通过或失败。生产源文件没有为该探测回退。

本次只交付标题栏拖动修复，不扩展修改快捷键功能。真实混合 DPI 多显示器拖动及安装后手动操作仍需验收；没有声称完成已安装应用的实机验证。

## 安装包

使用 `scripts/package.ps1 -PackageName LumaShot-setup-payload` 和 `scripts/build-installer.ps1` 生成。

- 安装包：`dist/LumaShot-Setup.exe`
- 大小：37,519,229 bytes
- SHA-256：`ae6253ae5cf1b49a4bce74d2705fa589488ba5d4ad55ededc72a85a7b0a20496`
- 安装包版本信息：LumaShot Setup，0.1.0。
- Inno Setup 编译成功；验证了安装包 MZ / PE 头。
- 主程序、3 个工作进程、ONNX Runtime、LumaText、FFmpeg 和 Gifsicle 共 8 个文件的构建产物与打包载荷 SHA-256 全部一致。
- `build/LumaShot.exe` 与 `dist/LumaShot-setup-payload/LumaShot.exe` 的 SHA-256 均为 `af5ea8cd651b16eafd83ed69e34faa60460bdc49bfec59f280e34390cca6c8de`。
- **未运行安装包、未替换已安装应用。** 安装执行与升级流程不在本轮验证范围内。

## 可复核记录

- `build/clipboard-header-drag-red-build.log`
- `build/clipboard-header-drag-red-tests.log`
- `build/clipboard-header-drag-build.log`
- `build/clipboard-header-drag-tests.log`：包含总回归未通过项。
- `build/clipboard-header-drag-focused-final.log`
- `build/clipboard-header-drag-focused-details.log`
- `build/clipboard-header-drag-baseline-build.log`：隔离对照未完成的诊断记录。
- `build/clipboard-header-drag-package.log`
- `build/clipboard-header-drag-installer.log`
- `build/clipboard-header-drag-delivery.json`
