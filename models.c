/*
 * models.c : read generic .obj models and auto-generate basic shapes (sphere, torus, cube ...)
 *
 * Written by T.Pierron, Dec 2019.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "utils.h"
#include "models.h"

void modelFree(Model model)
{
	if ((model->flags & MODEL_STATIC_VERTEX) == 0) free(model->vertices);
	if ((model->flags & MODEL_STATIC_TEX)    == 0) free(model->tex);
	if ((model->flags & MODEL_STATIC_NORM)   == 0) free(model->normals);
	if (model->tangent) free(model->tangent);
	free(model);
}

/*
 * Generate a sphere by subdividing into evenly spaced slices. Primitives will be triangles but
 * not of the same size. This way it is easy to map a texture on it. Faces are oriented clock-wise
 */
Model modelSphere(float size, int subdiv)
{
	int vertex = (subdiv + 1) * (subdiv + 1);
	int index  = subdiv * subdiv * 6;
	int i, j;

	Model ret = malloc(sizeof *ret + vertex * sizeof (float) * 3 + index * 2);

	if (! ret) return NULL;

	memset(ret, 0, sizeof *ret);
	ret->index    = index;
	ret->vertex   = vertex;
	ret->vertices = (float *) (ret + 1);
	ret->normals  = ret->vertices;
	ret->indices  = (DATA16) (ret->vertices + vertex * 3);
	ret->normals  = ret->vertices;
	ret->flags    = MODEL_STATIC_ALL | MODEL_TRIANGLES | MODEL_CCW;

	/* triangle vertices */
	float * v;
	for (i = 0, v = ret->vertices; i <= subdiv; i ++)
	{
		for (j = 0; j <= subdiv; j ++)
		{
			float y = cos(M_PI - i * M_PI / subdiv);
			float a = fabs(cos(asin(y)));
			float x = - cos(j * 2 * M_PI / subdiv) * a;
			float z = sin(j * 2 * M_PI / subdiv) * a;

			*v++ = x * size;
			*v++ = y * size;
			*v++ = z * size;
			//*t++ = j / (float) subdiv;
			//*t++ = i / (float) subdiv;
		}
	}

	/* triangle indices */
	DATA16 p;
	for (i = 0, p = ret->indices, index = 0; i < subdiv; i ++, index += subdiv+1)
	{
		for (j = 0; j < subdiv; j ++, p += 6)
		{
			p[0] = index + j;
			p[1] = index + j + 1;
			p[2] = p[1] + subdiv;
			p[3] = p[1];
			p[4] = p[2] + 1;
			p[5] = p[2];
		}
	}
	return ret;
}

#define D_TO_R     (M_PI/180)
Model modelTorus(int sides, int cs_sides, float radius, float cs_radius)
{
	int numVertices = (sides+1) * (cs_sides+1);
	int numIndices = (2*sides+4) * cs_sides;

	Model torus = malloc(sizeof *torus + numVertices * 8 * sizeof (float) + numIndices * 2);
	float * Vertices;
	float * TexCoord;
	float * Normals;
	DATA16  Indices;

	memset(torus, 0, sizeof *torus);
	torus->vertices = Vertices = (float *) (torus + 1);
	torus->normals  = Normals  = Vertices + numVertices * 3;
	torus->tex      = TexCoord = Normals + numVertices * 3;
	torus->indices  = Indices  = (DATA16) (torus->tex + numVertices * 2);
	torus->index    = numIndices;
	torus->vertex   = numVertices;
	torus->flags    = MODEL_STATIC_ALL;

	int angleincs = 360/sides;
	int cs_angleincs = 360/cs_sides;
	float currentradius, zval;
	int i, j, nextrow;

	/* iterate cs_sides: inner ring */
	for (j = 0; j <= 360; j += cs_angleincs)
	{
		currentradius = radius + (cs_radius * cosf(j * D_TO_R));
		zval = cs_radius * sinf(j * D_TO_R);

		/* iterate sides: outer ring */
		for (i = 0; i <= 360; i += angleincs, Vertices += 3, TexCoord += 2)
		{
			Vertices[0] = currentradius * cosf(i * D_TO_R);
			Vertices[1] = currentradius * sinf(i * D_TO_R);
			Vertices[2] = zval;

			float u = i / 360.;
			float v = 2. * j / 360 - 1;
			if (v < 0) v = -v;

			TexCoord[0] = u;
			TexCoord[1] = v;
		}
	}
	/* compute normals: loops are swapped */
	for (i = 0, nextrow = (sides+1) * 3, Vertices = torus->vertices; i <= 360; i += angleincs, Normals += 3, Vertices += 3)
	{
		float * vert;
		float * norm;
		float xc = radius * cos(i * D_TO_R);
		float yc = radius * sin(i * D_TO_R);
		for (j = 0, vert = Vertices, norm = Normals; j <= 360; j += cs_angleincs, norm += nextrow, vert += nextrow)
		{
			norm[0] = vert[0] - xc;
			norm[1] = vert[1] - yc;
			norm[2] = vert[2];
			vecNormalize(norm, norm);
		}
	}

	/* indices grouped by GL_TRIANGLE_STRIP */

	/* inner ring */
	for (i = 0, nextrow = sides + 1; i < cs_sides; i ++)
	{
		/* outer ring */
		for (j = 0; j < sides; j ++)
		{
			*Indices++ = i * nextrow + j;
			*Indices++ = (i + 1) * nextrow + j;
		}

		/* generate dummy triangle to avoid messing next ring */
		Indices[0] = i * nextrow + j;
		Indices[1] = Indices[2] = Indices[3] = Indices[0] + nextrow;
		Indices += 4;
	}
	return torus;
}

