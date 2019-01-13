// This pixel shader writes to an rtv that 

struct PSInput {
  float4 pos_clip : SV_POSITION;
  float3 pos_w : POSITION;
  float3 normal : NORMAL;
};


float4 PS_main(PSInput pin) : SV_TARGET
{
  float f = 0.0f;
  return float4(pin.normal, 1.0);
}
