#version 430 core

#ifdef QUAD_FS

layout(binding = 0) uniform sampler2D df_sampler;

in VsOut {
	vec2 st;
} fs_in;

out vec4 frag_color;

const float THICKNESS = 300.0;

void main() {
	const float d = texture(df_sampler, fs_in.st).x;

	if (d > 0.0) {
		float ch = smoothstep(0.0, 1.0, clamp(d / THICKNESS, 0.0, 1.0));
		frag_color = vec4(ch, ch, ch, 1.0);
	} else {
		frag_color = vec4(0.0, 0.0, 0.0, 1.0);
	}
}

#endif

#ifdef CIRCLE_FS

uniform vec4 line_color;

out vec4 frag_color;

void main() {
	// frag_color = vec4(1.0, 0.0, 0.0, 1.0);
	frag_color = line_color;
}

#endif
