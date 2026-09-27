# Win + 反引号截图快捷键冲突排查

日期：2026-09-22。本轮仅诊断，未修改应用代码或个人设置，未关闭终端进程，未触发截图，也未注入键盘输入。

## 本机证据

- Windows Terminal 1.24.11911.0 正在运行。安装目录的 defaults.json 定义 `Terminal.QuakeMode`，默认绑定为 `win+sc(41)`（反引号/波浪号所在的物理键）。用户 Terminal settings.json 的定向检索未发现相关覆盖项。
- 通过临时线程级 `RegisterHotKey(NULL, id, MOD_WIN | MOD_NOREPEAT, 0xC0)` 探测，返回失败，Win32 错误 **1409 / ERROR_HOTKEY_ALREADY_REGISTERED**。探测不解除其他程序的绑定；若注册成功才会立即释放自身绑定。本次探测未成功注册。
- LumaShot 正在运行；检查时持久化截图配置为 Hotkey=88、Modifiers=1，即 **Alt+X**；HotkeysDisabled=0、DisableHotkeysInGame=0。该保存值仅说明检查时的状态，不否认用户此前尝试设置 Win+反引号。
- 结合用户按键打开 PowerShell 的现象、Terminal 的运行状态及本机默认绑定，可定位为 Terminal 下拉终端快捷键与截图键冲突。Win32 探测本身只证明被占用，不返回占用者 PID。

## 代码检查

- `src/app/settings_dialog.cpp`：录入直接保存 KBDLLHOOKSTRUCT.vkCode，未发现将反引号解析为启动 PowerShell 的逻辑；新增组合键通过 RegisterHotKey 验证。
- `src/app/hotkeys.h`：截图、GIF、录屏用 RegisterHotKey 注册，变更失败时回滚。
- `src/app/hotkey_policy.cpp`：启动或恢复时注册失败通过托盘气泡提醒。显示保存的偏好不等于组合键此刻已被成功占用；本轮未更改提醒机制。

## 建议处理

若要将 Win+反引号留给截图，应在 Windows Terminal 的设置/操作中移除或改绑 Quake Mode 的对应快捷键，再在 LumaShot 重新录入并保存。若旧注册状态未刷新，可重新启动 LumaShot。不要关闭用户的终端会话，也不要用强制键盘钩子抢占来绕过冲突。

此操作会改变另一个应用的快捷键，需用户确认后再代为修改。本轮没有改动 Terminal、LumaShot 的个人配置，也不需要重建安装包。

证据日志：`build/win-backtick-discovery.log`、`build/win-backtick-probe.log`；探测脚本：`build/win-backtick-probe.ps1`。
