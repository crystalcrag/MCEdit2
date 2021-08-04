/*
 * fragment shader for selection
 */
#version 430

out vec4 color;
in  vec2 tex;
flat in int selType;

layout (binding=0) uniform sampler2D blockTex;

void main(void)
{
	switch (selType>>2) {
	default: color = texture(blockTex, tex); break;
	/* simulate a repeatable texture: blocktTex has repeat param set to clamp */
	case 3:  color = texture(blockTex, vec2(mod(tex.x, 0.0625), mod(tex.y, 0.03125) + 0.46875));
	}
	switch (selType&3) {
	case 1: color.a *= 0.5; break; /* hidden edges: less opaque */
	case 2: color.a *= 0.25;       /* hidden surface */
	}
}
