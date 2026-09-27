# MCP 连接就绪与执行规则

日期：2026-09-19。

- 已初始化新连接，服务 shuncode-bridge 0.7.5，协议 2024-11-05；取得 16 个工具定义，实际验证文件读取及终端。
- 当前工作区为 C:/Users/SS/Desktop/LumaShot，不是此前 ShowBox 工程。后续以当前任务及该工作区为准，不自动继续修改 CAD 项目。
- 已读取 AGENTS.md。当前有 21 个 git 状态条目，保留已有改动，不擅自清理、回退或提交。
- 工具分工：目录和文件发现 list_directory/find_files；文本搜索 search_files；语义导航 lsp；读取 read_files/read_image；修改 apply_patch；诊断 get_diagnostics；执行与跟踪 run_command/get_command_output/send_command_input/cancel_command；任务协作 set_todos/update_plan/report_progress。
- 先读后改，使用可用的文件版本校验；失配先重读。独立查询并行，同一文件写入串行。文件工具限工作区，其他路径先只读定位，再按任务授权操作。
- 远端 shell 为 PortableGit Bash，非默认 PowerShell；长命令返回 running 后按本会话 command_id 跟踪，不把超时当作已停止。
- 多步骤任务维护完整任务清单，至多一个 in_progress；交付前记录真实最终状态。分析/计划/评审放 docs/mcp-<topic>.md，LF、无尾空格；日志及探针放 docs 外。
- LumaShot 为独立 Windows C++20 截图应用，不能修改兄弟 Pulse。使用 Win32/Direct2D/DirectWrite/WIC，保持物理像素与 DIP 分离，截图与编码不阻塞 UI，保留捕获前鼠标形状、热点和位置。
- 修改后仅运行相关回归，不默认重跑无关旧测试；不得使用个人文件、设置或剪贴板内容作为测试样本；不递归或擅自委派代理。
- 应用修改交付须构建并验证 dist/LumaShot-Setup.exe，报告路径及 SHA-256，尊重宿主打包确认；未经要求不运行安装包或替换已安装应用。
- 本次仅连接、读取和记录规则，未修改应用代码、构建、打包或安装。
