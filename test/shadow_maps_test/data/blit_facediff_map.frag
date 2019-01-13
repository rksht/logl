#version 430 core

layout(binding = FACE_DEPTH_DIFF_BINDING) uniform sampler2D face_diff_sampler;

in VsOut {
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    // vec3 bitangent_w;
    vec2 st;
} fs_in;

out vec4 fc;

const float sigma = 30.0;
const vec3 color_min = vec3(0.2, 0.2, 0.2);
const vec3 color_max = vec3(0.8, 0.80, 0.99);

uniform float time_val_in_sec;

void main() {
	float diff = texture(face_diff_sampler, (fs_in.pos_w.xy + 1.0) * 0.5).r;
	float fresnel = texture(face_diff_sampler, (fs_in.pos_w.xy + 1.0) * 0.5).g;

	float t = abs(sin(time_val_in_sec/ 4.0));

	vec3 color = mix(color_min, color_max, t);

	float thickness = abs(diff) * t;
	if (thickness == 0.0) {
		// fc = vec4(t);
		discard;
	}
	float intensity = fresnel * exp(-sigma * thickness);
	fc = vec4(intensity * color, t);
}
