#version 450

#define USE_ARRAY_OF_TEXTURE 1

#if USE_ARRAY_OF_TEXTURE
layout (binding = 1) uniform sampler2D samplerArray[12];
#else
layout (binding = 1) uniform sampler2DArray samplerArray;
#endif


layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
#if USE_ARRAY_OF_TEXTURE
layout (location = 2) in vec2 inUV;
#else
layout (location = 2) in vec3 inUV;
#endif
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec3 inLightVec;
#if USE_ARRAY_OF_TEXTURE
layout (location = 5) in int instanceTexIndex;
#endif

layout (location = 0) out vec4 outFragColor;

void main()
{
#if USE_ARRAY_OF_TEXTURE
	vec4 color = texture(samplerArray[instanceTexIndex], inUV);
#else
	vec4 color = texture(samplerArray, inUV);
#endif

	if (color.a < 0.5)
	{
		discard;
	}

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 ambient = vec3(0.65);
	vec3 diffuse = max(dot(N, L), 0.0) * inColor;
	outFragColor = vec4((ambient + diffuse) * color.rgb, 1.0);
}
