/*
 * raycaster.fsh: perform raycasting to draw distant chunks that are way too expensive to render
 *                using rasterization.
 *
 * written by T.Pierron, june 2022.
 */
#version 430 core

out vec4 color;

#include "uniformBlock.glsl"

uniform vec4 rasterArea;   // .xy == min world coord, .zw = max coord
uniform vec4 distantArea;  // .xy == min coord coord, .z = size in blocks, .w = height in blocks

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
layout (binding=12) uniform sampler2D bankTex5;
layout (binding=13) uniform sampler2D bankTex6;
layout (binding=14) uniform sampler2D bankTex7;
layout (binding=15) uniform sampler2D bankTex8;
layout (binding=16) uniform sampler2D bankTex9;

const int opp[6] = int[6] (2,3,0,1,5,4);

/*
 * extract voxel color at position <pos>, returns:
 * - 0 if ray goes out of the map.
 * - 1 if it hits a solid voxel (<color> will be set).
 * - 2 if it is within an empty space (V0 and V1 are its bbox, containing min and max pos in voxel space).
 */
int voxelFindClosest(in vec3 pos, out vec3 V0, out vec3 V1, float upward, int side)
{
	if (pos.x < distantArea.x || pos.z < distantArea.y || pos.y < 0)
	    return 0;
	int X = int(pos.x - distantArea.x) >> 4;
	int Z = int(pos.z - distantArea.y) >> 4;
	int Y = int(pos.y) >> 4;
	int size = int(distantArea.z * 0.0625);

	if (Y >= distantArea.w)
	{
		// above raycasted chunks and going upward: no way we can reach a distant chunk
		if (upward >= 0)
			return 0;

		// maybe we can: check inter with top of distant chunks
		V0 = vec3(distantArea.x, distantArea.w * 16, distantArea.y);
		V1 = vec3(V0.x + distantArea.z, V0.y, V0.z + distantArea.z);
		return 2;
	}
	else if (X >= size || Z >= size)
	{
		// outside of map: stop raycasting here
		return 0;
	}

	vec2 texel = texelFetch(texMap, ivec2(X, Z + Y * size), 0).rg;
	int  texId = int(round(texel.x * 65280 + texel.y * 255));

	if (texId == 0xffff)
	{
		// missing ChunkData: assume empty then
		V0 = floor(pos * 0.0625) * 16; // == pos.xyz & ~15
		V1 = V0 + vec3(16, 16, 16);
		return 2;
	}

	if (texId >= 0xff00)
	{
		// empty space above chunk
		V0 = floor(pos * 0.0625) * 16;
		V0.y -= (texId - 0xff00) * 16;
		V1 = vec3(V0.x + 16, distantArea.w * 16, V0.z + 16);
		return 2;
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
	case 4: voxel = texelFetch(bankTex5, coord, 0); break;
	case 5: voxel = texelFetch(bankTex6, coord, 0); break;
	case 6: voxel = texelFetch(bankTex7, coord, 0); break;
	case 7: voxel = texelFetch(bankTex8, coord, 0); break;
	case 8: voxel = texelFetch(bankTex9, coord, 0); break;
	default: return 0;
	}

	if (voxel.a >= 0.5)
	{
		/* empty space within chunk */
		ivec3 tex = ivec3(voxel.rgb * 255);
		V0 = floor(pos * 0.0625) * 16 + vec3(tex.r & 15, tex.g & 15, tex.r >> 4);
		V1 = V0 + vec3((tex.b & 15) + 1, (tex.g >> 4) + 1, (tex.b >> 4) + 1);
		return 2;
	}
	color = voxel * shading[side].r;
	color.a = 1;
	return 1;
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
	vec3 pos = camera.xyz;
	vec3 plane = vec3(floor(camera.x), floor(camera.y), floor(camera.z));
	vec3 offset = vec3(dir.x < 0 ? -0.1 : 0.1, dir.y < 0 ? -0.1 : 0.1, dir.z < 0 ? -0.1 : 0.1);
	int  sides[3] = int[3] (dir.x < 0 ? 3 : 1, dir.z < 0 ? 2 : 0, dir.y < 0 ? 5 : 4);

	/* raster chunk area */
	vec3 pt1 = vec3(rasterArea.x, 0,   rasterArea.y);
	vec3 pt2 = vec3(rasterArea.z, 256, rasterArea.w);

	for (;;)
	{
		// empty space in voxel: skip this part as quickly as possible
		int i;
		for (i = 0; i < 3; i ++)
		{
			vec3 inter, norm, V0, V1;

			norm = normals[sides[i]].xyz;

			switch (sides[i]) {
			case 0:  V0 = vec3(pt1.x, pt1.y, pt2.z); V1 = pt2; break; // south
			case 1:  V0 = vec3(pt2.x, pt1.y, pt1.z); V1 = pt2; break; // east
			case 2:  V1 = vec3(pt2.x, pt2.y, pt1.z); V0 = pt1; break; // north
			case 3:  V1 = vec3(pt1.x, pt2.y, pt2.z); V0 = pt1; break; // west
			case 4:  V0 = vec3(pt1.x, pt2.y, pt1.z); V1 = pt2; break; // top
			default: V1 = vec3(pt2.x, pt1.y, pt2.z); V0 = pt1; // bottom
			}

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
						plane.x += offset.x;
				}
				else plane.x += norm.x * 0.5;
				if (norm.y == 0)
				{
					if (inter.y == V0.y || inter.y == V1.y)
						plane.y += offset.y;
				}
				else plane.y += norm.y * 0.5;
				if (norm.z == 0)
				{
					if (inter.z == V0.z || inter.z == V1.z)
						plane.z += offset.z;
				}
				else plane.z += norm.z * 0.5;

				switch (voxelFindClosest(plane, pt1, pt2, dir.y, opp[sides[i]])) {
				case 0: return false;
				case 1: return true;
				// else ray hit an empty space: need to continue raycasting
				}
				break;
			}
		}
		if (i == 3)
			return false;
	}
}

void main(void)
{
	if (! mapPointToVoxel(worldPos - camera.xyz))
		discard;
}
