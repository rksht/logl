#pragma once

constexpr auto samplepass_vs = R"(
	#version 430 core

	layout(location = 0) in vec3 position;
	layout(location = 1) in vec2 st;

	out vec2 frag_st;

	void main() {
		gl_Position = vec4(position.xy, 1.0, 1.0);
		frag_st = st;
	}
)";

constexpr auto samplepass_vs_depth_assist = R"(
	#version 430 core

	layout(location = 0) in vec3 position;
	layout(location = 1) in vec2 st;
)";

constexpr auto samplepass_fs_POS = R"(
	#version 430 core

	in vec2 frag_st;

	out vec4 frag_color;

	layout(binding = POSITION_TEXTURE_BINDING) uniform sampler2D pos_sampler;

	void main() {
		vec3 v3 = texture(pos_sampler, frag_st).xyz;
		frag_color = vec4(normalize(v3.xyz), 1.0);
	}
)";

constexpr auto samplepass_fs_DIFFUSE = R"(
	#version 430 core

	in vec2 frag_st;

	out vec4 frag_color;

	layout(binding = DIFFUSE_TEXTURE_BINDING) uniform sampler2D diffuse_sampler;

	void main() {
		vec3 v3 = texture(diffuse_sampler, frag_st).xyz;
		frag_color = vec4(v3, 1.0);
	}
)";

constexpr auto samplepass_fs_NORMAL = R"(
	#version 430 core

	in vec2 frag_st;

	out vec4 frag_color;

	layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D normal_sampler;

	void main() {
		vec3 v3 = texture(normal_sampler, frag_st).xyz;
		frag_color = vec4(v3, 1.0);
	}
)";

constexpr auto samplepass_fs_TCOORD = R"(
	#version 430 core

	in vec2 frag_st;

	out vec4 frag_color;

	layout(binding = TCOORD_TEXTURE_BINDING) uniform sampler2D st_sampler;

	void main() {
		vec2 v2 = texture(st_sampler, frag_st).xy;
		frag_color = vec4(normalize(v2.xy), 0.0, 1.0);
	}
)";

// Not at all optimized shader. Just for tinkering really.
// https://www.khronos.org/opengl/wiki/Compute_eye_space_from_window_space#From_XYZ_of_gl_FragCoord
constexpr auto samplepass_fs_DEPTH = R"(
	#version 430 core

	in vec2 frag_st;

	out vec4 frag_color;

	layout(binding = DEPTH_TEXTURE_BINDING) uniform sampler2D depth_sampler;

	layout(binding = 0) uniform MVP {
		mat4 view; // View matrix is unused
		mat4 proj; // Perspective projection matrix is needed below
	};

	const float E_1 = -1.0;

	void main() {
		// Window-space depth - this is what we would have in gl_FragCoord.z
		// if we were using nondeferred rendering.
		float window_depth = texture(depth_sampler, frag_st).x;

		// NDC space coordinates. The viewport corner and extent should be
		// that of the viewport used to render the actual scene.
		vec3 N = vec3(
			2.0 * (gl_FragCoord.x - DEPTH_QUAD_BOTLEFT_X) / DEPTH_QUAD_WIDTH - 1.0,
			2.0 * (gl_FragCoord.y - DEPTH_QUAD_BOTLEFT_Y) / DEPTH_QUAD_HEIGHT - 1.0,

			// NDC-space depth. Depthrange near and far are the default 0.0 and 1.0
			2.0 * window_depth - 1.0
		);

		const float T_1 = proj[2][2];
		const float T_2 = proj[3][2];

		const float R = T_2 / (N.z - T_1 / E_1);

		// View space z (negated so that we get a positive value)
		const float V_negz = -R / E_1;

		// Not using.
		// const float c = V_negz / 5; // Scale down a little

		// Clip space coordinates
		const vec3 C = R * N;

		// View space coords (EXPENSIVE!)
		const vec4 V = inverse(proj) * vec4(C, -V_negz);

		// Let's output depending on distance from eye to point.
		// The spheres are all in a radius of 4.0 units so we scale down a bit here.
		const float d = length(V.xyz) / 5.0;
		frag_color = vec4(d, d, d, 1.0);
	}
)";
