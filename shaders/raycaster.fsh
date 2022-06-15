/*
 * raycaster.fsh: perform raycasting to draw distant chunks that are way too expensive to render
 *                using rasterization.
 *
 * written by T.Pierron, june 2022.
 */
#version 430 core

out vec4 color;

#include "uniformBlock.glsl"

uniform mat4 INVMVP;
uniform vec4 chunk;   // .xy == world coord of distant region, .zw = coord of raster chunks
uniform vec4 size;    // .x == size of distant region in chunks, .y = height of distant chunk, .z = size of raster region

in vec3 worldPos;

/*
 * where to find ChunkData in the banks: renderDist.x x renderDist.x x 16, GL_LUMINANCE_ALPHA pixels
 * texture contain 16 planes of renderDist.x x renderDist.x pixels
 * (tex.r*256+tex.a)*255 = index in banks
 */
layout (binding=7)  uniform sampler2D texMap;

// each texture is a 4096x1024xRGBA containing at most 1024 ChunkData
layout (binding=8)  uniform sampler2D bankTex1;
layout (binding=9)  uniform sampler2D bankTex2;
layout (binding=10) uniform sampler2D bankTex3;
layout (binding=11) uniform sampler2D bankTex4;

const int opp[6] = int[6] (2,3,0,1,5,4);

void voxelGetBoundsForFace(in ivec4 tex, in int face, out vec3 V0, out vec3 V1, in vec3 posOffset)
{
	vec3 pt1, pt2;

	switch (tex.w) {
	case 0x80: // void space inside a ChunkData
		// encoding is done in raycast.c:chunkConvertToRGBA()
		pt1 = floor(posOffset * 0.0625) * 16 + vec3(tex.r & 15, tex.g & 15, tex.r >> 4);
		pt2 = pt1 + vec3((tex.b & 15) + 1, (tex.g >> 4) + 1, (tex.b >> 4) + 1);
		break;

	case 0x81: // void space inside Chunk
		pt1 = vec3(floor(posOffset.x * 0.0625) * 16, tex.r << 4, floor(posOffset.z * 0.0625) * 16);
		pt2 = pt1 + vec3(16, (tex.g - tex.r) << 4, 16);
		break;

	case 0x82: // raster chunks area
		pt1 = vec3(chunk.z, 0, chunk.w);
		pt2 = pt1 + vec3(size.z * 16, 256, size.z * 16);
		break;

	case 0x83: // area above distant chunks
		pt1 = vec3(chunk.x, 0, chunk.y);
		pt2 = pt1 + size.xyx * 16;
		break;

	default: return;
	}

	switch (face) {
	case 0: // south
		V0 = vec3(pt1.xy, pt2.z);
		V1 = pt2;
		break;
	case 1: // east
		V0 = vec3(pt2.x, pt1.yz);
		V1 = pt2;
		break;
	case 2: // north
		V0 = pt1;
		V1 = vec3(pt2.xy, pt1.z);
		break;
	case 3: // west
		V0 = pt1;
		V1 = vec3(pt1.x, pt2.yz);
		break;
	case 4: // top
		V0 = vec3(pt1.x, pt2.y, pt1.z);
		V1 = pt2;
		break;
	case 5: // bottom
		V0 = pt1;
		V1 = vec3(pt2.x, pt1.y, pt2.z);
	}
}

