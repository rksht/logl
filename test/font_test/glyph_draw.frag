#version 430 core

const vec3 glyph_color = vec3(0.7, 0.1, 0.2);
const vec3 glow_color = vec3(0.95, 0.95, 0.95);

layout(binding = 0) uniform sampler2D glyph_texture_sampler;

in GlyphVsOut {
    vec2 st;
    vec2 pos;
} fs_in;

out vec4 frag_color;

vec4 sample_df() {
    // Get the distance value
    const float d = texture(glyph_texture_sampler, fs_in.st).x;

    float smooth_width = fwidth(d);
    float mu = smoothstep(0.5 - smooth_width, 0.5 + smooth_width, d / 30.0);

    vec3 rgb = mix(glyph_color, glow_color, mu);
    float alpha = smoothstep(0.5, 0.6, sqrt(d));

    #if 0
    if (d < 0.0) {
        return vec4(1.0, 0.0, 0.0, 1.0);
    } else {
        return vec4(1.0);
    }
    #endif
    // return vec4(vec3(clamp(d * 3 + 128, 0, 255)), 1.0);
    return vec4(rgb, alpha);
}

void main() {
    // frag_color = vec4(1.0, 0.0, 1.0, 1.0);
    // frag_color = vec4(fs_in.pos, 0.0, 1.0);
    frag_color = sample_df();
    // frag_color = vec4(fs_in.st, 1.0, 1.0);
#if 0
    float f = fs_in.st.x / 3;
    frag_color = vec4(0.0, 0.0, 0.0, 1.0);
    if (0.0 <= f && f < 0.25) {
    	frag_color.r = 1.0;
    }
    if (0.25 <= f && f < 0.50) {
    	frag_color.g = 1.0;
    }
    if (0.50 <= f && f < 0.75) {
    	frag_color.b = 1.0;
    }
    if (0.75 <= f && f <= 1.0) {
    	frag_color = vec4(1.0);
    }
#endif
}
