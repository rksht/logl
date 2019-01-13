#version 430


// world rotate is a uniform matrix that rotates every point (of all
// instances) about an axis.

uniform mat4 view, proj;
uniform vec4 world_rot; // Rotation in world frame, as a quaternion

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 inst_color;
layout(location = 2) in vec4 inst_scale_trans; // scale in object frame, translation to world frame
layout(location = 3) in vec4 inst_orientation_ob; // rotation in object frame

out VO {
	vec3 color;
} vo;

/*

inline fo::Vector3 apply_versor(const fo::Quaternion &q, const fo::Vector3 &v) {
    auto q_vec = fo::Vector3{q.x, q.y, q.z};
    auto v_0 = mul_scalar_vec(q.w * q.w - square_magnitude(q_vec), v);
    auto v_1 = mul_scalar_vec(2 * q.w, cross(q_vec, v));
    auto v_2 = mul_scalar_vec(2 * dot(q_vec, v), q_vec);
    return add_vec_vec(v_0, add_vec_vec(v_1, v_2));
}

*/

vec3 apply_versor(vec4 q, vec3 v) {
	vec3 q_vec = q.xyz;
	vec3 v_0 = (q.w * q.w - dot(q_vec, q_vec)) * v;
	vec3 v_1 = (2 * q.w) * cross(q_vec, v);
	vec3 v_2 = (2 * dot(q_vec, v)) * q_vec;
	return v_0 + v_1 + v_2;
}

void main() {
	vec3 pos_wor = apply_versor(world_rot,
								(inst_scale_trans.x * apply_versor(inst_orientation_ob, pos)) + inst_scale_trans.yzw);
	gl_Position = proj * view * vec4(pos_wor, 1.0);
	vo.color = inst_color;
}
