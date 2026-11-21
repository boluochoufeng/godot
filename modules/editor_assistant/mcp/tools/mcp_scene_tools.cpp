/**************************************************************************/
/*  mcp_scene_tools.cpp                                                   */
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

#include "mcp_scene_tools.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/math/color.h"
#include "core/math/rect2.h"
#include "core/math/transform_2d.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/math/vector4.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/variant/callable.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

static bool _read_number_components(const Variant &p_json_val, int p_expected_count, const char **p_keys, Vector<double> &r_vals) {
	r_vals.clear();
	if (p_json_val.get_type() == Variant::ARRAY) {
		Array arr = p_json_val;
		if (arr.size() < p_expected_count) {
			return false;
		}
		for (int i = 0; i < p_expected_count; i++) {
			if (!arr[i].is_num()) {
				return false;
			}
			r_vals.push_back(static_cast<double>(arr[i]));
		}
		return true;
	}
	if (p_json_val.get_type() == Variant::DICTIONARY) {
		Dictionary dict = p_json_val;
		for (int i = 0; i < p_expected_count; i++) {
			Variant v = dict.get(p_keys[i], Variant());
			if (!v.is_num()) {
				return false;
			}
			r_vals.push_back(static_cast<double>(v));
		}
		return true;
	}
	return false;
}

Variant MCPSceneTools::json_to_variant(const Variant &p_json_val, Variant::Type p_target_type, const String &p_hint_string, String &r_error) {
	if (p_target_type == Variant::NIL || p_target_type == p_json_val.get_type()) {
		return p_json_val;
	}

	switch (p_target_type) {
		case Variant::BOOL: {
			if (p_json_val.get_type() == Variant::BOOL || p_json_val.is_num()) {
				return static_cast<bool>(p_json_val);
			}
		} break;
		case Variant::INT: {
			if (p_json_val.is_num() || p_json_val.get_type() == Variant::BOOL) {
				return static_cast<int64_t>(p_json_val);
			}
		} break;
		case Variant::FLOAT: {
			if (p_json_val.is_num()) {
				return static_cast<double>(p_json_val);
			}
		} break;
		case Variant::STRING: {
			return String(p_json_val);
		}
		case Variant::STRING_NAME: {
			return StringName(String(p_json_val));
		}
		case Variant::NODE_PATH: {
			return NodePath(String(p_json_val));
		}
		case Variant::VECTOR2: {
			static const char *keys[] = { "x", "y" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 2, keys, v)) {
				return Vector2(static_cast<real_t>(v[0]), static_cast<real_t>(v[1]));
			}
		} break;
		case Variant::VECTOR2I: {
			static const char *keys[] = { "x", "y" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 2, keys, v)) {
				return Vector2i(static_cast<int32_t>(v[0]), static_cast<int32_t>(v[1]));
			}
		} break;
		case Variant::VECTOR3: {
			static const char *keys[] = { "x", "y", "z" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 3, keys, v)) {
				return Vector3(static_cast<real_t>(v[0]), static_cast<real_t>(v[1]), static_cast<real_t>(v[2]));
			}
		} break;
		case Variant::VECTOR3I: {
			static const char *keys[] = { "x", "y", "z" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 3, keys, v)) {
				return Vector3i(static_cast<int32_t>(v[0]), static_cast<int32_t>(v[1]), static_cast<int32_t>(v[2]));
			}
		} break;
		case Variant::VECTOR4: {
			static const char *keys[] = { "x", "y", "z", "w" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 4, keys, v)) {
				return Vector4(static_cast<real_t>(v[0]), static_cast<real_t>(v[1]), static_cast<real_t>(v[2]), static_cast<real_t>(v[3]));
			}
		} break;
		case Variant::RECT2: {
			static const char *keys[] = { "x", "y", "width", "height" };
			Vector<double> v;
			if (_read_number_components(p_json_val, 4, keys, v)) {
				return Rect2(static_cast<real_t>(v[0]), static_cast<real_t>(v[1]), static_cast<real_t>(v[2]), static_cast<real_t>(v[3]));
			}
		} break;
		case Variant::COLOR: {
			if (p_json_val.get_type() == Variant::STRING) {
				String s = p_json_val;
				if (Color::html_is_valid(s)) {
					return Color::html(s);
				}
			}
			if (p_json_val.get_type() == Variant::ARRAY) {
				Array arr = p_json_val;
				if (arr.size() >= 3 && arr[0].is_num() && arr[1].is_num() && arr[2].is_num()) {
					float a = (arr.size() >= 4 && arr[3].is_num()) ? static_cast<float>(arr[3]) : 1.0f;
					return Color(static_cast<float>(arr[0]), static_cast<float>(arr[1]), static_cast<float>(arr[2]), a);
				}
			}
			if (p_json_val.get_type() == Variant::DICTIONARY) {
				Dictionary d = p_json_val;
				if (d.has("r") && d.has("g") && d.has("b")) {
					return Color(static_cast<float>(d["r"]), static_cast<float>(d["g"]), static_cast<float>(d["b"]), static_cast<float>(d.get("a", 1.0)));
				}
			}
		} break;
		case Variant::TRANSFORM2D: {
			if (p_json_val.get_type() == Variant::ARRAY) {
				Array arr = p_json_val;
				if (arr.size() == 6) {
					return Transform2D(
							static_cast<real_t>(arr[0]), static_cast<real_t>(arr[1]),
							static_cast<real_t>(arr[2]), static_cast<real_t>(arr[3]),
							static_cast<real_t>(arr[4]), static_cast<real_t>(arr[5]));
				}
			}
		} break;
		case Variant::TRANSFORM3D: {
			if (p_json_val.get_type() == Variant::ARRAY) {
				Array arr = p_json_val;
				if (arr.size() == 12) {
					Basis basis(
							Vector3(static_cast<real_t>(arr[0]), static_cast<real_t>(arr[1]), static_cast<real_t>(arr[2])),
							Vector3(static_cast<real_t>(arr[3]), static_cast<real_t>(arr[4]), static_cast<real_t>(arr[5])),
							Vector3(static_cast<real_t>(arr[6]), static_cast<real_t>(arr[7]), static_cast<real_t>(arr[8])));
					Vector3 origin(static_cast<real_t>(arr[9]), static_cast<real_t>(arr[10]), static_cast<real_t>(arr[11]));
					return Transform3D(basis, origin);
				}
			}
		} break;
		case Variant::OBJECT: {
			if (p_json_val.get_type() == Variant::NIL) {
				return Variant();
			}
			if (p_json_val.get_type() == Variant::STRING) {
				String res_path = String(p_json_val).strip_edges();
				if (res_path.is_empty()) {
					return Variant();
				}
				if (!res_path.begins_with("res://") && !res_path.begins_with("uid://")) {
					r_error = "Resource path string must start with 'res://' or 'uid://': " + res_path;
					return Variant();
				}
				Error load_err = OK;
				Ref<Resource> res = ResourceLoader::load(res_path, p_hint_string, ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
				if (res.is_null() || load_err != OK) {
					r_error = "Failed to load resource at: " + res_path;
					return Variant();
				}
				return res;
			}
		} break;
		case Variant::ARRAY: {
			if (p_json_val.get_type() == Variant::ARRAY) {
				return p_json_val;
			}
		} break;
		case Variant::DICTIONARY: {
			if (p_json_val.get_type() == Variant::DICTIONARY) {
				return p_json_val;
			}
		} break;
		default:
			break;
	}

	r_error = vformat("Cannot convert JSON value of type '%s' to target Variant type '%s'.",
			Variant::get_type_name(p_json_val.get_type()), Variant::get_type_name(p_target_type));
	return Variant();
}

