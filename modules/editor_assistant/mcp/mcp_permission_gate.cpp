/**************************************************************************/
/*  mcp_permission_gate.cpp                                               */
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

#include "mcp_permission_gate.h"

void MCPPermissionGate::set_policy(MCPPermissionLevel p_policy) {
	policy = p_policy;
}

MCPPermissionLevel MCPPermissionGate::get_policy() const {
	return policy;
}

String MCPPermissionGate::level_to_string(MCPPermissionLevel p_level) {
	switch (p_level) {
		case MCP_PERM_READ:
			return "Read";
		case MCP_PERM_EDIT:
			return "Edit";
		case MCP_PERM_ASK:
			return "Ask";
		case MCP_PERM_AUTO:
			return "Auto";
	}
	return "Unknown";
}

bool MCPPermissionGate::is_allowed(const String &p_tool_name, MCPPermissionLevel p_required_level, String &r_reason) const {
	if (policy == MCP_PERM_AUTO || static_cast<int>(p_required_level) <= static_cast<int>(policy)) {
		return true;
	}
	r_reason = vformat("Permission denied for tool '%s': requires '%s' level, current MCP policy is '%s'.",
			p_tool_name, level_to_string(p_required_level), level_to_string(policy));
	return false;
}
