# 剪贴板快捷键按输入焦点分流

日期：2026-09-21

## 行为与根因

默认 Win+V（以及同一入口的自定义剪贴板快捷键）原先在 `src/clipboard/panel.cpp` 的 `WM_HOTKEY` 分支中直接调用 `ShowQuick()`，未判断当前是否存在可编辑输入焦点。

修复后的行为：

- 无输入焦点，或焦点在普通窗口、按钮、只读文本框、选择型下拉框：展开完整剪贴板。
- 焦点在可编辑输入框：打开不激活的小剪贴板，保留原输入框焦点。
- 小剪贴板打开且仍在输入框中时，再次按快捷键仍可关闭小面板。
- 不复用已经失效的历史输入焦点；本应用的搜索框不会误用此前的外部输入目标。
- 保留快捷键禁用、主机策略、输入法候选窗口和粘贴/复制事务保护。

## 修改范围

- `src/clipboard/panel.cpp`：快捷键分流、短期异步探测、焦点变化及关闭时取消过期结果。
- `src/clipboard/input_focus.h`、`src/clipboard/input_focus.cpp`：原生 EDIT、RichEdit、可编辑组合框、Scintilla 的可编辑性判断，以及自定义控件的 UI Automation 元数据探测。
- `tests/clipboard_shortcut_focus_test.cpp`：专门的焦点分流与保护逻辑回归。
- `CMakeLists.txt`：注册新增模块、系统 UI Automation 链接库和测试目标。

原生控件即时判断；未知控件在独立 MTA 线程中查询焦点、可编辑性等属性，不查询输入文本、控件名称或剪贴板内容。每个面板最多一个探测任务在执行。UI 等待上限为 300 ms，慢或不可用的可访问性提供程序降级为完整面板；不会阻塞键盘钩子或 UI 线程。过期结果不能重新打开面板。

## 验证

`build.bat` 成功，使用现有 C++20、`/W4 /WX` 配置。`src/clipboard` 的诊断工具未报告错误或警告；实际编译和测试是主要验证依据。

以下 5 组针对性 CTest 全部通过：

1. `clipboard_shortcut_focus`：33/33 项检查通过。
2. `clipboard_quick_input`。
3. `clipboard_quick_window`。
4. `clipboard_continuous_paste`。
5. `hotkey_policy`。

焦点回归使用私有非输入桌面上的真实 Win32 控件，保留真实可见性、焦点和只读状态判断；只替代前台桌面桥接与低层输入钩子安装。不会切换用户的输入桌面，不启动剪贴板监听，不读取个人历史、设置或真实剪贴板，也不注入键盘输入。

覆盖无焦点、普通窗口、按钮、可写/只读 EDIT 与 RichEdit、选择型组合框、失效历史焦点、原焦点保留、重复快捷键、输入框到按钮的切换、禁用状态、主机策略、IME、延迟结果取消、探测超时和可访问性元数据策略。

日志位于：

- `build/clipboard-shortcut-build.log`
- `build/clipboard-shortcut-final-build.log`
- `build/clipboard-shortcut-tests-final.log`
- `build/clipboard-shortcut-package.log`
- `build/clipboard-shortcut-installer.log`

验证边界：未逐一对浏览器、聊天软件等第三方输入控件做真实桌面端到端实测。可访问性提供程序未暴露可编辑属性或超时的情况下按上述降级策略处理。未运行无关历史测试套件。

## 安装包交付

- 安装包：`dist/LumaShot-Setup.exe`
- 大小：37,532,496 字节。
- SHA-256：`bd372e3746beb152869763e9445d89a20d203d0d91d3b7efe5991d5ea52f3e4f`
- 应用二进制 `build/LumaShot.exe` 与 `dist/LumaShot-setup-payload/LumaShot.exe` 逐字节一致。
- 两份应用二进制的 SHA-256：`f3b6c3c581627ed9467b33851d5f9e46404ce7c2e86b1600aae7bb5ec7a1a821`
- 现有打包脚本的运行库、许可证和依赖验证通过；Inno Setup 编译成功；安装包 MZ 文件头及 SHA-256 已核验。

未运行安装程序，未替换已安装应用，未进行安装/升级执行测试。未修改相邻 Pulse 项目。