Variant MCPSceneTools::variant_to_json(const Variant &p_val) {
	switch (p_val.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			return p_val;
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
			return String(p_val);
		case Variant::VECTOR2: {
			Vector2 v = p_val;
			Array a;
			a.push_back(v.x);
			a.push_back(v.y);
			return a;
		}
		case Variant::VECTOR2I: {
			Vector2i v = p_val;
			Array a;
			a.push_back(v.x);
			a.push_back(v.y);
			return a;
		}
		case Variant::VECTOR3: {
			Vector3 v = p_val;
			Array a;
			a.push_back(v.x);
			a.push_back(v.y);
			a.push_back(v.z);
			return a;
		}
		case Variant::VECTOR3I: {
			Vector3i v = p_val;
			Array a;
			a.push_back(v.x);
			a.push_back(v.y);
			a.push_back(v.z);
			return a;
		}
		case Variant::VECTOR4: {
			Vector4 v = p_val;
			Array a;
			a.push_back(v.x);
			a.push_back(v.y);
			a.push_back(v.z);
			a.push_back(v.w);
			return a;
		}
		case Variant::RECT2: {
			Rect2 r = p_val;
			Array a;
			a.push_back(r.position.x);
			a.push_back(r.position.y);
			a.push_back(r.size.x);
			a.push_back(r.size.y);
			return a;
		}
		case Variant::COLOR: {
			Color c = p_val;
			Array a;
			a.push_back(c.r);
			a.push_back(c.g);
			a.push_back(c.b);
			a.push_back(c.a);
			return a;
		}
		case Variant::OBJECT: {
			Object *obj = p_val;
			if (!obj) {
				return Variant();
			}
			Resource *res = Object::cast_to<Resource>(obj);
			if (res) {
				Dictionary d;
				d["type"] = res->get_class();
				if (!res->get_path().is_empty()) {
					d["path"] = res->get_path();
				}
				return d;
			}
			return obj->get_class();
		}
		default:
			return String(p_val);
	}
}

