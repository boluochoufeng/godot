/**************************************************************************/
/*  mcp_server.cpp                                                        */
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

#include "mcp_server.h"

#include "core/io/ip_address.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "modules/jsonrpc/jsonrpc.h"

#include <cstring>

static String _strip_port_from_host(const String &p_host_port) {
	String trimmed = p_host_port.strip_edges().to_lower();
	if (trimmed.is_empty()) {
		return String();
	}
	if (trimmed.begins_with("[")) {
		int close_idx = trimmed.find_char(']');
		if (close_idx < 0) {
			return String();
		}
		return trimmed.substr(0, close_idx + 1);
	}
	int colon_idx = trimmed.find_char(':');
	if (colon_idx >= 0) {
		return trimmed.substr(0, colon_idx);
	}
	return trimmed;
}

static bool _is_loopback_hostname(const String &p_hostname) {
	return p_hostname == "127.0.0.1" || p_hostname == "localhost" || p_hostname == "[::1]";
}

static String _extract_origin_host(const String &p_origin) {
	String trimmed = p_origin.strip_edges().to_lower();
	if (trimmed.begins_with("http://")) {
		trimmed = trimmed.substr(7);
	} else if (trimmed.begins_with("https://")) {
		trimmed = trimmed.substr(8);
	} else {
		return String();
	}
	int slash_idx = trimmed.find_char('/');
	if (slash_idx >= 0) {
		trimmed = trimmed.substr(0, slash_idx);
	}
	return _strip_port_from_host(trimmed);
}

bool EditorMCPServer::is_valid_loopback_headers(const String &p_host_header, const String &p_origin_header) {
	String host_only = _strip_port_from_host(p_host_header);
	if (!_is_loopback_hostname(host_only)) {
		return false;
	}

	String origin_trimmed = p_origin_header.strip_edges();
	if (!origin_trimmed.is_empty()) {
		String origin_host = _extract_origin_host(origin_trimmed);
		if (!_is_loopback_hostname(origin_host)) {
			return false;
		}
	}
	return true;
}

EditorMCPServer::EditorMCPServer() {
	tcp_server.instantiate();
	registry.instantiate();
	permission_gate.instantiate();
}

EditorMCPServer::~EditorMCPServer() {
	stop();
}

void EditorMCPServer::set_registry(const Ref<MCPToolRegistry> &p_registry) {
	if (p_registry.is_valid()) {
		registry = p_registry;
	}
}

Ref<MCPToolRegistry> EditorMCPServer::get_registry() const {
	return registry;
}

void EditorMCPServer::set_permission_gate(const Ref<MCPPermissionGate> &p_gate) {
	if (p_gate.is_valid()) {
		permission_gate = p_gate;
	}
}

Ref<MCPPermissionGate> EditorMCPServer::get_permission_gate() const {
	return permission_gate;
}

Error EditorMCPServer::start(int p_port, const String &p_host, bool p_auto_fallback, int p_max_attempts) {
	stop();

	String target_host = p_host.strip_edges();
	if (target_host.is_empty()) {
		target_host = "127.0.0.1";
	}

	IPAddress addr(target_host);
	ERR_FAIL_COND_V_MSG(!addr.is_valid(), ERR_INVALID_PARAMETER, "EditorMCPServer: Invalid bind host: " + target_host);

	int attempts = p_auto_fallback ? MAX(1, p_max_attempts) : 1;
	Error last_err = ERR_CANT_CREATE;
	for (int i = 0; i < attempts; i++) {
		int candidate_port = p_port + i;
		if (candidate_port <= 0 || candidate_port > 65535) {
			break;
		}
		last_err = tcp_server->listen(static_cast<uint16_t>(candidate_port), addr);
		if (last_err == OK) {
			bind_host = target_host;
			active_port = candidate_port;
			return OK;
		}
	}

	return last_err;
}

