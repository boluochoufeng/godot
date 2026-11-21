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

String MCPConfigExporter::get_user_home_dir() {
	String os_name = OS::get_singleton()->get_name();
	if (os_name == "Windows") {
		String userprofile = OS::get_singleton()->get_environment("USERPROFILE").strip_edges();
		if (!userprofile.is_empty()) {
			return userprofile;
		}
		String homedrive = OS::get_singleton()->get_environment("HOMEDRIVE").strip_edges();
		String homepath = OS::get_singleton()->get_environment("HOMEPATH").strip_edges();
		if (!homedrive.is_empty() && !homepath.is_empty()) {
			return homedrive + homepath;
		}
	}
	return OS::get_singleton()->get_environment("HOME").strip_edges();
}

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

static String _strip_utf8_bom(const String &p_text) {
	if (!p_text.is_empty() && p_text[0] == 0xFEFF) {
		return p_text.substr(1);
	}
	return p_text;
}

static String _normalize_lf(const String &p_text) {
	return _strip_utf8_bom(p_text).replace("\r\n", "\n").replace("\r", "\n");
}

static String _escape_toml_basic_string(const String &p_str) {
	String out;
	for (int i = 0; i < p_str.length(); i++) {
		char32_t ch = p_str[i];
		if (ch == '\\') {
			out += "\\\\";
		} else if (ch == '"') {
			out += "\\\"";
		} else if (ch == '\n') {
			out += "\\n";
		} else if (ch == '\r') {
			out += "\\r";
		} else if (ch == '\t') {
			out += "\\t";
		} else {
			out += ch;
		}
	}
	return out;
}

static bool _parse_toml_table_header(const String &p_trimmed, bool &r_is_array_table, Vector<String> &r_segments) {
	r_segments.clear();
	if (!p_trimmed.begins_with("[")) {
		return false;
	}

	r_is_array_table = p_trimmed.begins_with("[[");
	int i = r_is_array_table ? 2 : 1;
	const int n = p_trimmed.length();

	auto at_closing_bracket = [&](int p_pos) -> bool {
		if (r_is_array_table) {
			return p_pos + 1 < n && p_trimmed[p_pos] == ']' && p_trimmed[p_pos + 1] == ']';
		}
		return p_pos < n && p_trimmed[p_pos] == ']';
	};

	while (i < n) {
		while (i < n && (p_trimmed[i] == ' ' || p_trimmed[i] == '\t')) {
			i++;
		}
		if (at_closing_bracket(i)) {
			i += r_is_array_table ? 2 : 1;
			while (i < n && (p_trimmed[i] == ' ' || p_trimmed[i] == '\t')) {
				i++;
			}
			if (i == n || p_trimmed[i] == '#') {
				return !r_segments.is_empty();
			}
			return false;
		}

		String seg;
		if (i < n && p_trimmed[i] == '"') {
			i++;
			bool closed = false;
			while (i < n) {
				if (p_trimmed[i] == '\\' && i + 1 < n) {
					char32_t esc = p_trimmed[i + 1];
					if (esc == 'n') {
						seg += '\n';
					} else if (esc == 't') {
						seg += '\t';
					} else if (esc == 'r') {
						seg += '\r';
					} else {
						seg += esc;
					}
					i += 2;
					continue;
				}
				if (p_trimmed[i] == '"') {
					closed = true;
					i++;
					break;
				}
				seg += p_trimmed[i++];
			}
			if (!closed) {
				return false;
			}
		} else if (i < n && p_trimmed[i] == '\'') {
			i++;
			bool closed = false;
			while (i < n) {
				if (p_trimmed[i] == '\'') {
					closed = true;
					i++;
					break;
				}
				seg += p_trimmed[i++]; // Literal TOML string: backslash is literal.
			}
			if (!closed) {
				return false;
			}
		} else {
			int start = i;
			while (i < n) {
				char32_t ch = p_trimmed[i];
				if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
					i++;
				} else {
					break;
				}
			}
			if (i == start) {
				return false;
			}
			seg = p_trimmed.substr(start, i - start);
		}

		r_segments.push_back(seg);

		while (i < n && (p_trimmed[i] == ' ' || p_trimmed[i] == '\t')) {
			i++;
		}
		if (i < n && p_trimmed[i] == '.') {
			i++;
			continue;
		}
		if (!at_closing_bracket(i)) {
			return false;
		}
	}

	return false;
}

