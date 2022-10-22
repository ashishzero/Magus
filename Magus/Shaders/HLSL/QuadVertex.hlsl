#include "QuadLayout.hlsli"

cbuffer constants : register(b0) {
	float4x4 Transform;
}

Vertex_Output main(Vertex_Input vertex) {
	Vertex_Output ouput;
	ouput.Position = mul(Transform, float4(vertex.Position, 1.0f));
	ouput.TexCoord = vertex.TexCoord;
	ouput.Color    = vertex.Color;
	return ouput;
}
