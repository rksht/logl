#version 430 core

in VsOut {
    vec3 color;
} fs_in;

out vec4 fc;

void main() {
	fc = vec4(fs_in.color, 0.6);
}
