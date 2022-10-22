#include "QuadLayout.hlsli"

Texture2D    TexImage : register(t0);
SamplerState Sampler  : register(s0);

float4 main(Vertex_Output input) : SV_Target {
	return TexImage.Sample(Sampler, input.TexCoord) * input.Color;
}
