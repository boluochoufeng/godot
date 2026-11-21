/**************************************************************************/
/*  mcp_doc_tools.cpp                                                     */
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

#include "mcp_doc_tools.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"

static bool _inherits_doc_or_class(const DocTools *p_docs, const String &p_class_name, const String &p_target_base) {
	if (p_target_base.is_empty() || p_class_name == p_target_base) {
		return true;
	}
	if (ClassDB::class_exists(p_class_name) && ClassDB::class_exists(p_target_base)) {
		if (ClassDB::is_parent_class(p_class_name, p_target_base)) {
			return true;
		}
	}
	if (!p_docs) {
		return false;
	}
	String cur = p_class_name;
	for (int depth = 0; depth < 32; depth++) {
		const DocData::ClassDoc *cd = p_docs->class_list.getptr(cur);
		if (!cd || cd->inherits.is_empty()) {
			break;
		}
		if (cd->inherits == p_target_base) {
			return true;
		}
		cur = cd->inherits;
	}
	return false;
}

static String _format_method_signature(const DocData::MethodDoc &p_m) {
	String sig = p_m.name + "(";
	for (int i = 0; i < p_m.arguments.size(); i++) {
		if (i > 0) {
			sig += ", ";
		}
		const DocData::ArgumentDoc &arg = p_m.arguments[i];
		sig += arg.name + ": " + (arg.type.is_empty() ? String("Variant") : arg.type);
		if (!arg.default_value.is_empty()) {
			sig += " = " + arg.default_value;
		}
	}
	sig += ")";
	if (!p_m.return_type.is_empty()) {
		sig += " -> " + p_m.return_type;
	}
	if (!p_m.qualifiers.is_empty()) {
		sig += " [" + p_m.qualifiers + "]";
	}
	return sig;
}

static String _format_classdb_method_signature(const MethodInfo &p_m) {
	String sig = p_m.name + "(";
	for (int i = 0; i < p_m.arguments.size(); i++) {
		if (i > 0) {
			sig += ", ";
		}
		const PropertyInfo &arg = p_m.arguments[i];
		String t = arg.class_name != StringName() ? String(arg.class_name) : Variant::get_type_name(arg.type);
		sig += arg.name + ": " + t;
	}
	sig += ")";
	if (p_m.return_val.type != Variant::NIL || p_m.return_val.class_name != StringName()) {
		String rt = p_m.return_val.class_name != StringName() ? String(p_m.return_val.class_name) : Variant::get_type_name(p_m.return_val.type);
		sig += " -> " + rt;
	}
	return sig;
}

static bool _is_supported_doc_extension(const String &p_ext) {
	String ext = p_ext.to_lower();
	return ext == "md" || ext == "txt" || ext == "rst";
}

bool MCPDocTools::validate_res_doc_path(const String &p_path, String &r_normalized, String &r_error) {
	String raw = p_path.strip_edges().replace("\\", "/");
	if (raw.is_empty()) {
		r_error = "Missing required 'path' parameter.";
		return false;
	}

	if (raw.begins_with("/") || raw.begins_with("user://") || raw.begins_with("uid://")) {
		r_error = "Access denied: path must reside inside 'res://'.";
		return false;
	}

	// Reject Windows drive paths (e.g. C:/...) or non-res schemes.
	int colon_idx = raw.find_char(':');
	if (colon_idx >= 0 && !raw.begins_with("res://")) {
		r_error = "Access denied: only 'res://' project paths are allowed.";
		return false;
	}

	String body = raw.begins_with("res://") ? raw.substr(6) : raw;
	Vector<String> segments = body.split("/", false);
	for (const String &seg : segments) {
		if (seg == "..") {
			r_error = "Access denied: '..' path traversal is not permitted.";
			return false;
		}
	}

	String normalized = ("res://" + body).simplify_path();
	if (!normalized.begins_with("res://")) {
		r_error = "Access denied: normalized path escaped 'res://'.";
		return false;
	}

	String rel = normalized.substr(6);
	if (rel.begins_with(".godot/") || rel == ".godot" || rel.begins_with(".git/") || rel == ".git" || rel.begins_with(".cursor/") || rel == ".cursor") {
		r_error = "Access denied: hidden internal directories cannot be read via project doc tools.";
		return false;
	}

	if (!_is_supported_doc_extension(normalized.get_extension())) {
		r_error = "Unsupported file extension. Only .md, .txt, and .rst project documentation files are allowed.";
		return false;
	}

	r_normalized = normalized;
	return true;
}

