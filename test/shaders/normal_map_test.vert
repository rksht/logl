#version 410

uniform mat4 ob_to_world;
uniform mat4 world_to_eye;
uniform mat4 eye_to_clip;

layout (location = 0) in vec3 vert_pos_ob;		// Object-frame position
layout (location = 1) in vec3 vert_normal_ob; 	// Object-frame normal
layout (location = 2) in vec4 vert_tangent_ob; 	// Object-frame tangent
layout (location = 3) in vec2 vert_st;			// Vertex tex2d coord

out VertOut {
	vec3 pos_eye;
	vec3 tangent_eye;	// T     	+
	vec3 bitangent_eye;	// B       	+  These 3 column vectors form a tangent-to-eye transform
	vec3 normal_eye;   	// N        +
	vec2 st;
} vo;

void main() {
	mat4 cewo_mat = eye_to_clip * world_to_eye * ob_to_world;
	gl_Position = cewo_mat * vec4(vert_pos_ob, 1.0);

	mat4 ob_to_eye = world_to_eye * ob_to_world;
	vo.pos_eye = vec3(ob_to_eye * vec4(vert_pos_ob, 1.0));
	vo.normal_eye = vec3(ob_to_eye * vec4(vert_normal_ob, 0.0));
	vo.tangent_eye = vec3(ob_to_eye * vec4(vert_tangent_ob.xyz, 0.0));

	vec4 bitangent_ob = vec4(cross(vert_normal_ob, vert_tangent_ob.xyz) * vert_tangent_ob.w, 0.0);
	vo.bitangent_eye = vec3(ob_to_eye * bitangent_ob);

	vo.st = vert_st;
}
