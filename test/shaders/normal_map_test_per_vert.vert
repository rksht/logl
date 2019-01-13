#version 410

uniform mat4 cewo_mat; 		// Object frame to clip frame matrix
uniform vec3 eye_pos_ob; 	// Eye position in object-frame
uniform vec3 light_dir_ob; 	// Test light's direction in object-frame

layout (location = 0) in vec3 vert_pos_ob;		// Object-frame position
layout (location = 1) in vec3 vert_normal_ob; 	// Object-frame normal
layout (location = 2) in vec4 vert_tangent_ob; 	// Object-frame tangent
layout (location = 3) in vec2 vert_st;			// Vertex tex2d coord

out VertOut {
	vec3 view_dir_tan; 	// View direction in tangent-frame
	vec3 light_dir_tan; // Light direction in tangent-frame
	vec2 st;
} vo;

void main() {
	gl_Position = cewo_mat * vec4(vert_pos_ob, 1.0);

	// Calculate bitangent B = (N x T) * T.w. Here, T.w is +1 or -1 depending
	// on the handedness of the tangent-frame
	vec3 vert_bitangent_ob = cross(vert_normal_ob, vert_tangent_ob.xyz) * vert_tangent_ob.w;

	// Calculate vertex's view direction in object-frame
	vec3 view_ob = eye_pos_ob - vert_pos_ob;

	// Transform the view vector V to tangent frame
	vo.view_dir_tan = vec3(
		dot(vert_tangent_ob.xyz, view_ob),
		dot(vert_bitangent_ob, view_ob),
		dot(vert_normal_ob, view_ob)
	);

	// Transform light direction in tangent-frame
	vo.light_dir_tan = vec3(
		dot(vert_tangent_ob.xyz, light_dir_ob),
		dot(vert_bitangent_ob, light_dir_ob),
		dot(vert_normal_ob, light_dir_ob)
	);

	vo.st = vert_st;
}