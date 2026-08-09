/**************************************************************************/
/*  line_3d.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                 */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
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

#include "line_3d.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/rendering_server.h"

// ---------------------------------------------------------------------------
// Static shader cache
// Line3D caches the four built-in Shader objects so they are only compiled
// once per editor/runtime session. register_scene_types.cpp calls
// init_shaders() / finish_shaders() (previously on Trail3D, now on Line3D).
// ---------------------------------------------------------------------------

static Ref<Shader> _shader_billboard_mix;
static Ref<Shader> _shader_billboard_add;
static Ref<Shader> _shader_local_mix;
static Ref<Shader> _shader_local_add;

// ---------------------------------------------------------------------------
// Built-in shader sources (match Trail3D's shaders exactly)
// ---------------------------------------------------------------------------

static const char *LINE3D_SHADER_BILLBOARD_MIX =
		"shader_type spatial;\n"
		"render_mode blend_mix, depth_draw_never, unshaded, skip_vertex_transform, cull_disabled;\n"
		"\n"
		"void vertex() {\n"
		"    vec3 p = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;\n"
		"    vec3 t = (MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;\n"
		"    VERTEX = p + UV.y * normalize(cross(p, t));\n"
		"    NORMAL = (VIEW_MATRIX * vec4(0, 1, 0, 0)).xyz;\n"
		"    UV.y = (sign(UV.y) + 1.0) / 2.0;\n"
		"}\n"
		"\n"
		"void fragment() {\n"
		"    ALBEDO = COLOR.rgb;\n"
		"    ALPHA = COLOR.a;\n"
		"}\n";

static const char *LINE3D_SHADER_BILLBOARD_ADD =
		"shader_type spatial;\n"
		"render_mode blend_add, depth_draw_never, unshaded, skip_vertex_transform, cull_disabled;\n"
		"\n"
		"void vertex() {\n"
		"    vec3 p = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;\n"
		"    vec3 t = (MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;\n"
		"    VERTEX = p + UV.y * normalize(cross(p, t));\n"
		"    NORMAL = (VIEW_MATRIX * vec4(0, 1, 0, 0)).xyz;\n"
		"    UV.y = (sign(UV.y) + 1.0) / 2.0;\n"
		"}\n"
		"\n"
		"void fragment() {\n"
		"    ALBEDO = COLOR.rgb;\n"
		"    ALPHA = COLOR.a;\n"
		"}\n";

static const char *LINE3D_SHADER_LOCAL_MIX =
		"shader_type spatial;\n"
		"render_mode blend_mix, depth_draw_never, unshaded, cull_disabled;\n"
		"\n"
		"void fragment() {\n"
		"    ALBEDO = COLOR.rgb;\n"
		"    ALPHA = COLOR.a;\n"
		"}\n";

static const char *LINE3D_SHADER_LOCAL_ADD =
		"shader_type spatial;\n"
		"render_mode blend_add, depth_draw_never, unshaded, cull_disabled;\n"
		"\n"
		"void fragment() {\n"
		"    ALBEDO = COLOR.rgb;\n"
		"    ALPHA = COLOR.a;\n"
		"}\n";

void Line3D::init_shaders() {
	_shader_billboard_mix.instantiate();
	_shader_billboard_mix->set_code(LINE3D_SHADER_BILLBOARD_MIX);

	_shader_billboard_add.instantiate();
	_shader_billboard_add->set_code(LINE3D_SHADER_BILLBOARD_ADD);

	_shader_local_mix.instantiate();
	_shader_local_mix->set_code(LINE3D_SHADER_LOCAL_MIX);

	_shader_local_add.instantiate();
	_shader_local_add->set_code(LINE3D_SHADER_LOCAL_ADD);
}

void Line3D::finish_shaders() {
	_shader_billboard_mix.unref();
	_shader_billboard_add.unref();
	_shader_local_mix.unref();
	_shader_local_add.unref();
}

// ---------------------------------------------------------------------------
// Material helpers
// ---------------------------------------------------------------------------

Ref<ShaderMaterial> Line3D::_create_builtin_material(bool p_additive) const {
	Ref<Shader> shader;
	if (mesh_alignment == MESH_ALIGNMENT_BILLBOARD) {
		shader = p_additive ? _shader_billboard_add : _shader_billboard_mix;
	} else {
		shader = p_additive ? _shader_local_add : _shader_local_mix;
	}
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(shader);
	return mat;
}

