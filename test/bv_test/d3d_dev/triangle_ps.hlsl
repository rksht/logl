struct PSInput {
    float4 pos_clip : SV_POSITION;
    float3 pos_w : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float> random_colors : register(t0);
SamplerState point_sampler : register(s0);

float4 PS_main(PSInput pin) : SV_TARGET {
    // return float4(0.5f * ((pin.pos_w) + 1.0f), 1.0);
    return float4(pin.texcoord.xy, 0.0, 1.0);
    // float f = random_colors.Sample(point_sampler, pin.texcoord.xy).r;
    // return float4(f, f, f, 1.0);
}
