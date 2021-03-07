#version 430

/* note: include is an extension provided by stb_include.h */
#include "uniformBlock.glsl"

layout (location=0) in vec4 position;

out float shade;
out vec2  tex;

void main(void)
{
	gl_Position = projMatrix * mvMatrix * vec4(position.xyz, 1);

	int slot  = int(position.w);
	int sky   = (slot & 0xf0) >> 4;
	int light = (slot & 15);
	slot >>= 8;
	/*
	 * slot encode where texture of sign is:
	 * - bit0~3: x coord * 128 (in px)
	 * - bit4~8: y coord * 64 (in px)
	 * texture is 512 x 512px
	 */
	tex = vec2(float(slot & 15) * 0.125, 1-float(slot >> 4) * 0.0625);
	shade = float(max(sky, light)) * 0.0625;
}
