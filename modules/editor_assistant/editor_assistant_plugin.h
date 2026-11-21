/**************************************************************************/
/*  editor_assistant_plugin.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "mcp/mcp_permission_gate.h"
#include "mcp/mcp_server.h"
#include "mcp/mcp_tool_registry.h"

#include "editor/plugins/editor_plugin.h"

class MenuButton;
class PopupMenu;

class EditorAssistantPlugin : public EditorPlugin {
	GDCLASS(EditorAssistantPlugin, EditorPlugin);

	enum ToolsMenuId {
		MENU_CONFIGURE_ANTIGRAVITY = 0,
		MENU_CONFIGURE_CODEX = 1,
		MENU_CONFIGURE_DEEPSEEK_HARNESS = 2,
		MENU_CONFIGURE_CURSOR = 3,
		MENU_CONFIGURE_CLAUDE_DESKTOP = 4,
		MENU_COPY_CONFIG_JSON = 5,
		MENU_COPY_ENDPOINT_URL = 6,
		MENU_RESTART_MCP_SERVER = 7,
		MENU_STATUS_HEADER = 100,
	};

	Ref<MCPPermissionGate> permission_gate;
	Ref<MCPToolRegistry> tool_registry;
	Ref<EditorMCPServer> mcp_server;

	PopupMenu *tools_menu = nullptr;
	String tools_menu_name;
	MenuButton *toolbar_menu_button = nullptr;

	bool start_attempted = false;
	bool settings_registered = false;
	bool mcp_enabled = true;
	String mcp_host = "127.0.0.1";
	int configured_port = 6550;
	bool auto_fallback_port = true;

	void _register_settings();
	int _get_cmdline_port_override() const;
	void _run_cmdline_exports();
	void _start_mcp_server();
	void _stop_mcp_server();
	void _populate_mcp_popup(PopupMenu *p_popup);
	void _update_mcp_ui_state();
	void _log_info(const String &p_msg) const;
	void _toast(const String &p_msg, bool p_is_error = false) const;
	void _on_tools_menu_id_pressed(int p_id);

protected:
	void _notification(int p_what);

public:
	EditorAssistantPlugin();
	~EditorAssistantPlugin();
};