static Node *_get_edited_root(String &r_error) {
	if (!EditorNode::get_singleton()) {
		r_error = "EditorNode singleton is not initialized.";
		return nullptr;
	}
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (!root) {
		r_error = "No scene is currently open in the editor. Use scene_create_root first.";
		return nullptr;
	}
	return root;
}

static Node *_resolve_scene_node(Node *p_root, const String &p_path, String &r_error) {
	if (!p_root) {
		r_error = "No edited scene root available.";
		return nullptr;
	}
	String trimmed = p_path.strip_edges();
	if (trimmed.is_empty() || trimmed == "." || trimmed == "/" || trimmed == String(p_root->get_name())) {
		return p_root;
	}
	if (trimmed.begins_with("./")) {
		trimmed = trimmed.substr(2);
	} else if (trimmed.begins_with(String(p_root->get_name()) + "/")) {
		trimmed = trimmed.substr(String(p_root->get_name()).length() + 1);
	}
	if (trimmed.is_empty()) {
		return p_root;
	}
	Node *found = p_root->get_node_or_null(NodePath(trimmed));
	if (!found) {
		r_error = vformat("Node not found at path '%s' (relative to root '%s').", p_path, p_root->get_name());
	}
	return found;
}

static String _relative_node_path(Node *p_root, Node *p_node) {
	if (!p_root || !p_node || p_root == p_node) {
		return ".";
	}
	return String(p_root->get_path_to(p_node));
}

static Dictionary _serialize_scene_node(Node *p_root, Node *p_node, int p_depth, int p_max_depth) {
	Dictionary d;
	d["name"] = String(p_node->get_name());
	d["type"] = p_node->get_class();
	d["path"] = _relative_node_path(p_root, p_node);
	if (!p_node->get_scene_file_path().is_empty()) {
		d["scene_file_path"] = p_node->get_scene_file_path();
	}

	Ref<Script> scr = p_node->get_script();
	if (scr.is_valid() && !scr->get_path().is_empty()) {
		d["script"] = scr->get_path();
	}

	List<Node::GroupInfo> groups;
	p_node->get_groups(&groups);
	if (!groups.is_empty()) {
		Array g_arr;
		for (const Node::GroupInfo &gi : groups) {
			if (gi.persistent) {
				g_arr.push_back(String(gi.name));
			}
		}
		if (!g_arr.is_empty()) {
			d["groups"] = g_arr;
		}
	}

	Array children;
	if (p_max_depth < 0 || p_depth < p_max_depth) {
		for (int i = 0; i < p_node->get_child_count(); i++) {
			Node *ch = p_node->get_child(i);
			if (ch->get_owner() == p_root || ch->get_owner() != nullptr) {
				children.push_back(_serialize_scene_node(p_root, ch, p_depth + 1, p_max_depth));
			}
		}
	} else if (p_node->get_child_count() > 0) {
		d["truncated_children_count"] = p_node->get_child_count();
	}
	d["children"] = children;
	return d;
}

Variant MCPSceneTools::tool_get_tree(const Dictionary &p_args, String &r_error) {
	if (!EditorNode::get_singleton()) {
		r_error = "EditorNode singleton is not initialized.";
		return Variant();
	}
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (!root) {
		Dictionary empty_out;
		empty_out["has_scene"] = false;
		return empty_out;
	}

	int max_depth = static_cast<int>(p_args.get("max_depth", -1));
	Dictionary out;
	out["has_scene"] = true;
	out["scene_file_path"] = root->get_scene_file_path();
	out["root"] = _serialize_scene_node(root, root, 0, max_depth);
	return out;
}

