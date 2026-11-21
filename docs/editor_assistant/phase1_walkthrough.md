# Phase 1 完成总结：`modules/editor_assistant` 纯 HTTP MCP 底座与三端接入（已同步最新提交）

Phase 1 已按核准的**方案 3（纯内置 Streamable HTTP MCP，引擎原有源码零改动）**完成全部搭建、UI 入口完善及 **Antigravity / Codex 桌面版 / DeepSeek Harness** 导出适配，并分两次本地提交至 `master`（未推送远程）：
* `4a10783578`：`Add editor_assistant module with built-in MCP server (Phase 1)`（19 个新建文件）
* `07907dc588`：`Add Antigravity, Codex, and DeepSeek Harness MCP config exporters`（6 个文件增量更新）

---

## 1. 模块与核心交付清单（[modules/editor_assistant/](../../modules/editor_assistant)）

### 构建骨架与插件总控
* [config.py](../../modules/editor_assistant/config.py) / [SCsub](../../modules/editor_assistant/SCsub) / [register_types.h](../../modules/editor_assistant/register_types.h) / [register_types.cpp](../../modules/editor_assistant/register_types.cpp)：限定 `env.editor_build` 与 `TOOLS_ENABLED`，声明依赖 `jsonrpc`，在 `MODULE_INITIALIZATION_LEVEL_EDITOR` 注册 `EditorAssistantPlugin`。
* [editor_assistant_plugin.h](../../modules/editor_assistant/editor_assistant_plugin.h) / [editor_assistant_plugin.cpp](../../modules/editor_assistant/editor_assistant_plugin.cpp)：
  * 注册 `EditorSettings` 配置项（`network/editor_assistant/mcp_enabled`、`mcp_host`、`mcp_port`、`auto_fallback_port`）。
  * 通过 `OS::get_singleton()->get_cmdline_user_args()` 解析 `-- --mcp-port <port>` 与 `-- --mcp-export=<target>`。
  * 在顶部工具栏右侧通过 `CONTAINER_TOOLBAR` 挂载带状态图标（`StatusSuccess` / `StatusWarning`）的 **`MCP :6550`** `MenuButton`，同时保留 **`项目 -> 工具 -> AI Assistant (MCP)`** 子菜单，提供 5 客户端配置导出、复制 JSON、复制 Endpoint URL 与重启服务操作。
  * 通过 `print_line()` 将启动与导出状态日志统一路由至编辑器底部**输出（Output）**面板（`MSG_TYPE_STD`）及 `.console.exe` 终端。

### 纯内置 Streamable HTTP MCP 与三端导出器 (`mcp/`)
* [mcp_permission_gate.h](../../modules/editor_assistant/mcp/mcp_permission_gate.h) / [mcp_permission_gate.cpp](../../modules/editor_assistant/mcp/mcp_permission_gate.cpp)：四级权限闸门（`Read` / `Edit` / `Ask` / `Auto`），Phase 1 默认策略 `Edit`。
* [mcp_tool_registry.h](../../modules/editor_assistant/mcp/mcp_tool_registry.h) / [mcp_tool_registry.cpp](../../modules/editor_assistant/mcp/mcp_tool_registry.cpp)：注册 15 个下划线命名工具 Schema 并统一封装 MCP `CallToolResult`。
* [mcp_server.h](../../modules/editor_assistant/mcp/mcp_server.h) / [mcp_server.cpp](../../modules/editor_assistant/mcp/mcp_server.cpp)：基于非阻塞 `TCPServer` 与 `StreamPeerTCP` 的 HTTP/1.1 状态机；提供回环 `Host`/`Origin` 校验、`GET /health`、`GET /mcp`（`405`）、`POST /mcp`（兼容 `/mcp/` 与 `/`），支持 `initialize`（`2024-11-05` / `2025-03-26` / `2025-06-18` / `2025-11-25`）、`notifications/initialized`（`202`）、`ping`、`tools/list`、`tools/call`。
* [mcp_config_exporter.h](../../modules/editor_assistant/mcp/mcp_config_exporter.h) / [mcp_config_exporter.cpp](../../modules/editor_assistant/mcp/mcp_config_exporter.cpp)：
  * **Antigravity**：无损合并 JSON 到 `~/.gemini/config/mcp_config.json`（原生 Streamable HTTP URL）；
  * **Codex Desktop / CLI**：状态感知 TOML 合并写入 `%CODEX_HOME%/config.toml` 或 `~/.codex/config.toml`（保留单引号 Windows 路径、多行数组与兄弟条目）；
  * **DeepSeek Harness (`dsh`)**：无 BOM UTF-8 合并写入 `%DSH_HOME%/cordis.patch.yml` 或 `~/.dsh/cordis.patch.yml`（清理 `[]` 占位、保留同级 `- insert:` 兄弟项并设为 `transport: streamable-http`）；
  * **Cursor / Claude Desktop**：兼容 `.cursor/mcp.json` 与 `claude_desktop_config.json`（`cmd /c npx -y mcp-remote`）。