Ref<Material> Line3D::_get_active_material() const {
	switch (material_mode) {
		case MATERIAL_MODE_ADD:
			return _create_builtin_material(true);
		case MATERIAL_MODE_CUSTOM:
			return custom_material;
		case MATERIAL_MODE_MIX:
		default:
			return _create_builtin_material(false);
	}
}

// ---------------------------------------------------------------------------
// Mesh building from points[]
//
// Convention (same as Trail3D so shaders are interchangeable):
//   VERTEX  = spine position (both verts at same pos for billboard mode)
//   NORMAL  = tangent direction along the line
//   UV.x    = longitudinal (0..1 or 0..length depending on tiling_mode)
//   UV.y    = -half_width / +half_width  (billboard shader fans these out)
//             or 0/1 for local mode (actual offset baked into VERTEX)
// ---------------------------------------------------------------------------

void Line3D::_build_mesh() {
	// Clear any existing surface.
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->mesh_clear(mesh_rid);

	const int n = points.size();
	if (n < 2) {
		return;
	}

	// Cumulative arc-lengths for UV/gradient sampling.
	Vector<float> lengths;
	lengths.resize(n);
	lengths.write[0] = 0.0f;
	float total_length = 0.0f;
	for (int i = 1; i < n; i++) {
		total_length += points[i - 1].distance_to(points[i]);
		lengths.write[i] = total_length;
	}

	// Two verts per spine point → quad strip.
	const int vert_count = n * 2;
	const int index_count = (n - 1) * 6;

	PackedVector3Array verts;
	verts.resize(vert_count);
	PackedVector3Array normals;
	normals.resize(vert_count);
	PackedVector2Array uvs;
	uvs.resize(vert_count);
	PackedColorArray colors;
	colors.resize(vert_count);
	PackedInt32Array indices;
	indices.resize(index_count);

	for (int i = 0; i < n; i++) {
		const float t = (total_length > 0.0f) ? (lengths[i] / total_length) : 0.0f;

		// Tangent: average of neighbouring segments, clamped at endpoints.
		Vector3 tangent;
		if (i == 0) {
			tangent = (points[1] - points[0]).normalized();
		} else if (i == n - 1) {
			tangent = (points[n - 1] - points[n - 2]).normalized();
		} else {
			tangent = ((points[i] - points[i - 1]).normalized() +
					(points[i + 1] - points[i]).normalized())
					.normalized();
		}

		float w = width;
		if (width_curve.is_valid()) {
			w *= width_curve->sample(t);
		}

		float uv_x;
		if (tiling_mode == TILING_MODE_LENGTH) {
			uv_x = (pin_uv ? lengths[i] : lengths[i]) * tiling_multiplier;
		} else {
			uv_x = t * tiling_multiplier;
		}

		Color c = color;
		if (color_gradient.is_valid()) {
			c = color_gradient->get_color_at_offset(t) * color;
		}

		const int vi = i * 2;

		if (mesh_alignment == MESH_ALIGNMENT_BILLBOARD) {
			// Both verts sit on the spine; the shader fans them outward
			// perpendicular to the camera using UV.y as the signed half-width.
			const float hw = w * 0.5f;
			verts.write[vi + 0] = points[i];
			verts.write[vi + 1] = points[i];
			normals.write[vi + 0] = tangent;
			normals.write[vi + 1] = tangent;
			uvs.write[vi + 0] = Vector2(uv_x, -hw);
			uvs.write[vi + 1] = Vector2(uv_x, hw);
		} else {
			// Local: compute the actual side offset here in C++.
			Vector3 up = Vector3(0, 1, 0);
			if (Math::abs(tangent.dot(up)) > 0.99f) {
				up = Vector3(0, 0, 1);
			}
			const Vector3 side = tangent.cross(up).normalized();
			const Vector3 offset = side * (w * 0.5f);

			verts.write[vi + 0] = points[i] - offset;
			verts.write[vi + 1] = points[i] + offset;
			normals.write[vi + 0] = side;
			normals.write[vi + 1] = side;
			uvs.write[vi + 0] = Vector2(uv_x, 0.0f);
			uvs.write[vi + 1] = Vector2(uv_x, 1.0f);
		}

		colors.write[vi + 0] = c;
		colors.write[vi + 1] = c;
	}

	// Quad-strip indices.
	for (int i = 0; i < n - 1; i++) {
		const int base = i * 6;
		const int v = i * 2;
		indices.write[base + 0] = v + 0;
		indices.write[base + 1] = v + 2;
		indices.write[base + 2] = v + 1;
		indices.write[base + 3] = v + 1;
		indices.write[base + 4] = v + 2;
		indices.write[base + 5] = v + 3;
	}

	Array arr;
	arr.resize(Mesh::ARRAY_MAX);
	arr[Mesh::ARRAY_VERTEX] = verts;
	arr[Mesh::ARRAY_NORMAL] = normals;
	arr[Mesh::ARRAY_TEX_UV] = uvs;
	arr[Mesh::ARRAY_COLOR] = colors;
	arr[Mesh::ARRAY_INDEX] = indices;

	rs->mesh_add_surface_from_arrays(mesh_rid, RenderingServerEnums::PRIMITIVE_TRIANGLES, arr);

	Ref<Material> mat = _get_active_material();
	if (mat.is_valid()) {
		rs->mesh_surface_set_material(mesh_rid, 0, mat->get_rid());
	}

	mesh_dirty = false;
}

