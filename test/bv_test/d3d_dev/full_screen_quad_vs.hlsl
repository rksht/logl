// The near_z, far_z, g_tan_half_yfov, g_tan_half_xfov parameters are not needed.
cbuffer SSAOBuildPassCB : register(b0)
{
	float4 g_direction_samples[DIRECTION_SAMPLES_ARRAY_LENGTH];
	float4 g_blur_weights[SSAO_BLUR_KERNEL_XM4_COUNT];

	float g_near_z;
	float g_far_z;
	float g_tan_half_yfov;
	float g_tan_half_xfov;

	float2 g_scene_textures_wh;
	float2 g_rnd_xy_texture_wh;
	float2 g_occlusion_texture_wh;

	float max_z_offset = 0.05f; // Maximum offset along view space z to
	float g_surface_epsilon = 0.05f;
};


struct VSOutput {
	float4 pos_clip : SV_POSITION;
	float2 tex : TEXCOORD_OUT;
	float2 near_plane_point : NEAR_PLANE_POINT;
};

VSOutput VS_main(uint id : SV_VERTEXID)
{
	VSOutput o;

	o.pos_clip.x = (float)(id / 2) * 4.0 - 1.0;
	o.pos_clip.y = (float)(id % 2) * 4.0 - 1.0;
	o.pos_clip.z = 0.0;
	o.pos_clip.w = 1.0;

	o.tex.x = (float)(id / 2) * 2.0;
	o.tex.y = 1.0 - (float)(id % 2) * 2.0;

	o.near_plane_point = o.pos_clip.xy;

	const float2 near_plane_hextent = float2(g_near_z * g_tan_half_xfov, g_near_z * g_tan_half_yfov);
	o.near_plane_point *= near_plane_hextent;

	return o;
}