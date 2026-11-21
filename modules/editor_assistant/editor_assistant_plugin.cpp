/**************************************************************************/
/*  editor_assistant_plugin.cpp                                           */
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

#include "editor_assistant_plugin.h"

#include "mcp/mcp_config_exporter.h"
#include "mcp/tools/mcp_doc_tools.h"
#include "mcp/tools/mcp_scene_tools.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/gui/editor_toaster.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/scene_string_names.h"
#include "servers/display/display_server.h"

EditorAssistantPlugin::EditorAssistantPlugin() {
	permission_gate.instantiate();
	tool_registry.instantiate();
	mcp_server.instantiate();

	MCPDocTools::register_tools(tool_registry);
	MCPSceneTools::register_tools(tool_registry);

	mcp_server->set_permission_gate(permission_gate);
	mcp_server->set_registry(tool_registry);

	set_process_internal(true);
}

EditorAssistantPlugin::~EditorAssistantPlugin() {
	_stop_mcp_server();
}

void EditorAssistantPlugin::_register_settings() {
	if (settings_registered || !EditorSettings::get_singleton()) {
		return;
	}
	settings_registered = true;

	EDITOR_DEF("network/editor_assistant/mcp_enabled", true);
	EDITOR_DEF("network/editor_assistant/mcp_host", "127.0.0.1");
	EDITOR_DEF("network/editor_assistant/mcp_port", 6550);
	EDITOR_DEF("network/editor_assistant/auto_fallback_port", true);

	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(Variant::INT, "network/editor_assistant/mcp_port", PROPERTY_HINT_RANGE, "1024,65535,1"));
}

int EditorAssistantPlugin::_get_cmdline_port_override() const {
	const List<String> user_args = OS::get_singleton()->get_cmdline_user_args();
	for (const List<String>::Element *E = user_args.front(); E; E = E->next()) {
		const String &arg = E->get();
		if (arg == "--mcp-port" && E->next()) {
			int port = E->next()->get().to_int();
			if (port > 0 && port <= 65535) {
				return port;
			}
		} else if (arg.begins_with("--mcp-port=")) {
			int port = arg.get_slicec('=', 1).to_int();
			if (port > 0 && port <= 65535) {
				return port;
			}
		}
	}
	return -1;
}

void EditorAssistantPlugin::_log_info(const String &p_msg) const {
	print_line(p_msg);
}

void EditorAssistantPlugin::_toast(const String &p_msg, bool p_is_error) const {
	if (EditorToaster::get_singleton()) {
		EditorToaster::get_singleton()->popup_str(p_msg, p_is_error ? EditorToaster::SEVERITY_ERROR : EditorToaster::SEVERITY_INFO);
	}
}

void EditorAssistantPlugin::_populate_mcp_popup(PopupMenu *p_popup) {
	if (!p_popup) {
		return;
	}
	p_popup->add_item(TTR("Status: Starting..."), MENU_STATUS_HEADER);
	p_popup->set_item_disabled(p_popup->get_item_index(MENU_STATUS_HEADER), true);
	p_popup->add_separator();
	p_popup->add_item(TTR("Configure Cursor (.cursor/mcp.json)"), MENU_CONFIGURE_CURSOR);
	p_popup->add_item(TTR("Configure Claude Desktop"), MENU_CONFIGURE_CLAUDE_DESKTOP);
	p_popup->add_item(TTR("Copy MCP Config JSON"), MENU_COPY_CONFIG_JSON);
	p_popup->add_separator();
	p_popup->add_item(TTR("Restart MCP Server"), MENU_RESTART_MCP_SERVER);
	p_popup->connect(SceneStringName(id_pressed), callable_mp(this, &EditorAssistantPlugin::_on_tools_menu_id_pressed));
}

