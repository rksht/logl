Texture2D<float> occlusion_factor_texture : register(t0);
SamplerState linear_sampler : register(s0);

struct PSInput
{
	float4 pos_clip : SV_POSITION;
	float2 tex : TEXCOORD_OUT;
	float2 near_plane_point : NEAR_PLANE_POINT;
};

float4 PS_main(PSInput pin) : SV_TARGET {
	float f = 1.0 - occlusion_factor_texture.Sample(linear_sampler, pin.tex).r;
	return float4(f, f, f, 1.0);
}