void Line3D::_mark_dirty() {
	mesh_dirty = true;
}

// ---------------------------------------------------------------------------
// Node lifecycle
// ---------------------------------------------------------------------------

void Line3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;

		case NOTIFICATION_READY: {
			// Push the (possibly empty) mesh into the visual server so the
			// node is at least known even before the first _process.
			_build_mesh();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (mesh_dirty) {
				_build_mesh();
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			set_process_internal(false);
		} break;
	}
}

// ---------------------------------------------------------------------------
// Point management
// ---------------------------------------------------------------------------

void Line3D::set_points(const PackedVector3Array &p_points) {
	points = p_points;
	_mark_dirty();
}

PackedVector3Array Line3D::get_points() const {
	return points;
}

void Line3D::add_point(const Vector3 &p_position, int p_at_index) {
	if (p_at_index < 0 || p_at_index >= points.size()) {
		points.append(p_position);
	} else {
		points.insert(p_at_index, p_position);
	}
	_mark_dirty();
}

void Line3D::remove_point(int p_index) {
	ERR_FAIL_INDEX(p_index, points.size());
	points.remove_at(p_index);
	_mark_dirty();
}

void Line3D::set_point_position(int p_index, const Vector3 &p_position) {
	ERR_FAIL_INDEX(p_index, points.size());
	points.write[p_index] = p_position;
	_mark_dirty();
}

Vector3 Line3D::get_point_position(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, points.size(), Vector3());
	return points[p_index];
}

int Line3D::get_point_count() const {
	return points.size();
}

void Line3D::clear_points() {
	points.clear();
	_mark_dirty();
}

void Line3D::rebuild() {
	_build_mesh();
}

// ---------------------------------------------------------------------------
// Property setters/getters
// ---------------------------------------------------------------------------

void Line3D::set_width(float p_width) { width = p_width; _mark_dirty(); }
float Line3D::get_width() const { return width; }

void Line3D::set_width_curve(const Ref<Curve> &p_curve) { width_curve = p_curve; _mark_dirty(); }
Ref<Curve> Line3D::get_width_curve() const { return width_curve; }

void Line3D::set_color(const Color &p_color) { color = p_color; _mark_dirty(); }
Color Line3D::get_color() const { return color; }

void Line3D::set_color_gradient(const Ref<Gradient> &p_gradient) { color_gradient = p_gradient; _mark_dirty(); }
Ref<Gradient> Line3D::get_color_gradient() const { return color_gradient; }

void Line3D::set_mesh_alignment(MeshAlignment p_alignment) { mesh_alignment = p_alignment; _mark_dirty(); }
Line3D::MeshAlignment Line3D::get_mesh_alignment() const { return mesh_alignment; }

void Line3D::set_material_mode(MaterialMode p_mode) { material_mode = p_mode; _mark_dirty(); }
Line3D::MaterialMode Line3D::get_material_mode() const { return material_mode; }

void Line3D::set_material(const Ref<ShaderMaterial> &p_material) {
	custom_material = p_material;
	if (material_mode == MATERIAL_MODE_CUSTOM) {
		_mark_dirty();
	}
}
Ref<ShaderMaterial> Line3D::get_material() const { return custom_material; }

void Line3D::set_tiling_mode(TilingMode p_mode) { tiling_mode = p_mode; _mark_dirty(); }
Line3D::TilingMode Line3D::get_tiling_mode() const { return tiling_mode; }

void Line3D::set_tiling_multiplier(float p_multiplier) { tiling_multiplier = p_multiplier; _mark_dirty(); }
float Line3D::get_tiling_multiplier() const { return tiling_multiplier; }

