float3x3 mat3_from_columns(float3 c0, float3 c1, float3 c2)
{
	// clang-format off
    return float3x3(c0.x, c1.x, c2.x,
                    c0.y, c1.y, c2.y,
                    c0.z, c1.z, c2.z);
	// clang-format on
}

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

cbuffer cb_per_camera : register(b1)
{
	matrix view : packoffset(c0);
	matrix proj : packoffset(c4);
};

struct PSInput {
	float4 pos_clip : SV_POSITION;
	float2 tex : TEXCOORD_OUT;
	float2 near_plane_point : NEAR_PLANE_POINT;
};

// Textures read from in the ssao construction pass
Texture2D<float4> normals_texture : register(t0);
Texture2D<float> depth_texture : register(t1);
Texture2D<float4> rnd_xy_vector_texture : register(t2);

SamplerState point_sampler : register(s0);
SamplerState random_directions_sampler : register(s1);

float frag_z_wrt_view_space(float2 tex)
{
	const float zrange = g_far_z - g_near_z;
	const float A = proj[2][2];
	const float B = proj[2][3];
	float frag_z = depth_texture.Sample(point_sampler, tex).r;
	frag_z = B / (frag_z - A);

	return frag_z;
}

float3 frag_pos_wrt_view_space(float2 near_plane_point, float2 tex)
{
	float frag_z = frag_z_wrt_view_space(tex);

	// Get view space xy of the fragment using similar triangles argument.
	float3 pos = float3(near_plane_point * frag_z / g_near_z, frag_z);
	return pos;
}

struct PSDebugOutput {
	float4 position : SV_TARGET0;
};

struct PS_AO_Output {
	float occlusion_factor : SV_TARGET0;
};

PSDebugOutput PS_main(PSInput pin)
{
	PSDebugOutput o;

	float3 frag_pos = frag_pos_wrt_view_space(pin.near_plane_point, pin.tex);
	float dist = length(frag_pos);

	// dist /= 1.0;

	dist /= 10.0;
	o.position = float4(dist, dist, dist, 1.0);

	float fz = frag_z_wrt_view_space(pin.near_plane_point, pin.tex);

	fz /= 10.0;

	// o.position = float4(fz, fz, fz * dist, 1.0);

	o.position = float4(1, 1, 1, 1);

	return o;
}

PS_AO_Output PS_ao_main(PSInput pin)
{
	PS_AO_Output o;

	// We want to sample the random texture as if it's tiled repeated to fill
	// out the size of the normals texture.
	const float2 repeat_factor = g_scene_textures_wh / g_rnd_xy_texture_wh;
	// const float2 repeat_factor = g_scene_textures_wh;
	// const float2 repeat_factor = float2(1, 1);
	// const float2 repeat_factor = float2(4, 4);

	// Sample the textures here
	float3 normal = normals_texture.Sample(point_sampler, pin.tex).rgb;
	float4 frag_pos = float4(frag_pos_wrt_view_space(pin.near_plane_point, pin.tex), 1.0);
	float3 rnd_xy =
		float3(rnd_xy_vector_texture.Sample(random_directions_sampler, repeat_factor * pin.tex).xy, 0.0);

	normal = normalize(normal);
	rnd_xy.xy = rnd_xy.xy * 2.0 - 1.0;

	// This random vector is used to construct a basis such that the local
	// hemisphere aligns with the normal.
	float3 tangent = normalize(rnd_xy - dot(rnd_xy, normal) * normal);
	float3 bitangent = normalize(cross(normal, tangent));
	// float3 bitangent = -normalize(cross(normal, tangent));

	float3x3 to_view_space = mat3_from_columns(tangent, bitangent, normal);
	// float3x3 to_view_space = float3x3(tangent, bitangent, normal);

	float occlusion_factor = 0.0;

	// Consider each sample direction
	for (uint i = 0; i < DIRECTION_SAMPLES_ARRAY_LENGTH; ++i) {
		// Get the sample direction to view space
		float3 d = mul(to_view_space, g_direction_samples[i].xyz);

		// Get the view space position at this offset
		d = frag_pos.xyz + max_z_offset * d;

		// Project to clip, then NDC.
		float4 offset_ndc = mul(proj, float4(d, 1.0));
		offset_ndc.xyz /= offset_ndc.w;

		// NDC xy to texture coordinates
		offset_ndc.x = offset_ndc.x * 0.5 + 0.5;
		offset_ndc.y = offset_ndc.y * -0.5 + 0.5;

		// Get the actual depth that got rendered at this location of the screen.
		float actual_samples_z = frag_z_wrt_view_space(pin.near_plane_point, offset_ndc.xy);

		// How far is the sampled point from the fragment along view space z. Out
		// of range amounts to no occlusion at all.
		float in_range_along_z = abs(actual_samples_z - frag_pos.z) < max_z_offset ? 1.0 : 0.0;

		// If the actual sample is in front of the
		occlusion_factor += (actual_samples_z <= d.z ? 1.0 : 0.0) * in_range_along_z;
	}

	occlusion_factor = occlusion_factor / DIRECTION_SAMPLES_ARRAY_LENGTH;

	o.occlusion_factor = occlusion_factor;

	return o;
}