### 双源知识库与场景编辑工具 (`mcp/tools/`)
* [mcp_doc_tools.h](../../modules/editor_assistant/mcp/tools/mcp_doc_tools.h) / [mcp_doc_tools.cpp](../../modules/editor_assistant/mcp/tools/mcp_doc_tools.cpp)（5 个只读工具）：`doc_search_classes`、`doc_get_class_detail`、`doc_check_api`、`doc_search_project_docs`、`doc_read_project_doc`。
* [mcp_scene_tools.h](../../modules/editor_assistant/mcp/tools/mcp_scene_tools.h) / [mcp_scene_tools.cpp](../../modules/editor_assistant/mcp/tools/mcp_scene_tools.cpp)（10 个场景与资源工具）：`scene_get_tree`、`scene_create_root`、`scene_add_node`、`scene_remove_node`、`scene_reparent_node`、`scene_get_properties`、`scene_set_property`、`scene_connect_signal`、`scene_edit_resource`、`scene_save`（写操作均提交至 `EditorUndoRedoManager`）。

### 单元测试定义 (`tests/`)
* [test_editor_assistant_mcp.h](../../modules/editor_assistant/tests/test_editor_assistant_mcp.h)：包含回环校验、JSON-RPC 生命周期、`doc_*` 沙箱、`JSON <-> Variant` 转换以及 JSON / Codex TOML / DeepSeek Harness YAML 无损合并测试。

---

## 2. 编译与验证记录（区分已实测通过与仅靠推断）

### 已实测通过
1. **SCons LLVM ThinLTO 增量构建**：使用 `$env:CLICOLOR_FORCE = '1'` 与仓库一致缓存参数编译通过，0 errors、0 warnings，产出 `bin/godot.windows.editor.x86_64.llvm.exe` 与 `bin/godot.windows.editor.x86_64.llvm.console.exe`。
2. **GUI 界面与默认端口实测**：在 GUI 编辑器实机确认顶部菜单与输出面板提示可见；无头模式不传 `--mcp-port` 时 `GET http://127.0.0.1:6550/health` 返回 HTTP 200（`port=6550`、`tools=15`）。
3. **MCP 协议与 15 个工具 E2E**（`verify_phase1_mcp.ps1`）：9/9 项全过（覆盖 `/health`、`405`/`403`、`initialize`/`202`/`tools/list`、`doc_*` 沙箱与 API 检查，以及 `scene_*` 全链路生成有效 `world.tscn`）。
4. **三端配置导出隔离 E2E**（`verify_client_exporters.ps1`）：4/4 项全过：
   * 在隔离主目录通过 `--mcp-export=antigravity --mcp-export=codex --mcp-export=deepseek-harness` 验证空白/`[]` 首次写入与 `16550 -> 16551` 幂等覆盖；
   * `mcp_config.json` 通过 PowerShell `ConvertFrom-Json` 校验且保留兄弟条目 `other-tool`；
   * `config.toml` 通过 Python `tomllib` 与本机 `codex.exe mcp get godot-editor`、`codex.exe mcp list` 实测校验（无 BOM、保留反斜杠结尾字面量路径、多行数组与 `godot-ai`）；
   * `cordis.patch.yml` 校验无 UTF-8 BOM、保留 `mcp-godot-ai`、`id: mcp-godot-editor` 唯一且含 `transport: streamable-http`。
5. **Git 状态隔离验证**：两次代码提交均仅包含 [modules/editor_assistant/](../../modules/editor_assistant)，引擎原有跟踪文件未修改。

### 仅靠推断（尚未在隔离环境中单独实测）
* 从图形界面顶部 **`MCP :6550`** 下拉菜单直接点击写入用户真实主目录后，由 Antigravity / Codex 桌面版 / DeepSeek Harness 实机连入编辑器调用工具的端到端人工联调；
* [test_editor_assistant_mcp.h](../../modules/editor_assistant/tests/test_editor_assistant_mcp.h) C++ doctest 用例尚未用 `tests=yes` 编译运行（为保护当前非测试 `.obj` 缓存）；
* `target=template_release` / `target=template_debug` 导出模板构建跳过本模块基于 [config.py](../../modules/editor_assistant/config.py) 返回 `env.editor_build` 推断；
* `/mcp/` 尾斜杠路由与 `"2025-11-25"` 版本协商由代码分支判断，尚未单独构造发包验证。

---

## 3. 接下来可选项

1. **三端桌面实机联调**：打开编译产出的 `bin/godot.windows.editor.x86_64.llvm.exe` 进入工程，点击顶部 **`MCP :6550`** 对应配置项并在 Antigravity / Codex 桌面版 / DeepSeek Harness 中实测操控场景树。
2. **进入 Phase 2 开发**：在 `modules/editor_assistant/` 内推进原生右侧停靠面板 `AssistantDock`（`EditorDock`）、`BuiltInAPIDriver`、`script_*` 缓冲区协同工具、`Ask` 内联审批卡片与 `Revert Checkpoint`。
