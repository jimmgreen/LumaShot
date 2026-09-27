# LumaShot 代码审查报告（2026-09-25）

范围：`src/` 全部 9 个模块（约 186 个文件，约 1 MB 源码）。审查方式：通读核心链路（capture / export / application 截图流程、IPC、设置持久化、OCR 与录制子进程协议、剪贴板存储），其余 UI/模型代码采用定向检索（资源释放、线程、钩子、临时文件、进程生命周期）。**本报告只读审查，未改任何代码，也未构建。**

严重度：**高** = 隐私/数据风险或与文档承诺明显不符；**中** = 实际会遇到的缺陷；**低** = 健壮性/可维护性。

---

## 高

### H1 剪贴板历史实际会写入磁盘并跨重启恢复，与 README 承诺不符
- 位置：`src/clipboard/panel.cpp:443` → `SessionStore(..., L"clipboard-history", /*persistent=*/true)`；`src/clipboard/session_store.cpp`（`Init` / `CommitIndex` / `Restore`）。
- 现状：内容（文本、图片、文件路径）与索引经 DPAPI 加密后保存在 `%LOCALAPPDATA%\LumaShot\clipboard-history\history-v1`，下次启动时恢复。关闭剪贴板功能时（`ReleaseDisabledResources`）会先 `Commit` 索引，文件**继续保留**。
- 文档：README 写的是“历史与收藏仅保存在内存，关闭功能或退出即清空”；`docs/mcp-clipboard-session-storage.md` 写的是“只保留在当前会话，不跨重启”。
- 风险：复制过的密码（来源程序没有设置排除格式时）、令牌、私人图片会长期留在磁盘上。
- 建议：先确认产品意图。① 如果应只保存在当前会话：改回 `persistent=false`，并在启动时清理 `history-v1`；② 如果有意持久化：更新 README，在设置中加上“退出后保留历史”开关（默认关闭），并在关闭剪贴板功能时提供“清除已保存历史”。

## 中

### M1 默认开机自启，且首次运行就写注册表 Run 项，与 README 不符
- 位置：`src/app/preferences.h`（`start_with_windows{true}`），`preferences.cpp` 读取时默认为 1；`application.cpp` 中 `Run()` 每次启动都会调用 `ConfigureLoginStartup(...)`。
- README 写“不自动启动”。便携版（`dist/LumaShot-ocr/`）运行一次就会把便携目录路径写入 `HKCU\...\Run`，之后删除该目录会留下失效的启动项。
- 建议：默认改为 false（或只在安装版中开启），便携版不写 Run；或者修改 README 说明。

### M2 设置文件以 ANSI 编码创建，路径或字体名中的非 ANSI 字符会变成 “?”
- 位置：`src/app/preferences.cpp`（`SaveTo`）、`src/app/tool_preferences.cpp`、`src/clipboard/layout.h`。
- 原因：`WritePrivateProfileStringW` 在目标文件不存在时会创建 **ANSI** INI，只有已带 UTF‑16 LE BOM 的文件才会以 Unicode 写入。`Folder`（保存目录）和 `font_family` 直接写入；代码里 `number_label` 已改用十六进制编码，说明这个问题之前遇到过，但没有覆盖到这两个字段。
- 复现思路：保存目录选一个包含当前代码页之外字符的路径（如在简体系统上用韩文或 emoji 目录名），重启后保存目录会失效。
- 建议：在 `SaveTo` 中先创建只含 BOM（`FF FE`）的临时文件，再复制或写入；首次迁移时把旧的 ANSI 文件转换为 UTF‑16。

### M3 `%TEMP%\LumaShot-Clipboard` 中的文件从不清理
- 位置：`src/export/clipboard.cpp`（`PrepareClipboardFile`）。已放入剪贴板（published）的文件会一直保留（这是为了保证之后粘贴有效），但整个项目里没有任何清理逻辑。开启“截图粘贴为文件”（默认开启）时，每复制一次就多一个 PNG/BMP，BMP 尤其大。
- 建议：启动时（以及每次新复制前）删除该目录中超过 N 小时的文件，并跳过当前剪贴板里的 `CF_HDROP` 指向的那个文件。