Variant MCPDocTools::tool_search_classes(const Dictionary &p_args, String &r_error) {
	String query = String(p_args.get("query", "")).strip_edges().to_lower();
	String inherits_filter = String(p_args.get("inherits", "")).strip_edges();
	int limit = CLAMP(static_cast<int>(p_args.get("limit", 25)), 1, 100);

	const DocTools *docs = EditorHelp::get_doc_data();

	struct MatchEntry {
		int score = 0;
		Dictionary data;
	};
	Vector<MatchEntry> ranked;

	if (docs && !docs->class_list.is_empty()) {
		for (const KeyValue<String, DocData::ClassDoc> &E : docs->class_list) {
			const DocData::ClassDoc &cd = E.value;
			if (!inherits_filter.is_empty() && !_inherits_doc_or_class(docs, cd.name, inherits_filter)) {
				continue;
			}

			String name_lower = cd.name.to_lower();
			int score = 0;
			if (query.is_empty()) {
				score = 1;
			} else if (name_lower == query) {
				score = 100;
			} else if (name_lower.begins_with(query)) {
				score = 80;
			} else if (name_lower.contains(query)) {
				score = 50;
			} else if (cd.brief_description.to_lower().contains(query) || cd.keywords.to_lower().contains(query)) {
				score = 20;
			}

			if (score == 0) {
				continue;
			}

			Dictionary item;
			item["name"] = cd.name;
			item["inherits"] = cd.inherits;
			item["brief"] = cd.brief_description.strip_edges();
			if (cd.is_deprecated) {
				item["deprecated"] = cd.deprecated_message;
			}
			if (cd.is_script_doc) {
				item["script_path"] = cd.script_path;
			}
			ranked.push_back({ score, item });
		}
	} else {
		LocalVector<StringName> classes;
		ClassDB::get_class_list(classes);
		for (const StringName &cls : classes) {
			String name = String(cls);
			if (!inherits_filter.is_empty() && (!ClassDB::class_exists(inherits_filter) || !ClassDB::is_parent_class(cls, inherits_filter))) {
				continue;
			}
			String name_lower = name.to_lower();
			int score = 0;
			if (query.is_empty()) {
				score = 1;
			} else if (name_lower == query) {
				score = 100;
			} else if (name_lower.begins_with(query)) {
				score = 80;
			} else if (name_lower.contains(query)) {
				score = 50;
			}
			if (score == 0) {
				continue;
			}
			Dictionary item;
			item["name"] = name;
			item["inherits"] = String(ClassDB::get_parent_class(cls));
			ranked.push_back({ score, item });
		}
	}

	// Simple sort by descending score then ascending name.
	for (int i = 0; i < ranked.size(); i++) {
		for (int j = i + 1; j < ranked.size(); j++) {
			bool swap = false;
			if (ranked[j].score > ranked[i].score) {
				swap = true;
			} else if (ranked[j].score == ranked[i].score) {
				String ni = ranked[i].data["name"];
				String nj = ranked[j].data["name"];
				if (nj.naturalcasecmp_to(ni) < 0) {
					swap = true;
				}
			}
			if (swap) {
				MatchEntry tmp = ranked[i];
				ranked.write[i] = ranked[j];
				ranked.write[j] = tmp;
			}
		}
	}

	Array results;
	int count = MIN(limit, ranked.size());
	for (int i = 0; i < count; i++) {
		results.push_back(ranked[i].data);
	}

	Dictionary out;
	out["total_matches"] = ranked.size();
	out["returned"] = results.size();
	out["classes"] = results;
	return out;
}