void EditorMCPServer::stop() {
	for (ClientConn &conn : clients) {
		if (conn.peer.is_valid()) {
			conn.peer->disconnect_from_host();
		}
	}
	clients.clear();
	if (tcp_server.is_valid() && tcp_server->is_listening()) {
		tcp_server->stop();
	}
	active_port = 0;
}

bool EditorMCPServer::is_running() const {
	return tcp_server.is_valid() && tcp_server->is_listening();
}

int EditorMCPServer::get_active_port() const {
	return active_port;
}

String EditorMCPServer::get_bind_host() const {
	return bind_host;
}

String EditorMCPServer::get_endpoint_url() const {
	return vformat("http://%s:%d/mcp", bind_host, active_port);
}

static int _find_header_end(const Vector<uint8_t> &p_buf, int &r_sep_len) {
	const int sz = p_buf.size();
	const uint8_t *ptr = p_buf.ptr();
	for (int i = 0; i <= sz - 4; i++) {
		if (ptr[i] == '\r' && ptr[i + 1] == '\n' && ptr[i + 2] == '\r' && ptr[i + 3] == '\n') {
			r_sep_len = 4;
			return i;
		}
	}
	for (int i = 0; i <= sz - 2; i++) {
		if (ptr[i] == '\n' && ptr[i + 1] == '\n') {
			r_sep_len = 2;
			return i;
		}
	}
	r_sep_len = 0;
	return -1;
}

bool EditorMCPServer::_parse_headers(ClientConn &r_conn, int &r_status_out, String &r_err_text) {
	int sep_len = 0;
	int sep_idx = _find_header_end(r_conn.recv_buf, sep_len);
	if (sep_idx < 0) {
		if (r_conn.recv_buf.size() > MAX_HEADER_BYTES) {
			r_status_out = 431;
			r_err_text = "Request Header Fields Too Large";
			return true;
		}
		return false;
	}

	r_conn.header_end_pos = sep_idx + sep_len;

	String header_str = String::utf8(reinterpret_cast<const char *>(r_conn.recv_buf.ptr()), sep_idx);
	header_str = header_str.replace("\r\n", "\n");
	Vector<String> lines = header_str.split("\n", false);
	if (lines.is_empty()) {
		r_status_out = 400;
		r_err_text = "Bad Request";
		return true;
	}

	Vector<String> req_parts = lines[0].strip_edges().split(" ", false);
	if (req_parts.size() < 2) {
		r_status_out = 400;
		r_err_text = "Bad Request";
		return true;
	}

	r_conn.method = req_parts[0].to_upper();
	r_conn.path = req_parts[1].get_slicec('?', 0);
	r_conn.host_header = String();
	r_conn.origin_header = String();
	r_conn.content_length = 0;
	r_conn.is_chunked = false;

	for (int i = 1; i < lines.size(); i++) {
		int colon = lines[i].find_char(':');
		if (colon <= 0) {
			continue;
		}
		String key = lines[i].substr(0, colon).strip_edges().to_lower();
		String val = lines[i].substr(colon + 1).strip_edges();

		if (key == "host") {
			r_conn.host_header = val;
		} else if (key == "origin") {
			r_conn.origin_header = val;
		} else if (key == "content-length") {
			r_conn.content_length = val.to_int();
			if (r_conn.content_length < 0 || r_conn.content_length > MAX_BODY_BYTES) {
				r_status_out = 413;
				r_err_text = "Payload Too Large";
				return true;
			}
		} else if (key == "transfer-encoding") {
			if (val.to_lower().contains("chunked")) {
				r_conn.is_chunked = true;
			}
		}
	}

	r_status_out = 0;
	return true;
}

