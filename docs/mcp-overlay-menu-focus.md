# 覆盖层在外部弹出菜单期间的键盘焦点

## 问题

用户在 CAD 等程序弹出右键菜单后按快捷键截图，覆盖层显示了，但 Esc（以及 Enter、Ctrl+C 等）不起作用，无法取消截图。

## 根因

`Application::ShowViews` 用 `SetForegroundWindow(focus);SetFocus(focus);` 激活覆盖层。LumaShot 是托盘进程，不是当前前台进程；当前台程序正处于菜单模态循环（`GUI_INMENUMODE`）时，Windows 拒绝其他进程的 `SetForegroundWindow`，调用返回 0，覆盖层永远拿不到键盘焦点。此时按键继续投递给 CAD 的菜单，覆盖层的 `WM_KEYDOWN` 从未到达，`Key()` 里的 `VK_ESCAPE → Cancel()` 不会执行。

本机探针（自建前台进程打开 `TrackPopupMenu`，另一个进程模拟覆盖层）：

```
overlay: foreground before = Probe.MenuHost, menu-mode flag=1
overlay: plain SetForegroundWindow returned 0, is foreground now=0
overlay: AttachThreadInput trick attached=1 returned 1, is foreground now=1
overlay: WM_KEYDOWN vk=113
```

## 修复

`src/app/application.cpp` 新增 `ActivateOverlay(HWND)`，替换 `ShowViews` 末尾的两行调用：

1. 先尝试普通 `SetForegroundWindow` + `SetFocus`；成功即返回（常规场景行为不变）。
2. 失败时用 `AttachThreadInput` 临时共享当前前台线程的输入队列，再次激活并聚焦，随后立即解除共享。
3. 仍失败时至少 `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE)` 保证覆盖层可见可点。

不使用 `LockSetForegroundWindow`、模拟按键或常驻钩子。共享输入队列只在一次激活调用内存在，不影响菜单本身——菜单在覆盖层出现后仍保持打开，截图内容包含菜单。

## 测试

新增 `tests/menu_focus_test.cpp`（CMake 目标 `lumashot_menu_focus_test`，ctest 名 `capture_menu_focus`）：

- 启动本可执行文件的 `--menu-host` 子进程，创建自有窗口并 `TrackPopupMenu` 保持菜单打开、占据前台。
- 主进程走生产 `WM_HOTKEY(1)` → `Start` → `ShowViews` 路径（diagnostic session，不写偏好/剪贴板）。
- 断言：覆盖层可见后 `GetForegroundWindow()` 是覆盖层窗口；宿主线程仍处于 `GUI_INMENUMODE`；`SendInput` 发送真实 Esc 后会话取消。

修复前后对照（同一测试）：

| 版本 | overlay takes foreground | Escape cancels |
|---|---|---|
| 旧 `SetForegroundWindow;SetFocus` | FAIL | FAIL |
| `ActivateOverlay` | PASS | PASS（连续 3 次） |

回归：`capture_startup`、`capture_reselect`、`capture_to_pin`、`capture_latency`（首次因夹具像素抖动失败，两次复跑通过）、`selection_burst` 通过。`magnifier_follow`（`--hover`）在修复前后均 FAIL，输出统计项与 PASS 条件逐项吻合，失败原因与本次改动无关，未在本次处理。

## 依赖

需要真实前台输入桌面。在无前台会话的自动化环境中，测试第一项断言会因宿主无法成为前台而超时失败。
