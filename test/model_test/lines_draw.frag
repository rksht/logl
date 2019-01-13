#version 410

out vec4 frag_color;

flat in vec3 vo_color;

void main() {
    frag_color = vec4(vo_color.xyz, 1.0);
}
