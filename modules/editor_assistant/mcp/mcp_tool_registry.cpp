/**************************************************************************/
/*  mcp_tool_registry.cpp                                                 */
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

#include "mcp_tool_registry.h"

#include "core/io/json.h"

Dictionary MCPToolRegistry::make_prop(const String &p_type, const String &p_description, const Variant &p_default) {
	Dictionary prop;
	prop["type"] = p_type;
	if (!p_description.is_empty()) {
		prop["description"] = p_description;
	}
	if (p_default.get_type() != Variant::NIL) {
		prop["default"] = p_default;
	}
	return prop;
}

Dictionary MCPToolRegistry::make_enum_prop(const Vector<String> &p_values, const String &p_description, const Variant &p_default) {
	Dictionary prop = make_prop("string", p_description, p_default);
	Array enum_arr;
	for (const String &v : p_values) {
		enum_arr.push_back(v);
	}
	prop["enum"] = enum_arr;
	return prop;
}

Dictionary MCPToolRegistry::make_object_schema(const Dictionary &p_properties, const Vector<String> &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	if (!p_required.is_empty()) {
		Array req;
		for (const String &r : p_required) {
			req.push_back(r);
		}
		schema["required"] = req;
	}
	schema["additionalProperties"] = false;
	return schema;
}

void MCPToolRegistry::register_tool(const String &p_name, const String &p_description, const Dictionary &p_input_schema, MCPPermissionLevel p_perm_level, MCPToolHandler p_handler) {
	ERR_FAIL_COND_MSG(p_name.is_empty() || p_handler == nullptr, "MCPToolRegistry: Invalid tool registration.");
	if (!tools.has(p_name)) {
		tool_order.push_back(p_name);
	}
	MCPToolDef def;
	def.name = p_name;
	def.description = p_description;
	def.input_schema = p_input_schema;
	def.perm_level = p_perm_level;
	def.handler = p_handler;
	tools[p_name] = def;
}

bool MCPToolRegistry::has_tool(const String &p_name) const {
	return tools.has(p_name);
}

int MCPToolRegistry::get_tool_count() const {
	return tools.size();
}

Array MCPToolRegistry::get_tools_list() const {
	Array list;
	for (const String &name : tool_order) {
		const MCPToolDef *def = tools.getptr(name);
		if (!def) {
			continue;
		}
		Dictionary item;
		item["name"] = def->name;
		item["description"] = def->description;
		item["inputSchema"] = def->input_schema;
		list.push_back(item);
	}
	return list;
}

static Dictionary _make_tool_result(const String &p_text, bool p_is_error) {
	Dictionary text_block;
	text_block["type"] = "text";
	text_block["text"] = p_text;

	Array content;
	content.push_back(text_block);

	Dictionary result;
	result["content"] = content;
	result["isError"] = p_is_error;
	return result;
}

Dictionary MCPToolRegistry::call_tool(const String &p_name, const Dictionary &p_arguments, const Ref<MCPPermissionGate> &p_gate) const {
	const MCPToolDef *def = tools.getptr(p_name);
	if (!def || !def->handler) {
		return _make_tool_result("Unknown tool: " + p_name, true);
	}

	if (p_gate.is_valid()) {
		String reason;
		if (!p_gate->is_allowed(def->name, def->perm_level, reason)) {
			return _make_tool_result(reason, true);
		}
	}

	String err_msg;
	Variant result = def->handler(p_arguments, err_msg);
	if (!err_msg.is_empty()) {
		return _make_tool_result(err_msg, true);
	}

	String text_out;
	if (result.get_type() == Variant::DICTIONARY || result.get_type() == Variant::ARRAY) {
		text_out = JSON::stringify(result, "  ");
	} else if (result.get_type() == Variant::NIL) {
		text_out = "{}";
	} else {
		text_out = String(result);
	}
	return _make_tool_result(text_out, false);
}
