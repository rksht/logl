#version 430 core

layout(binding = 1) uniform sampler2DMS depth_map_sampler;
layout(binding = 2) uniform sampler2DMS color_sampler;

in VsOut { vec2 st; }
fs_in;

out vec4 fc;

const float NEAR = 0.1;
const float FAR = 1000.0;

struct Eye {
    vec4 orientation;
    vec3 position;
};

layout(binding = 0, std140) uniform ViewProj {
    mat4 view;
    mat4 proj;
    Eye eye_world;
}
viewproj;

const float E_1 = -1.0;

void main() {
    // float d = texture(depth_map_sampler, fs_in.st).r;
    float d = texelFetch(depth_map_sampler, ivec2(gl_FragCoord.xy), gl_SampleID).r;
    fc = vec4(d, d, d, 1.0);
}
