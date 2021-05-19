#version 430

/*
 * vertex shader for drawing entities (mobs, falling block, paintings, ...).
 */
layout (location=0) in ivec3 position;
layout (location=1) in ivec2 info;
layout (location=2) in vec4  offsets;
layout (location=3) in vec2  rotation;
layout (location=4) in uvec3 lightSEN;
layout (location=5) in uvec3 lightWTB;

#include "uniformBlock.glsl"

flat out int   isBlock;
flat out int   isSelected;
     out vec2  texcoord;
     out float skyLight;
     out float blockLight;

void main(void)
{
	vec3 pos = vec3(
		float(position.x - 15360) * 0.00048828125 /* 1/2048. */,
		float(position.y - 15360) * 0.00048828125,
		float(position.z - 15360) * 0.00048828125
	);

	if (rotation.x > 0.001)
	{
		/* yaw: rotate along Y axis actually :-/ */
		float ca = cos(rotation.x);
		float sa = sin(rotation.x);
		pos = (vec4(pos, 1) * mat4(
			ca, 0, sa, 0,
			0, 1, 0, 0,
			-sa, 0, ca, 0,
			0, 0, 0, 1
		)).xyz;
	}

	int   norm  = (info.y >> 3) & 7;
	float shade = shading[norm].x;
	gl_Position = projMatrix * mvMatrix * vec4(pos + offsets.xyz, 1);
	float U = float(info.x & 511);
	float V = float(((info.x >> 6) & ~7) | (info.y & 7));
	if (V == 1023) V = 1024;
	if (U == 511)  U = 512;
	texcoord = vec2(U * 0.001953125 /* 1/512. */, V * 0.0009765625 /* 1/1024. */);

	uint light, corner;
	switch (norm) {
	case 0: /* south: varying in XY */
		corner = ((pos.x < 0.5 ? 0 : 1) + (pos.y < 0.5 ? 0 : 2)) << 3;
		light = lightSEN.x >> corner;
		break;
	case 1: /* east: varying in ZY */
		corner = ((pos.z < 0.5 ? 0 : 1) + (pos.y < 0.5 ? 0 : 2)) << 3;
		light = lightSEN.y >> corner;
		break;
	case 2: /* north: varying in XY */
		corner = ((pos.x < 0.5 ? 0 : 1) + (pos.y < 0.5 ? 0 : 2)) << 3;
		light = lightSEN.z >> corner;
		break;
	case 3: /* west: varying in ZY */
		corner = ((pos.z < 0.5 ? 0 : 1) + (pos.y < 0.5 ? 0 : 2)) << 3;
		light = lightWTB.x >> corner;
		break;
	case 4: /* top: varying in XZ */
		corner = ((pos.x < 0.5 ? 0 : 1) + (pos.z < 0.5 ? 0 : 2)) << 3;
		light = lightWTB.y >> corner;
		break;
	case 5: /* bottom: varying in XZ */
		corner = ((pos.x < 0.5 ? 0 : 1) + (pos.z < 0.5 ? 0 : 2)) << 3;
		light = lightWTB.z >> corner;
	}

	blockLight = float(light & 15) / 15.;
	skyLight = float(light & 0xf0) / 240.;

	/* need to do the same thing than blocks.vsh */
	if (blockLight > skyLight)
	{
		/* diminish slightly ambient occlusion if there is blockLight overpowering skyLight */
		shade += (blockLight - skyLight) * 0.35 +
			/* cancel some of the shading per face */
			(1 - shading[(info.y >> 3) & 7].x) * 0.5;
		if (shade > 1) shade = 1;
	}

	blockLight *= shade;
	skyLight   *= shade;

	isBlock = 1;
	isSelected = int(offsets.w);
}