bool EditorMCPServer::_try_decode_chunked(const uint8_t *p_data, int p_len, String &r_decoded, bool &r_incomplete) const {
	r_incomplete = false;
	Vector<uint8_t> out;
	int pos = 0;
	while (pos < p_len) {
		int line_end = -1;
		for (int i = pos; i + 1 < p_len; i++) {
			if (p_data[i] == '\r' && p_data[i + 1] == '\n') {
				line_end = i;
				break;
			}
		}
		if (line_end < 0) {
			r_incomplete = true;
			return false;
		}
		String size_line = String::utf8(reinterpret_cast<const char *>(p_data + pos), line_end - pos).get_slicec(';', 0).strip_edges();
		int64_t chunk_size = size_line.hex_to_int();
		if (chunk_size < 0 || out.size() + chunk_size > MAX_BODY_BYTES) {
			return false;
		}
		pos = line_end + 2;
		if (chunk_size == 0) {
			if (pos + 2 <= p_len) {
				r_decoded = String::utf8(reinterpret_cast<const char *>(out.ptr()), out.size());
				return true;
			}
			r_incomplete = true;
			return false;
		}
		if (pos + chunk_size + 2 > p_len) {
			r_incomplete = true;
			return false;
		}
		int old_sz = out.size();
		out.resize(old_sz + static_cast<int>(chunk_size));
		memcpy(out.ptrw() + old_sz, p_data + pos, static_cast<size_t>(chunk_size));
		pos += static_cast<int>(chunk_size) + 2;
	}
	r_incomplete = true;
	return false;
}

void EditorMCPServer::_queue_http_response(ClientConn &r_conn, int p_status_code, const String &p_status_text, const String &p_content_type, const String &p_body, const String &p_extra_headers) const {
	CharString body_utf8 = p_body.utf8();

	String hdr = vformat("HTTP/1.1 %d %s\r\n", p_status_code, p_status_text);
	hdr += "Connection: close\r\n";
	hdr += "Cache-Control: no-store\r\n";
	if (!p_content_type.is_empty()) {
		hdr += "Content-Type: " + p_content_type + "\r\n";
	}
	hdr += vformat("Content-Length: %d\r\n", body_utf8.length());
	if (!p_extra_headers.is_empty()) {
		hdr += p_extra_headers;
	}
	hdr += "\r\n";

	CharString hdr_utf8 = hdr.utf8();
	int total_len = hdr_utf8.length() + body_utf8.length();
	r_conn.send_buf.resize(total_len);
	memcpy(r_conn.send_buf.ptrw(), hdr_utf8.get_data(), static_cast<size_t>(hdr_utf8.length()));
	if (body_utf8.length() > 0) {
		memcpy(r_conn.send_buf.ptrw() + hdr_utf8.length(), body_utf8.get_data(), static_cast<size_t>(body_utf8.length()));
	}
	r_conn.send_offset = 0;
	r_conn.state = ClientConn::STATE_WRITING_RESPONSE;
}

static String _status_phrase(int p_code) {
	switch (p_code) {
		case 200:
			return "OK";
		case 202:
			return "Accepted";
		case 204:
			return "No Content";
		case 400:
			return "Bad Request";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 405:
			return "Method Not Allowed";
		case 413:
			return "Payload Too Large";
		case 431:
			return "Request Header Fields Too Large";
		default:
			return "Internal Server Error";
	}
}

