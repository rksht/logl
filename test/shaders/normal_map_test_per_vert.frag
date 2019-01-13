#version 410

uniform vec3 reflect_spec, reflect_diff, reflect_amb;
uniform sampler2D color_sampler, normal_sampler;

in VertOut {
	vec3 view_dir_tan;
	vec3 light_dir_tan;
	vec2 st;
} vo;

out vec4 frag_color;

vec3 E_spec = vec3(1.0, 1.0, 1.0);  // white specular colour
vec3 E_diff = vec3(0.2, 0.2, 0.2);  // dull white diffuse light colour
vec3 E_amb = vec3(0.2, 0.2, 0.2);   // grey ambient colour
float spec_exponent = 100.0;    	// specular 'power'

void main() {
	vec3 normal_tan = texture(normal_sampler, vo.st).xyz;

	vec3 Ia = reflect_amb * E_amb;

	vec3 Id = reflect_diff * E_diff	* max(dot(normal_tan, vo.light_dir_tan), 0.0);

	vec3 half_vec_tan = normalize(vo.view_dir_tan + vo.light_dir_tan);
	float spec_factor = pow(max(dot(half_vec_tan, normal_tan), 0.0), spec_exponent);
	vec3 Is = reflect_spec * spec_factor * E_spec;

	frag_color = vec4(Is + Id + Ia, 1.0);
}