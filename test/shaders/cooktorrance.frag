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
// Derive using refractive index eta. F_0 = ((eta - 1) / (eta + 1))**2
vec3 fresnel_0 = vec3(1.0, 1.0, 1.0);

float beckman_distrib(float x) {
	float sqr_x = x * x;
	float sqr_m = m * m;
	float expo = exp((sqr_x - 1) / (sqr_m * sqr_x));
	return 1.0 / (4 * sqr_m * sqr_x * sqr_x);
}

// Schick's approx fresnel term
vec3 fresnel_term(vec3 fresnel_normal, float dot_half_view) {
	return fresnel_normal + pow(1 - dot_half_view, 5) * (1 - fresnel_normal);
}

float geometric_atten_term(float dot_normal_half, float dot_normal_view,
						   float dot_normal_light, float dot_light_half) {
	return min(1, min(2 * dot_normal_half * dot_normal_view / dot_light_half,
					  2 * dot_normal_half * dot_normal_light / dot_light_half));
}

vec3 cook_torrance_rho(vec3 view_dir, vec3 light_dir, vec3 normal) {
	vec3 halfway = normalize(light_dir + view_dir);
	float dot_normal_view = max(dot(normal, view_dir), 0.0);
	float dot_normal_light = max(dot(normal, light_dir), 0.0);
	float dot_normal_half = max(dot(normal, halfway), 0.0);
	float dot_light_half = max(dot(light_dir, halfway), 0.0);

	float d = beckman_distrib(dot_normal_half);
	vec3 f = fresnel_term(reflect_spec, dot_normal_half);
	float g = geometric_atten_term(dot_normal_half, dot_normal_view, dot_normal_light, dot_light_half);
	return f * g * d / max(3.14159265 * dot_normal_view * dot_normal_light, 0.000001);
}

vec3 E_diff = vec3(0.2, 0.2, 0.2);
vec3 E_spec = vec3(0.2, 0.2, 0.2);
vec3 E_amb = vec3(0.01, 0.01, 0.01);

vec3 diffuse_color(float dot_normal_light) {
	// vec3 E_diff = vec3(0.2, 0.2, 0.2);
	vec3 reflect_diff = texture(color_sampler, vo.st).xyz;
	return reflect_diff * E_diff * dot_normal_light;
}

void main() {
	vec3 normal_tan = normalize(texture(normal_sampler, vo.st).xyz * 2.0 - 1.0);

	// Tangent frame to eye frame
	mat3 tan_to_eye = mat3(normalize(vo.tangent_eye),
						   normalize(vo.bitangent_eye),
						   normalize(vo.normal_eye));

	vec3 normal_eye = normalize(tan_to_eye * normal_tan);
	vec3 light_dir_eye = normalize(vec3(world_to_eye * vec4(light_pos_world, 1.0) - vec4(vo.pos_eye, 1.0)));
	vec3 view_dir_eye = normalize(-vo.pos_eye);

	vec3 rho = cook_torrance_rho(view_dir_eye, light_dir_eye, normal_eye);
	float dot_normal_light = max(dot(normal_eye, light_dir_eye), 0.0);

	vec3 spec = rho * E_spec * dot_normal_light;
	// vec3 diff = diffuse_color(dot_normal_light);
	vec3 diff = vec3(0);

	frag_color = vec4(spec + diff, 1.0);
	// frag_color =  vec4(texture(color_sampler, vo.st).xyz, 1);
}