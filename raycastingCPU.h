/*
 * the following functions mimic the raycasting done on the GPU, they are used mostly for debug, to
 * have a reference implementation to fallback to, in case of problem.
 */
static int     iteration;
static uint8_t color[4];
static vec4    camera;
static vec4    rasterArea;
static vec4    distantArea;

static float normals[] = { /* S, E, N, W, T, B */
	 0,  0,  1, 1,
	 1,  0,  0, 1,
	 0,  0, -1, 1,
	-1,  0,  0, 1,
	 0,  1,  0, 1,
	 0, -1,  0, 1,
};

#define vec3(dst, x,y,z)   dst[0]=x,dst[1]=y,dst[2]=z

static void texelFetch(vec4 ret, int texId, int X, int Y, int lod)
{
	DATA8 src;
	if (texId == 0)
	{
		src = (DATA8) (&raycast.texMap[X + Y * raycast.distantChunks]);
		ret[0] = src[0] / 255.0f;
		ret[1] = src[1] / 255.0f;
		ret[2] = ret[3] = 1.0f;
	}
	else
	{
		ChunkTexture bank;
		for (bank = HEAD(raycast.texBanks); bank && texId > 1; NEXT(bank), texId --);

		if (! bank->data)
		{
			/* need to retrieve the entire tex data :-/ */
			bank->data = malloc(4096 * 1024 * 4); // 16 mb
			if (bank->data == NULL)
				return;
			glBindTexture(GL_TEXTURE_2D, bank->textureId);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bank->data);
		}
		src = bank->data + X * 4 + Y * 16*1024;
		ret[0] = src[0] / 255.0f;
		ret[1] = src[1] / 255.0f;
		ret[2] = src[2] / 255.0f;
		ret[3] = src[3] / 255.0f;
	}
}

// extract voxel color at position <pos>
int voxelFindClosest(vec4 pos, vec4 V0, vec4 V1, float upward, int side)
{
	if (pos[VX] < distantArea[0] || pos[VZ] < distantArea[1] || pos[VY] < 0)
	    return 0;

	int X = (int) (pos[VX] - distantArea[0]) >> 4;
	int Z = (int) (pos[VZ] - distantArea[1]) >> 4;
	int Y = (int) pos[VY] >> 4;
	int size = (int) (distantArea[2] * 0.0625f);

	if (Y >= distantArea[3])
	{
		// above raycasted chunks and going upward: no way we can reach a distant chunk
		if (upward >= 0)
			return 0;

		// maybe we can
		vec3(V0, distantArea[0], distantArea[3] * 16, distantArea[1]);
		vec3(V1, V0[VX] + distantArea[2], V0[VY], V0[VZ] + distantArea[2]);
		return 2;
	}
	else if (X >= size || Z >= size)
	{
		return 0;
	}

	vec4 texel;
	texelFetch(texel, 0, X, Z + Y * size, 0);
	int texId = roundf(texel[0] * 65280.0f + texel[1] * 255.0f);

	if (texId < 0xff00 && texId > raycast.maxSlot)
		/* this is not good :-/ */
		return 0;

	if (texId == 0xffff)
	{
		/* missing ChunkData: assume empty then */
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16,
			floorf(pos[VY] * 0.0625f) * 16,
			floorf(pos[VZ] * 0.0625f) * 16
		);
		vec3(V1, V0[VX] + 16, V0[VY] + 16, V0[VZ] + 16);
		return 2;
	}

	if (texId >= 0xff00)
	{
		/* empty space above chunk */
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16,
			floorf(pos[VY] * 0.0625f) * 16,
			floorf(pos[VZ] * 0.0625f) * 16
		);
		V0[VY] -= (texId - 0xff00) * 16;
		vec3(V1, V0[VX] + 16, distantArea[3] * 16, V0[VZ] + 16);
		return 2;
	}

	int coord[2] = {
		((int) floor(pos[VX]) & 15) +
		((int) floor(pos[VZ]) & 15) * 16 +
		((int) floor(pos[VY]) & 15) * 256, texId & 1023
	};

	// should consider using bindless texture one day (not supported on intel though :-/).
	vec4 voxel;
	switch (texId >> 10) {
	// 4096 ChunkData is not much actually  :-/ should increase texture size...
	case 0: texelFetch(voxel, 1, coord[0], coord[1], 0); break;
	case 1: texelFetch(voxel, 2, coord[0], coord[1], 0); break;
	case 2: texelFetch(voxel, 3, coord[0], coord[1], 0); break;
	case 3: texelFetch(voxel, 4, coord[0], coord[1], 0); break;
	default: return 0;
	}

	if (voxel[3] >= 0.5f)
	{
		int tex[3] = {voxel[0] * 255, voxel[1] * 255, voxel[2] * 255};
		vec3(V0,
			floorf(pos[VX] * 0.0625f) * 16 + (tex[0] & 15),
			floorf(pos[VY] * 0.0625f) * 16 + (tex[1] & 15),
			floorf(pos[VZ] * 0.0625f) * 16 + (tex[0] >> 4)
		);

		vec3(V1,
			V0[0] + (tex[2] & 15) + 1,
			V0[1] + (tex[1] >> 4) + 1,
			V0[2] + (tex[2] >> 4) + 1
		);
		return 2;
	}
	color[0] = (int) (voxel[0] * 255) * raycast.shading[side] >> 8;
	color[1] = (int) (voxel[1] * 255) * raycast.shading[side] >> 8;
	color[2] = (int) (voxel[2] * 255) * raycast.shading[side] >> 8;
	color[3] = 1;
	return 1;
}