static void _advance_toml_lexer(const String &p_line, bool &r_in_ml_basic, bool &r_in_ml_literal, int &r_bracket_depth) {
	int i = 0;
	const int n = p_line.length();
	while (i < n) {
		if (r_in_ml_basic) {
			if (p_line[i] == '\\') {
				i += 2;
				continue;
			}
			int qcount = 0;
			while (i + qcount < n && p_line[i + qcount] == '"') {
				qcount++;
			}
			if (qcount >= 3) {
				r_in_ml_basic = false;
				i += MIN(qcount, 5);
				continue;
			}
			i++;
			continue;
		}
		if (r_in_ml_literal) {
			int qcount = 0;
			while (i + qcount < n && p_line[i + qcount] == '\'') {
				qcount++;
			}
			if (qcount >= 3) {
				r_in_ml_literal = false;
				i += MIN(qcount, 5);
				continue;
			}
			i++;
			continue;
		}

		char32_t ch = p_line[i];
		if (ch == '#') {
			break;
		}
		if (i + 2 < n && ch == '"' && p_line[i + 1] == '"' && p_line[i + 2] == '"') {
			r_in_ml_basic = true;
			i += 3;
			continue;
		}
		if (i + 2 < n && ch == '\'' && p_line[i + 1] == '\'' && p_line[i + 2] == '\'') {
			r_in_ml_literal = true;
			i += 3;
			continue;
		}
		if (ch == '"') {
			i++;
			while (i < n) {
				if (p_line[i] == '\\') {
					i += 2;
					continue;
				}
				if (p_line[i] == '"') {
					i++;
					break;
				}
				i++;
			}
			continue;
		}
		if (ch == '\'') {
			i++;
			while (i < n) {
				if (p_line[i] == '\'') {
					i++;
					break;
				}
				i++; // Literal string has no escape sequence.
			}
			continue;
		}
		if (ch == '[') {
			r_bracket_depth++;
		} else if (ch == ']' && r_bracket_depth > 0) {
			r_bracket_depth--;
		}
		i++;
	}
}

static int _count_leading_spaces(const String &p_line) {
	int i = 0;
	while (i < p_line.length() && p_line[i] == ' ') {
		i++;
	}
	return i;
}

static bool _is_yaml_seq_item_at(const String &p_line, int p_indent) {
	if (p_indent < 0 || p_line.length() <= p_indent) {
		return false;
	}
	for (int i = 0; i < p_indent; i++) {
		if (p_line[i] != ' ') {
			return false;
		}
	}
	if (p_line[p_indent] != '-') {
		return false;
	}
	return (p_line.length() == p_indent + 1) || p_line[p_indent + 1] == ' ' || p_line[p_indent + 1] == '\t';
}

static String _yaml_strip_comment_and_quotes(const String &p_raw) {
	String s = p_raw.strip_edges();
	if (s.is_empty()) {
		return String();
	}
	if (s[0] == '\'') {
		int close_idx = s.find_char('\'', 1);
		if (close_idx >= 1) {
			return s.substr(1, close_idx - 1);
		}
		return s.substr(1);
	}
	if (s[0] == '"') {
		String out;
		for (int i = 1; i < s.length(); i++) {
			if (s[i] == '\\' && i + 1 < s.length()) {
				out += s[i + 1];
				i++;
				continue;
			}
			if (s[i] == '"') {
				break;
			}
			out += s[i];
		}
		return out;
	}
	if (s[0] == '#') {
		return String();
	}
	for (int i = 1; i < s.length(); i++) {
		if (s[i] == '#' && (s[i - 1] == ' ' || s[i - 1] == '\t')) {
			s = s.substr(0, i);
			break;
		}
	}
	return s.strip_edges();
}

static bool _yaml_first_line_is_insert(const String &p_line) {
	if (!_is_yaml_seq_item_at(p_line, 0)) {
		return false;
	}
	String rest = _yaml_strip_comment_and_quotes(p_line.substr(1));
	return rest == "insert:";
}

static bool _yaml_mapping_line_matches_id(const String &p_key_part, const String &p_target_id) {
	String trimmed = p_key_part.strip_edges();
	if (!trimmed.begins_with("id:")) {
		return false;
	}
	String val = _yaml_strip_comment_and_quotes(trimmed.substr(3));
	return val == p_target_id;
}