Variant MCPSceneTools::tool_create_root(const Dictionary &p_args, String &r_error) {
	if (!EditorNode::get_singleton() || !EditorUndoRedoManager::get_singleton()) {
		r_error = "Editor singletons are not initialized.";
		return Variant();
	}

	String class_name = String(p_args.get("class_name", "")).strip_edges();
	if (class_name.is_empty()) {
		r_error = "Missing required 'class_name' parameter.";
		return Variant();
	}
	if (!ClassDB::class_exists(class_name) || !ClassDB::is_parent_class(class_name, "Node") || !ClassDB::can_instantiate(class_name)) {
		r_error = vformat("Class '%s' is not an instantiable Node subclass.", class_name);
		return Variant();
	}

	String node_name = String(p_args.get("node_name", "")).strip_edges();
	if (node_name.is_empty()) {
		node_name = class_name;
	}

	if (EditorNode::get_singleton()->get_edited_scene() != nullptr) {
		EditorNode::get_singleton()->new_scene();
	}

	Object *obj = ClassDB::instantiate(class_name);
	Node *root = Object::cast_to<Node>(obj);
	if (!root) {
		if (obj) {
			memdelete(obj);
		}
		r_error = "Failed to instantiate root node of class: " + class_name;
		return Variant();
	}
	root->set_name(node_name);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action_for_history("MCP: Create Scene Root " + node_name, EditorNode::get_editor_data().get_current_edited_scene_history_id());
	ur->add_do_method(EditorNode::get_singleton(), "set_edited_scene", root);
	ur->add_do_reference(root);
	ur->add_undo_method(EditorNode::get_singleton(), "set_edited_scene", static_cast<Object *>(nullptr));
	ur->commit_action();

	Dictionary out;
	out["created"] = true;
	out["name"] = String(root->get_name());
	out["type"] = root->get_class();
	out["path"] = ".";
	return out;
}

