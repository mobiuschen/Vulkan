// Copyright 2020 Google LLC
struct VSInput
{
	[[vk::binding(0)]] uint indexOffset;
	[[vk::binding(1)]] uint instanceOffset;
};

struct UBO
{
	float4x4 projection;
	float4x4 modelview;
};

 
struct Instance
{
	float3 pos;
	float pad;
	float3 rot;
	float scale;
};

struct VertexData
{
	float4 pos;
	float3 normal;
	float pad0;
	float2 uv;
	float2 pad1;
	float3 color;
	float pad2;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float3 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
};

StructuredBuffer<Instance> instances : register(t3);
StructuredBuffer<int> instanceTexIndices : register(t4);
StructuredBuffer<VertexData> vertexDatas : register(t5);
StructuredBuffer<uint> indexDatas : register(t6);

VSOutput main(VSInput input, uint VertexIndex : SV_VertexID)
{
	uint vertexIndex = indexDatas[input.indexOffset + VertexIndex];
	uint instanceIndex = input.instanceOffset;

	float4 inputPos = vertexDatas[vertexIndex].pos;
	float3 inputNormal = vertexDatas[vertexIndex].normal;
	float2 inputUV = vertexDatas[vertexIndex].uv;
	float3 inputColor = vertexDatas[vertexIndex].color;

	float3 instancePos = instances[instanceIndex].pos.xyz;
	float3 instanceRot = instances[instanceIndex].rot;
	float instanceScale = instances[instanceIndex].scale;
	int instanceTexIndex = instanceTexIndices[instanceIndex];

	VSOutput output = (VSOutput)0;
	output.Color = inputColor;
	output.UV = float3(inputUV, instanceTexIndex);

	float4x4 mx, my, mz;

	// rotate around x
	float s = sin(instanceRot.x);
	float c = cos(instanceRot.x);

	mx[0] = float4(c, s, 0.0, 0.0);
	mx[1] = float4(-s, c, 0.0, 0.0);
	mx[2] = float4(0.0, 0.0, 1.0, 0.0);
	mx[3] = float4(0.0, 0.0, 0.0, 1.0);

	// rotate around y
	s = sin(instanceRot.y);
	c = cos(instanceRot.y);

	my[0] = float4(c, 0.0, s, 0.0);
	my[1] = float4(0.0, 1.0, 0.0, 0.0);
	my[2] = float4(-s, 0.0, c, 0.0);
	my[3] = float4(0.0, 0.0, 0.0, 1.0);

	// rot around z
	s = sin(instanceRot.z);
	c = cos(instanceRot.z);

	mz[0] = float4(1.0, 0.0, 0.0, 0.0);
	mz[1] = float4(0.0, c, s, 0.0);
	mz[2] = float4(0.0, -s, c, 0.0);
	mz[3] = float4(0.0, 0.0, 0.0, 1.0);

	float4x4 rotMat = mul(mz, mul(my, mx));

	output.Normal = mul((float4x3)rotMat, inputNormal).xyz;


	float4 pos = mul(rotMat, float4((inputPos.xyz * instanceScale) + instancePos, 1.0));

	output.Pos = mul(ubo.projection, mul(ubo.modelview, pos));

	float4 wPos = mul(ubo.modelview, float4(pos.xyz, 1.0));
	float4 lPos = float4(0.0, -5.0, 0.0, 1.0);
	output.LightVec = lPos.xyz - pos.xyz;
	output.ViewVec = -pos.xyz;
	return output;
}
