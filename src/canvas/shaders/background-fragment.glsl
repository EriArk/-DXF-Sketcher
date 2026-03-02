#version 330
layout(location = 0) out vec4 outputColor;
layout(location = 1) out int pick;
layout(location = 2) out vec4 select;
in vec3 color_to_fragment;
uniform float alpha;
uniform int grid_enabled;
uniform float grid_spacing;
uniform vec3 grid_minor_color;
uniform vec3 grid_major_color;
uniform vec3 grid_axis_x_color;
uniform vec3 grid_axis_y_color;
uniform vec2 viewport_size;
uniform mat4 projmat_viewmat_inv;

vec2 distance_to_grid(vec2 world_xy, float spacing) {
	vec2 cell = world_xy / spacing;
	vec2 frac_cell = abs(fract(cell + 0.5) - 0.5);
	return frac_cell * spacing;
}

float line_strength(vec2 world_xy, float spacing) {
	vec2 dist = distance_to_grid(world_xy, spacing);
	vec2 fw = max(fwidth(world_xy), vec2(1e-5));
	float x = 1.0 - smoothstep(0.0, fw.x, dist.x);
	float y = 1.0 - smoothstep(0.0, fw.y, dist.y);
	return max(x, y);
}

void main() {
	vec3 color_lin = color_to_fragment;
	if(grid_enabled != 0 && grid_spacing > 0.0 && viewport_size.x > 1.0 && viewport_size.y > 1.0) {
		vec2 ndc = vec2(
			(gl_FragCoord.x / viewport_size.x) * 2.0 - 1.0,
			(gl_FragCoord.y / viewport_size.y) * 2.0 - 1.0
		);

		vec4 near_clip = vec4(ndc, -1.0, 1.0);
		vec4 far_clip = vec4(ndc, 1.0, 1.0);
		vec4 near_world_h = projmat_viewmat_inv * near_clip;
		vec4 far_world_h = projmat_viewmat_inv * far_clip;
		vec3 near_world = near_world_h.xyz / near_world_h.w;
		vec3 far_world = far_world_h.xyz / far_world_h.w;
		vec3 ray = far_world - near_world;

		if(abs(ray.z) > 1e-8) {
			float t = -near_world.z / ray.z;
			if(t >= 0.0 && t <= 1.0) {
				vec2 world_xy = (near_world + ray * t).xy;
				float minor = line_strength(world_xy, grid_spacing) * 0.17;
				float major = line_strength(world_xy, grid_spacing * 10.0) * 0.35;
				vec3 minor_lin = pow(grid_minor_color, vec3(2.2));
				vec3 major_lin = pow(grid_major_color, vec3(2.2));
				vec3 axis_x_lin = pow(grid_axis_x_color, vec3(2.2));
				vec3 axis_y_lin = pow(grid_axis_y_color, vec3(2.2));
				float axis_x = (1.0 - smoothstep(0.0, max(fwidth(world_xy.x), 1e-5) * 1.4, abs(world_xy.y))) * 0.62;
				float axis_y = (1.0 - smoothstep(0.0, max(fwidth(world_xy.y), 1e-5) * 1.4, abs(world_xy.x))) * 0.62;
				color_lin = mix(color_lin, minor_lin, clamp(minor, 0.0, 1.0));
				color_lin = mix(color_lin, major_lin, clamp(major, 0.0, 1.0));
				color_lin = mix(color_lin, axis_x_lin, clamp(axis_x, 0.0, 1.0));
				color_lin = mix(color_lin, axis_y_lin, clamp(axis_y, 0.0, 1.0));
			}
		}
	}

	outputColor = vec4(pow(color_lin, vec3(1/2.2)), alpha);
	select = vec4(0,0,0,0);
	pick = 0;
}
