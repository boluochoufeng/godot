/**************************************************************************/
/*  mcp_config_exporter.h                                                 */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class MCPConfigExporter {
public:
	static String get_user_home_dir();
	static String build_mcp_url(const String &p_host, int p_port);

	static Dictionary build_cursor_server_entry(const String &p_url);
	static Dictionary build_claude_desktop_server_entry(const String &p_url, bool p_is_windows);

	static String merge_mcp_servers_json(const String &p_existing_json, const String &p_server_name, const Dictionary &p_server_entry, Error &r_err, String &r_error_msg);
	static String merge_codex_config_toml(const String &p_existing_toml, const String &p_server_name, const String &p_url);
	static String merge_deepseek_harness_patch_yaml(const String &p_existing_yaml, const String &p_server_name, const String &p_url, Error &r_err, String &r_error_msg);

	static String get_cursor_config_path(const String &p_project_dir);
	static String get_claude_desktop_config_path();
	static String get_antigravity_config_path();
	static String get_codex_config_path();
	static String get_deepseek_harness_config_path();

	static Error export_cursor_config(const String &p_project_dir, const String &p_host, int p_port, String &r_written_path, String &r_error_msg);
	static Error export_claude_desktop_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg);
	static Error export_antigravity_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg);
	static Error export_codex_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg);
	static Error export_deepseek_harness_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg);

	static String format_clipboard_snippet(const String &p_host, int p_port);
};
