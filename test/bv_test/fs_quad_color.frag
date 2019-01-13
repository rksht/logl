#version 430 core

in VsOut {
    vec2 uv;
} fin;

out vec4 fc;

void main() {
    fc = vec4(fin.uv.x, 0.0, fin.uv.y, 1.0);
}
