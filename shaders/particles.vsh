#version 430

/* note: include is an extension provided by stb_include.h */
#include "uniformBlock.glsl"

/*
 * info layout:
 * - info.x[bit0  ~  5] : particle type (6bits)
 * - info.x[bit6  ~  9] : size (4bits)
 * - info.x[bit10 ~ 18] : U texture coord (9bits, [0~511])
 * - info.x[bit19 - 28] : V texture coord (10bits, [0~1023])
 * - info.y[bit0  -  3] : block light
 * - info.y[bit4  -  7] : sky light
 */
layout (location=0) in  vec3 position;
layout (location=1) in ivec2 info;

flat out float size;
flat out int   texbase;
flat out int   type;
flat out int   light;
flat out int   color;

void main(void)
{
	type    = info.x & 63;
	size    = (info.x >> 6) & 15;
	texbase = info.x >> 10;

	switch (type) {
	case 1: // bits
		light = info.y & 0xff;
		break;
	case 2: // smoke
		color = info.y;
		light = 0xf0;
	}
	gl_Position = projMatrix * mvMatrix * vec4(position, 1);
}
