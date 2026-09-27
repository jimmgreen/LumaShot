# MCP 连接与工作规则确认

日期：2026-09-18。

已完成 initialize、notifications/initialized、tools/list，并通过目录读取、AGENTS.md 读取和只读 pwd 验证会话。服务为 shuncode-bridge 0.7.5，协议为 2025-03-26；当前工作区是 C:/Users/SS/Desktop/LumaShot。会话令牌不写入仓库。

## 工具分工

- list_directory / find_files：目录与文件名定位；search_files：字面量内容检索。
- lsp：符号、定义、引用、类型信息；空结果不等于符号不存在。
- read_files / read_image：读取文本或图片；编辑前先读取当前版本。
- apply_patch：集中提交小范围关联改动；对已读取文件带 expected_versions，过期或上下文不匹配时重新读取。
- get_diagnostics：读取当前诊断；run_command：执行本机 Bash 命令、构建和相关验证。
- get_command_output / send_command_input / cancel_command：凭本会话的 command_id 查看、输入与停止命令；超时返回 running 不代表命令已结束。
- set_todos / update_plan：维护完整任务快照，最多一项 in_progress；report_progress：临时进展。最终回复前同步真实的终态并等待确认。

## 执行约定

1. 以用户当前任务和最新工作区文件为准，不将历史文件内容当作当前状态；不修改同级 Pulse 项目。
2. 独立读取、搜索可并行；存在先后依赖的操作串行，不并行写同一文件。构建、性能测量及打包避免相互争抢资源导致失真的结果。
3. Windows 命令运行于产品自带 PortableGit Bash，使用 /c/... 路径；需要 PowerShell 或 .ps1 时显式调用 powershell.exe。显式指定 cwd，避免误用持久终端中的旧目录。
4. 文件工具限工作区；其他本机路径仅在任务需要时，先确定准确路径并只读定位，不把可访问范围视作任意修改授权。
5. 重大改动后做诊断和本次改动相关的回归，不默认重跑无关旧套件。遵循当前任务的测试要求；编译通过不等于功能或视觉验收通过。
6. 打包、签名、安装、迁移、批量移动等遇到主机确认时等待用户，不能自行代为批准。未要求时不安装或覆盖正在使用的程序，不修改个人偏好或剪贴板，不做无关清理，不进行未经请求的递归委派。
7. LumaShot 应用改动完成后提供 dist/LumaShot-Setup.exe，核验并报告路径、SHA-256；包含所有已完成改动，不仅交付 build/ 裸程序。独立 HTML 等任务按其自身交付要求处理。
8. 遵守 AGENTS.md 的 Win32/C++20、模块职责、DPI/坐标、光标、后台捕获与编码、RAII 及 /W4 /WX 约定；不使用个人文件、偏好或剪贴板作为回归夹具。
9. 分析、审查和计划同步至 docs/mcp-<topic>.md，采用 LF、无行尾空格与仓库内路径。探针、日志和机器可读基线放在 docs/ 之外。
10. 连接中断时明确区分已完成和未确认结果；恢复后先检查原任务是否仍在运行，避免重复启动或误报完成。

本轮只完成连接、能力发现、只读工作区确认与此规则文档同步；未修改业务代码、运行构建/测试或安装程序。后续等待用户具体任务。
