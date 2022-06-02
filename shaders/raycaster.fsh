/*
 * raycaster.fsh: perform raycasting to draw distant chunks that are way too expensive to render
 *                using rasterization.
 *
 * written by T.Pierron, june 2022.
 */
#version 430 core

out vec4 color;

uniform vec4 camera;
uniform vec2 renderDist;

#if 0
// where to find ChunkData in the banks
layout (binding=7)  uniform sampler2D texMap;

// each texture is a 2048x2048xRGBA containing at most 1024 ChunkData
layout (binding=8)  uniform sampler2D bankTex1;
layout (binding=9)  uniform sampler2D bankTex2;
layout (binding=10) uniform sampler2D bankTex3;
layout (binding=11) uniform sampler2D bankTex4;
#endif

void main(void)
{
	discard;
	// TODO
	color = vec4(1, 0, 1, 1);
}