static bool _yaml_item_has_id(const Vector<String> &p_item_lines, int p_dash_indent, const String &p_target_id) {
	if (p_item_lines.is_empty()) {
		return false;
	}

	const String &line0 = p_item_lines[0];
	int key_col = -1;
	int after_dash = p_dash_indent + 1;
	while (after_dash < line0.length() && (line0[after_dash] == ' ' || line0[after_dash] == '\t')) {
		after_dash++;
	}
	if (after_dash < line0.length() && line0[after_dash] != '#') {
		key_col = after_dash;
		if (_yaml_mapping_line_matches_id(line0.substr(after_dash), p_target_id)) {
			return true;
		}
	}

	for (int i = 1; i < p_item_lines.size(); i++) {
		const String &line = p_item_lines[i];
		String trimmed = line.strip_edges();
		if (trimmed.is_empty() || trimmed.begins_with("#")) {
			continue;
		}
		int indent = _count_leading_spaces(line);
		if (indent <= p_dash_indent) {
			break;
		}
		if (key_col < 0) {
			key_col = indent;
		}
		if (indent == key_col && _yaml_mapping_line_matches_id(line.substr(indent), p_target_id)) {
			return true;
		}
	}

	return false;
}

String MCPConfigExporter::merge_mcp_servers_json(const String &p_existing_json, const String &p_server_name, const Dictionary &p_server_entry, Error &r_err, String &r_error_msg) {
	Dictionary root;
	String trimmed = _strip_utf8_bom(p_existing_json).strip_edges();
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

String MCPConfigExporter::merge_codex_config_toml(const String &p_existing_toml, const String &p_server_name, const String &p_url) {
	String normalized = _normalize_lf(p_existing_toml);
	Vector<String> out_lines;

	auto append_canonical_block = [&]() {
		while (!out_lines.is_empty() && out_lines[out_lines.size() - 1].strip_edges().is_empty()) {
			out_lines.remove_at(out_lines.size() - 1);
		}
		if (!out_lines.is_empty()) {
			out_lines.push_back("");
		}
		out_lines.push_back(vformat("[mcp_servers.\"%s\"]", _escape_toml_basic_string(p_server_name)));
		out_lines.push_back(vformat("url = \"%s\"", _escape_toml_basic_string(p_url)));
		out_lines.push_back("enabled = true");
	};

	if (normalized.strip_edges().is_empty()) {
		append_canonical_block();
		return String("\n").join(out_lines) + "\n";
	}

	Vector<String> lines = normalized.split("\n", true);
	bool in_ml_basic = false;
	bool in_ml_literal = false;
	int bracket_depth = 0;
	bool skipping = false;
	bool inserted = false;

	for (int i = 0; i < lines.size(); i++) {
		const String &line = lines[i];

		if (!in_ml_basic && !in_ml_literal && bracket_depth == 0) {
			String trimmed = line.strip_edges();
			if (trimmed.begins_with("[")) {
				bool is_array_table = false;
				Vector<String> segments;
				if (_parse_toml_table_header(trimmed, is_array_table, segments)) {
					bool is_target = !is_array_table && segments.size() >= 2 && segments[0] == "mcp_servers" && segments[1] == p_server_name;
					if (is_target) {
						if (!inserted) {
							append_canonical_block();
							inserted = true;
						}
						skipping = true;
					} else {
						if (skipping && !out_lines.is_empty() && !out_lines[out_lines.size() - 1].strip_edges().is_empty()) {
							out_lines.push_back("");
						}
						skipping = false;
					}
				}
			}
		}

		_advance_toml_lexer(line, in_ml_basic, in_ml_literal, bracket_depth);

		if (!skipping) {
			out_lines.push_back(line);
		}
	}

	if (!inserted) {
		append_canonical_block();
	} else {
		while (!out_lines.is_empty() && out_lines[out_lines.size() - 1].strip_edges().is_empty()) {
			out_lines.remove_at(out_lines.size() - 1);
		}
	}

	return String("\n").join(out_lines) + "\n";
}

String MCPConfigExporter::merge_deepseek_harness_patch_yaml(const String &p_existing_yaml, const String &p_server_name, const String &p_url, Error &r_err, String &r_error_msg) {
	const String target_id = "mcp-" + p_server_name;
	String normalized = _normalize_lf(p_existing_yaml);

	Vector<String> canonical;
	canonical.push_back("- insert:");
	canonical.push_back(vformat("    - id: %s", target_id));
	canonical.push_back("      name: '@deepseek-ai/dsh-mcp-client'");
	canonical.push_back("      config:");
	canonical.push_back(vformat("        serverName: %s", p_server_name));
	canonical.push_back("        transport: streamable-http");
	canonical.push_back(vformat("        url: %s", p_url));

	Vector<String> lines = normalized.split("\n", true);
	bool only_comments_or_empty_array = true;
	bool has_col0_seq_item = false;

	for (int i = 0; i < lines.size(); i++) {
		String trimmed = lines[i].strip_edges();
		if (trimmed.is_empty() || trimmed.begins_with("#") || trimmed == "[]") {
			continue;
		}
		only_comments_or_empty_array = false;
		if (_is_yaml_seq_item_at(lines[i], 0)) {
			has_col0_seq_item = true;
			break;
		}
	}

	if (only_comments_or_empty_array) {
		Vector<String> out_lines;
		for (int i = 0; i < lines.size(); i++) {
			if (lines[i].strip_edges() == "[]") {
				continue;
			}
			out_lines.push_back(lines[i]);
		}
		while (!out_lines.is_empty() && out_lines[out_lines.size() - 1].strip_edges().is_empty()) {
			out_lines.remove_at(out_lines.size() - 1);
		}
		if (!out_lines.is_empty()) {
			out_lines.push_back("");
		}
		out_lines.append_array(canonical);
		r_err = OK;
		r_error_msg = String();
		return String("\n").join(out_lines) + "\n";
	}

	if (!has_col0_seq_item) {
		r_err = ERR_PARSE_ERROR;
		r_error_msg = "Existing cordis.patch.yml is not a top-level YAML sequence.";
		return String();
	}

	Vector<String> preamble;
	Vector<Vector<String>> top_blocks;
	int first_item_idx = -1;
	for (int i = 0; i < lines.size(); i++) {
		if (_is_yaml_seq_item_at(lines[i], 0)) {
			first_item_idx = i;
			break;
		}
		preamble.push_back(lines[i]);
	}

	Vector<String> current_block;
	for (int i = first_item_idx; i < lines.size(); i++) {
		if (_is_yaml_seq_item_at(lines[i], 0)) {
			if (!current_block.is_empty()) {
				top_blocks.push_back(current_block);
				current_block.clear();
			}
		}
		current_block.push_back(lines[i]);
	}
	if (!current_block.is_empty()) {
		top_blocks.push_back(current_block);
	}

	Vector<String> out_lines;
	out_lines.append_array(preamble);

	for (int b = 0; b < top_blocks.size(); b++) {
		const Vector<String> &block = top_blocks[b];
		if (_yaml_first_line_is_insert(block[0])) {
			int child_indent = -1;
			for (int k = 1; k < block.size(); k++) {
				String trimmed = block[k].strip_edges();
				if (trimmed.is_empty() || trimmed.begins_with("#")) {
					continue;
				}
				int ind = _count_leading_spaces(block[k]);
				if (ind > 0 && _is_yaml_seq_item_at(block[k], ind)) {
					child_indent = ind;
					break;
				}
			}

			if (child_indent <= 0) {
				out_lines.append_array(block);
				continue;
			}

			Vector<String> insert_preamble;
			Vector<Vector<String>> children;
			int first_child = -1;
			for (int k = 0; k < block.size(); k++) {
				if (k > 0 && _is_yaml_seq_item_at(block[k], child_indent)) {
					first_child = k;
					break;
				}
				insert_preamble.push_back(block[k]);
			}

			Vector<String> cur_child;
			for (int k = first_child; k < block.size(); k++) {
				if (_is_yaml_seq_item_at(block[k], child_indent)) {
					if (!cur_child.is_empty()) {
						children.push_back(cur_child);
						cur_child.clear();
					}
				}
				cur_child.push_back(block[k]);
			}
			if (!cur_child.is_empty()) {
				children.push_back(cur_child);
			}

			Vector<Vector<String>> kept_children;
			for (int c = 0; c < children.size(); c++) {
				if (!_yaml_item_has_id(children[c], child_indent, target_id)) {
					kept_children.push_back(children[c]);
				}
			}

			if (!kept_children.is_empty()) {
				out_lines.append_array(insert_preamble);
				for (int c = 0; c < kept_children.size(); c++) {
					out_lines.append_array(kept_children[c]);
				}
			}
		} else if (_yaml_item_has_id(block, 0, target_id)) {
			continue;
		} else {
			out_lines.append_array(block);
		}
	}

	while (!out_lines.is_empty() && out_lines[out_lines.size() - 1].strip_edges().is_empty()) {
		out_lines.remove_at(out_lines.size() - 1);
	}
	if (!out_lines.is_empty()) {
		out_lines.push_back("");
	}
	out_lines.append_array(canonical);

	r_err = OK;
	r_error_msg = String();
	return String("\n").join(out_lines) + "\n";
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

String MCPConfigExporter::get_antigravity_config_path() {
	String home = get_user_home_dir();
	if (home.is_empty()) {
		return String();
	}
	return home.path_join(".gemini").path_join("config").path_join("mcp_config.json");
}

String MCPConfigExporter::get_codex_config_path() {
	String codex_home = OS::get_singleton()->get_environment("CODEX_HOME").strip_edges();
	if (!codex_home.is_empty()) {
		return codex_home.path_join("config.toml");
	}
	String home = get_user_home_dir();
	if (home.is_empty()) {
		return String();
	}
	return home.path_join(".codex").path_join("config.toml");
}

String MCPConfigExporter::get_deepseek_harness_config_path() {
	String dsh_home = OS::get_singleton()->get_environment("DSH_HOME").strip_edges();
	if (!dsh_home.is_empty()) {
		return dsh_home.path_join("cordis.patch.yml");
	}
	String home = get_user_home_dir();
	if (home.is_empty()) {
		return String();
	}
	return home.path_join(".dsh").path_join("cordis.patch.yml");
}

static Error _read_optional_config_file(const String &p_target_path, String &r_existing, String &r_error_msg) {
	if (p_target_path.is_empty()) {
		r_error_msg = "Could not determine configuration file path.";
		return ERR_CANT_CREATE;
	}
	r_existing = String();
	if (FileAccess::exists(p_target_path)) {
		Error read_err = OK;
		r_existing = FileAccess::get_file_as_string(p_target_path, &read_err);
		if (read_err != OK) {
			r_error_msg = "Failed to read existing config file: " + p_target_path;
			return read_err;
		}
	}
	return OK;
}

static Error _write_config_file(const String &p_target_path, const String &p_content, String &r_error_msg) {
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

	f->store_string(p_content);
	f->close();
	return OK;
}

static Error _write_merged_mcp_config(const String &p_target_path, const Dictionary &p_entry, String &r_error_msg) {
	String existing;
	Error read_err = _read_optional_config_file(p_target_path, existing, r_error_msg);
	if (read_err != OK) {
		return read_err;
	}

	Error merge_err = OK;
	String merged = MCPConfigExporter::merge_mcp_servers_json(existing, "godot-editor", p_entry, merge_err, r_error_msg);
	if (merge_err != OK) {
		return merge_err;
	}

	return _write_config_file(p_target_path, merged, r_error_msg);
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

Error MCPConfigExporter::export_antigravity_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg) {
	r_written_path = get_antigravity_config_path();
	Dictionary entry = build_cursor_server_entry(build_mcp_url(p_host, p_port));
	return _write_merged_mcp_config(r_written_path, entry, r_error_msg);
}

Error MCPConfigExporter::export_codex_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg) {
	r_written_path = get_codex_config_path();
	String existing;
	Error read_err = _read_optional_config_file(r_written_path, existing, r_error_msg);
	if (read_err != OK) {
		return read_err;
	}
	String merged = merge_codex_config_toml(existing, "godot-editor", build_mcp_url(p_host, p_port));
	return _write_config_file(r_written_path, merged, r_error_msg);
}

Error MCPConfigExporter::export_deepseek_harness_config(const String &p_host, int p_port, String &r_written_path, String &r_error_msg) {
	r_written_path = get_deepseek_harness_config_path();
	String existing;
	Error read_err = _read_optional_config_file(r_written_path, existing, r_error_msg);
	if (read_err != OK) {
		return read_err;
	}
	Error merge_err = OK;
	String merged = merge_deepseek_harness_patch_yaml(existing, "godot-editor", build_mcp_url(p_host, p_port), merge_err, r_error_msg);
	if (merge_err != OK) {
		return merge_err;
	}
	return _write_config_file(r_written_path, merged, r_error_msg);
}

String MCPConfigExporter::format_clipboard_snippet(const String &p_host, int p_port) {
	Dictionary entry = build_cursor_server_entry(build_mcp_url(p_host, p_port));
	Error err = OK;
	String err_msg;
	return merge_mcp_servers_json(String(), "godot-editor", entry, err, err_msg);
}