static void _append_class_doc_section(const DocData::ClassDoc &p_cd, const String &p_section, Dictionary &r_out) {
	if (p_section == "all" || p_section == "methods") {
		Array arr = r_out.get("methods", Array());
		for (const DocData::MethodDoc &m : p_cd.methods) {
			Dictionary d;
			d["name"] = m.name;
			d["defined_in"] = p_cd.name;
			d["signature"] = _format_method_signature(m);
			if (m.is_deprecated) {
				d["deprecated"] = m.deprecated_message;
			}
			arr.push_back(d);
		}
		r_out["methods"] = arr;
	}

	if (p_section == "all" || p_section == "properties") {
		Array arr = r_out.get("properties", Array());
		for (const DocData::PropertyDoc &p : p_cd.properties) {
			Dictionary d;
			d["name"] = p.name;
			d["type"] = p.type;
			d["defined_in"] = p_cd.name;
			if (!p.default_value.is_empty()) {
				d["default"] = p.default_value;
			}
			if (!p.enumeration.is_empty()) {
				d["enum"] = p.enumeration;
			}
			if (p.is_deprecated) {
				d["deprecated"] = p.deprecated_message;
			}
			arr.push_back(d);
		}
		r_out["properties"] = arr;
	}

	if (p_section == "all" || p_section == "signals") {
		Array arr = r_out.get("signals", Array());
		for (const DocData::MethodDoc &s : p_cd.signals) {
			Dictionary d;
			d["name"] = s.name;
			d["defined_in"] = p_cd.name;
			d["signature"] = _format_method_signature(s);
			if (s.is_deprecated) {
				d["deprecated"] = s.deprecated_message;
			}
			arr.push_back(d);
		}
		r_out["signals"] = arr;
	}

	if (p_section == "all" || p_section == "enums" || p_section == "constants") {
		Array arr = r_out.get("constants", Array());
		for (const DocData::ConstantDoc &c : p_cd.constants) {
			if (p_section == "enums" && c.enumeration.is_empty()) {
				continue;
			}
			Dictionary d;
			d["name"] = c.name;
			d["value"] = c.value;
			d["defined_in"] = p_cd.name;
			if (!c.enumeration.is_empty()) {
				d["enum"] = c.enumeration;
			}
			if (c.is_deprecated) {
				d["deprecated"] = c.deprecated_message;
			}
			arr.push_back(d);
		}
		r_out["constants"] = arr;
	}
}

