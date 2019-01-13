#version 430 core

in VsOut {
    vec2 st;
} fs_in;

layout(binding = 0) uniform sampler2D atlas_sampler;

const vec3 glyph_color = vec3(0.7, 0.1, 0.2);
const vec3 glow_color = vec3(0.95, 0.95, 0.95);

out vec4 color;

void main() {
    // Get the distance value
    const float d = texture(atlas_sampler, fs_in.st).x;

    float smooth_width = fwidth(d);
    float mu = smoothstep(0.5 - smooth_width, 0.5 + smooth_width, d/30.0);

    vec3 rgb = mix(glyph_color, glow_color, mu);
    float alpha = smoothstep(0.5, 0.6, sqrt(d));

    color = vec4(rgb, alpha);
}
