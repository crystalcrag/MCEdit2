/*
 * decals.vsh: handle texture printed on top of terrain, which includes: signs and maps (item frame).
 */
#version 430

/* stb_include.h extension */
#include "uniformBlock.glsl"

layout (location=0) in vec4 position;

flat out float shade;
flat out uint  colorDecal;
flat out uint  selected;
     out vec2  tex;

void main(void)
{
	gl_Position = projMatrix * mvMatrix * vec4(position.xyz, 1);

	/*
	 * position.w encodes:
	 * - bit0~3:   skylight value [0-15]
	 * - bit4~7:   blocklight value [0-15]
	 * - bit8:     color decal
	 * - bit9:     selected
	 * - bit10~13: x coord [0-15] * 128 (in px)
	 * - bit14~18: y coord [0-31] * 64 (in px)
	 */
	int slot  = int(position.w);
	int sky   = bitfieldExtract(slot, 4, 4);
	int light = bitfieldExtract(slot, 0, 4);

	colorDecal = bitfieldExtract(slot, 8, 1);
	selected   = bitfieldExtract(slot, 9, 1);
	tex        = vec2(float(bitfieldExtract(slot, 10, 4)) * 0.125, float(slot >> 14) * 0.0625);
	shade      = float(max(sky, light)) * 0.0625;
}
