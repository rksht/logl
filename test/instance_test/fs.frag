#version 410 core

in float opacity;
uniform sampler2D tex;
out vec4 frag_color;

void main() {
    vec4 texel = texture(tex, gl_PointCoord);
    frag_color = vec4(texel.rgb, texel.a * opacity);
}

// -- rem - gl_PointCoord is always 0.0, 0.0 ??
