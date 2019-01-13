// This pixel shader writes to an rtv that

struct PSInput {
	float4 pos_clip : SV_POSITION;
	float3 pos_w : POSITION_WRT_WORLD;
	float3 pos_v : POSITION_WRT_VIEW;
	float3 normal : NORMAL;
};

struct PSOutput {
    float4 normal_and_depth : SV_TARGET0;
};

PSOutput PS_main(PSInput pin)
{
    PSOutput o;

    o.normal_and_depth = float4(normalize(pin.normal), pin.pos_v.z);
    return o;
}
