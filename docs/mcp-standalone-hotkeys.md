# 单键快捷键与弹出菜单期间的截图

## 需求

1. 截图、GIF、录屏快捷键支持 F1 等单键，不强制携带修饰键。
2. 在 CAD 等程序弹出右键菜单后，Ctrl/Shift 组合键无法触发截图，导致截不到弹出菜单。

## 现状

源码在本次会话开始前（2026-09-17 11:06）已包含单键支持：

- `src/app/hotkeys.h`：`StandaloneShortcutKey` 允许 F1–F11、F13–F24、PrtSc、Pause、ScrollLock 单独绑定；`ValidShortcut` 对字母、数字等普通键仍要求修饰键，F12 保留。
- `src/app/settings_dialog.cpp`：录入钩子接受单独功能键；提示文案「请按下快捷键（F1–F11 可单独使用）…」。
- `src/app/preferences.cpp`：加载时使用 `ValidShortcut` 校验，单键配置不会被回退为 Ctrl+Alt+A。
- `src/app/application.cpp`、`ApplyShortcuts`：注册逻辑与修饰键无关，`RegisterHotKey(main_, id, 0 | MOD_NOREPEAT, key)` 直接生效。

但用户正在运行的是 `%LOCALAPPDATA%\Programs\LumaShot\LumaShot.exe`（08:17 构建，进程 09:51 启动），其二进制中不含单键提示字符串，因此界面仍拒绝单键。`build/LumaShot.exe`（11:07）已包含该功能。根因是安装版落后于源码，不是源码缺失。

## 本次改动

### `src/app/settings_dialog.cpp`

`ShortcutLabel` 对单键专用键给出可读名称。本机实测 `GetKeyNameTextW`：PrtSc 返回「Sys Req」，Pause 无扫描码返回空，F13–F24 返回空。现在 PrtSc → `PrtSc`，Pause → `Pause`，F13–F24 → `F13`…`F24`；其余键仍走系统键名。

### `tests/settings_dialog_test.cpp`

新增断言：

- `ValidShortcut` 对 F1、F11、PrtSc、Pause 单独有效，对 Shift+F1 有效；对 A、Space、F12、Ctrl+F12 无效。
- 单键标签 `PrtSc`、`Pause`、`F13`、`Shift + F24`。
- 录入场景：Ctrl+F12 拒绝后松开 Ctrl，单按 F1 立即提交且 `modifiers==0`；标签为 `F1`；再次录入并在按住 Ctrl 时失焦，钩子移除且已提交的 F1 保留。
- 配置回写：`key=VK_F1, modifiers=0` 经 `SaveTo`/`LoadFrom` 后不被回退。

## 弹出菜单期间的热键行为

`RegisterHotKey` 由系统在原始输入层匹配，不经过前台窗口消息循环。菜单模态循环（`TrackPopupMenu`）只影响本进程窗口消息，不阻止热键投递给 LumaShot。用本机探针验证（自建前台窗口 + 自建弹出菜单 + `SendInput`，未触碰用户桌面）：

```
phase0: Ctrl+Shift+F9 while menu open  -> WM_HOTKEY id=1 in_menu=1
phase1: F9 alone while menu open       -> WM_HOTKEY id=2 in_menu=1
RESULT combo_delivered=1 combo_while_menu_open=1 single_delivered=1 single_while_menu_open=1
```

标准 Win32 菜单下组合键与单键都能触发，且菜单保持打开。CAD 中组合键失效的原因不在 Win32 菜单本身，而是 CAD 类程序常见的两种行为：

- 自绘菜单/浮动面板在收到 Ctrl 或 Shift 按下时立即关闭或切换状态（很多 CAD 用 Shift/Ctrl 作为捕捉、多选修饰，按下即处理）。菜单在 `WM_HOTKEY` 到达前已消失，截图内容自然没有菜单。
- 程序通过低级键盘钩子或 DirectInput 在修饰键阶段就吞掉输入，使系统热键匹配不到完整组合。

无论哪种情况，单键 F1 等不需要先按修饰键，菜单不会在截图开始前被关闭，因此单键就是这个场景的解决方案。这也是 `StandaloneShortcutKey` 注释和 `docs/recording-design.md` 中记录的设计意图。

截图时机：`Application::Start` 在 `WM_HOTKEY` 到达后立即冻结光标并在后台线程 `BitBlt(CAPTUREBLT)` 整个桌面，覆盖层在拿到帧后才显示（`SW_SHOWNOACTIVATE`），不会先激活自身把菜单关掉。菜单在按键瞬间仍可见即可被捕获。

覆盖层出现后能否接收 Esc/Enter 是另一个问题：前台程序处于菜单模态时会拒绝托盘进程的 `SetForegroundWindow`，见 `docs/mcp-overlay-menu-focus.md`。

## 验证

- `build.bat`（/W4 /WX）通过，`get_diagnostics` 无错误无警告。
- `build\lumashot_settings_test.exe`：159 项 PASS，0 FAIL，含新增 7 项。
- `build\lumashot_capture_test.exe`：Failures 0。`build\lumashot_startup_test.exe`：PASS。
- 弹出菜单热键探针：见上文输出。探针源码不进入仓库。

## 使用建议

- 更新到本次构建（`dist/LumaShot-Setup.exe`）后，托盘 → 设置 → 点击截图快捷键框 → 直接按 F1（或其他 F 键、PrtSc、Pause）。
- 若 F1 与 CAD 自身帮助键冲突，推荐 F9、F10、Pause 或 PrtSc。PrtSc 若已被系统「截图工具」占用，注册会失败并在设置中提示，需先在 Windows 设置中关闭「使用 Print Screen 键打开屏幕截图」。
- 字母、数字、空格等普通键仍需修饰键，避免全局绑定吞掉正常输入。
