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
