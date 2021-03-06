/*
 * decals.vsh: handle texture printed on top of terrain, which includes: signs and maps (item frame).
 */
#version 430

/* stb_include.h extension */
#include "uniformBlock.glsl"

layout (location=0) in vec4 position;

flat out uint  colorDecal;
flat out uint  selected;
     out vec2  tex;
     out float skyLight;
     out float blockLight;

void main(void)
{
	gl_Position = MVP * vec4(position.xyz, 1);

	/*
	 * position.w encodes:
	 * - bit0~3:   skylight value [0-15]
	 * - bit4~7:   blocklight value [0-15]
	 * - bit8:     color decal
	 * - bit9:     selected
	 * - bit10~13: x coord [0-15] * 128 (in px)
	 * - bit14~18: y coord [0-31] * 64 (in px)
	 */
	uint slot = uint(position.w);

	colorDecal = bitfieldExtract(slot, 8, 1);
	selected   = bitfieldExtract(slot, 9, 1);
	tex        = vec2(float(bitfieldExtract(slot, 10, 4)) * 0.125, float(slot >> 14) * 0.0625);
	skyLight   = float(bitfieldExtract(slot, 4, 4)) * 0.0625;
	blockLight = float(bitfieldExtract(slot, 0, 4)) * 0.0625;
}
