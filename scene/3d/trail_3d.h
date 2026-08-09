/**************************************************************************/
/*  trail_3d.h                                                            */
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

#pragma once

#include "line_3d.h"

// Trail3D extends Line3D by automatically managing a time-ordered deque of
// world-space positions. Each frame it records its own global position,
// culls old points by lifetime or max length, and then calls the inherited
// Line3D mesh builder via the overridden _build_mesh().
//
// The inherited point-management API (add_point / set_points / etc.) is
// intentionally NOT used for the automatic trail – Trail3D keeps its own
// timed-point list so it can cull by timestamp. The inherited API is still
// available so users can pre-seed a trail or inspect it.
class Trail3D : public Line3D {
	GDCLASS(Trail3D, Line3D);

public:
	enum LimitMode {
		LIMIT_MODE_LIFETIME,
		LIMIT_MODE_MAX_LENGTH,
		LIMIT_MODE_MAX,
	};

private:
	struct TrailPoint {
		Vector3 position; // World space at the moment of recording.
		float timestamp = 0.0f;
	};

	LocalVector<TrailPoint> trail_points;

	// Trail-only properties.
	bool emitting = true;
	float lifetime = 0.2f;
	float max_length = 10.0f;
	float min_section_length = 0.2f;
	LimitMode limit_mode = LIMIT_MODE_LIFETIME;

	float current_length = 0.0f;
	float time_elapsed = 0.0f;

	void _update_trail(float p_delta);

protected:
	// Override Line3D's mesh build to use trail_points instead of points[].
	// This is called from Line3D::rebuild() and the internal-process dirty
	// check, so the inherited API still works.
	virtual void _build_mesh() override;

	static void _bind_methods();
	void _notification(int p_what);

public:
	// Trail-specific API
	void set_emitting(bool p_emitting);
	bool is_emitting() const;

	void set_lifetime(float p_lifetime);
	float get_lifetime() const;

	void set_max_length(float p_max_length);
	float get_max_length() const;

	void set_min_section_length(float p_length);
	float get_min_section_length() const;

	void set_limit_mode(LimitMode p_mode);
	LimitMode get_limit_mode() const;

	float get_current_length() const;

	// Clears all recorded trail points and rebuilds an empty mesh.
	void clear();

	// Forwarding stubs so register_scene_types.cpp call sites need no changes.
	// The actual shader cache now lives in Line3D.
	static void init_shaders() { Line3D::init_shaders(); }
	static void finish_shaders() { Line3D::finish_shaders(); }

		Trail3D() = default;
	~Trail3D() override = default;
};

VARIANT_ENUM_CAST(Trail3D::LimitMode);