/* parse an arbitrary object from a .obj file */
Model modelParseObj(const char * path)
{
	FILE *   in = fopen(path, "rb");
	char     buffer[256];
	Model    ret;
	vector_t vertices, modelVTX;
	vector_t tex,      modelTEX;
	vector_t normals,  modelNORM;

	if (! in) return NULL;

	vectorInit(vertices, 3 * sizeof (float));
	vectorInit(tex,      2 * sizeof (float));
	vectorInit(normals,  3 * sizeof (float));

	/* OpenGL limitation makes it so we will have to duplicate all the data */
	vectorInit(modelVTX,  3 * sizeof (float));
	vectorInit(modelTEX,  2 * sizeof (float));
	vectorInit(modelNORM, 3 * sizeof (float));
	ret = NULL;

	while (fgets(buffer, sizeof buffer, in))
	{
		STRPTR p;
		if (buffer[0] == '#') continue;
		for (p = buffer; *p && ! isspace(*p); p ++);
		if (*p) *p ++ = 0;
		if (strcmp(buffer, "v") == 0)
		{
			float * v = vectorNth(&vertices, vertices.count);

			if (sscanf(p, "%f %f %f", v, v+1, v+2) != 3)
				v[0] = v[1] = v[2] = 0;
		}
		else if (strcmp(buffer, "vt") == 0)
		{
			float * t = vectorNth(&tex, tex.count);

			if (sscanf(p, "%f %f", t, t+1) != 2)
				t[0] = t[1] = 0;
		}
		else if (strcmp(buffer, "vn") == 0)
		{
			float * n = vectorNth(&normals, normals.count);

			if (sscanf(p, "%f %f %f", n, n+1, n+2) != 3)
				n[0] = n[1] = n[2] = 0;
		}
		else if (strcmp(buffer, "f") == 0)
		{
			STRPTR modelParseFaceIdx(STRPTR p, int vtn[3]);
			int vtn[3], i;

			if (ret == NULL)
			{
				ret = calloc(sizeof *ret, 1);
				ret->flags = MODEL_TRIANGLES | MODEL_CCW;
			}

			/* 3 coords per face: objects are made of triangles */
			for (i = 0; i < 3; i ++)
			{
				p = modelParseFaceIdx(p, vtn);
				if (vtn[0] > 0)
					memcpy(vectorNth(&modelVTX, modelVTX.count), vectorNth(&vertices, vtn[0]-1), modelVTX.itemsize);

				if (vtn[1] > 0)
					memcpy(vectorNth(&modelTEX, modelTEX.count), vectorNth(&tex, vtn[1]-1), modelTEX.itemsize);

				if (vtn[2] > 0)
					memcpy(vectorNth(&modelNORM, modelNORM.count), vectorNth(&normals, vtn[2]-1), modelNORM.itemsize);
			}
		}
	}
	fclose(in);

	if (modelVTX.count > 0)
	{
		/* model will be stored as GL_TRIANGLES */
		ret->vertex   = modelVTX.count;
		ret->vertices = vectorFirst(modelVTX);
		ret->tex      = vectorFirst(modelTEX);
		ret->normals  = vectorFirst(modelNORM);
	}
	vectorFree(vertices);
	vectorFree(tex);
	vectorFree(normals);
	return ret;
}

