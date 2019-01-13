#version 430 core

layout(binding = STRUCTURED_TEXTURE_BINDING) uniform sampler2D structured_texture;

out vec4 fc;

in VsOut {
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    vec2 st;
} fs_in;

void main() {
	fc = texture(structured_texture, fs_in.st).rgba;
}