// extract voxel color at position <pos>
bool voxelFindClosest(in vec3 pos, out ivec4 tex, float upward)
{
	int X = int(pos.x - chunk.x) >> 4;
	int Z = int(pos.z - chunk.y) >> 4;
	int Y = int(pos.y) >> 4;

	if (Y >= size.y)
	{
		// above raycasted chunks and going upward: no way we can reach a distant chunk
		if (upward >= 0)
			return false;
			
		// maybe we can
		tex = ivec4(0, 0, 0, 0x83);
	}
	else if (X < 0 || Z < 0 || X >= size.x || Z >= size.x || Y < 0)
	{
		return false;
	}

	vec2 texel = texelFetch(texMap, ivec2(X, Z + Y * size.x), 0).rg;
	int  texId = int(round(texel.x * 65280 + texel.y * 255));

	if (texId == 0xffff)
	{
		// missing ChunkData: assume empty then
		tex = ivec4(0, 0xf0, 0xff, 0x80);
		return true;
	}

	if (texId >= 0xff00)
	{
		// empty space above chunk
		tex = ivec4(Y - (texId - 0xff00), size.y, 0, 0x81);
		return true;
	}

	ivec2 coord = ivec2(
		(int(floor(pos.x)) & 15) +
		(int(floor(pos.z)) & 15) * 16 +
		(int(floor(pos.y)) & 15) * 256, texId & 1023);

	// should consider using bindless texture one day (not supported on intel though :-/).
	vec4 voxel;
	switch (texId >> 10) {
	// 4096 ChunkData is not much actually  :-/ should increase texture size...
	case 0: voxel = texelFetch(bankTex1, coord, 0); break;
	case 1: voxel = texelFetch(bankTex2, coord, 0); break;
	case 2: voxel = texelFetch(bankTex3, coord, 0); break;
	case 3: voxel = texelFetch(bankTex4, coord, 0); break;
	default: return false;
	}

	tex = ivec4(voxel * 255);
	return true;
}

bool intersectRayPlane(in vec3 P0, in vec3 u, in vec3 V0, in vec3 norm, out vec3 I)
{
	vec3 w = P0 - V0;

	float D =  dot(norm, u);
	float N = -dot(norm, w);

	if (abs(D) < 0.0001) // segment is parallel to plane
		return false;

	// they are not parallel: compute intersect param
	I = P0 + (N / D) * u;

    return true;
}

bool mapPointToVoxel(in vec3 dir)
{
	vec3 pos, V0, V1;
	vec3 plane = vec3(floor(camera.x), floor(camera.y), floor(camera.z));
	int  sides[3] = int[3] (dir.x < 0 ? 3 : 1, dir.z < 0 ? 2 : 0, dir.y < 0 ? 5 : 4);
	int  side = 4;

	pos = camera.xyz;

	ivec4 tex = ivec4(0, 0, 0, 0x82);

	for (;;)
	{
		if (tex.a < 0x80)
		{
			color = vec4(vec3(tex.xyz) / 255.0 * shading[side].r, 1);
			return true;
		}

		// empty space in voxel: skip this part as quickly as possible
		int i;
		for (i = 0; i < 3; i ++)
		{
			vec3 inter, norm;

			voxelGetBoundsForFace(tex, sides[i], V0, V1, plane);
			norm = normals[sides[i]].xyz;

			if (intersectRayPlane(pos, dir, V0, norm, inter))
			{
				// need to check that intersection point remains within box V0-V1
				if (norm.x == 0 && ! (V0.x <= inter.x && inter.x <= V1.x)) continue;
				if (norm.y == 0 && ! (V0.y <= inter.y && inter.y <= V1.y)) continue;
				if (norm.z == 0 && ! (V0.z <= inter.z && inter.z <= V1.z)) continue;

				plane = pos = inter;

				// <plane> needs to be offseted to reach next voxel
				if (norm.x == 0)
				{
					if (inter.x == V0.x || inter.x == V1.x)
						plane.x += dir.x;
				}
				else plane.x += norm.x * 0.5;
				if (norm.y == 0)
				{
					if (inter.y == V0.y || inter.y == V1.y)
						plane.y += dir.y;
				}
				else plane.y += norm.y * 0.5;
				if (norm.z == 0)
				{
					if (inter.z == V0.z || inter.z == V1.z)
						plane.z += dir.z;
				}
				else plane.z += norm.z * 0.5;

				if (! voxelFindClosest(plane, tex, dir.y))
					return false;
				side = opp[sides[i]];
				break;
			}
		}
		if (i == 3)
			return false;
	}
}

void main(void)
{
	if (! mapPointToVoxel(normalize(worldPos - camera.xyz) * 0.01))
		discard;
}