void EditorAssistantPlugin::_update_mcp_ui_state() {
	const bool running = mcp_server.is_valid() && mcp_server->is_running();
	const int tool_count = tool_registry.is_valid() ? tool_registry->get_tool_count() : 0;
	const String status_line = running
			? vformat(TTR("Running: %s (%d tools)"), mcp_server->get_endpoint_url(), tool_count)
			: TTR("Status: Stopped");

	PopupMenu *popups[2] = { tools_menu, toolbar_menu_button ? toolbar_menu_button->get_popup() : nullptr };
	for (PopupMenu *popup : popups) {
		if (!popup) {
			continue;
		}
		int idx = popup->get_item_index(MENU_STATUS_HEADER);
		if (idx >= 0) {
			popup->set_item_text(idx, status_line);
		}
	}

	if (toolbar_menu_button) {
		toolbar_menu_button->set_text(running ? vformat("MCP :%d", mcp_server->get_active_port()) : TTR("MCP: Off"));
		toolbar_menu_button->set_tooltip_text(running
						? vformat(TTR("AI Assistant MCP Server: Running\nEndpoint: %s (%d tools)\nClick to configure Cursor / Claude Desktop or copy config."), mcp_server->get_endpoint_url(), tool_count)
						: TTR("AI Assistant MCP Server is stopped. Click to restart or copy config."));
		if (toolbar_menu_button->is_inside_tree()) {
			toolbar_menu_button->set_button_icon(toolbar_menu_button->get_editor_theme_icon(running ? SNAME("StatusSuccess") : SNAME("StatusWarning")));
		}
	}
}

void EditorAssistantPlugin::_start_mcp_server() {
	_register_settings();

	if (EditorSettings::get_singleton()) {
		mcp_enabled = static_cast<bool>(EDITOR_GET("network/editor_assistant/mcp_enabled"));
		mcp_host = String(EDITOR_GET("network/editor_assistant/mcp_host")).strip_edges();
		configured_port = static_cast<int>(EDITOR_GET("network/editor_assistant/mcp_port"));
		auto_fallback_port = static_cast<bool>(EDITOR_GET("network/editor_assistant/auto_fallback_port"));
	}

	if (mcp_host.is_empty()) {
		mcp_host = "127.0.0.1";
	}
	if (configured_port <= 0 || configured_port > 65535) {
		configured_port = 6550;
	}

	int port_override = _get_cmdline_port_override();
	int target_port = (port_override > 0) ? port_override : configured_port;
	bool allow_fallback = (port_override > 0) ? false : auto_fallback_port;

	if (!mcp_enabled && port_override <= 0) {
		_stop_mcp_server();
		_update_mcp_ui_state();
		return;
	}

	Error err = mcp_server->start(target_port, mcp_host, allow_fallback);
	if (err != OK) {
		_log_info(vformat("--- Failed to start AI Assistant MCP server on %s:%d (error %d) ---", mcp_host, target_port, static_cast<int>(err)));
		_update_mcp_ui_state();
		return;
	}

	_log_info(vformat("--- AI Assistant MCP server listening at %s (%d tools) ---", mcp_server->get_endpoint_url(), tool_registry->get_tool_count()));
	_update_mcp_ui_state();
}

void EditorAssistantPlugin::_stop_mcp_server() {
	if (mcp_server.is_valid() && mcp_server->is_running()) {
		mcp_server->stop();
		_log_info("--- AI Assistant MCP server stopped ---");
	}
	_update_mcp_ui_state();
}

void EditorAssistantPlugin::_on_tools_menu_id_pressed(int p_id) {
	String active_host = mcp_server->is_running() ? mcp_server->get_bind_host() : mcp_host;
	int active_port = mcp_server->is_running() ? mcp_server->get_active_port() : configured_port;

	switch (p_id) {
		case MENU_CONFIGURE_CURSOR: {
			if (!mcp_server->is_running()) {
				_toast(TTR("MCP server is not running. Enable or restart it first."), true);
				return;
			}
			String project_dir = ProjectSettings::get_singleton()->get_resource_path();
			String written_path;
			String err_msg;
			Error err = MCPConfigExporter::export_cursor_config(project_dir, active_host, active_port, written_path, err_msg);
			if (err == OK) {
				String msg = vformat(TTR("Cursor MCP config updated: %s"), written_path);
				_log_info("--- " + msg + " ---");
				_toast(msg);
			} else {
				_log_info("--- Failed to export Cursor MCP config: " + err_msg + " ---");
				_toast(err_msg, true);
			}
		} break;

		case MENU_CONFIGURE_CLAUDE_DESKTOP: {
			if (!mcp_server->is_running()) {
				_toast(TTR("MCP server is not running. Enable or restart it first."), true);
				return;
			}
			String written_path;
			String err_msg;
			Error err = MCPConfigExporter::export_claude_desktop_config(active_host, active_port, written_path, err_msg);
			if (err == OK) {
				String msg = vformat(TTR("Claude Desktop MCP config updated (restart Claude Desktop to apply): %s"), written_path);
				_log_info("--- " + msg + " ---");
				_toast(msg);
			} else {
				_log_info("--- Failed to export Claude Desktop MCP config: " + err_msg + " ---");
				_toast(err_msg, true);
			}
		} break;

		case MENU_COPY_CONFIG_JSON: {
			String snippet = MCPConfigExporter::format_clipboard_snippet(active_host, active_port);
			if (DisplayServer::get_singleton()) {
				DisplayServer::get_singleton()->clipboard_set(snippet);
				_toast(vformat(TTR("Copied MCP JSON config (%s) to clipboard."), MCPConfigExporter::build_mcp_url(active_host, active_port)));
			}
		} break;

		case MENU_RESTART_MCP_SERVER: {
			_stop_mcp_server();
			_start_mcp_server();
			if (mcp_server->is_running()) {
				_toast(vformat(TTR("AI Assistant MCP restarted at %s"), mcp_server->get_endpoint_url()));
			} else {
				_toast(TTR("Failed to start AI Assistant MCP server. Check Output log."), true);
			}
		} break;
	}
}

void EditorAssistantPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_register_settings();

			tools_menu = memnew(PopupMenu);
			_populate_mcp_popup(tools_menu);

			tools_menu_name = TTR("AI Assistant (MCP)");
			add_tool_submenu_item(tools_menu_name, tools_menu);

			toolbar_menu_button = memnew(MenuButton);
			toolbar_menu_button->set_flat(true);
			toolbar_menu_button->set_theme_type_variation("MainMenuBar");
			toolbar_menu_button->set_focus_mode(Control::FOCUS_NONE);
			toolbar_menu_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
			_populate_mcp_popup(toolbar_menu_button->get_popup());
			toolbar_menu_button->connect("theme_changed", callable_mp(this, &EditorAssistantPlugin::_update_mcp_ui_state));
			add_control_to_container(CONTAINER_TOOLBAR, toolbar_menu_button);

			// Keep the button before EditorNode::right_menu_spacer so Windows caption buttons (-/[]/X) do not overlap it.
			Control *title_bar = EditorNode::get_title_bar();
			if (title_bar && toolbar_menu_button->get_parent() == title_bar && toolbar_menu_button->get_index() > 0) {
				Control *prev_ctrl = Object::cast_to<Control>(title_bar->get_child(toolbar_menu_button->get_index() - 1));
				if (prev_ctrl && prev_ctrl->get_class() == "Control" && prev_ctrl->get_mouse_filter() == Control::MOUSE_FILTER_IGNORE) {
					title_bar->move_child(toolbar_menu_button, prev_ctrl->get_index());
				}
			}
			_update_mcp_ui_state();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_stop_mcp_server();
			if (toolbar_menu_button) {
				if (toolbar_menu_button->get_parent()) {
					remove_control_from_container(CONTAINER_TOOLBAR, toolbar_menu_button);
				}
				memdelete(toolbar_menu_button);
				toolbar_menu_button = nullptr;
			}
			if (!tools_menu_name.is_empty()) {
				remove_tool_menu_item(tools_menu_name);
				tools_menu_name = String();
				tools_menu = nullptr; // Freed by EditorNode::remove_tool_menu_item.
			}
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!start_attempted && EditorNode::get_singleton() && EditorNode::get_singleton()->is_editor_ready()) {
				start_attempted = true;
				_start_mcp_server();
			}
			if (mcp_server.is_valid() && mcp_server->is_running()) {
				mcp_server->poll();
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton() || !EditorSettings::get_singleton()->check_changed_settings_in_group("network/editor_assistant")) {
				break;
			}
			bool new_enabled = static_cast<bool>(EDITOR_GET("network/editor_assistant/mcp_enabled"));
			String new_host = String(EDITOR_GET("network/editor_assistant/mcp_host")).strip_edges();
			int new_port = static_cast<int>(EDITOR_GET("network/editor_assistant/mcp_port"));
			bool new_fallback = static_cast<bool>(EDITOR_GET("network/editor_assistant/auto_fallback_port"));

			if (new_enabled != mcp_enabled || new_host != mcp_host || new_port != configured_port || new_fallback != auto_fallback_port) {
				_stop_mcp_server();
				_start_mcp_server();
			}
		} break;
	}
}