Variant MCPDocTools::tool_get_class_detail(const Dictionary &p_args, String &r_error) {
	String class_name = String(p_args.get("class_name", "")).strip_edges();
	if (class_name.is_empty()) {
		r_error = "Missing required 'class_name' parameter.";
		return Variant();
	}

	String section = String(p_args.get("section", "all")).strip_edges().to_lower();
	if (section.is_empty()) {
		section = "all";
	}
	bool include_inherited = bool(p_args.get("include_inherited", false));

	const DocTools *docs = EditorHelp::get_doc_data();
	const DocData::ClassDoc *cd = docs ? docs->class_list.getptr(class_name) : nullptr;

	if (cd) {
		Dictionary out;
		out["name"] = cd->name;
		out["inherits"] = cd->inherits;
		out["brief"] = cd->brief_description.strip_edges();
		if (section == "all" || section == "summary") {
			out["description"] = cd->description.strip_edges();
		}
		if (cd->is_deprecated) {
			out["deprecated"] = cd->deprecated_message;
		}
		if (cd->is_experimental) {
			out["experimental"] = cd->experimental_message;
		}
		if (cd->is_script_doc) {
			out["script_path"] = cd->script_path;
		}

		if (section != "summary") {
			_append_class_doc_section(*cd, section, out);
			if (include_inherited && docs) {
				String parent = cd->inherits;
				for (int depth = 0; depth < 16 && !parent.is_empty(); depth++) {
					const DocData::ClassDoc *pcd = docs->class_list.getptr(parent);
					if (!pcd) {
						break;
					}
					_append_class_doc_section(*pcd, section, out);
					parent = pcd->inherits;
				}
			}
		}
		return out;
	}

	if (!ClassDB::class_exists(class_name)) {
		r_error = vformat("Class '%s' was not found in Godot 4.8 DocTools or ClassDB.", class_name);
		return Variant();
	}

	Dictionary out;
	out["name"] = class_name;
	out["inherits"] = String(ClassDB::get_parent_class(class_name));
	out["source"] = "ClassDB";

	if (section == "all" || section == "methods") {
		List<MethodInfo> methods;
		ClassDB::get_method_list(class_name, &methods, !include_inherited);
		Array arr;
		for (const MethodInfo &m : methods) {
			Dictionary d;
			d["name"] = m.name;
			d["signature"] = _format_classdb_method_signature(m);
			arr.push_back(d);
		}
		out["methods"] = arr;
	}

	if (section == "all" || section == "properties") {
		List<PropertyInfo> props;
		ClassDB::get_property_list(class_name, &props, !include_inherited);
		Array arr;
		for (const PropertyInfo &p : props) {
			if ((p.usage & PROPERTY_USAGE_GROUP) || (p.usage & PROPERTY_USAGE_CATEGORY) || (p.usage & PROPERTY_USAGE_SUBGROUP)) {
				continue;
			}
			Dictionary d;
			d["name"] = p.name;
			d["type"] = p.class_name != StringName() ? String(p.class_name) : Variant::get_type_name(p.type);
			arr.push_back(d);
		}
		out["properties"] = arr;
	}

	if (section == "all" || section == "signals") {
		List<MethodInfo> sigs;
		ClassDB::get_signal_list(class_name, &sigs, !include_inherited);
		Array arr;
		for (const MethodInfo &s : sigs) {
			Dictionary d;
			d["name"] = s.name;
			d["signature"] = _format_classdb_method_signature(s);
			arr.push_back(d);
		}
		out["signals"] = arr;
	}

	if (section == "all" || section == "constants" || section == "enums") {
		List<String> consts;
		ClassDB::get_integer_constant_list(class_name, &consts, !include_inherited);
		Array arr;
		for (const String &c : consts) {
			Dictionary d;
			d["name"] = c;
			d["value"] = ClassDB::get_integer_constant(class_name, c);
			arr.push_back(d);
		}
		out["constants"] = arr;
	}

	return out;
}