### M4 从托盘退出或注销时，录制进程被直接杀掉，未保存的录制丢失且临时视频残留
- 位置：`src/recording/process.h` 中 Job 设置了 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`；`application.cpp` 中托盘“退出” → `WM_CLOSE` 时不检查 `recording_process_.Active()`。
- 结果：录制窗口自带的“当前录制尚未保存”提示被绕过；正在导出的 MP4/GIF 被中断；`%TEMP%\LumaShot-Recording\{GUID}\capture.mp4`（可能有数 GB）残留，而且项目中没有孤儿目录清理。
- 建议：退出前如果录制进程还在运行，先向它发 `WM_CLOSE`，让它走自己的确认流程，或者提示“录制进行中，确定退出？”；录制进程启动时清理超过一定时间、且没有活跃租约的 `LumaShot-Recording\*` 目录。

### M5 Win+V 低级键盘钩子挂在忙碌的 UI 线程上，并在钩子回调里做重操作
- 位置：`src/clipboard/win_v_shortcut.h`。每次按下 Win+V 时，钩子内会调用 `guard()` → `RefreshHotkeys()`（其中有 Register/UnregisterHotKey、`FullScreenGameActive`、`Shell_NotifyIcon`）。
- 风险：`WH_KEYBOARD_LL` 回调运行在安装钩子的线程上。UI 线程只要卡住（例如截图覆盖层首帧准备、剪贴板存储析构时 `WaitIdle` 最多 15 秒、同步 `Flush` 设置），**整个系统的键盘输入都会延迟**；超过 `LowLevelHooksTimeout` 后，Windows 会静默移除钩子，Win+V 失效且没有任何提示。
- 建议：钩子回调里只做判断并 `PostMessage`，把 `RefreshHotkeys` 移出回调；或者把钩子放到一个专用的轻量线程上，并在该线程运行消息循环。

## 低

| # | 位置 | 问题 | 建议 |
|---|---|---|---|
| L1 | `application.cpp` `Start()` | `FrozenCursor::Snapshot()` 失败（GetCursorInfo/CopyIcon/GetIconInfo）时抛异常，整次截图中止 | 捕获异常，退化为不带指针的截图 |
| L2 | `export/clipboard.cpp` `PublishClipboardImage` | `CF_DIB` 成功、`CF_HDROP` 失败时，报“复制失败”，但图片其实已在剪贴板上 | 区分成“图片已复制，文件形式失败”的提示 |
| L3 | `application.cpp` `ResultReady` 贴图编辑分支 | `PublishClipboardImage` 抛异常时发生在 `CompleteAnnotation` 之前，这次贴图标注结果会丢失 | 先完成标注，再发布到剪贴板，或者分别 try |
| L4 | `export/png.cpp` | 使用了 `std::system_error`，但没有 `#include <system_error>`（靠间接包含才能编译） | 补上 include |
| L5 | `app/settings_process.cpp` | 设置子进程无限期等待 response；父进程只在 `Settings()` 的嵌套循环中处理请求，其他模态循环（保存对话框、主题消息框）期间子进程会卡住 | 子进程等待加超时，或在其他模态循环中也处理该事件 |
| L6 | `clipboard/native.cpp` `Private()` | 没有识别 `Clipboard Viewer Ignore` 格式（部分密码管理器使用） | 加入检测 |
| L7 | `clipboard/session_store.cpp` `Restore` | 单条读取失败（DPAPI 或杀毒软件临时锁定）会被永久删除；名为 `数字.bin` 的目录会导致整个存储不可用；缺少 index.bin 时会删除全部内容（与注释“只在读到有效索引后清理”不符） | 读取失败的条目保留，下次再试；遇到目录跳过而不是抛异常；缺少索引时不删除 |
| L8 | `SessionStore::~SessionStore` | 持久模式下在 UI 线程上 `WaitIdle(15000)` | 缩短超时时间，或把等待移到后台 |
| L9 | `ocr/service.cpp` | 用 10 ms 轮询 `PeekNamedPipe`；每次取消都会杀掉进程，下次需要冷启动重新加载模型 | 改用重叠 I/O；取消时只丢弃结果，不杀进程（协议已有长度前缀，可以读完后丢弃） |
| L10 | `pin/session.cpp` | 贴图截图以明文保存在 `%LOCALAPPDATA%\LumaShot\pin-session`，而剪贴板用了 DPAPI，两者不一致 | 统一使用 DPAPI，或在文档中说明 |
| L11 | `clipboard/input_focus.cpp` | detach 的 UIA 线程在进程退出时可能仍在运行 | 退出时等待它结束，或改为可取消的 worker |
| L12 | README | “当前不提供贴图内再编辑、缩放和历史恢复”已过时（贴图缩放、标注、会话恢复都已实现） | 更新文档 |

## 做得好的地方（不需要改）
- `capture_ipc.h`：请求校验完整（会话 ID、token 格式、调用方进程存活、`.png`、目标文件不存在、命名事件）。
- 截图保存采用“临时文件 → `MoveFileEx`”，取消后不会留下半成品；录制导出同样先写副本再提交。
- OCR 与元素识别子进程：Job 对象 + 句柄白名单继承 + 长度与几何范围校验，协议解码边界检查严格。
- 剪贴板读取尊重 `ExcludeClipboardContentFromMonitorProcessing`、`CanIncludeInClipboardHistory`；单条和总量都有上限。
- `DeferredWriter`、`SessionStore` 的代际（generation）机制能防止过期结果回写。

## 建议修复顺序
1. H1（先确认产品意图）→ M1（同样需要确认）
2. M4、M3（数据丢失和磁盘占用）
3. M2、M5
4. 低优先级问题按需处理

修复后，按 AGENTS.md 的要求：用 `build.bat` 构建，只运行与改动相关的测试，然后生成 `dist/LumaShot-Setup.exe` 并报告 SHA‑256。
## 修复状态（2026-09-25）

| 编号 | 状态 | 说明 |
| --- | --- | --- |
| H1 | 已修复 | 新增设置“退出后保留剪贴板历史”（`ClipboardPersist`，默认关闭）。关闭时为仅会话存储，并删除旧的 `history-v1`；打开时才跨重启保存。设置 IPC 版本从 8 升到 9。 |
| M1 | 仅改文档 | 保留开机自启动默认开启；README 已说明默认值及关闭方法。 |
| M2 | 已修复 | settings.ini 改为 UTF-16 LE 格式；旧的 ANSI 文件保存时按节迁移，未知字段保留。 |
| M3 | 已修复 | 导出时和启动时清理超过 24 小时的 `LumaShot-Clipboard\LumaShot-*`，启动时跳过当前剪贴板上的文件。 |
| M4 | 已修复 | 录制中从托盘退出会先确认；录制进程持有 `lease`（DELETE_ON_CLOSE）文件，被强制结束后留下的目录会在下次录制或启动时清理。 |
| M5 | 已修复 | Win+V 低级键盘钩子移到独立线程，该线程有自己的消息循环；回调不访问任何 UI 状态。暂停快捷键时会卸载钩子；实时的游戏/快捷键录入检查在 UI 线程收到 `WM_HOTKEY` 后执行。 |
