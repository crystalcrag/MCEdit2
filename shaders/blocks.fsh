/*
 * fragment shader for drawing opaque and translucent blocks.
 */
#version 430 core

#include "uniformBlock.glsl"
#include "uniformTexture.glsl"

out vec4 color;
in vec3  vPoint;
in vec2  tc;
flat in uint rswire;
flat in uint ocsmap;
flat in uint normal;
flat in uint waterFog;
flat in vec2 texStart;

uniform uint underWater;
uniform uint timeMS;

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

	if (rswire > 0) /* rswire: [1-15] == signal strength */
	{
		// use color from terrain to shade wire: coord are located at tile 31x3.5 to 32x3.5
		color *= texture(blockTex, vec2(0.96875 + float(rswire-1) * 0.001953125, 0.0556640625));
	}

	// compute fog contribution
	#if 0
	if (underWater > 0 && waterFog > 0)
	{
		if (fogFactor < 1)
		{
			float factor = clamp(float(underWater >> 8) / 255.0, 0.5, 1.0);
			color = mix(vec4(0.094 * factor, 0.141 * factor, 0.5 * factor, 1), color, fogFactor);
		}
	}
	else if (fogFactor < 1)
	{
		vec4 skyColor = (skyLight > 0 || waterFog > 0 ? texelFetch(skyTex, ivec2(int(gl_FragCoord.x / SCR_WIDTH*255), int(gl_FragCoord.y / SCR_HEIGHT*255)), 0) : vec4(0.1,0.1,0.1,1));
		skyColor.a = 1;
		color = mix(skyColor, color, fogFactor);
	}
	#endif
}
