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

cbuffer SSAOBlurParamsCB : register(b2) { uint g_is_horizontal_or_vertical; };

Texture2D<float4> normals_texture : register(t0);
Texture2D<float> depth_texture : register(t1);
Texture2D<float> occlusion_factor_texture : register(t2);

SamplerState point_sampler : register(s0);

float frag_z_wrt_view_space(float2 near_plane_point, float2 tex)
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
	float frag_z = frag_z_wrt_view_space(near_plane_point, tex);

	// Get view space xy of the fragment using similar triangles argument.
	float3 pos = float3(near_plane_point * frag_z / g_near_z, frag_z);
	return pos;
}

struct PSInput {
	float4 pos_clip : SV_POSITION;
	float2 tex : TEXCOORD_OUT;
	float2 near_plane_point : NEAR_PLANE_POINT;
};

struct PSOutput {
	float occlusion_factor : SV_TARGET0;
};

PSOutput PS_main(PSInput pin)
{
	// Unpack the weights into array of floats.
	float blur_weights[SSAO_BLUR_KERNEL_SIZE] = { g_blur_weights[0].x, g_blur_weights[0].y, g_blur_weights[0].z,
																								g_blur_weights[0].w, g_blur_weights[1].x, g_blur_weights[1].y,
																								g_blur_weights[1].z, g_blur_weights[1].w, g_blur_weights[2].x,
																								g_blur_weights[2].y, g_blur_weights[2].z };

	const int blur_radius = SSAO_BLUR_KERNEL_SIZE / 2;

	float2 offset_per_texel;

	if (g_is_horizontal_or_vertical == 0) {
		offset_per_texel = float2(1.0 / g_occlusion_texture_wh.x, 0.0);
	} else {
		offset_per_texel = float2(0.0, 1.0 / g_occlusion_texture_wh.y);
	}

	float3 center_normal = normals_texture.Sample(point_sampler, pin.tex).xyz;
	float center_depth = frag_z_wrt_view_space(pin.near_plane_point, pin.tex);

	float sum_of_considered_weights = blur_weights[blur_radius];

	float ff = occlusion_factor_texture.Sample(point_sampler, pin.tex).r;

	if (false) {
		PSOutput o;

		if (ff >= 0.2) {
			o.occlusion_factor = 1.0;
		} else {
			o.occlusion_factor = 0.0;
		}
		return o;
	}

	float blurred_factor =
		blur_weights[blur_radius] * occlusion_factor_texture.Sample(point_sampler, pin.tex).r;

	for (int i = -blur_radius; i <= blur_radius; ++i) {
		if (i == 0) {
			continue;
		}

		float2 neighbor_uv = pin.tex + (float)i * offset_per_texel;

		float3 neighbor_normal = normals_texture.Sample(point_sampler, neighbor_uv).xyz;
		float neighbor_depth = frag_z_wrt_view_space(pin.near_plane_point, neighbor_uv);

		// Don't include this sample in the blur if both the normal and depth are
		// too close to the normal and depth of the center sample

// Macro enable this to see how the blur looks if we don't preserve edges.
#if 1
		if (dot(neighbor_normal, center_normal) >= 0.8f && abs(center_depth - neighbor_depth) <= 0.2) {
			float weight = blur_weights[i + blur_radius];

			blurred_factor += weight * occlusion_factor_texture.Sample(point_sampler, neighbor_uv).r;

			sum_of_considered_weights += weight;
		}

#else
		float weight = blur_weights[i + blur_radius];

		blurred_factor += weight * occlusion_factor_texture.Sample(point_sampler, pin.tex).r;

		sum_of_considered_weights += weight;

#endif
	}

	PSOutput o;
	o.occlusion_factor = blurred_factor / sum_of_considered_weights;
	// o.occlusion_factor = blurred_factor;

	return o;
}