STRPTR modelParseFaceIdx(STRPTR p, int vtn[3])
{
	vtn[0] = strtoul(p, &p, 10);
	vtn[1] = 0;
	vtn[2] = 0;
	if (*p == '/')
	{
		vtn[1] = strtoul(p+1, &p, 10);
		if (*p == '/')
		{
			vtn[2] = strtoul(p+1, &p, 10);
		}
	}
	return p;
}

/*
 * Normal mapping: get tangent from normal vector and tex coord:
 * to get bitangent use cross product from tangent and normal.
 */
Bool modelGetTangent(Model model)
{
	DATA16  pos;
	float * tex;
	float * vec;
	float * norm;
	float * tan;
	int     i, cnt, type;

	pos  = model->indices;
	tex  = model->tex;
	vec  = model->vertices;
	norm = model->normals;
	if (! norm || ! tex) return False;

	model->tangent = tan = malloc(model->vertex * 3 * sizeof (float));
	if (! model->tangent) return False;

	type = pos ? (model->flags & MODEL_TRIANGLES) > 0 : 2;

	for (i = 0, cnt = type == 2 ? model->vertex : model->index; i < cnt; )
	{
		/* need to get triangle vertices */
		float * uv1,  * uv2,  * uv3;
		float * pos1, * pos2, * pos3;
		float * tan1, * tan2, * tan3;

		switch (type) {
		case 0: /* triangle index */
		case 1: /* triangle strip index */
			pos1 = vec + pos[0]*3;    tan1 = tan + pos[0]*3;      uv1 = tex + pos[0]*2;
			pos2 = vec + pos[1]*3;    tan2 = tan + pos[1]*3;      uv2 = tex + pos[1]*2;
			pos3 = vec + pos[2]*3;    tan3 = tan + pos[2]*3;      uv3 = tex + pos[2]*2;
			if (type == 1)
				pos += 3, i += 3;
			else
				pos ++, i ++;
			break;
		case 2: /* triangles */
			pos1 = vec;       tan1 = tan;         uv1 = tex;
			pos2 = vec + 3;   tan2 = tan + 3;     uv2 = tex + 2;
			pos3 = vec + 6;   tan3 = tan + 6;     uv3 = tex + 4;
			vec += 9; i += 3; tex += 4;
		}

		float deltaV1[]  = {pos2[VX] - pos1[VX], pos2[VY] - pos1[VY], pos2[VZ] - pos1[VZ]};
		float deltaV2[]  = {pos3[VX] - pos1[VX], pos3[VY] - pos1[VY], pos3[VZ] - pos1[VZ]};
		float deltaUV1[] = {uv2[0] - uv1[0], uv2[1] - uv1[1]};
		float deltaUV2[] = {uv3[0] - uv1[0], uv3[1] - uv1[1]};

		float f = (deltaUV1[VX] * deltaUV2[VY] - deltaUV2[VX] * deltaUV1[VY]);
		if (f == 0) f = 0.01; /* well, better than nothing :-/ */
		f = 1.0f / f;

		tan1[VX] = f * (deltaUV2[VY] * deltaV1[VX] - deltaUV1[VY] * deltaV2[VX]);
		tan1[VY] = f * (deltaUV2[VY] * deltaV1[VY] - deltaUV1[VY] * deltaV2[VY]);
		tan1[VZ] = f * (deltaUV2[VY] * deltaV1[VZ] - deltaUV1[VY] * deltaV2[VZ]);
		vecNormalize(tan1, tan1);

		/* same for all 3 vertices */
		if (type != 1)
		{
			memcpy(tan2, tan1, 3 * sizeof (float));
			memcpy(tan3, tan1, 3 * sizeof (float));
		}
		/* bitangent is the cross-product between normal and tangent */
	}
	return True;
}


/*
 * Material properties: quoted from http://www.barradeau.com/nicoptere/dump/materials.html
 */
