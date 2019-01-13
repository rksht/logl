struct Material {
    vec4 diffuse_albedo;
    vec3 fresnel_R0;
    float shininess;
};

vec3 x_point(mat4 m, vec3 p) {
    return (m * vec4(p, 1.0)).xyz;
}

vec3 x_vec(mat4 m, vec3 v) {
    return (m * vec4(v, 0.0)).xyz;
}

struct DirLight {
    vec3 position;
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
