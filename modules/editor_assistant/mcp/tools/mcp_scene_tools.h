/**************************************************************************/
/*  mcp_scene_tools.h                                                     */
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

#include "../mcp_tool_registry.h"

class MCPSceneTools {
public:
	static void register_tools(const Ref<MCPToolRegistry> &p_registry);

	// Exposed for unit testing and shared conversion across tools.
	static Variant json_to_variant(const Variant &p_json_val, Variant::Type p_target_type, const String &p_hint_string, String &r_error);
	static Variant variant_to_json(const Variant &p_val);

	static Variant tool_get_tree(const Dictionary &p_args, String &r_error);
	static Variant tool_create_root(const Dictionary &p_args, String &r_error);
	static Variant tool_add_node(const Dictionary &p_args, String &r_error);
	static Variant tool_remove_node(const Dictionary &p_args, String &r_error);
	static Variant tool_reparent_node(const Dictionary &p_args, String &r_error);
	static Variant tool_get_properties(const Dictionary &p_args, String &r_error);
	static Variant tool_set_property(const Dictionary &p_args, String &r_error);
	static Variant tool_connect_signal(const Dictionary &p_args, String &r_error);
	static Variant tool_edit_resource(const Dictionary &p_args, String &r_error);
	static Variant tool_save(const Dictionary &p_args, String &r_error);
};