void EditorMCPServer::_process_http_request(ClientConn &r_conn, const String &p_body_utf8) {
	if (!is_valid_loopback_headers(r_conn.host_header, r_conn.origin_header)) {
		_queue_http_response(r_conn, 403, "Forbidden", "application/json", "{\"error\":\"Forbidden host or origin\"}");
		return;
	}

	String cors_headers;
	if (!r_conn.origin_header.strip_edges().is_empty()) {
		cors_headers += "Access-Control-Allow-Origin: " + r_conn.origin_header.strip_edges() + "\r\n";
		cors_headers += "Vary: Origin\r\n";
	}

	if (r_conn.method == "OPTIONS") {
		cors_headers += "Access-Control-Allow-Methods: POST, GET, DELETE, OPTIONS\r\n";
		cors_headers += "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Session-Id, Mcp-Protocol-Version\r\n";
		_queue_http_response(r_conn, 204, "No Content", String(), String(), cors_headers);
		return;
	}

	if (r_conn.method == "GET" && r_conn.path == "/health") {
		Dictionary health;
		health["status"] = "ok";
		health["engine"] = "Godot";
		health["version"] = "4.8";
		health["port"] = active_port;
		health["tools"] = registry.is_valid() ? registry->get_tool_count() : 0;
		_queue_http_response(r_conn, 200, "OK", "application/json", JSON::stringify(health), cors_headers);
		return;
	}

	if (r_conn.path == "/mcp" || r_conn.path == "/mcp/" || r_conn.path == "/") {
		if (r_conn.method == "GET") {
			cors_headers += "Allow: POST, DELETE, OPTIONS\r\n";
			_queue_http_response(r_conn, 405, "Method Not Allowed", "application/json", "{\"error\":\"SSE stream not offered on GET /mcp; use POST /mcp.\"}", cors_headers);
			return;
		}
		if (r_conn.method == "DELETE") {
			_queue_http_response(r_conn, 200, "OK", "application/json", "{}", cors_headers);
			return;
		}
		if (r_conn.method == "POST") {
			int http_status = 200;
			String resp_json = handle_jsonrpc_body(p_body_utf8, http_status);
			String ctype = (http_status == 202 || resp_json.is_empty()) ? String() : String("application/json");
			_queue_http_response(r_conn, http_status, _status_phrase(http_status), ctype, resp_json, cors_headers);
			return;
		}
		cors_headers += "Allow: POST, GET, DELETE, OPTIONS\r\n";
		_queue_http_response(r_conn, 405, "Method Not Allowed", "application/json", "{\"error\":\"Method Not Allowed\"}", cors_headers);
		return;
	}

	_queue_http_response(r_conn, 404, "Not Found", "application/json", "{\"error\":\"Not Found\"}", cors_headers);
}

Variant EditorMCPServer::_handle_single_rpc(const Dictionary &p_req) {
	JSONRPC rpc;

	const bool is_notification = !p_req.has("id");
	Variant id = is_notification ? Variant() : p_req["id"];
	if (id.get_type() == Variant::FLOAT && id.operator float() == static_cast<float>(id.operator int64_t())) {
		id = id.operator int64_t();
	}

	if (!p_req.has("jsonrpc") || String(p_req["jsonrpc"]) != "2.0" || !p_req.has("method") || p_req["method"].get_type() != Variant::STRING) {
		return is_notification ? Variant() : Variant(rpc.make_response_error(JSONRPC::INVALID_REQUEST, "Invalid JSON-RPC 2.0 request.", id));
	}

	String method = p_req["method"];
	Dictionary params;
	if (p_req.has("params")) {
		Variant raw_params = p_req["params"];
		if (raw_params.get_type() == Variant::DICTIONARY) {
			params = raw_params;
		} else if (raw_params.get_type() != Variant::NIL) {
			return is_notification ? Variant() : Variant(rpc.make_response_error(JSONRPC::INVALID_PARAMS, "JSON-RPC 'params' must be an object.", id));
		}
	}

	if (is_notification || method.begins_with("notifications/")) {
		// Notifications do not produce a response payload per JSON-RPC 2.0 & MCP spec.
		return Variant();
	}

	if (method == "initialize") {
		String requested_version = params.get("protocolVersion", "2025-03-26");
		String negotiated_version = "2025-03-26";
		if (requested_version == "2024-11-05" || requested_version == "2025-03-26" || requested_version == "2025-06-18" || requested_version == "2025-11-25") {
			negotiated_version = requested_version;
		}

		Dictionary tools_cap;
		tools_cap["listChanged"] = false;
		Dictionary caps;
		caps["tools"] = tools_cap;

		Dictionary server_info;
		server_info["name"] = "godot-editor-mcp";
		server_info["version"] = "4.8.0";

		Dictionary result;
		result["protocolVersion"] = negotiated_version;
		result["capabilities"] = caps;
		result["serverInfo"] = server_info;
		return rpc.make_response(result, id);
	}

	if (method == "ping") {
		return rpc.make_response(Dictionary(), id);
	}

	if (method == "tools/list") {
		Dictionary result;
		result["tools"] = registry.is_valid() ? registry->get_tools_list() : Array();
		return rpc.make_response(result, id);
	}

	if (method == "tools/call") {
		if (!params.has("name") || params["name"].get_type() != Variant::STRING) {
			return rpc.make_response_error(JSONRPC::INVALID_PARAMS, "Missing required string parameter 'name'.", id);
		}
		String tool_name = params["name"];
		if (registry.is_null() || !registry->has_tool(tool_name)) {
			return rpc.make_response_error(JSONRPC::INVALID_PARAMS, "Unknown tool: " + tool_name, id);
		}
		Dictionary arguments;
		if (params.has("arguments") && params["arguments"].get_type() != Variant::NIL) {
			if (params["arguments"].get_type() != Variant::DICTIONARY) {
				return rpc.make_response_error(JSONRPC::INVALID_PARAMS, "Tool 'arguments' must be an object.", id);
			}
			arguments = params["arguments"];
		}
		Dictionary call_result = registry->call_tool(tool_name, arguments, permission_gate);
		return rpc.make_response(call_result, id);
	}

	if (method == "resources/list") {
		Dictionary result;
		result["resources"] = Array();
		return rpc.make_response(result, id);
	}

	if (method == "prompts/list") {
		Dictionary result;
		result["prompts"] = Array();
		return rpc.make_response(result, id);
	}

	return rpc.make_response_error(JSONRPC::METHOD_NOT_FOUND, "Method not found: " + method, id);
}

