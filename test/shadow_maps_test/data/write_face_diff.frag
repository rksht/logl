#version 430 core

layout(location = 0) out vec2 depth_val;

in VsOut {
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    // vec3 bitangent_w;
    vec2 st;
} fs_in;

void main() {
	float sign = gl_FrontFacing? 1.0 : -1.0;
	depth_val.x = sign * gl_FragCoord.z;

	vec3 n = normalize(fs_in.normal_w);
    vec3 p = fs_in.pos_w;
    vec3 i = normalize(p);
    float cos_theta = abs(dot(i, n));
    float fresnel = pow(1.0 - cos_theta, 4.0);

    depth_val.y = fresnel;

	// depth_val = sin(gl_FragCoord.x / 1360.0);
	// depth_val = 1.0;
}
