#version 410

uniform vec3 reflect_spec, reflect_diff, reflect_amb;
uniform sampler2D color_sampler, normal_sampler;
uniform mat4 world_to_eye;
uniform vec3 light_pos_world;

in VertOut {
	vec3 pos_eye;
	vec3 tangent_eye;	// T     	+
	vec3 bitangent_eye;	// B       	+  These 3 column vectors form a tangent-to-eye transform
	vec3 normal_eye;   	// N        +
	vec2 st;
} vo;

out vec4 frag_color;

uniform float m;

vec3 E_amb = vec3(0.0, 0.0, 0.0); // not using

vec3 diffuse_color(vec3 light_dir, vec3 normal) {
	vec3 E_diff = vec3(0.2, 0.2, 0.2);
	vec3 reflect_diff = texture(color_sampler, vo.st).xyz;
	return reflect_diff * E_diff * max(dot(normal, light_dir), 0.0);
}

vec3 specular_color(vec3 light_dir, vec3 view_dir, vec3 normal) {
	float spec_exponent = 50.0;
	vec3 E_spec = vec3(0.2, 0.1, 0.5);

	vec3 half_vec = normalize(light_dir + view_dir);
	float spec_factor = pow(max(dot(half_vec, normal), 0.0), spec_exponent);
	return spec_factor * reflect_spec * E_spec;
}

void main() {
	vec3 normal_tan = normalize(texture(normal_sampler, vo.st).xyz * 2.0 - 1.0);

	// Tangent frame to eye frame
	mat3 tan_to_eye = mat3(normalize(vo.tangent_eye),
						   normalize(vo.bitangent_eye),
						   normalize(vo.normal_eye));

	vec3 normal_eye = tan_to_eye * normal_tan;

	vec3 light_dir_eye = normalize(vec3(world_to_eye * vec4(light_pos_world, 1.0) - vec4(vo.pos_eye, 1.0)));

	vec3 Ia = reflect_amb * E_amb;
	vec3 Id = diffuse_color(light_dir_eye, normal_eye);
	vec3 Is = specular_color(light_dir_eye, -vo.pos_eye, normal_eye);

	frag_color = vec4(Is + Id + Ia, 1.0);
	// frag_color = vec4(normalize(light_pos_world), 1.0);
}