String EditorMCPServer::handle_jsonrpc_body(const String &p_body, int &r_http_status) {
	JSONRPC rpc;
	JSON json;
	if (json.parse(p_body) != OK) {
		r_http_status = 400;
		return JSON::stringify(rpc.make_response_error(JSONRPC::PARSE_ERROR, "Parse error: " + json.get_error_message(), Variant()));
	}

	Variant data = json.get_data();
	if (data.get_type() == Variant::DICTIONARY) {
		Variant res = _handle_single_rpc(data);
		if (res.get_type() == Variant::NIL) {
			r_http_status = 202;
			return String();
		}
		r_http_status = 200;
		return JSON::stringify(res);
	}

	if (data.get_type() == Variant::ARRAY) {
		Array arr = data;
		if (arr.is_empty()) {
			r_http_status = 400;
			return JSON::stringify(rpc.make_response_error(JSONRPC::INVALID_REQUEST, "Empty JSON-RPC batch.", Variant()));
		}
		Array out;
		for (int i = 0; i < arr.size(); i++) {
			if (arr[i].get_type() != Variant::DICTIONARY) {
				out.push_back(rpc.make_response_error(JSONRPC::INVALID_REQUEST, "Invalid Request in batch.", Variant()));
				continue;
			}
			Variant res = _handle_single_rpc(arr[i]);
			if (res.get_type() != Variant::NIL) {
				out.push_back(res);
			}
		}
		if (out.is_empty()) {
			r_http_status = 202;
			return String();
		}
		r_http_status = 200;
		return JSON::stringify(out);
	}

	r_http_status = 400;
	return JSON::stringify(rpc.make_response_error(JSONRPC::INVALID_REQUEST, "Invalid Request payload.", Variant()));
}

