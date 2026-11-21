/**************************************************************************/
/*  test_editor_assistant_mcp.h                                           */
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

#ifdef TOOLS_ENABLED

#include "../mcp/mcp_config_exporter.h"
#include "../mcp/mcp_server.h"
#include "../mcp/tools/mcp_doc_tools.h"
#include "../mcp/tools/mcp_scene_tools.h"

#include "core/io/json.h"
#include "tests/test_macros.h"

namespace TestEditorAssistantMCP {

TEST_CASE("[EditorAssistant][MCP] Loopback Host and Origin Validation") {
	CHECK(EditorMCPServer::is_valid_loopback_headers("127.0.0.1:6550", ""));
	CHECK(EditorMCPServer::is_valid_loopback_headers("localhost:6550", "http://localhost:3000"));
	CHECK(EditorMCPServer::is_valid_loopback_headers("[::1]:6550", "http://127.0.0.1"));

	CHECK_FALSE(EditorMCPServer::is_valid_loopback_headers("", ""));
	CHECK_FALSE(EditorMCPServer::is_valid_loopback_headers("evil.example:6550", ""));
	CHECK_FALSE(EditorMCPServer::is_valid_loopback_headers("127.0.0.1:6550", "https://evil.example"));
	CHECK_FALSE(EditorMCPServer::is_valid_loopback_headers("127.0.0.1:6550", "null"));
}

TEST_CASE("[EditorAssistant][MCP] JSON-RPC Lifecycle and Omitted Params") {
	Ref<MCPPermissionGate> gate;
	gate.instantiate();
	Ref<MCPToolRegistry> registry;
	registry.instantiate();
	MCPDocTools::register_tools(registry);

	Ref<EditorMCPServer> server;
	server.instantiate();
	server->set_permission_gate(gate);
	server->set_registry(registry);

	int status = 0;

	// 1. initialize
	String init_resp = server->handle_jsonrpc_body("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\"}}", status);
	CHECK(status == 200);
	Dictionary init_dict = JSON::parse_string(init_resp);
	CHECK(init_dict["id"] == Variant(1));
	Dictionary init_res = init_dict["result"];
	CHECK(String(init_res["protocolVersion"]) == "2025-03-26");

	// 2. notifications/initialized -> 202 empty
	String notif_resp = server->handle_jsonrpc_body("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", status);
	CHECK(status == 202);
	CHECK(notif_resp.is_empty());

	// 3. ping without params
	String ping_resp = server->handle_jsonrpc_body("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}", status);
	CHECK(status == 200);
	Dictionary ping_dict = JSON::parse_string(ping_resp);
	CHECK(ping_dict.has("result"));

	// 4. tools/list without params
	String list_resp = server->handle_jsonrpc_body("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}", status);
	CHECK(status == 200);
	Dictionary list_dict = JSON::parse_string(list_resp);
	Array tools = Dictionary(list_dict["result"])["tools"];
	CHECK(tools.size() >= 5);

	// 5. tools/call doc_check_api
	String call_resp = server->handle_jsonrpc_body("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"doc_check_api\",\"arguments\":{\"class_name\":\"Node3D\",\"member_name\":\"global_position\"}}}", status);
	CHECK(status == 200);
	Dictionary call_dict = JSON::parse_string(call_resp);
	Dictionary call_res = call_dict["result"];
	CHECK(bool(call_res["isError"]) == false);
}

TEST_CASE("[EditorAssistant][MCP] Doc Path Sandboxing and API Verification") {
	String normalized;
	String err;

	CHECK(MCPDocTools::validate_res_doc_path("docs/design.md", normalized, err));
	CHECK(normalized == "res://docs/design.md");

	CHECK_FALSE(MCPDocTools::validate_res_doc_path("res://../secret.md", normalized, err));
	CHECK_FALSE(MCPDocTools::validate_res_doc_path("C:/Windows/win.ini", normalized, err));
	CHECK_FALSE(MCPDocTools::validate_res_doc_path("res://.godot/internal.md", normalized, err));
	CHECK_FALSE(MCPDocTools::validate_res_doc_path("res://scripts/player.gd", normalized, err));

	String api_err;
	Dictionary present = MCPDocTools::tool_check_api(Dictionary(), api_err);
	CHECK_FALSE(api_err.is_empty());

	Dictionary args_ok;
	args_ok["class_name"] = "CharacterBody3D";
	args_ok["member_name"] = "move_and_slide";
	api_err = String();
	Dictionary ok_res = MCPDocTools::tool_check_api(args_ok, api_err);
	CHECK(api_err.is_empty());
	CHECK(bool(ok_res["exists"]) == true);

	Dictionary args_old;
	args_old["class_name"] = "KinematicBody3D";
	Dictionary old_res = MCPDocTools::tool_check_api(args_old, api_err);
	CHECK(bool(old_res["exists"]) == false);
}

TEST_CASE("[EditorAssistant][MCP] JSON to Variant Conversion") {
	String err;
	Array vec_arr;
	vec_arr.push_back(1.5);
	vec_arr.push_back(2.0);
	vec_arr.push_back(-3.0);
	Variant v3 = MCPSceneTools::json_to_variant(vec_arr, Variant::VECTOR3, "", err);
	CHECK(err.is_empty());
	CHECK(v3.get_type() == Variant::VECTOR3);
	CHECK(Vector3(v3).is_equal_approx(Vector3(1.5, 2.0, -3.0)));

	Variant col = MCPSceneTools::json_to_variant("#336699", Variant::COLOR, "", err);
	CHECK(err.is_empty());
	CHECK(col.get_type() == Variant::COLOR);

	Variant np = MCPSceneTools::json_to_variant("Player/Collider", Variant::NODE_PATH, "", err);
	CHECK(err.is_empty());
	CHECK(np.get_type() == Variant::NODE_PATH);
	CHECK(String(NodePath(np)) == "Player/Collider");
}

TEST_CASE("[EditorAssistant][MCP] Non-Destructive Config Export Merge") {
	const String existing = "{\n  \"mcpServers\": {\n    \"other-tool\": {\n      \"command\": \"node\",\n      \"args\": [\"server.js\"]\n    }\n  },\n  \"theme\": \"dark\"\n}";
	Error err = OK;
	String err_msg;

	Dictionary cursor_entry = MCPConfigExporter::build_cursor_server_entry("http://127.0.0.1:6550/mcp");
	String merged = MCPConfigExporter::merge_mcp_servers_json(existing, "godot-editor", cursor_entry, err, err_msg);
	CHECK(err == OK);
	Dictionary parsed = JSON::parse_string(merged);
	CHECK(String(parsed["theme"]) == "dark");
	Dictionary servers = parsed["mcpServers"];
	CHECK(servers.has("other-tool"));
	CHECK(servers.has("godot-editor"));
	CHECK(String(Dictionary(servers["godot-editor"])["url"]) == "http://127.0.0.1:6550/mcp");

	Dictionary claude_win = MCPConfigExporter::build_claude_desktop_server_entry("http://127.0.0.1:6550/mcp", true);
	CHECK(String(claude_win["command"]) == "cmd");
	Array win_args = claude_win["args"];
	CHECK(String(win_args[0]) == "/c");
	CHECK(String(win_args[1]) == "npx");
}

} // namespace TestEditorAssistantMCP

#endif // TOOLS_ENABLED