static Material materials[] = /* indexed by MAT_* */
{
	/* brass */   {.ambient = {0.329412, 0.223529, 0.027451, 1}, .diffuse = {0.780392, 0.568627, 0.113725,  1}, .spec = {0.992157, 0.941176, 0.807843,  1}, .shine = 27.8974},
	/* bronze */  {.ambient = {0.2125,   0.1275,   0.054,    1}, .diffuse = {0.714,    0.4284,   0.18144,   1}, .spec = {0.393548, 0.271906, 0.166721,  1}, .shine = 25.6},
	/* pbronze */ {.ambient = {0.25,     0.148,    0.06475,  1}, .diffuse = {0.4,      0.2368,   0.1036,    1}, .spec = {0.774597, 0.458561, 0.200621,  1}, .shine = 76.8},
	/* chrome */  {.ambient = {0.25,     0.25,     0.25,     1}, .diffuse = {0.4,      0.4,      0.4,       1}, .spec = {0.774597, 0.774597, 0.774597,  1}, .shine = 76.8},
	/* copper */  {.ambient = {0.19125,  0.0735,   0.0225,   1}, .diffuse = {0.7038,   0.27048,  0.0828,    1}, .spec = {0.256777, 0.137622, 0.086014,  1}, .shine = 12.8},
	/* pcopper */ {.ambient = {0.2295,   0.08825,  0.0275,   1}, .diffuse = {0.5508,   0.2118,   0.066,     1}, .spec = {0.580594, 0.223257, 0.069570,  1}, .shine = 51.2},
	/* gold */    {.ambient = {0.24725,  0.1995,   0.0745,   1}, .diffuse = {0.75164,  0.60648,  0.22648,   1}, .spec = {0.628281, 0.555802, 0.366065,  1}, .shine = 51.2},
	/* pgold */   {.ambient = {0.24725,  0.2245,   0.0645,   1}, .diffuse = {0.34615,  0.3143,   0.0903,    1}, .spec = {0.797357, 0.723991, 0.208006,  1}, .shine = 83.2},
	/* pewter */  {.ambient = {0.105882, 0.058824, 0.113725, 1}, .diffuse = {0.427451, 0.470588, 0.541176,  1}, .spec = {0.333333, 0.333333, 0.521569,  1}, .shine = 9.84615},
	/* silver */  {.ambient = {0.19225,  0.19225,  0.19225,  1}, .diffuse = {0.50754,  0.50754,  0.50754,   1}, .spec = {0.508273, 0.508273, 0.508273,  1}, .shine = 51.2},
	/* psilver */ {.ambient = {0.23125,  0.23125,  0.23125,  1}, .diffuse = {0.2775,   0.2775,   0.2775,    1}, .spec = {0.773911, 0.773911, 0.773911,  1}, .shine = 89.6},
	/* emerald */ {.ambient = {0.0215,   0.1745,   0.0215, .55}, .diffuse = {0.07568,  0.61424,  0.07568, .55}, .spec = {0.633,    0.727811, 0.633,   .55}, .shine = 76.8},
	/* jade */    {.ambient = {0.135,    0.2225,   0.1575, .95}, .diffuse = {0.54,     0.89,     0.63,    .95}, .spec = {0.316228, 0.316228, 0.31622, .95}, .shine = 12.8},
	/* obsidian*/ {.ambient = {0.05375,  0.05,     0.0662, .82}, .diffuse = {0.18275,  0.17,     0.22525, .82}, .spec = {0.332741, 0.328634, 0.34643, .82}, .shine = 38.4},
	/* pearl */   {.ambient = {0.25,     0.20725,  0.2072, .92}, .diffuse = {1.0,      0.829,    0.829,   .92}, .spec = {0.296648, 0.296648, 0.29664, .92}, .shine = 11.264},
	/* ruby */    {.ambient = {0.1745,   0.01175,  0.0117, .55}, .diffuse = {0.61424,  0.04136,  0.04136, .55}, .spec = {0.727811, 0.626959, 0.62695, .55}, .shine = 76.8},
	/* turquoi */ {.ambient = {0.1,      0.18725,  0.1745,  .8}, .diffuse = {0.396,    0.74151,  0.69102,  .8}, .spec = {0.297254, 0.30829,  0.306678, .8}, .shine = 12.8},
	/* plastic */ {.ambient = {0,        0,        0,        1}, .diffuse = {0.01,     0.01,     0.01,      1}, .spec = {0.5,      0.5,      0.5,       1}, .shine = 32},
	/* rubber */  {.ambient = {0.02,     0.02,     0.02,     1}, .diffuse = {0.01,     0.01,     0.01,      1}, .spec = {0.4,      0.4,      0.4,       1}, .shine = 10},
	/* xxx */     {.ambient = {0.3,      0.3,      0.3,      1}, .diffuse = {0,        0,        0,         1}, .spec = {0.6,      0.6,      0.6,       1}, .shine = 30}
};

void  modelInitMaterial(Material * mat, int type)
{
	if (type == MAT_NONE)
	{
		memset(mat, 0, sizeof *mat);
		return;
	}
	if (type < 0 || type > DIM(materials))
		type = 0;
	else
		type --;

	memcpy(mat, materials + type, sizeof *mat);
}
