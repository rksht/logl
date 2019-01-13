#version 430 core

/* __macro__

POS_AND_NORMAL_ATTACHMENT = int
NORMAL_MAP_BINDING = int

*/

in VsOut {
    vec2 st;
    vec3 position_world;
    vec3 tangent_world;
    vec3 bitangent_world;
    vec3 normal_world;
} fs_in;

layout(location = POS_AND_NORMAL_ATTACHMENT) out uvec3 pos_and_normal;

layout(binding = NORMAL_MAP_BINDING) uniform sampler2D normal_sampler;

layout(binding = NORMAL_MAP_BINDING) uniform sampler2D other_sampler;

void main() {
    vec3 sampled_normal = (texture(normal_sampler, fs_in.st).xyz) * 2.0 - 1.0;

    mat3 TBN =
        mat3(normalize(fs_in.tangent_world), normalize(fs_in.bitangent_world), normalize(fs_in.normal_world));

    sampled_normal = TBN * normalize(sampled_normal); // Take it to world space

    uvec3 pn = uvec3(0, 0, 0);

    pn.x = packHalf2x16(fs_in.position_world.xy);
    pn.y = packHalf2x16(vec2(fs_in.position_world.z, sampled_normal.x));
    pn.z = packHalf2x16(sampled_normal.yz);

    pos_and_normal = pn;
}
