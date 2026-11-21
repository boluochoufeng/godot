/**************************************************************************/
/*  mcp_server.h                                                          */
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
#include "mcp_tool_registry.h"

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"

class EditorMCPServer : public RefCounted {
	GDCLASS(EditorMCPServer, RefCounted);

	enum {
		MAX_ACTIVE_CLIENTS = 32,
		MAX_HEADER_BYTES = 16 * 1024,
		MAX_BODY_BYTES = 4 * 1024 * 1024,
		CLIENT_TIMEOUT_MS = 15000,
	};

	struct ClientConn {
		enum State {
			STATE_READING_HEADERS,
			STATE_READING_BODY,
			STATE_WRITING_RESPONSE,
			STATE_DONE,
		};

		Ref<StreamPeerTCP> peer;
		State state = STATE_READING_HEADERS;
		uint64_t last_activity_ms = 0;

		Vector<uint8_t> recv_buf;
		int header_end_pos = -1;
		String method;
		String path;
		String host_header;
		String origin_header;
		int content_length = 0;
		bool is_chunked = false;

		Vector<uint8_t> send_buf;
		int send_offset = 0;
	};

	Ref<TCPServer> tcp_server;
	Ref<MCPToolRegistry> registry;
	Ref<MCPPermissionGate> permission_gate;
	List<ClientConn> clients;

	String bind_host = "127.0.0.1";
	int active_port = 0;

	bool _parse_headers(ClientConn &r_conn, int &r_status_out, String &r_err_text);
	bool _try_decode_chunked(const uint8_t *p_data, int p_len, String &r_decoded, bool &r_incomplete) const;
	void _queue_http_response(ClientConn &r_conn, int p_status_code, const String &p_status_text, const String &p_content_type, const String &p_body, const String &p_extra_headers = String()) const;
	void _process_http_request(ClientConn &r_conn, const String &p_body_utf8);
	Variant _handle_single_rpc(const Dictionary &p_req);

public:
	static bool is_valid_loopback_headers(const String &p_host_header, const String &p_origin_header);

	void set_registry(const Ref<MCPToolRegistry> &p_registry);
	Ref<MCPToolRegistry> get_registry() const;

	void set_permission_gate(const Ref<MCPPermissionGate> &p_gate);
	Ref<MCPPermissionGate> get_permission_gate() const;

	Error start(int p_port, const String &p_host = "127.0.0.1", bool p_auto_fallback = true, int p_max_attempts = 11);
	void stop();
	void poll();

	bool is_running() const;
	int get_active_port() const;
	String get_bind_host() const;
	String get_endpoint_url() const;

	String handle_jsonrpc_body(const String &p_body, int &r_http_status);

	EditorMCPServer();
	~EditorMCPServer();
};
