/*
 * fragment shader for drawing opaque and translucent blocks.
 */
#version 430 core

#include "uniformBlock.glsl"
#include "uniformTexture.glsl"
#line 8

out vec4 color;
in vec3  vPoint;
in vec2  tc;
flat in uint ocsmap;
flat in uint normal;
flat in uint waterFog;
flat in uint lightingId;
flat in vec2 texStart;
flat in vec3 chunkStart;

uniform uint underWater;
uniform uint timeMS;

#define OFFSET  0.4
const vec3 lightNormals[8] = vec3[8] (
	vec3(0,0,OFFSET),
	vec3(OFFSET,0,0),
	vec3(0,0,-OFFSET),
	vec3(-OFFSET,0,0),
	vec3(0,OFFSET,0),
	vec3(0,-OFFSET,0),
	vec3(0,0,0),
	vec3(0,0,0)
);

void main(void)
{
	if (texStart.x >= 0)
	{
		// greedy meshing
		color = texture(blockTex, vec2(texStart.x + mod(tc.x, 16/512.), texStart.y + mod(tc.y, 16/1024.)));
	}
	else color = texture(blockTex, tc);

	// prevent writing to the depth buffer: easy way to handle opacity for transparent block
	if (color.a < 0.004)
		discard;

	float fogFactor = length(vPoint.xz - camera.xz) / FOG_DISTANCE;

	vec2 light = vec2(0, 1);
	if (fogFactor < 0.6)
	{
		uint slot   = lightingId >> 7;
		vec3 offset = ((vPoint - chunkStart + vec3(1,1,1) + lightNormals[normal&7]).xzy + vec3((slot & 7)*18, ((slot >> 3) & 7)*18, ((slot >> 6)&7)*18)) / 144.0;

		// bindless texture would have been nice if we were part of opengl (sadly, only available as extension)
		switch (lightingId & 127) {
		// 8192 slots, about 100Mb worth of texture data (only allocated on demand)
		case 0:  light = texture(lightBank0,  offset).gr; break;
		case 1:  light = texture(lightBank1,  offset).gr; break;
		case 2:  light = texture(lightBank2,  offset).gr; break;
		case 3:  light = texture(lightBank3,  offset).gr; break;
		case 4:  light = texture(lightBank4,  offset).gr; break;
		case 5:  light = texture(lightBank5,  offset).gr; break;
		case 6:  light = texture(lightBank6,  offset).gr; break;
		case 7:  light = texture(lightBank7,  offset).gr; break;
		case 8:  light = texture(lightBank8,  offset).gr; break;
		case 9:  light = texture(lightBank9,  offset).gr; break;
		case 10: light = texture(lightBank10, offset).gr; break;
		case 11: light = texture(lightBank11, offset).gr; break;
		case 12: light = texture(lightBank12, offset).gr; break;
		case 13: light = texture(lightBank13, offset).gr; break;
		case 14: light = texture(lightBank14, offset).gr; break;
		case 15: light = texture(lightBank15, offset).gr; break;
		case 127: light = vec2(0,0);
		}
	}

	// shading per face
	float texY = (1.0/108.0) + (normal < 6 ? float(normal) : 4) * (18.0/108.0) + light.y * (16.0/108.0);
	color *= vec4(texture(lightShadeTex, vec2(light.x, texY)).rgb, 1);

	// compute fog contribution
	#if 1

	if (underWater > 0 && waterFog > 0)
	{
		fogFactor = 1 - fogFactor * fogFactor;
		if (fogFactor < 1)
		{
			float factor = clamp(float(underWater >> 8) / 255.0, 0.5, 1.0);
			color = mix(vec4(0.094 * factor, 0.141 * factor, 0.5 * factor, 1), color, fogFactor);
		}
		else color = vec4(0.094, 0.141, 0.5, 1);
	}
	else if (fogFactor < 1)
	{
		fogFactor = 1 - fogFactor * fogFactor * fogFactor;
		vec4 skyColor = texelFetch(skyTex, ivec2(int(gl_FragCoord.x / SCR_WIDTH*255), int(gl_FragCoord.y / SCR_HEIGHT*255)), 0);
		skyColor.a = 1;
		color = mix(skyColor, color, fogFactor);
	}
	else discard;
	#endif
}
