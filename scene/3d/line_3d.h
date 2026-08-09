/**************************************************************************/
/*  line_3d.h                                                             */
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

#pragma once

#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/curve.h"
#include "scene/resources/gradient.h"
#include "scene/resources/material.h"

class Line3D : public GeometryInstance3D {
	GDCLASS(Line3D, GeometryInstance3D);

public:
	enum MeshAlignment {
		MESH_ALIGNMENT_LOCAL,
		MESH_ALIGNMENT_BILLBOARD,
		MESH_ALIGNMENT_MAX,
	};

	enum TilingMode {
		TILING_MODE_UNIT,
		TILING_MODE_LENGTH,
		TILING_MAX,
	};

	enum MaterialMode {
		MATERIAL_MODE_MIX,
		MATERIAL_MODE_ADD,
		MATERIAL_MODE_CUSTOM,
		MATERIAL_MODE_MAX,
	};

protected:
	// ---- Shared visual properties (also used by Trail3D) ----
	float width = 0.1f;
	Ref<Curve> width_curve;
	Color color = Color(1, 1, 1, 1);
	Ref<Gradient> color_gradient;
	MeshAlignment mesh_alignment = MESH_ALIGNMENT_BILLBOARD;
	MaterialMode material_mode = MATERIAL_MODE_MIX;
	Ref<ShaderMaterial> custom_material;
	TilingMode tiling_mode = TILING_MODE_LENGTH;
	float tiling_multiplier = 1.0f;
	bool pin_uv = false;

	// ---- Point storage (local space) ----
	PackedVector3Array points;

	// ---- RenderingServer mesh handle ----
	RID mesh_rid;
	bool mesh_dirty = false;

	// Subclasses override this to build from their own point representation.
	// The base implementation builds directly from points[].
	virtual void _build_mesh();
	void _mark_dirty();

	// Shared helpers used by _build_mesh implementations.
	Ref<Material> _get_active_material() const;
	Ref<ShaderMaterial> _create_builtin_material(bool p_additive) const;

	static void _bind_methods();
	void _notification(int p_what);

public:
	// ---- Point management ----
	void set_points(const PackedVector3Array &p_points);
	PackedVector3Array get_points() const;

	void add_point(const Vector3 &p_position, int p_at_index = -1);
	void remove_point(int p_index);
	void set_point_position(int p_index, const Vector3 &p_position);
	Vector3 get_point_position(int p_index) const;
	int get_point_count() const;
	void clear_points();

	// Force an immediate mesh rebuild (useful after bulk edits).
	void rebuild();

	// ---- Shared visual properties ----
	void set_width(float p_width);
	float get_width() const;
	void set_width_curve(const Ref<Curve> &p_curve);
	Ref<Curve> get_width_curve() const;

	void set_color(const Color &p_color);
	Color get_color() const;
	void set_color_gradient(const Ref<Gradient> &p_gradient);
	Ref<Gradient> get_color_gradient() const;

	void set_mesh_alignment(MeshAlignment p_alignment);
	MeshAlignment get_mesh_alignment() const;

	void set_material_mode(MaterialMode p_mode);
	MaterialMode get_material_mode() const;
	void set_material(const Ref<ShaderMaterial> &p_material);
	Ref<ShaderMaterial> get_material() const;

	void set_tiling_mode(TilingMode p_mode);
	TilingMode get_tiling_mode() const;
	void set_tiling_multiplier(float p_multiplier);
	float get_tiling_multiplier() const;

	void set_pin_uv(bool p_pin);
	bool get_pin_uv() const;

	// Shader cache lifecycle – called from register_scene_types.
	static void init_shaders();
	static void finish_shaders();

		Line3D();
	~Line3D() override;
};

VARIANT_ENUM_CAST(Line3D::MeshAlignment);
VARIANT_ENUM_CAST(Line3D::TilingMode);
VARIANT_ENUM_CAST(Line3D::MaterialMode);
