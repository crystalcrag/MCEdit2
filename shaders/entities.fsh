/*
 * fragment shader for entities
 */
#version 430

flat in uint flags;

out vec4  color;
in  vec2  texcoord;
in  float skyLight;
in  float blockLight;

layout (binding=0) uniform sampler2D blocksTex;
layout (binding=1) uniform sampler2D entitiesTex;

void main(void)
{
	if ((flags & 2) > 0)
		color = texture(entitiesTex, texcoord);
	else
		color = texture(blocksTex, texcoord);

	if (color.a < 0.004)
		discard;

	float sky = 0.9 * skyLight * skyLight + 0.1; if (sky < 0) sky = 0;
	float block = blockLight * blockLight * (1 - sky);
	color *= vec4(sky, sky, sky, 1) + vec4(1.5 * block, 1.2 * block, 1 * block, 0);

	if ((flags & 1) > 0)
		color = mix(color, vec4(1,1,1,1), 0.5);
}