void EditorMCPServer::poll() {
	if (!is_running()) {
		return;
	}

	uint64_t now_ms = OS::get_singleton()->get_ticks_msec();

	while (tcp_server->is_connection_available()) {
		Ref<StreamPeerTCP> peer = tcp_server->take_connection();
		if (peer.is_null()) {
			break;
		}
		if (clients.size() >= MAX_ACTIVE_CLIENTS) {
			peer->disconnect_from_host();
			continue;
		}
		peer->set_no_delay(true);
		ClientConn conn;
		conn.peer = peer;
		conn.last_activity_ms = now_ms;
		clients.push_back(conn);
	}

	List<ClientConn>::Element *E = clients.front();
	while (E) {
		List<ClientConn>::Element *next = E->next();
		ClientConn &conn = E->get();

		if (conn.peer.is_null()) {
			clients.erase(E);
			E = next;
			continue;
		}

		conn.peer->poll();
		StreamPeerSocket::Status st = conn.peer->get_status();
		if (st == StreamPeerSocket::STATUS_NONE || st == StreamPeerSocket::STATUS_ERROR) {
			clients.erase(E);
			E = next;
			continue;
		}

		if (now_ms - conn.last_activity_ms > CLIENT_TIMEOUT_MS) {
			conn.peer->disconnect_from_host();
			clients.erase(E);
			E = next;
			continue;
		}

		if (conn.state == ClientConn::STATE_READING_HEADERS || conn.state == ClientConn::STATE_READING_BODY) {
			int avail = conn.peer->get_available_bytes();
			if (avail > 0) {
				int to_read = MIN(avail, 64 * 1024);
				int old_sz = conn.recv_buf.size();
				if (old_sz + to_read > MAX_HEADER_BYTES + MAX_BODY_BYTES) {
					_queue_http_response(conn, 413, "Payload Too Large", "application/json", "{\"error\":\"Request too large\"}");
				} else {
					conn.recv_buf.resize(old_sz + to_read);
					int received = 0;
					Error rerr = conn.peer->get_partial_data(conn.recv_buf.ptrw() + old_sz, to_read, received);
					if (rerr == OK && received > 0) {
						conn.recv_buf.resize(old_sz + received);
						conn.last_activity_ms = now_ms;
					} else {
						conn.recv_buf.resize(old_sz);
					}
				}
			}

			if (conn.state == ClientConn::STATE_READING_HEADERS) {
				int err_status = 0;
				String err_text;
				if (_parse_headers(conn, err_status, err_text)) {
					if (err_status > 0) {
						_queue_http_response(conn, err_status, err_text, "application/json", vformat("{\"error\":\"%s\"}", err_text));
					} else {
						conn.state = ClientConn::STATE_READING_BODY;
					}
				}
			}

			if (conn.state == ClientConn::STATE_READING_BODY) {
				int body_received = conn.recv_buf.size() - conn.header_end_pos;
				if (conn.is_chunked) {
					String decoded;
					bool incomplete = false;
					if (_try_decode_chunked(conn.recv_buf.ptr() + conn.header_end_pos, body_received, decoded, incomplete)) {
						_process_http_request(conn, decoded);
					} else if (!incomplete) {
						_queue_http_response(conn, 400, "Bad Request", "application/json", "{\"error\":\"Malformed chunked encoding\"}");
					}
				} else if (body_received >= conn.content_length) {
					String body_str;
					if (conn.content_length > 0) {
						body_str = String::utf8(reinterpret_cast<const char *>(conn.recv_buf.ptr() + conn.header_end_pos), conn.content_length);
					}
					_process_http_request(conn, body_str);
				}
			}
		}

		if (conn.state == ClientConn::STATE_WRITING_RESPONSE) {
			int remaining = conn.send_buf.size() - conn.send_offset;
			if (remaining > 0) {
				int sent = 0;
				Error werr = conn.peer->put_partial_data(conn.send_buf.ptr() + conn.send_offset, remaining, sent);
				if (werr == OK && sent > 0) {
					conn.send_offset += sent;
					conn.last_activity_ms = now_ms;
				} else if (werr != OK && werr != ERR_BUSY) {
					conn.state = ClientConn::STATE_DONE;
				}
			}
			if (conn.send_offset >= conn.send_buf.size()) {
				conn.state = ClientConn::STATE_DONE;
			}
		}

		if (conn.state == ClientConn::STATE_DONE) {
			conn.peer->disconnect_from_host();
			clients.erase(E);
		}

		E = next;
	}
}
