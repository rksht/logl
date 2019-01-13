#version 430 core

struct Material {
    vec4 diffuse_albedo;
    vec3 fresnel_R0;
    float shininess;
};

// Per model  data (and per mesh since only 1 mesh per model)
layout(binding = PER_OBJECT_UBLOCK_BINDING, std140) uniform ublock_PerObject {
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material;
};

out vec4 fc;

void main() {
	fc = object_material.diffuse_albedo;
    // fc = vec4(0.0, 0.0, 0.0, 1.0);
}
