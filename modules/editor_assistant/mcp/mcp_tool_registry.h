/**************************************************************************/
/*  mcp_tool_registry.h                                                   */
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

#include "mcp_permission_gate.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

typedef Variant (*MCPToolHandler)(const Dictionary &p_args, String &r_error);

struct MCPToolDef {
	String name;
	String description;
	Dictionary input_schema;
	MCPPermissionLevel perm_level = MCP_PERM_READ;
	MCPToolHandler handler = nullptr;
};

class MCPToolRegistry : public RefCounted {
	GDCLASS(MCPToolRegistry, RefCounted);

	HashMap<String, MCPToolDef> tools;
	Vector<String> tool_order;

public:
	static Dictionary make_prop(const String &p_type, const String &p_description, const Variant &p_default = Variant());
	static Dictionary make_enum_prop(const Vector<String> &p_values, const String &p_description, const Variant &p_default = Variant());
	static Dictionary make_object_schema(const Dictionary &p_properties, const Vector<String> &p_required = Vector<String>());

	void register_tool(const String &p_name, const String &p_description, const Dictionary &p_input_schema, MCPPermissionLevel p_perm_level, MCPToolHandler p_handler);
	bool has_tool(const String &p_name) const;
	int get_tool_count() const;

	Array get_tools_list() const;
	Dictionary call_tool(const String &p_name, const Dictionary &p_arguments, const Ref<MCPPermissionGate> &p_gate) const;
};
