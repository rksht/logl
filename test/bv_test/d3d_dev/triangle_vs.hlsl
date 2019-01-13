cbuffer cb_per_camera : register(b0) {
    matrix view     : packoffset(c0);
    matrix proj     : packoffset(c4);
};

struct VSInput {
    float4 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0; 
};

struct VSOutput {
    float4 pos_clip     : SV_POSITION;
    float3 pos_w        : POSITION;
    float3 normal       : NORMAL;
    float2 texcoord     : TEXCOORD0;
};

VSOutput VS_main(VSInput vin) {
    VSOutput vout;
    vout.pos_clip = mul(mul(proj, view), vin.position);
    // vout.pos_clip = float4(vin.position.xyz, 1.0f);
    vout.pos_w = vin.position.xyz;
    vout.normal = mul(view, float4(vin.normal, 0.0)).xyz;
    vout.texcoord = vin.texcoord;
    return vout;
}
