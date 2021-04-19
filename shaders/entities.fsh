#version 430

/*
 * fragment shader for entities
 */
out vec4 color;
in  vec2 texcoord;
flat in float shade;
flat in int   isBlock;

layout (binding=0) uniform sampler2D blocksTex;
layout (binding=1) uniform sampler2D mobTex;

void main(void)
{
	if (isBlock > 0)
		color = texture(blocksTex, texcoord) * shade;
	else
		color = texture(mobTex, texcoord) * shade;
}
