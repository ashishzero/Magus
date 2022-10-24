[[Blend0=true, Depth=true, Fill=solid, Cull=none, Scissor=true, FrontFace=cw, Filter=linear]]

struct Vertex_Input {
	float3 Position : POSITION;
	float2 TexCoord : TEXCOORD;
	float4 Color    : COLOR;
};

struct Vertex_Output {
	float2 TexCoord : TEXCOORD;
	float4 Color    : COLOR;
	float4 Position : SV_Position;
};

cbuffer constants : register(b0) {
	row_major float4x4 Transform;
}

Vertex_Output VertexMain(Vertex_Input vertex) {
	Vertex_Output ouput;
	ouput.Position = mul(Transform, float4(vertex.Position, 1.0f));
	ouput.TexCoord = vertex.TexCoord;
	ouput.Color = vertex.Color;
	return ouput;
}

Texture2D    TexImage : register(t0);
SamplerState Sampler  : register(s0);

float4 PixelMain(Vertex_Output input) : SV_Target{
	return TexImage.Sample(Sampler, input.TexCoord) * input.Color;
}
