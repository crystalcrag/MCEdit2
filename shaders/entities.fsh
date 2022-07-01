/*
 * fragment shader for entities
 */
#version 430

#include "uniformBlock.glsl"
#include "uniformTexture.glsl"

flat in uint  flags;
     in vec2  texcoord;
     in float fogFactor;
     in float skyLight;
     in float blockLight;

out vec4  color;


void main(void)
{
	if ((flags & 2) > 0)
		color = texture(entitiesTex, texcoord);
	else
		color = texture(blockTex, texcoord);

	if (color.a < 0.004)
		discard;

	float sky = 0.9 * skyLight * skyLight + 0.1; if (sky < 0) sky = 0;
	float block = blockLight * blockLight * (1 - sky);
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);

	if (fogFactor < 1)
	{
		vec4 skyColor = (skyLight > 0 ? texelFetch(skyTex, ivec2(int(gl_FragCoord.x / SCR_WIDTH*255), int(gl_FragCoord.y / SCR_HEIGHT*255)), 0) : vec4(0.1,0.1,0.1,1));
		skyColor.a = 1;
		color = mix(skyColor, color, fogFactor);
	}

	// entity is highlighted
	if ((flags & 1) > 0)
		color = mix(color, vec4(1,1,1,1), 0.5);
}