void Line3D::set_pin_uv(bool p_pin) { pin_uv = p_pin; _mark_dirty(); }
bool Line3D::get_pin_uv() const { return pin_uv; }

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void Line3D::_bind_methods() {
	// Point management
	ClassDB::bind_method(D_METHOD("set_points", "points"), &Line3D::set_points);
	ClassDB::bind_method(D_METHOD("get_points"), &Line3D::get_points);
	ClassDB::bind_method(D_METHOD("add_point", "position", "at_index"), &Line3D::add_point, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("remove_point", "index"), &Line3D::remove_point);
	ClassDB::bind_method(D_METHOD("set_point_position", "index", "position"), &Line3D::set_point_position);
	ClassDB::bind_method(D_METHOD("get_point_position", "index"), &Line3D::get_point_position);
	ClassDB::bind_method(D_METHOD("get_point_count"), &Line3D::get_point_count);
	ClassDB::bind_method(D_METHOD("clear_points"), &Line3D::clear_points);
	ClassDB::bind_method(D_METHOD("rebuild"), &Line3D::rebuild);

	// Visual properties
	ClassDB::bind_method(D_METHOD("set_width", "width"), &Line3D::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &Line3D::get_width);
	ClassDB::bind_method(D_METHOD("set_width_curve", "curve"), &Line3D::set_width_curve);
	ClassDB::bind_method(D_METHOD("get_width_curve"), &Line3D::get_width_curve);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Line3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &Line3D::get_color);
	ClassDB::bind_method(D_METHOD("set_color_gradient", "gradient"), &Line3D::set_color_gradient);
	ClassDB::bind_method(D_METHOD("get_color_gradient"), &Line3D::get_color_gradient);
	ClassDB::bind_method(D_METHOD("set_mesh_alignment", "alignment"), &Line3D::set_mesh_alignment);
	ClassDB::bind_method(D_METHOD("get_mesh_alignment"), &Line3D::get_mesh_alignment);
	ClassDB::bind_method(D_METHOD("set_material_mode", "mode"), &Line3D::set_material_mode);
	ClassDB::bind_method(D_METHOD("get_material_mode"), &Line3D::get_material_mode);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &Line3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &Line3D::get_material);
	ClassDB::bind_method(D_METHOD("set_tiling_mode", "mode"), &Line3D::set_tiling_mode);
	ClassDB::bind_method(D_METHOD("get_tiling_mode"), &Line3D::get_tiling_mode);
	ClassDB::bind_method(D_METHOD("set_tiling_multiplier", "multiplier"), &Line3D::set_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("get_tiling_multiplier"), &Line3D::get_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("set_pin_uv", "pin"), &Line3D::set_pin_uv);
	ClassDB::bind_method(D_METHOD("get_pin_uv"), &Line3D::get_pin_uv);

	// Properties
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "points"), "set_points", "get_points");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0.0,10.0,0.001,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "width_curve", PROPERTY_HINT_RESOURCE_TYPE, Curve::get_class_static()), "set_width_curve", "get_width_curve");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_gradient", PROPERTY_HINT_RESOURCE_TYPE, Gradient::get_class_static()), "set_color_gradient", "get_color_gradient");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mesh_alignment", PROPERTY_HINT_ENUM, "Local,Billboard"), "set_mesh_alignment", "get_mesh_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Default,Default Additive,Custom"), "set_material_mode", "get_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, ShaderMaterial::get_class_static()), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tiling_mode", PROPERTY_HINT_ENUM, "Unit,Length"), "set_tiling_mode", "get_tiling_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tiling_multiplier", PROPERTY_HINT_RANGE, "-100.0,100.0,0.001"), "set_tiling_multiplier", "get_tiling_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pin_uv"), "set_pin_uv", "get_pin_uv");

	// Enums
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_LOCAL);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_BILLBOARD);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_MAX);

	BIND_ENUM_CONSTANT(TILING_MODE_UNIT);
	BIND_ENUM_CONSTANT(TILING_MODE_LENGTH);
	BIND_ENUM_CONSTANT(TILING_MAX);

	BIND_ENUM_CONSTANT(MATERIAL_MODE_MIX);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_ADD);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_CUSTOM);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_MAX);
}

Line3D::Line3D() {
	mesh_rid = RenderingServer::get_singleton()->mesh_create();
	RenderingServer::get_singleton()->instance_set_base(get_instance(), mesh_rid);
}

Line3D::~Line3D() {
	RenderingServer::get_singleton()->free(mesh_rid);
}