Bool mapPointToVoxel(vec4 dir)
{
	vec4 pos, pt1, pt2;
	vec4 plane    = {floor(camera[VX]), floor(camera[VY]), floor(camera[VZ])};
	vec4 offset   = {dir[VX] < 0 ? -0.1 : 0.1, dir[VY] < 0 ? -0.1 : 0.1, dir[VZ] < 0 ? -0.1 : 0.1};
	int  sides[3] = {dir[VX] < 0 ? 3 : 1, dir[VZ] < 0 ? 2 : 0, dir[VY] < 0 ? 5 : 4};

	memcpy(pos, camera, 16);

	vec3(pt1, rasterArea[0], 0,   rasterArea[1]);
	vec3(pt2, rasterArea[2], 256, rasterArea[3]);

	for (;;)
	{
		// empty space in voxel: skip this part as quickly as possible
		int i;
		for (i = 0; i < 3; i ++)
		{
			vec4 inter, V0, V1;
			vec  norm;

			iteration ++;
			switch (sides[i]) {
			case 0:  vec3(V0, pt1[VX], pt1[VY], pt2[VZ]); memcpy(V1, pt2, 12); break; // south
			case 1:  vec3(V0, pt2[VX], pt1[VY], pt1[VZ]); memcpy(V1, pt2, 12); break; // east
			case 2:  vec3(V1, pt2[VX], pt2[VY], pt1[VZ]); memcpy(V0, pt1, 12); break; // north
			case 3:  vec3(V1, pt1[VX], pt2[VY], pt2[VZ]); memcpy(V0, pt1, 12); break; // west
			case 4:  vec3(V0, pt1[VX], pt2[VY], pt1[VZ]); memcpy(V1, pt2, 12); break; // top
			default: vec3(V1, pt2[VX], pt1[VY], pt2[VZ]); memcpy(V0, pt1, 12); // bottom
			}
			norm = normals + sides[i] * 4;

			if (intersectRayPlane(pos, dir, V0, norm, inter))
			{
				// need to check that intersection point remains within box
				if (norm[VX] == 0 && ! (V0[VX] <= inter[VX] && inter[VX] <= V1[VX])) continue;
				if (norm[VY] == 0 && ! (V0[VY] <= inter[VY] && inter[VY] <= V1[VY])) continue;
				if (norm[VZ] == 0 && ! (V0[VZ] <= inter[VZ] && inter[VZ] <= V1[VZ])) continue;

				memcpy(plane, inter, 12);
				memcpy(pos, inter, 12);

				if (norm[VX] == 0)
				{
					if (inter[VX] == V0[VX] || inter[VX] == V1[VX])
						plane[VX] += offset[VX];
				}
				else plane[VX] += norm[VX] * 0.5f;
				if (norm[VY] == 0)
				{
					if (inter[VY] == V0[VY] || inter[VY] == V1[VY])
						plane[VY] += offset[VY];
				}
				else plane[VY] += norm[VY] * 0.5f;
				if (norm[VZ] == 0)
				{
					if (inter[VZ] == V0[VZ] || inter[VZ] == V1[VZ])
						plane[VZ] += offset[VZ];
				}
				else plane[VZ] += norm[VZ] * 0.5f;

				switch (voxelFindClosest(plane, pt1, pt2, dir[VY], opp[sides[i]])) {
				case 0: return False;
				case 1: return True;
				}
				break;
			}
		}
		if (i == 3)
			return False;
	}
}

#define SCR_WIDTH         1000
#define SCR_HEIGHT        1000

/* raycasting on CPU, mostly used for debugging */
void raycastWorld(Map map, mat4 invMVP, vec4 pos)
{
	static uint8_t skyColor[] = {0x72, 0xae, 0xf1};
	DATA8 bitmap, px;
	int   i, j;

	bitmap = malloc(SCR_WIDTH * SCR_HEIGHT * 3);
	camera[VX] = pos[VX];
	camera[VZ] = pos[VZ];
	camera[VY] = pos[VY] + 1.6f;
	camera[VT] = 1;
	iteration = 0;

	memcpy(rasterArea,  raycast.rasterArea, sizeof rasterArea);
	memcpy(distantArea, raycast.distantArea, sizeof distantArea);

	for (j = 0, px = bitmap; j < SCR_HEIGHT; j ++)
	{
		for (i = 0; i < SCR_WIDTH; i ++, px += 3)
		{
			vec4 clip = {i * 2. / SCR_WIDTH - 1, 1 - j * 2. / SCR_HEIGHT, 0, 1};
			vec4 dir;

			matMultByVec(dir, invMVP, clip);

			/* ray direction according to position on screen and player's view vector */
			dir[VX] = dir[VX] / dir[VT] - camera[VX];
			dir[VY] = dir[VY] / dir[VT] - camera[VY];
			dir[VZ] = dir[VZ] / dir[VT] - camera[VZ];

			if (mapPointToVoxel(dir))
			{
				px[0] = color[0];
				px[1] = color[1];
				px[2] = color[2];
			}
			else
			{
				/* no intersection with voxel terrain: use sky color then */
				px[0] = skyColor[0];
				px[1] = skyColor[1];
				px[2] = skyColor[2];
			}
		}
	}

	ChunkTexture bank;
	for (bank = HEAD(raycast.texBanks); bank; NEXT(bank))
	{
		free(bank->data);
		bank->data = NULL;
	}

	FILE * out = fopen("dump.ppm", "wb");

	if (out)
	{
		fprintf(out, "P6\n%d %d 255\n", SCR_WIDTH, SCR_HEIGHT);
		fwrite(bitmap, SCR_WIDTH * SCR_HEIGHT, 3, out);
		fclose(out);

		fprintf(stderr, "image dumped in dump.ppm, iteration avg: %.1f\n", iteration / (double) (SCR_WIDTH * SCR_HEIGHT));
	}
	free(bitmap);
}
