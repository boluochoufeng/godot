/**************************************************************************/
/*  mcp_config_exporter.cpp                                               */
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

#include "mcp_config_exporter.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/array.h"

String MCPConfigExporter::build_mcp_url(const String &p_host, int p_port) {
	String h = p_host.strip_edges();
	if (h.is_empty()) {
		h = "127.0.0.1";
	}
	return vformat("http://%s:%d/mcp", h, p_port);
}

Dictionary MCPConfigExporter::build_cursor_server_entry(const String &p_url) {
	Dictionary entry;
	entry["url"] = p_url;
	return entry;
}

Dictionary MCPConfigExporter::build_claude_desktop_server_entry(const String &p_url, bool p_is_windows) {
	Dictionary entry;
	Array args;
	if (p_is_windows) {
		entry["command"] = "cmd";
		args.push_back("/c");
		args.push_back("npx");
	} else {
		entry["command"] = "npx";
	}
	args.push_back("-y");
	args.push_back("mcp-remote");
	args.push_back(p_url);
	entry["args"] = args;
	return entry;
}

String MCPConfigExporter::merge_mcp_servers_json(const String &p_existing_json, const String &p_server_name, const Dictionary &p_server_entry, Error &r_err, String &r_error_msg) {
	Dictionary root;
	String trimmed = p_existing_json.strip_edges();
	if (!trimmed.is_empty()) {
		JSON json;
		Error perr = json.parse(trimmed);
		if (perr != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			r_err = ERR_PARSE_ERROR;
			r_error_msg = "Existing MCP config is not a valid JSON object: " + json.get_error_message();
			return String();
		}
		root = json.get_data();
	}

	Dictionary mcp_servers;
	if (root.has("mcpServers")) {
		if (root["mcpServers"].get_type() != Variant::DICTIONARY) {
			r_err = ERR_PARSE_ERROR;
			r_error_msg = "Existing 'mcpServers' field is not a JSON object.";
			return String();
		}
		mcp_servers = root["mcpServers"];
	}

	mcp_servers[p_server_name] = p_server_entry;
	root["mcpServers"] = mcp_servers;

	r_err = OK;
	r_error_msg = String();
	return JSON::stringify(root, "  ") + "\n";
}

String MCPConfigExporter::get_cursor_config_path(const String &p_project_dir) {
	return p_project_dir.path_join(".cursor").path_join("mcp.json");
}

String MCPConfigExporter::get_claude_desktop_config_path() {
	String os_name = OS::get_singleton()->get_name();
	if (os_name == "Windows") {
		String appdata = OS::get_singleton()->get_environment("APPDATA");
		if (appdata.is_empty()) {
			return String();
		}
		return appdata.path_join("Claude").path_join("claude_desktop_config.json");
	}
	String home = OS::get_singleton()->get_environment("HOME");
	if (home.is_empty()) {
		return String();
	}
	if (os_name == "macOS") {
		return home.path_join("Library/Application Support/Claude/claude_desktop_config.json");
	}
	return home.path_join(".config/Claude/claude_desktop_config.json");
}

static Error _write_merged_mcp_config(const String &p_target_path, const Dictionary &p_entry, String &r_error_msg) {
	if (p_target_path.is_empty()) {
		r_error_msg = "Could not determine configuration file path.";
		return ERR_CANT_CREATE;
	}

	String existing;
	if (FileAccess::exists(p_target_path)) {
		Error read_err = OK;
		existing = FileAccess::get_file_as_string(p_target_path, &read_err);
		if (read_err != OK) {
			r_error_msg = "Failed to read existing config file: " + p_target_path;
			return read_err;
		}
	}

	Error merge_err = OK;
	String merged = MCPConfigExporter::merge_mcp_servers_json(existing, "godot-editor", p_entry, merge_err, r_error_msg);
	if (merge_err != OK) {
		return merge_err;
	}

	String parent_dir = p_target_path.get_base_dir();
	Error mk_err = DirAccess::make_dir_recursive_absolute(parent_dir);
	if (mk_err != OK && mk_err != ERR_ALREADY_EXISTS) {
		r_error_msg = "Failed to create directory: " + parent_dir;
		return mk_err;
	}

	Error open_err = OK;
	Ref<FileAccess> f = FileAccess::open(p_target_path, FileAccess::WRITE, &open_err);
	if (f.is_null() || open_err != OK) {
		r_error_msg = "Failed to write config file: " + p_target_path;
		return open_err != OK ? open_err : ERR_CANT_OPEN;
	}

	f->store_string(merged);
	f->close();
	return OK;
}

Error MCPConfigExporter::export_cursor_config(const String &p_project_dir, const String &p_host, int p_port, String &r_written_path, String &r_error_msg) {
	r_written_path = get_cursor_config_path(p_project_dir);
	Dictionary entry = build_cursor_server_entry(build_mcp_url(p_host, p_port));
	return _write_merged_mcp_config(r_written_path, entry, r_error_msg);
}

Error MCPConfigExporter::export_claude_desktop_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg) {
	r_written_path = get_claude_desktop_config_path();
	bool is_windows = OS::get_singleton()->get_name() == "Windows";
	Dictionary entry = build_claude_desktop_server_entry(build_mcp_url(p_host, p_port), is_windows);
	return _write_merged_mcp_config(r_written_path, entry, r_error_msg);
}

String MCPConfigExporter::format_clipboard_snippet(const String &p_host, int p_port) {
	Dictionary entry = build_cursor_server_entry(build_mcp_url(p_host, p_port));
	Error err = OK;
	String err_msg;
	return merge_mcp_servers_json(String(), "godot-editor", entry, err, err_msg);
}
