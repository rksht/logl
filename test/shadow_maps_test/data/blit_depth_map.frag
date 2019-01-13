#version 430 core

#ifndef DEPTH_TEXTURE_UNIT
#define DEPTH_TEXTURE_UNIT 0
#endif

layout(binding = DEPTH_TEXTURE_UNIT) uniform sampler2D depth_sampler;

in VsOut {
	vec3 pos_w;
} fs_in;

out vec4 fc;

void main() {
    const float factor = 0.5; // Factor to reduce the depth
    const vec2 tcoord = (fs_in.pos_w.xy + 1.0f) / 2.0;
    float depth = texture(depth_sampler, tcoord).r;
    // depth = clamp(depth 0.0, 0.3);

    fc = vec4(depth, depth, depth, 1.0);
}