Variant MCPDocTools::tool_check_api(const Dictionary &p_args, String &r_error) {
	String class_name = String(p_args.get("class_name", "")).strip_edges();
	if (class_name.is_empty()) {
		r_error = "Missing required 'class_name' parameter.";
		return Variant();
	}
	String member_name = String(p_args.get("member_name", "")).strip_edges();

	const DocTools *docs = EditorHelp::get_doc_data();
	const DocData::ClassDoc *root_cd = docs ? docs->class_list.getptr(class_name) : nullptr;
	bool class_exists_in_db = ClassDB::class_exists(class_name);

	if (!root_cd && !class_exists_in_db) {
		Dictionary missing;
		missing["exists"] = false;
		missing["class_name"] = class_name;
		if (!member_name.is_empty()) {
			missing["member_name"] = member_name;
		}
		missing["hint"] = vformat("Class '%s' does not exist in Godot 4.8. Use doc_search_classes to locate its Godot 4 equivalent.", class_name);
		return missing;
	}

	Array chain;
	String cur = class_name;
	for (int depth = 0; depth < 32 && !cur.is_empty(); depth++) {
		chain.push_back(cur);
		if (docs && docs->class_list.has(cur)) {
			cur = docs->class_list[cur].inherits;
		} else if (ClassDB::class_exists(cur)) {
			cur = String(ClassDB::get_parent_class(cur));
		} else {
			break;
		}
	}

	if (member_name.is_empty()) {
		Dictionary res;
		res["exists"] = true;
		res["class_name"] = class_name;
		res["inheritance_chain"] = chain;
		res["instantiable"] = class_exists_in_db ? ClassDB::can_instantiate(class_name) : false;
		if (root_cd && root_cd->is_deprecated) {
			res["deprecated"] = root_cd->deprecated_message;
		}
		return res;
	}

	// Walk inheritance chain in DocTools first for rich metadata.
	if (docs) {
		for (int idx = 0; idx < chain.size(); idx++) {
			String cname = chain[idx];
			const DocData::ClassDoc *cd = docs->class_list.getptr(cname);
			if (!cd) {
				continue;
			}
			for (const DocData::MethodDoc &m : cd->methods) {
				if (m.name == member_name) {
					Dictionary res;
					res["exists"] = true;
					res["class_name"] = class_name;
					res["member_name"] = member_name;
					res["member_kind"] = "method";
					res["defined_in"] = cname;
					res["signature"] = _format_method_signature(m);
					if (m.is_deprecated) {
						res["deprecated"] = m.deprecated_message;
					}
					if (m.is_experimental) {
						res["experimental"] = m.experimental_message;
					}
					return res;
				}
			}
			for (const DocData::PropertyDoc &p : cd->properties) {
				if (p.name == member_name) {
					Dictionary res;
					res["exists"] = true;
					res["class_name"] = class_name;
					res["member_name"] = member_name;
					res["member_kind"] = "property";
					res["defined_in"] = cname;
					res["type"] = p.type;
					if (!p.default_value.is_empty()) {
						res["default"] = p.default_value;
					}
					if (p.is_deprecated) {
						res["deprecated"] = p.deprecated_message;
					}
					return res;
				}
			}
			for (const DocData::MethodDoc &s : cd->signals) {
				if (s.name == member_name) {
					Dictionary res;
					res["exists"] = true;
					res["class_name"] = class_name;
					res["member_name"] = member_name;
					res["member_kind"] = "signal";
					res["defined_in"] = cname;
					res["signature"] = _format_method_signature(s);
					if (s.is_deprecated) {
						res["deprecated"] = s.deprecated_message;
					}
					return res;
				}
			}
			for (const DocData::ConstantDoc &c : cd->constants) {
				if (c.name == member_name) {
					Dictionary res;
					res["exists"] = true;
					res["class_name"] = class_name;
					res["member_name"] = member_name;
					res["member_kind"] = "constant";
					res["defined_in"] = cname;
					res["value"] = c.value;
					if (c.is_deprecated) {
						res["deprecated"] = c.deprecated_message;
					}
					return res;
				}
			}
		}
	}

	// Fallback to live ClassDB checks (handles un-documented internal/runtime classes or unit test environments).
	if (class_exists_in_db) {
		MethodInfo mi;
		if (ClassDB::get_method_info(class_name, member_name, &mi)) {
			Dictionary res;
			res["exists"] = true;
			res["class_name"] = class_name;
			res["member_name"] = member_name;
			res["member_kind"] = "method";
			res["signature"] = _format_classdb_method_signature(mi);
			return res;
		}
		PropertyInfo pi;
		if (ClassDB::get_property_info(class_name, member_name, &pi)) {
			Dictionary res;
			res["exists"] = true;
			res["class_name"] = class_name;
			res["member_name"] = member_name;
			res["member_kind"] = "property";
			res["type"] = pi.class_name != StringName() ? String(pi.class_name) : Variant::get_type_name(pi.type);
			return res;
		}
		if (ClassDB::has_signal(class_name, member_name)) {
			Dictionary res;
			res["exists"] = true;
			res["class_name"] = class_name;
			res["member_name"] = member_name;
			res["member_kind"] = "signal";
			return res;
		}
		bool ok_const = false;
		int64_t cval = ClassDB::get_integer_constant(class_name, member_name, &ok_const);
		if (ok_const) {
			Dictionary res;
			res["exists"] = true;
			res["class_name"] = class_name;
			res["member_name"] = member_name;
			res["member_kind"] = "constant";
			res["value"] = cval;
			return res;
		}
	}

	Dictionary not_found;
	not_found["exists"] = false;
	not_found["class_name"] = class_name;
	not_found["member_name"] = member_name;
	not_found["inheritance_chain"] = chain;
	return not_found;
}

