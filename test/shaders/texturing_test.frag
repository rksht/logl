#version 410

in VertOutputBlock {
	vec3 pos_eye;
	vec3 normal_eye;
	vec2 texcoord;
} vs_out;


// Yeah, just using uniforms without any block. It's simpler that way so shut
// up.
uniform sampler2D basic_texture;
uniform vec3 reflect_spec;
uniform vec3 reflect_diff;
uniform vec3 reflect_amb;
uniform mat4 view_mat;

out vec4 color;

// fixed point light properties
vec3 light_position_world  = vec3 (0.0, 0.0, 4.0);
vec3 E_spec = vec3(1.0, 1.0, 1.0);  // white specular colour
vec3 E_diff = vec3(0.2, 0.2, 0.2);  // dull white diffuse light colour
vec3 E_amb = vec3(0.2, 0.2, 0.2);   // grey ambient colour
float specular_exponent = 100.0;    // specular 'power'

void main() {
	vec4 texel = texture(basic_texture, vs_out.texcoord);
	// color = texel;
	// color = vec4(vs_out.texcoord.x, 0.0, vs_out.texcoord.y, 1);

	// ambient intensity
    vec3 Ia = reflect_amb * E_amb;

    // diffuse intensity
    // raise light position to eye space
    vec3 light_pos_eye = vec3(view_mat * vec4 (light_position_world, 1.0));
    vec3 point_to_light_eye = normalize(light_pos_eye - vs_out.pos_eye);
    float dot_prod = dot(point_to_light_eye, vs_out.normal_eye);
    dot_prod = max(dot_prod, 0.0);
    vec3 Id = reflect_diff * E_diff * dot_prod; // final diffuse intensity

    // specular intensity
    vec3 point_to_eye_eye = normalize(-vs_out.pos_eye);

    // blinn
    vec3 half_vec_eye = normalize(point_to_eye_eye + point_to_light_eye);
    float dot_prod_specular = max(dot(half_vec_eye, vs_out.normal_eye), 0.0);
    float specular_factor = pow(dot_prod_specular, specular_exponent);

    vec3 Is = reflect_spec * E_spec * specular_factor; // final specular intensity

    // final colour
    color = vec4(Is + Id + Ia, 1.0) * texel;
}