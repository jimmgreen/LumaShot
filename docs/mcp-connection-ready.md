# MCP 连接准备

- 已完成 initialize、notifications/initialized 和 tools/list；服务端为 shuncode-bridge 0.7.5，协议为 2024-11-05。
- 已通过目录与文件读取、只读 pwd 命令确认连接。当前项目为 C:/Users/SS/Desktop/LumaShot。
- 已读取当前 AGENTS.md 和 README.md 的项目/开发说明。远端工作区已有后续变更，继续工作前重新读取相关文件，不使用旧源码缓存代替现状。

## 可用工具（15 个）

- 文件定位和读取：list_directory、find_files、search_files、read_files、read_image。
- 语义与编辑：lsp、apply_patch、get_diagnostics。
- 命令管理：run_command、get_command_output、send_command_input、cancel_command。
- 任务协作：set_todos、update_plan、report_progress。

## 操作约定

- 先读后改，补丁携带读取时的版本；出现版本或上下文不匹配时重新读取，不覆盖未知改动。
- 独立读取可并行；有依赖的操作按顺序执行，不并行修改同一文件。大文件使用搜索和行范围定位，读取批次最多 20 个文件。
- 符号定位优先 LSP；空结果不等于不存在，必要时使用文本搜索核实。
- 远端命令使用 PortableGit Bash，Windows 盘符优先 /c/ 路径；需要 PowerShell 时显式调用 powershell.exe。用返回的 command_id 和 next_offset 跟踪长命令。
- 复杂工作使用稳定任务 ID、完整任务列表，最多一个进行中任务，结束时同步最终状态。
- 按当前任务范围检查诊断和运行相关回归，不默认重跑无关旧测试。不使用个人文件、配置或剪贴板内容作测试夹具。
- 项目使用 C++20、Win32、Direct2D、DirectWrite、WIC，保留物理像素/DIP 语义与事件驱动设计；不修改相邻 Pulse 项目。
- 应用变更按项目要求使用 build.bat 构建并交付已核对的安装包路径与 SHA-256；遵守宿主确认，不擅自安装或替换已安装程序。
- 文件工具仅用于工作区。工作区外操作先确认准确路径和任务相关性，不将连接权限视为任意修改授权。
- 分析、计划、审查等交付记录放入 docs/mcp-<topic>.md，使用 LF、无行尾空白；探测脚本与日志不放入 docs/。

本次仅完成连接、规则了解与准备记录；没有修改业务代码，没有构建、运行测试或安装软件。
