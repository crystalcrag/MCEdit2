/*
 * blocks.gsh : convert GL_POINT from blocks.vsh into GL_QUAD.
 */
#version 430 core

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

/* extension provided by stb_include.h */
#include "uniformBlock.glsl"

/* GL_POINT needs to be converted to quad */
in vec3 vertex1[];
in vec3 vertex2[];
in vec3 vertex3[];
in vec4 texCoord[];
in uint skyBlockLight[];
in uint ocsNorm[];
in vec3 offsets[];


out vec2 tc;
out float skyLight;
out float blockLight;
out float shadeOCS;
flat out int rswire;

float getOCSValue(in uint ocs, in float skyLight, in float blockLight, in int normal, out float shade)
{
	float OCS = 0;
	if (normal == 7)
		return blockLight;
	if (normal == 6)
		return 0;
	switch (ocs) {
	case 1: shade -= 0.1; OCS = 0.025; break;
	case 2: shade -= 0.2; OCS = 0.05; break;
	case 3: shade -= 0.3; OCS = 0.1; 
	}
	if (blockLight > skyLight)
	{
		/* diminish slightly ambient occlusion if there is blockLight overpowering skyLight */
		shade += (blockLight - skyLight) * 0.2 +
			/* cancel some of the shading per face */
			(1 - shading[normal].x) * 0.5;
		if (shade > 1) shade = 1;
	}
	return OCS;
}

void main(void)
{
	int   normal = int(bitfieldExtract(ocsNorm[0], 8, 3));
	mat4  MVP    = projMatrix * mvMatrix;
	bool  keepX  = (ocsNorm[0] & (1 << 11)) > 0;

	/* ascending quad */
	if ((ocsNorm[0] & (1 << 12)) > 0)
		normal = 4;

	/* first vertex */
	gl_Position = MVP * vec4(vertex1[0], 1);
	float globalShade = normal < 6 ? shading[normal].x : 1;
	float shade = globalShade;

	skyLight    = float(bitfieldExtract(skyBlockLight[0], 28, 4)) / 15;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 24, 4)) / 15;
	/* ambient occlusion */
	shadeOCS    = getOCSValue(bitfieldExtract(ocsNorm[0], 6, 2), skyLight, blockLight, normal, shade);
	tc          = keepX ? vec2(texCoord[0].x, texCoord[0].w) :
	                      vec2(texCoord[0].y, texCoord[0].z) ;
	rswire      = normal == 7 ? 1 : 0;
	skyLight   *= shade;
	blockLight *= shade;

	EmitVertex();

	/* second vertex */
	gl_Position = MVP * vec4(vertex2[0], 1);
	shade       = globalShade;
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 4, 4)) / 15;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 0, 4)) / 15;
	shadeOCS    = getOCSValue(bitfieldExtract(ocsNorm[0], 0, 2), skyLight, blockLight, normal, shade);
	tc          = vec2(texCoord[0].x, texCoord[0].z);
	skyLight   *= shade;
	blockLight *= shade;

	EmitVertex();

	/* third vertex */
	gl_Position = MVP * vec4(vertex3[0], 1);
	shade       = globalShade;
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 20, 4)) / 15;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 16, 4)) / 15;
	shadeOCS    = getOCSValue(bitfieldExtract(ocsNorm[0], 4, 2), skyLight, blockLight, normal, shade);
	tc          = vec2(texCoord[0].y, texCoord[0].w);
	skyLight   *= shade;
	blockLight *= shade;

	EmitVertex();

	/* fourth vertex */
	gl_Position = MVP * vec4(vertex3[0] + (vertex2[0] - vertex1[0]), 1);
	shade       = globalShade;
	skyLight    = float(bitfieldExtract(skyBlockLight[0], 12, 4)) / 15;
	blockLight  = float(bitfieldExtract(skyBlockLight[0], 8,  4)) / 15;
	shadeOCS    = getOCSValue(bitfieldExtract(ocsNorm[0], 2, 2), skyLight, blockLight, normal, shade);
	tc          = keepX ? vec2(texCoord[0].y, texCoord[0].z) :
	                      vec2(texCoord[0].x, texCoord[0].w) ;
	skyLight   *= shade;
	blockLight *= shade;

	EmitVertex();

	EndPrimitive();

	#if 0
	if (shading[0].w > 0)
	{
		/* inventory items: using an orthogonal projection on XY plane */
		float scale = offsets.z;
		/* <offsets> is relative to bottom left corner of rendering box */
		if (normal < 6)
		{
			/* cube: due to rotation, scale and offset need to be slightly adjusted */
			scale *= 0.625;
			pos = mvMatrix * ((pos - vec4(0.5, 0.5, 0.5, 0)) * vec4(scale,scale,scale,1)) + vec4(offsets.x + 0.85 * scale, offsets.y + 0.8 * scale, 25, 0);
		}
		else pos = pos * vec4(scale,scale,1,1) + vec4(offsets.x, offsets.y, 20, 0); /* quad */
		gl_Position = projMatrix * pos;
	}
	#endif
}
