// Copyright 2020 Google LLC

struct VSInput
{
[[vk::location(0)]] float4 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
[[vk::location(3)]] float3 Color : COLOR0;
											   
[[vk::location(4)]] float4 instanceMatRow0 : TEXCOORD1;
[[vk::location(5)]] float4 instanceMatRow1 : TEXCOORD2;
[[vk::location(6)]] float4 instanceMatRow2 : TEXCOORD3;
[[vk::location(7)]] float4 instanceMatRow3 : TEXCOORD4;
[[vk::location(8)]] int instanceTexIndex : TEXCOORD5;
[[vk::location(9)]] int primitiveIndex : TEXCOORD6;
};

struct UBO
{
	float4x4 projection;
	float4x4 modelview;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct PrimitiveData
{
    float4x4 transform;
    float3 pos;
    float cullDistance;
};

StructuredBuffer<PrimitiveData> primitiveData : register(t3);

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Normal : NORMAL0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float3 UV : TEXCOORD0;
[[vk::location(3)]] float3 ViewVec : TEXCOORD1;
[[vk::location(4)]] float3 LightVec : TEXCOORD2;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.Color = input.Color;
	output.UV = float3(input.UV, input.instanceTexIndex);

	float4x4 instanceMat;				  																			
	instanceMat[0] = input.instanceMatRow0;
	instanceMat[1] = input.instanceMatRow1;
	instanceMat[2] = input.instanceMatRow2;
	instanceMat[3] = input.instanceMatRow3;
																		   
	float4x4 primMat = primitiveData[input.primitiveIndex].transform;
	float4x4 ins2PrimMat = mul(primMat, instanceMat);

	output.Normal = mul((float4x3)instanceMat, input.Normal).xyz;	
	// float4 pos = mul(rotMat, float4((input.Pos.xyz * input.instanceScale) + input.instancePos, 1.0));
	float4 pos = mul(ins2PrimMat, float4(input.Pos.xyz, 1.0));
	output.Pos = mul(ubo.projection, mul(ubo.modelview, pos));

	float4 wPos = mul(ubo.modelview, float4(pos.xyz, 1.0));
	float4 lPos = float4(0.0, -5.0, 0.0, 1.0);
	output.LightVec = lPos.xyz - pos.xyz;
	output.ViewVec = -pos.xyz;
	return output;
}
