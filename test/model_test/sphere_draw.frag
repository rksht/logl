#version 410

uniform vec3 sphere_color;

out vec4 frag_color;

void main() {
    frag_color = vec4(sphere_color, 0.2);
}
