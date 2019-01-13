cbuffer cb_per_camera : register(b0)
{
	matrix view : packoffset(c0);
	matrix proj : packoffset(c4);
};

struct VSInput {
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct VSOutput {
	float4 pos_clip : SV_POSITION;
	float3 pos_w : POSITION_WRT_WORLD;
	float3 pos_v : POSITION_WRT_VIEW;
	float3 normal : NORMAL;
};

VSOutput VS_main(VSInput vin)
{
	VSOutput vout;

	float4 pos_wrt_world = float4(vin.position, 1.0);

	vout.pos_v = mul(view, pos_wrt_world).xyz;
	vout.pos_w = vin.position.xyz;
	vout.pos_clip = mul(proj, float4(vout.pos_v, 1.0));
	// vout.normal = mul(view, float4(vin.normal, 0.0)).xyz; // Assuming orthogonal view matrix

	vout.normal = mul(view, float4(vin.normal, 0.0)).xyz; // Assuming orthogonal view matrix
	return vout;
}