Variant MCPSceneTools::tool_add_node(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	String parent_path = String(p_args.get("parent_path", "."));
	Node *parent = _resolve_scene_node(root, parent_path, r_error);
	if (!parent) {
		return Variant();
	}

	String class_or_scene = String(p_args.get("class_or_scene", "")).strip_edges();
	if (class_or_scene.is_empty()) {
		r_error = "Missing required 'class_or_scene' parameter.";
		return Variant();
	}

	Node *child = nullptr;
	if (class_or_scene.begins_with("res://") || class_or_scene.begins_with("uid://")) {
		Error load_err = OK;
		Ref<PackedScene> packed = ResourceLoader::load(class_or_scene, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
		if (packed.is_null() || load_err != OK) {
			r_error = "Failed to load PackedScene: " + class_or_scene;
			return Variant();
		}
		child = packed->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
		if (!child) {
			r_error = "Failed to instantiate PackedScene: " + class_or_scene;
			return Variant();
		}
	} else {
		if (!ClassDB::class_exists(class_or_scene) || !ClassDB::is_parent_class(class_or_scene, "Node") || !ClassDB::can_instantiate(class_or_scene)) {
			r_error = vformat("'%s' is neither a valid 'res://' PackedScene nor an instantiable Node class.", class_or_scene);
			return Variant();
		}
		Object *obj = ClassDB::instantiate(class_or_scene);
		child = Object::cast_to<Node>(obj);
		if (!child) {
			if (obj) {
				memdelete(obj);
			}
			r_error = "Failed to instantiate Node class: " + class_or_scene;
			return Variant();
		}
	}

	String desired_name = String(p_args.get("node_name", "")).strip_edges();
	if (desired_name.is_empty()) {
		desired_name = child->get_name().is_empty() ? String(child->get_class()) : String(child->get_name());
	}
	String unique_name = parent->prevalidate_child_name(child, desired_name);
	child->set_name(unique_name);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Add Node " + unique_name, UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(parent, "add_child", child, true);
	ur->add_do_method(child, "set_owner", root);
	ur->add_do_reference(child);
	ur->add_undo_method(parent, "remove_child", child);
	ur->commit_action();

	Dictionary out;
	out["added"] = true;
	out["name"] = String(child->get_name());
	out["type"] = child->get_class();
	out["path"] = _relative_node_path(root, child);
	return out;
}

Variant MCPSceneTools::tool_remove_node(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	String node_path = String(p_args.get("node_path", ""));
	Node *target = _resolve_scene_node(root, node_path, r_error);
	if (!target) {
		return Variant();
	}
	if (target == root) {
		r_error = "Cannot remove the scene root node directly.";
		return Variant();
	}

	Node *parent = target->get_parent();
	if (!parent) {
		r_error = "Target node has no parent.";
		return Variant();
	}

	int old_idx = target->get_index(false);
	Node *old_owner = target->get_owner();

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Remove Node " + String(target->get_name()), UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(parent, "remove_child", target);
	ur->add_undo_method(parent, "add_child", target, true);
	ur->add_undo_method(parent, "move_child", target, old_idx);
	if (old_owner) {
		ur->add_undo_method(target, "set_owner", old_owner);
	}
	ur->add_undo_reference(target);
	ur->commit_action();

	Dictionary out;
	out["removed"] = true;
	out["path"] = node_path;
	return out;
}

Variant MCPSceneTools::tool_reparent_node(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	Node *node = _resolve_scene_node(root, String(p_args.get("node_path", "")), r_error);
	if (!node) {
		return Variant();
	}
	if (node == root) {
		r_error = "Cannot reparent the scene root node.";
		return Variant();
	}

	Node *new_parent = _resolve_scene_node(root, String(p_args.get("new_parent_path", "")), r_error);
	if (!new_parent) {
		return Variant();
	}
	if (new_parent == node || node->is_ancestor_of(new_parent)) {
		r_error = "Cannot reparent a node into itself or one of its descendants.";
		return Variant();
	}

	Node *old_parent = node->get_parent();
	int old_idx = node->get_index(false);
	String old_name = node->get_name();
	Node *old_owner = node->get_owner();

	String desired_name = String(p_args.get("new_name", "")).strip_edges();
	String final_name = desired_name.is_empty() ? old_name : desired_name;
	if (new_parent != old_parent || final_name != old_name) {
		final_name = new_parent->prevalidate_child_name(node, final_name);
	}

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Reparent Node " + old_name, UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(old_parent, "remove_child", node);
	ur->add_do_method(new_parent, "add_child", node, true);
	if (final_name != old_name) {
		ur->add_do_method(node, "set_name", final_name);
	}
	ur->add_do_method(node, "set_owner", root);

	ur->add_undo_method(new_parent, "remove_child", node);
	ur->add_undo_method(old_parent, "add_child", node, true);
	ur->add_undo_method(old_parent, "move_child", node, old_idx);
	if (final_name != old_name) {
		ur->add_undo_method(node, "set_name", old_name);
	}
	if (old_owner) {
		ur->add_undo_method(node, "set_owner", old_owner);
	}
	ur->commit_action();

	Dictionary out;
	out["reparented"] = true;
	out["name"] = String(node->get_name());
	out["path"] = _relative_node_path(root, node);
	return out;
}

Variant MCPSceneTools::tool_get_properties(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	Node *node = _resolve_scene_node(root, String(p_args.get("node_path", ".")), r_error);
	if (!node) {
		return Variant();
	}

	String category_filter = String(p_args.get("category_filter", "")).strip_edges().to_lower();

	List<PropertyInfo> plist;
	node->get_property_list(&plist);

	String current_category;
	Dictionary props_out;
	for (const PropertyInfo &pi : plist) {
		if (pi.usage & PROPERTY_USAGE_CATEGORY) {
			current_category = pi.name;
			continue;
		}
		if ((pi.usage & PROPERTY_USAGE_GROUP) || (pi.usage & PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		if (!(pi.usage & PROPERTY_USAGE_EDITOR) && !(pi.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		if (!category_filter.is_empty() && !current_category.to_lower().contains(category_filter) && !pi.name.to_lower().contains(category_filter)) {
			continue;
		}

		Dictionary entry;
		entry["type"] = Variant::get_type_name(pi.type);
		entry["value"] = variant_to_json(node->get(pi.name));
		props_out[pi.name] = entry;
	}

	Dictionary out;
	out["path"] = _relative_node_path(root, node);
	out["type"] = node->get_class();
	out["properties"] = props_out;
	return out;
}

Variant MCPSceneTools::tool_set_property(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	Node *node = _resolve_scene_node(root, String(p_args.get("node_path", ".")), r_error);
	if (!node) {
		return Variant();
	}

	String prop_name = String(p_args.get("property", "")).strip_edges();
	if (prop_name.is_empty()) {
		r_error = "Missing required 'property' parameter.";
		return Variant();
	}
	if (!p_args.has("value")) {
		r_error = "Missing required 'value' parameter.";
		return Variant();
	}
	Variant raw_val = p_args["value"];

	List<PropertyInfo> plist;
	node->get_property_list(&plist);
	bool found_prop = false;
	PropertyInfo target_pi;
	for (const PropertyInfo &pi : plist) {
		if (pi.name == prop_name) {
			target_pi = pi;
			found_prop = true;
			break;
		}
	}

	if (!found_prop) {
		r_error = vformat("Property '%s' was not found on node '%s' (%s).", prop_name, node->get_name(), node->get_class());
		return Variant();
	}

	String conv_err;
	Variant new_val = json_to_variant(raw_val, target_pi.type, target_pi.hint_string, conv_err);
	if (!conv_err.is_empty()) {
		r_error = conv_err;
		return Variant();
	}

	Variant old_val = node->get(prop_name);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(vformat("MCP: Set %s.%s", node->get_name(), prop_name), UndoRedo::MERGE_DISABLE, root);
	ur->add_do_property(node, prop_name, new_val);
	ur->add_undo_property(node, prop_name, old_val);
	ur->commit_action();

	Dictionary out;
	out["updated"] = true;
	out["path"] = _relative_node_path(root, node);
	out["property"] = prop_name;
	out["value"] = variant_to_json(node->get(prop_name));
	return out;
}

Variant MCPSceneTools::tool_connect_signal(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	Node *source = _resolve_scene_node(root, String(p_args.get("source_path", "")), r_error);
	if (!source) {
		return Variant();
	}
	Node *target = _resolve_scene_node(root, String(p_args.get("target_path", "")), r_error);
	if (!target) {
		return Variant();
	}

	String signal_name = String(p_args.get("signal_name", "")).strip_edges();
	String method_name = String(p_args.get("method_name", "")).strip_edges();
	if (signal_name.is_empty() || method_name.is_empty()) {
		r_error = "Missing 'signal_name' or 'method_name'.";
		return Variant();
	}

	if (!source->has_signal(signal_name)) {
		r_error = vformat("Node '%s' (%s) does not have signal '%s'.", source->get_name(), source->get_class(), signal_name);
		return Variant();
	}

	Callable cb(target, StringName(method_name));
	if (source->is_connected(signal_name, cb)) {
		Dictionary out;
		out["connected"] = true;
		out["already_connected"] = true;
		return out;
	}

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(vformat("MCP: Connect %s.%s -> %s.%s", source->get_name(), signal_name, target->get_name(), method_name), UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(source, "connect", StringName(signal_name), cb, static_cast<uint32_t>(Object::CONNECT_PERSIST));
	ur->add_undo_method(source, "disconnect", StringName(signal_name), cb);
	ur->commit_action();

	Dictionary out;
	out["connected"] = true;
	out["source_path"] = _relative_node_path(root, source);
	out["signal_name"] = signal_name;
	out["target_path"] = _relative_node_path(root, target);
	out["method_name"] = method_name;
	return out;
}

Variant MCPSceneTools::tool_edit_resource(const Dictionary &p_args, String &r_error) {
	String res_path = String(p_args.get("resource_path", "")).strip_edges();
	String res_type = String(p_args.get("resource_type", "")).strip_edges();

	Ref<Resource> res;
	if (!res_path.is_empty()) {
		if (!res_path.begins_with("res://")) {
			r_error = "resource_path must begin with 'res://'.";
			return Variant();
		}
		Error load_err = OK;
		res = ResourceLoader::load(res_path, res_type, ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
		if (res.is_null() || load_err != OK) {
			r_error = "Failed to load resource: " + res_path;
			return Variant();
		}
	} else {
		if (res_type.is_empty()) {
			r_error = "Provide either 'resource_type' (to create a new Resource) or 'resource_path' (to edit an existing one).";
			return Variant();
		}
		if (!ClassDB::class_exists(res_type) || !ClassDB::is_parent_class(res_type, "Resource") || !ClassDB::can_instantiate(res_type)) {
			r_error = vformat("Class '%s' is not an instantiable Resource subclass.", res_type);
			return Variant();
		}
		Object *obj = ClassDB::instantiate(res_type);
		Resource *raw_res = Object::cast_to<Resource>(obj);
		if (!raw_res) {
			if (obj) {
				memdelete(obj);
			}
			r_error = "Failed to instantiate Resource: " + res_type;
			return Variant();
		}
		res = Ref<Resource>(raw_res);
	}

	Dictionary props = p_args.get("properties", Dictionary());
	if (!props.is_empty()) {
		List<PropertyInfo> plist;
		res->get_property_list(&plist);
		HashMap<String, PropertyInfo> pmap;
		for (const PropertyInfo &pi : plist) {
			pmap[pi.name] = pi;
		}
		for (const KeyValue<Variant, Variant> &E : props) {
			String pname = String(E.key);
			const PropertyInfo *pi = pmap.getptr(pname);
			if (!pi) {
				r_error = vformat("Property '%s' not found on Resource '%s'.", pname, res->get_class());
				return Variant();
			}
			String conv_err;
			Variant val = json_to_variant(E.value, pi->type, pi->hint_string, conv_err);
			if (!conv_err.is_empty()) {
				r_error = conv_err;
				return Variant();
			}
			res->set(pname, val);
		}
	}

	String save_path = String(p_args.get("save_path", "")).strip_edges();
	if (!save_path.is_empty()) {
		if (!save_path.begins_with("res://")) {
			r_error = "save_path must begin with 'res://'.";
			return Variant();
		}
		String abs_dir = ProjectSettings::get_singleton()->globalize_path(save_path.get_base_dir());
		DirAccess::make_dir_recursive_absolute(abs_dir);
		Error serr = ResourceSaver::save(res, save_path);
		if (serr != OK) {
			r_error = vformat("Failed to save resource to '%s' (error %d).", save_path, static_cast<int>(serr));
			return Variant();
		}
		res->set_path(save_path);
	}

	String assign_node_path = String(p_args.get("assign_node_path", "")).strip_edges();
	String assign_property = String(p_args.get("assign_property", "")).strip_edges();
	if (!assign_node_path.is_empty() || !assign_property.is_empty()) {
		if (assign_node_path.is_empty() || assign_property.is_empty()) {
			r_error = "Both 'assign_node_path' and 'assign_property' are required when assigning a resource to a node.";
			return Variant();
		}
		Node *root = _get_edited_root(r_error);
		if (!root) {
			return Variant();
		}
		Node *target = _resolve_scene_node(root, assign_node_path, r_error);
		if (!target) {
			return Variant();
		}
		Variant old_val = target->get(assign_property);
		EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
		ur->create_action(vformat("MCP: Assign Resource to %s.%s", target->get_name(), assign_property), UndoRedo::MERGE_DISABLE, root);
		ur->add_do_property(target, assign_property, res);
		ur->add_undo_property(target, assign_property, old_val);
		ur->commit_action();
	}

	Dictionary out;
	out["resource_type"] = res->get_class();
	if (!res->get_path().is_empty()) {
		out["path"] = res->get_path();
	}
	if (!assign_node_path.is_empty()) {
		out["assigned_to"] = assign_node_path + "." + assign_property;
	}
	return out;
}

Variant MCPSceneTools::tool_save(const Dictionary &p_args, String &r_error) {
	Node *root = _get_edited_root(r_error);
	if (!root) {
		return Variant();
	}

	String save_path = String(p_args.get("path", "")).strip_edges();
	if (save_path.is_empty()) {
		save_path = root->get_scene_file_path();
	}
	if (save_path.is_empty()) {
		r_error = "Scene has no existing file path. Provide 'path' (e.g. 'res://main.tscn').";
		return Variant();
	}
	if (!save_path.begins_with("res://")) {
		r_error = "Scene save path must begin with 'res://'.";
		return Variant();
	}

	String abs_dir = ProjectSettings::get_singleton()->globalize_path(save_path.get_base_dir());
	DirAccess::make_dir_recursive_absolute(abs_dir);

	EditorNode::get_singleton()->save_scene_to_path(save_path, false);

	if (!FileAccess::exists(save_path)) {
		r_error = "Scene save failed for path: " + save_path;
		return Variant();
	}

	Dictionary out;
	out["saved"] = true;
	out["path"] = save_path;
	return out;
}

void MCPSceneTools::register_tools(const Ref<MCPToolRegistry> &p_registry) {
	ERR_FAIL_COND(p_registry.is_null());

	{
		Dictionary props;
		props["max_depth"] = MCPToolRegistry::make_prop("integer", "Maximum tree depth (-1 for unlimited).", -1);
		p_registry->register_tool(
				"scene_get_tree",
				"Inspect the node hierarchy of the currently open scene in the Godot Editor.",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_READ,
				&MCPSceneTools::tool_get_tree);
	}

	{
		Dictionary props;
		props["class_name"] = MCPToolRegistry::make_prop("string", "Root Node class name (e.g. 'Node3D', 'Node2D', 'Control').");
		props["node_name"] = MCPToolRegistry::make_prop("string", "Optional name for the root node.", "");
		Vector<String> req;
		req.push_back("class_name");
		p_registry->register_tool(
				"scene_create_root",
				"Create a new scene with the specified root Node class (undoable via Ctrl+Z).",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_create_root);
	}

	{
		Dictionary props;
		props["parent_path"] = MCPToolRegistry::make_prop("string", "Parent node path relative to scene root ('.' for root).", ".");
		props["class_or_scene"] = MCPToolRegistry::make_prop("string", "Node class name (e.g. 'CollisionShape3D') or scene path ('res://...').");
		props["node_name"] = MCPToolRegistry::make_prop("string", "Optional node name.", "");
		Vector<String> req;
		req.push_back("class_or_scene");
		p_registry->register_tool(
				"scene_add_node",
				"Add a Node or instantiate a sub-scene under a parent node using EditorUndoRedoManager.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_add_node);
	}

	{
		Dictionary props;
		props["node_path"] = MCPToolRegistry::make_prop("string", "Path relative to root of the node to remove.");
		Vector<String> req;
		req.push_back("node_path");
		p_registry->register_tool(
				"scene_remove_node",
				"Remove a non-root node from the edited scene using EditorUndoRedoManager.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_remove_node);
	}

	{
		Dictionary props;
		props["node_path"] = MCPToolRegistry::make_prop("string", "Path of the node to move.");
		props["new_parent_path"] = MCPToolRegistry::make_prop("string", "Destination parent node path ('.' for root).");
		props["new_name"] = MCPToolRegistry::make_prop("string", "Optional new node name.", "");
		Vector<String> req;
		req.push_back("node_path");
		req.push_back("new_parent_path");
		p_registry->register_tool(
				"scene_reparent_node",
				"Reparent (and optionally rename) a node in the edited scene using EditorUndoRedoManager.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_reparent_node);
	}

	{
		Dictionary props;
		props["node_path"] = MCPToolRegistry::make_prop("string", "Path relative to root ('.' for root).", ".");
		props["category_filter"] = MCPToolRegistry::make_prop("string", "Optional case-insensitive category or property name filter.", "");
		p_registry->register_tool(
				"scene_get_properties",
				"List inspector-exposed properties and current values of a node in the edited scene.",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_READ,
				&MCPSceneTools::tool_get_properties);
	}

	{
		Dictionary props;
		props["node_path"] = MCPToolRegistry::make_prop("string", "Path relative to root ('.' for root).", ".");
		props["property"] = MCPToolRegistry::make_prop("string", "Property name (e.g. 'position', 'visible', 'script').");
		Dictionary val_schema;
		props["value"] = val_schema;
		Vector<String> req;
		req.push_back("property");
		req.push_back("value");
		p_registry->register_tool(
				"scene_set_property",
				"Set a property on a scene node (supports Vector2/3/4, Color, Transform3D, NodePath, and res:// paths) with UndoRedo.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_set_property);
	}

	{
		Dictionary props;
		props["source_path"] = MCPToolRegistry::make_prop("string", "Source node emitting the signal.");
		props["signal_name"] = MCPToolRegistry::make_prop("string", "Signal name on the source node.");
		props["target_path"] = MCPToolRegistry::make_prop("string", "Target node receiving the signal.");
		props["method_name"] = MCPToolRegistry::make_prop("string", "Method name on the target node.");
		Vector<String> req;
		req.push_back("source_path");
		req.push_back("signal_name");
		req.push_back("target_path");
		req.push_back("method_name");
		p_registry->register_tool(
				"scene_connect_signal",
				"Persistently connect a signal between two nodes in the edited scene via EditorUndoRedoManager.",
				MCPToolRegistry::make_object_schema(props, req),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_connect_signal);
	}

	{
		Dictionary props;
		props["resource_type"] = MCPToolRegistry::make_prop("string", "Resource class name to create (e.g. 'CapsuleShape3D', 'BoxMesh').", "");
		props["resource_path"] = MCPToolRegistry::make_prop("string", "Existing res:// resource path to modify instead of creating a new one.", "");
		props["properties"] = MCPToolRegistry::make_prop("object", "Key-value map of properties to apply to the resource.");
		props["assign_node_path"] = MCPToolRegistry::make_prop("string", "Optional node path to assign this resource onto.", "");
		props["assign_property"] = MCPToolRegistry::make_prop("string", "Optional property on the target node (e.g. 'shape', 'mesh').", "");
		props["save_path"] = MCPToolRegistry::make_prop("string", "Optional res:// path to save the resource (e.g. 'res://player_shape.tres').", "");
		p_registry->register_tool(
				"scene_edit_resource",
				"Create or modify a Godot Resource, optionally saving to disk and/or assigning to a scene node property with UndoRedo.",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_edit_resource);
	}

	{
		Dictionary props;
		props["path"] = MCPToolRegistry::make_prop("string", "Target 'res://*.tscn' path (optional if scene already has a file path).", "");
		p_registry->register_tool(
				"scene_save",
				"Save the currently edited scene to disk.",
				MCPToolRegistry::make_object_schema(props),
				MCP_PERM_EDIT,
				&MCPSceneTools::tool_save);
	}
}
