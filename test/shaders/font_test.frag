#version 430

in vec2 tex2d;

out vec4 frag_color;

layout(binding = 0) uniform sampler2D bitmap_sampler;
uniform vec3 text_color;

void main() {
	float alpha = texture(bitmap_sampler, tex2d).r;
	frag_color = vec4(text_color, 1.0 * alpha);
	// frag_color = vec4(1.0, 1.0, 1.0, 1.0);
}