# 剪贴板预览与键盘焦点修复

## 范围与发现

本次移除剪贴板主面板底部的“内容预览”卡片，保留列表摘要/缩略图和空格 Quick Look 浮窗。仅修改 LumaShot 剪贴板模块、相关测试及构建源文件列表；未修改相邻 Pulse 项目。

检查发现：

1. `src/clipboard/session_store.cpp` 将历史条目保存为加密文件后清空 `Entry.formats`，只保留摘要与文件租约。原 `src/clipboard/panel.cpp` 却直接把该元数据交给浮窗；浮窗在 `Show` 和 `Render` 中调用解码器，永远拿不到原图片。列表缩略图完成后重新显示同一份元数据也无法修复此问题。文本同样只显示了截断摘要。
2. 原键盘处理只截获 `WM_KEYDOWN`，遗漏 `TranslateMessage` 生成的 `WM_CHAR`。列表收到空格后开启预览，紧接着又把空格字符转发到原生搜索框并切换焦点；空搜索框本身也存在同样的问题。
3. 原 `Fold` 没有关闭 Quick Look；预览窗口也缺少不可激活标记，点击时可能改变主面板激活状态。
4. 原预览测试直接传入带原始 DIB 的内存条目，没有覆盖实际磁盘历史链路；仅检查窗口/ID，且部分检查用 `assert`，Release 构建会移除。没有验证真正显示出了图片像素。

## 实现

- 主面板从 480 × 920 DIP 缩短至 480 × 744 DIP，删除底部卡片及专用代码预览字体/绘制逻辑，保留五行列表和操作按钮。
- 新增 `src/clipboard/preview_data.h`、`src/clipboard/preview_data.cpp`：将原始条目转换为有界的图片帧或文本。图片最长边 640 px；文本最多 2048 个 UTF-16 单元，截断时避免拆开代理对。复制/粘贴原始内容不受预览截断影响。
- `SessionStore::LoadPreview` 在既有后台线程读取、解密、校验和解码，UI 不进行磁盘读取或图片解码。预览与复制/粘贴使用独立结果类型与令牌；快速切换只保留最新预览请求。关闭时清理待处理预览，过期完成结果不能重新打开窗口或覆盖当前选择。
- `PreviewWindow` 只接收有界展示数据，明确区分加载中、已完成和失败状态。损坏、不可读取或超出支持范围的图片显示终态错误，不再显示“无图片数据或正在加载”的混合占位。
- 浮窗使用 `WS_EX_NOACTIVATE`、`MA_NOACTIVATE`，关闭释放图片、文字与渲染资源。重建渲染目标时重建其位图，不复用旧目标资源。
- 折叠、清空、删除、禁用及筛选后没有选中条目时，关闭或取消对应预览。

## 键盘规则

- 打开面板默认焦点在列表；空格打开/关闭预览，长按不会反复切换，也不会产生搜索空格。
- 直接输入普通文字进入搜索；Ctrl+F 或 Tab 可进入搜索框。
- 搜索框非空时，空格正常输入，不侵占多词查询。
- 搜索框中按上下键切换条目并返回列表焦点；随后空格预览筛选结果，不改变查询。
- 搜索框按 Tab 返回列表。空搜索框按空格开启预览并返回列表焦点，消费对应的字符消息。
- Esc 优先关闭预览，再次按下折叠面板；Enter 和原有复制/粘贴路径保留。

## 资源边界

按需预览源像素上限为 32 × 1024 × 1024，覆盖常见 4K PNG；超过上限明确报错。640 × 640 BGRA 结果像素最多约 1.56 MiB。列表缩略图维持原有 128 px 和独立解码限制，不用列表缩略图冒充 Quick Look 数据源。

上述数值不是全进程内存上限：加密载荷、WIC 解码器、原图解码暂存、DC/D2D 资源及新旧请求交接会产生额外瞬时内存。不沿用旧报告中“解码绝不产生原图规模内存”的结论。

## 验证与交付

首轮 `build.bat` 成功，6 项专项回归通过（27.18 秒）：`clipboard_history`、`clipboard_panel`、`clipboard_file_icons`、`clipboard_session_store`、`clipboard_disable_memory`、`clipboard_preview`。

新增/强化测试覆盖：真实 `TranslateMessage` 键盘流程、默认焦点、搜索空格、上下键后预览、Tab、重复空格、两级 Esc；元数据历史的异步文本/图片预览；DIB、DIBV5 和压缩 4K PNG；读取损坏及解码失败；快速切换、过期结果、关闭/折叠/删除取消；预览和复制并行；真实绘制像素、窗口不可激活、重建目标及 50 轮开关资源释放。相关检查使用 Release 下仍执行的断言函数。

所有数据均为合成测试条目，使用隔离临时目录和模拟粘贴接收端。没有读取或修改系统剪贴板，没有使用个人文件/偏好作为夹具，没有运行会改动真实剪贴板的 `clipboard_file` 集成测试。没有将通过编译等同于功能验证，也未声称已测试第三方输入应用、所有输入法或全部多显示器组合。

已人工检查合成的浅色 100% 和深色 150% 主面板截图，底部卡片已移除、列表和页脚无遮挡。截图输出位于 `build/`，日志在仓库根目录，不作为文档正文或用户数据保存。

## 最终复验

- 最终 `build.bat` 成功（59 步），6/6 相关测试再次全部通过，用时 28.67 秒；剪贴板源代码诊断返回 0 条错误/警告。
- `build/clipboard-quicklook-fixed.png` 显示从合成 3840 × 2160 PNG 的加密历史载荷生成的实际预览，已人工检查内容和宽高比；测试同时检查对应中心像素。
- Quick Look 50 轮启闭：私有提交从 20,496,384 字节到 21,975,040 字节，低于测试允许的 8 MiB 增量；每轮专有图像/渲染资源均检查释放。不把分配器的波动解释为全进程零泄漏证明。
- 日志：`clipboard-preview-fix-build.log`、`clipboard-preview-fix-tests.log`、`clipboard-preview-fix-final-build.log`、`clipboard-preview-fix-final-tests.log`。逐条检查输出在 `build/Testing/Temporary/LastTest.log`。

## 安装包

现有 `scripts/package.ps1` 与 `scripts/build-installer.ps1` 成功完成（命令退出码 0，Inno 编译 18.734 秒）。

- 安装包：`dist/LumaShot-Setup.exe`
- 安装包 SHA-256：`47c0b0a9cfab9182766a7861e087aaae882f22f54c1051d33babb6b3f0eba8e4`
- 主程序：`build/LumaShot.exe`
- 安装载荷：`dist/LumaShot-setup-payload/LumaShot.exe`
- 主程序及载荷 SHA-256 均为：`a285f759fa455675e7c4a232f027a13709cd7bf393b370c8dc1b95fff96c783c`
- 已使用 `cmp -s` 和 SHA-256 双重确认构建主程序与打包载荷逐字节一致。
- 打包日志：`clipboard-preview-fix-package.log`、`clipboard-preview-fix-installer.log`、`clipboard-preview-fix-hashes.log`。

没有运行安装程序或替换已安装应用；生成和检查安装包不等于已验证在全新系统上的安装执行过程。

