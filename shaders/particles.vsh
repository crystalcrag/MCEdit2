#version 430

/* note: include is an extension provided by stb_include.h */
#include "uniformBlock.glsl"

/*
 * info layout:
 * - info.x[bit0  ~  8] : U tex coord (9 bits)
 * - info.x[bit9  ~ 18] : V tex coord (10 bits)
 * - info.x[bit19 ~ 26] : skylight + blocklight
 * - info.x[bit27 - 31] : type
 */
layout (location=0) in  vec3 position;
layout (location=1) in ivec3 info;

flat out float size;
flat out uint  patternHI;
flat out uint  patternLO;
flat out int   texbase;
flat out int   type;
flat out int   light;

void main(void)
{
	/* encode size and block/sky light */
	type    = info.x & 63;
	size    = (info.x >> 6) & 15;
	texbase = info.x >> 10;

	switch (type) {
	case 0: // explode particles
		light = info.y & 0xff;
		break;
	case 1: // spark particles
		light = 0xf0;
		patternHI = info.y;
		patternLO = info.z;
	}
	gl_Position = projMatrix * mvMatrix * vec4(position, 1);
}
