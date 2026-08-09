/**************************************************************************/
/*  trail_3d.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2024, CozyCubeGames                                     */
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

#include "trail_3d.h"

#include "core/object/class_db.h"

// ---------------------------------------------------------------------------
// Trail point management
// ---------------------------------------------------------------------------

void Trail3D::_update_trail(float p_delta) {
	time_elapsed += p_delta;

	const Vector3 current_pos = get_global_position();

	if (emitting) {
		// Add a new point when the node has moved far enough.
		if (trail_points.is_empty() ||
				current_pos.distance_to(trail_points[trail_points.size() - 1].position) >= min_section_length) {
			TrailPoint tp;
			tp.position = current_pos;
			tp.timestamp = time_elapsed;
			trail_points.push_back(tp);
		} else {
			// Always keep the tip up to date even without a new section.
			trail_points[trail_points.size() - 1].position = current_pos;
			trail_points[trail_points.size() - 1].timestamp = time_elapsed;
		}
	}

	// Cull old points from the tail.
	if (limit_mode == LIMIT_MODE_LIFETIME) {
		const float cutoff = time_elapsed - lifetime;
		while (trail_points.size() > 1 && trail_points[0].timestamp < cutoff) {
			trail_points.remove_at(0);
		}
	} else {
		// LIMIT_MODE_MAX_LENGTH: trim from tail until total length <= max_length.
		current_length = 0.0f;
		for (uint32_t i = 1; i < trail_points.size(); i++) {
			current_length += trail_points[i - 1].position.distance_to(trail_points[i].position);
		}
		while (trail_points.size() > 1 && current_length > max_length) {
			current_length -= trail_points[0].position.distance_to(trail_points[1].position);
			trail_points.remove_at(0);
		}
	}
}

// ---------------------------------------------------------------------------
// Mesh building (overrides Line3D::_build_mesh)
//
// Copies trail_points into the inherited points[] array, then delegates to
// Line3D::_build_mesh() so all the shared geometry/shader/material logic
// lives in one place.
// ---------------------------------------------------------------------------

void Trail3D::_build_mesh() {
	// Sync trail_points → Line3D::points (local space via inverse transform).
	const Transform3D inv = get_global_transform().affine_inverse();

	points.resize(trail_points.size());
	for (uint32_t i = 0; i < trail_points.size(); i++) {
		points.write[i] = inv.xform(trail_points[i].position);
	}

	// Let Line3D handle the actual ArrayMesh construction.
	Line3D::_build_mesh();
}

// ---------------------------------------------------------------------------
// Node lifecycle
// ---------------------------------------------------------------------------

void Trail3D::_notification(int p_what) {
	// Let Line3D handle ENTER_TREE / EXIT_TREE / READY / INTERNAL_PROCESS
	// notifications for the mesh side.
	Line3D::_notification(p_what);

	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update_trail(get_process_delta_time());
			// _build_mesh is triggered by Line3D's own NOTIFICATION_INTERNAL_PROCESS
			// via mesh_dirty. We need to make sure it rebuilds every frame while the
			// trail is active, so mark dirty after updating.
			_mark_dirty();
		} break;
	}
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Trail3D::set_emitting(bool p_emitting) {
	if (emitting == p_emitting) {
		return;
	}
	emitting = p_emitting;
	if (emitting) {
		// Clear old trail so the new emission starts fresh.
		clear();
	}
}

bool Trail3D::is_emitting() const {
	return emitting;
}

void Trail3D::set_lifetime(float p_lifetime) {
	lifetime = MAX(0.001f, p_lifetime);
}

float Trail3D::get_lifetime() const {
	return lifetime;
}

void Trail3D::set_max_length(float p_max_length) {
	max_length = MAX(0.0f, p_max_length);
}

float Trail3D::get_max_length() const {
	return max_length;
}

void Trail3D::set_min_section_length(float p_length) {
	min_section_length = MAX(0.001f, p_length);
}

float Trail3D::get_min_section_length() const {
	return min_section_length;
}

void Trail3D::set_limit_mode(LimitMode p_mode) {
	limit_mode = p_mode;
}

Trail3D::LimitMode Trail3D::get_limit_mode() const {
	return limit_mode;
}

float Trail3D::get_current_length() const {
	if (limit_mode == LIMIT_MODE_MAX_LENGTH) {
		return current_length;
	}
	// Compute on demand for lifetime mode.
	float len = 0.0f;
	for (uint32_t i = 1; i < trail_points.size(); i++) {
		len += trail_points[i - 1].position.distance_to(trail_points[i].position);
	}
	return len;
}

void Trail3D::clear() {
	trail_points.clear();
	current_length = 0.0f;
	_mark_dirty();
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void Trail3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_emitting", "emitting"), &Trail3D::set_emitting);
	ClassDB::bind_method(D_METHOD("is_emitting"), &Trail3D::is_emitting);

	ClassDB::bind_method(D_METHOD("set_lifetime", "lifetime"), &Trail3D::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &Trail3D::get_lifetime);

	ClassDB::bind_method(D_METHOD("set_max_length", "max_length"), &Trail3D::set_max_length);
	ClassDB::bind_method(D_METHOD("get_max_length"), &Trail3D::get_max_length);

	ClassDB::bind_method(D_METHOD("set_min_section_length", "length"), &Trail3D::set_min_section_length);
	ClassDB::bind_method(D_METHOD("get_min_section_length"), &Trail3D::get_min_section_length);

	ClassDB::bind_method(D_METHOD("set_limit_mode", "mode"), &Trail3D::set_limit_mode);
	ClassDB::bind_method(D_METHOD("get_limit_mode"), &Trail3D::get_limit_mode);

	ClassDB::bind_method(D_METHOD("get_current_length"), &Trail3D::get_current_length);
	ClassDB::bind_method(D_METHOD("clear"), &Trail3D::clear);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emitting"), "set_emitting", "is_emitting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_mode", PROPERTY_HINT_ENUM, "Lifetime,Max Length"), "set_limit_mode", "get_limit_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0.01,600.0,0.01,or_greater,exp,suffix:s"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_length", PROPERTY_HINT_RANGE, "0.0,100.0,0.001,or_greater,suffix:m"), "set_max_length", "get_max_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_section_length", PROPERTY_HINT_RANGE, "0.001,1.0,0.001,or_greater,suffix:m"), "set_min_section_length", "get_min_section_length");

	BIND_ENUM_CONSTANT(LIMIT_MODE_LIFETIME);
	BIND_ENUM_CONSTANT(LIMIT_MODE_MAX_LENGTH);
	BIND_ENUM_CONSTANT(LIMIT_MODE_MAX);
}
