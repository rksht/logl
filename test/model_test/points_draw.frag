#version 420

uniform sampler2D points_texture;

out vec4 frag_color;

void main() {
    frag_color = texture(points_texture, gl_PointCoord);
    // frag_color = vec4(1.0, 1.0, 1.0, 1.0);
}
