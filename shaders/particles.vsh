#version 430

/* note: include is an extension provided by stb_include.h */
#include "uniformBlock.glsl"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;

flat out int texbase;
flat out float skyLight;
flat out float blockLight;
flat out float size;

void main(void)
{
	/* encode size and block/sky light */
	int info = int(tex.y);
	texbase    = int(tex.x);
	size       = float(info & 255);
	blockLight = float((info >> 8)  & 15) / 15.;
	skyLight   = float((info >> 12) & 15) / 15.;
	gl_Position = projMatrix * mvMatrix * vec4(position, 1);
}
