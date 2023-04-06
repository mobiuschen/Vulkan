// Copyright 2020 Google LLC

struct VSInput
{
[[vk::location(0)]] float4 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 Color : COLOR0;
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

VSOutput main(VSInput input, uint InstanceIndex : SV_InstanceID)
{
	float3 instancePos = instances[InstanceIndex].pos.xyz;
	float3 instanceRot = instances[InstanceIndex].rot;
	float instanceScale = instances[InstanceIndex].scale;
	int instanceTexIndex = instanceTexIndices[InstanceIndex];

	VSOutput output = (VSOutput)0;
	output.Color = input.Color;
	output.UV = float3(input.UV, instanceTexIndex);


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

	output.Normal = mul((float4x3)rotMat, input.Normal).xyz;


	float4 pos = mul(rotMat, float4((input.Pos.xyz * instanceScale) + instancePos, 1.0));

	output.Pos = mul(ubo.projection, mul(ubo.modelview, pos));

	float4 wPos = mul(ubo.modelview, float4(pos.xyz, 1.0));
	float4 lPos = float4(0.0, -5.0, 0.0, 1.0);
	output.LightVec = lPos.xyz - pos.xyz;
	output.ViewVec = -pos.xyz;
	return output;
}