static void _collect_project_doc_files(const String &p_dir, Vector<String> &r_files, int p_depth = 0) {
	if (p_depth > 16 || r_files.size() >= 500) {
		return;
	}
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	String item = da->get_next();
	while (!item.is_empty()) {
		if (item != "." && item != "..") {
			if (da->current_is_dir()) {
				if (!item.begins_with(".") && item != "bin" && item != "addons") {
					String sub = p_dir.ends_with("/") ? (p_dir + item) : (p_dir + "/" + item);
					_collect_project_doc_files(sub, r_files, p_depth + 1);
				}
			} else if (_is_supported_doc_extension(item.get_extension())) {
				String full = p_dir.ends_with("/") ? (p_dir + item) : (p_dir + "/" + item);
				r_files.push_back(full);
			}
		}
		item = da->get_next();
	}
	da->list_dir_end();
}

static String _extract_doc_title(const Vector<String> &p_lines, const String &p_fallback_path) {
	for (int i = 0; i < MIN(15, p_lines.size()); i++) {
		String trimmed = p_lines[i].strip_edges();
		if (trimmed.begins_with("# ")) {
			return trimmed.substr(2).strip_edges();
		}
	}
	return p_fallback_path.get_file();
}

Variant MCPDocTools::tool_search_project_docs(const Dictionary &p_args, String &r_error) {
	String query = String(p_args.get("query", "")).strip_edges().to_lower();
	int limit = CLAMP(static_cast<int>(p_args.get("limit", 20)), 1, 100);

	Vector<String> files;
	_collect_project_doc_files("res://", files);

	Array matched_docs;
	for (const String &path : files) {
		if (matched_docs.size() >= limit) {
			break;
		}
		Error err = OK;
		String content = FileAccess::get_file_as_string(path, &err);
		if (err != OK) {
			continue;
		}
		content = content.replace("\r\n", "\n");
		Vector<String> lines = content.split("\n", true);
		String title = _extract_doc_title(lines, path);

		if (query.is_empty()) {
			Dictionary doc_entry;
			doc_entry["path"] = path;
			doc_entry["title"] = title;
			doc_entry["total_lines"] = lines.size();
			matched_docs.push_back(doc_entry);
			continue;
		}

		bool path_hit = path.to_lower().contains(query) || title.to_lower().contains(query);
		Array snippets;
		for (int i = 0; i < lines.size(); i++) {
			if (lines[i].to_lower().contains(query)) {
				Dictionary hit;
				hit["line"] = i + 1;
				hit["snippet"] = lines[i].strip_edges().left(240);
				snippets.push_back(hit);
				if (snippets.size() >= 5) {
					break;
				}
			}
		}

		if (path_hit || !snippets.is_empty()) {
			Dictionary doc_entry;
			doc_entry["path"] = path;
			doc_entry["title"] = title;
			doc_entry["matches"] = snippets;
			matched_docs.push_back(doc_entry);
		}
	}

	Dictionary out;
	out["scanned_files"] = files.size();
	out["returned"] = matched_docs.size();
	out["documents"] = matched_docs;
	return out;
}

Variant MCPDocTools::tool_read_project_doc(const Dictionary &p_args, String &r_error) {
	String raw_path = String(p_args.get("path", ""));
	String normalized_path;
	if (!validate_res_doc_path(raw_path, normalized_path, r_error)) {
		return Variant();
	}

	if (!FileAccess::exists(normalized_path)) {
		r_error = "Documentation file does not exist: " + normalized_path;
		return Variant();
	}

	Error read_err = OK;
	String content = FileAccess::get_file_as_string(normalized_path, &read_err);
	if (read_err != OK) {
		r_error = "Failed to read documentation file: " + normalized_path;
		return Variant();
	}

	content = content.replace("\r\n", "\n");
	Vector<String> lines = content.split("\n", true);
	int total_lines = lines.size();

	int start_line = MAX(1, static_cast<int>(p_args.get("start_line", 1)));
	int end_line = static_cast<int>(p_args.get("end_line", MIN(total_lines, start_line + 199)));
	if (end_line < start_line) {
		end_line = start_line;
	}
	if (end_line - start_line + 1 > 400) {
		end_line = start_line + 399;
	}
	end_line = MIN(end_line, total_lines);

	String slice_text;
	for (int i = start_line; i <= end_line; i++) {
		if (i > start_line) {
			slice_text += "\n";
		}
		slice_text += lines[i - 1];
	}

	Dictionary out;
	out["path"] = normalized_path;
	out["total_lines"] = total_lines;
	out["start_line"] = start_line;
	out["end_line"] = end_line;
	out["content"] = slice_text;
	return out;
}

void MCPDocTools::register_tools(const Ref<MCPToolRegistry> &p_registry) {
	ERR_FAIL_COND(p_registry.is_null());

	{
		Dictionary props;
		props["query"] = MCPToolRegistry::make_prop("string", "Keyword or class name substring to search for (leave empty to list classes).", "");
		props["inherits"] = MCPToolRegistry::make_prop("string", "Optional base class filter (e.g. 'Node3D', 'Control', 'Resource').", "");
		props["limit"] = MCPToolRegistry::make_prop("integer", "Maximum results to return (1-100).", 25);
		p_registry->register_tool(
				"doc_search_classes",
				"Search Godot 4.8 built-in and project script classes by name, description, or inheritance.",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_READ,
				&MCPDocTools::tool_search_classes);
	}

	{
		Dictionary props;
		props["class_name"] = MCPToolRegistry::make_prop("string", "Exact Godot class name (e.g. 'CharacterBody3D').");
		Vector<String> sections;
		sections.push_back("all");
		sections.push_back("summary");
		sections.push_back("methods");
		sections.push_back("properties");
		sections.push_back("signals");
		sections.push_back("enums");
		sections.push_back("constants");
		props["section"] = MCPToolRegistry::make_enum_prop(sections, "Which section of documentation to return.", "all");
		props["include_inherited"] = MCPToolRegistry::make_prop("boolean", "Whether to include inherited members.", false);
		Vector<String> req;
		req.push_back("class_name");
		p_registry->register_tool(
				"doc_get_class_detail",
				"Retrieve Godot 4.8 class documentation (methods, properties, signals, enums) from the built-in DocTools database.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_READ,
				&MCPDocTools::tool_get_class_detail);
	}

	{
		Dictionary props;
		props["class_name"] = MCPToolRegistry::make_prop("string", "Class name to verify in Godot 4.8.");
		props["member_name"] = MCPToolRegistry::make_prop("string", "Optional method, property, signal, or constant name to check across the inheritance chain.", "");
		Vector<String> req;
		req.push_back("class_name");
		p_registry->register_tool(
				"doc_check_api",
				"Verify whether a Godot 4.8 class or member exists, get its exact signature, and detect deprecated or removed Godot 3 APIs.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_READ,
				&MCPDocTools::tool_check_api);
	}

	{
		Dictionary props;
		props["query"] = MCPToolRegistry::make_prop("string", "Search term across project .md/.txt/.rst documentation files (empty lists available files).", "");
		props["limit"] = MCPToolRegistry::make_prop("integer", "Maximum documents to return (1-100).", 20);
		p_registry->register_tool(
				"doc_search_project_docs",
				"Search local Markdown and text knowledge base documents inside the Godot project (res://).",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_READ,
				&MCPDocTools::tool_search_project_docs);
	}

	{
		Dictionary props;
		props["path"] = MCPToolRegistry::make_prop("string", "Project-relative path inside res:// (e.g. 'res://docs/design.md').");
		props["start_line"] = MCPToolRegistry::make_prop("integer", "1-based start line.", 1);
		props["end_line"] = MCPToolRegistry::make_prop("integer", "1-based inclusive end line (max 400 lines per call).", 200);
		Vector<String> req;
		req.push_back("path");
		p_registry->register_tool(
				"doc_read_project_doc",
				"Read a local Markdown or text documentation file inside the Godot project (res://) by line range.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_READ,
				&MCPDocTools::tool_read_project_doc);
	}
}
