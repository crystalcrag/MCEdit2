/*
 * models.h: public function for reading/generating models/basic shapes
 *
 * Written by T.Pierron, Dec 2019.
 */

#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include "utils.h"

typedef struct Model_t *       Model;
typedef struct Material_t      Material;

struct Model_t
{
	int        vertex, index, flags;
	float *    vertices;
	float *    tex;
	float *    normals;
	float *    tangent;
	uint16_t * indices;
};

struct Material_t
{
	float ambient[4];
	float diffuse[4];
	float spec[4];
	float shine;
};

Model modelSphere(float size, int subdiv);
Model modelTorus(int sides, int small_sides, float radius, float small_radius);
Model modelParseObj(const char * path);
void  modelFree(Model);
void  modelInitMaterial(Material *, int type);
Bool  modelGetTangent(Model);

enum /* values for <type> parameter of modelInitMaterial */
{
	MAT_NONE,
	MAT_BRASS,
	MAT_BRONZE,
	MAT_PBRONZE, /* polished */
	MAT_CHROME,
	MAT_COPPER,
	MAT_PCOPPER, /* polished */
	MAT_GOLD,
	MAT_PGOLD,   /* polished */
	MAT_PEWTER,
	MAT_SILVER,
	MAT_PSILVER, /* polished */
	MAT_EMERALD,
	MAT_JADE,
	MAT_OBSIDIAN,
	MAT_PEARL,
	MAT_RUBY,
	MAT_TURQUOISE,
	MAT_BLACKPLASTIC,
	MAT_BLACKRUBBER,
	MAT_CUSTOM
};

enum /* Model.flags */
{
	MODEL_STATIC_VERTEX = 1,
	MODEL_STATIC_TEX    = 2,
	MODEL_STATIC_NORM   = 4,
	MODEL_STATIC_ALL    = 7,
	MODEL_TRIANGLES     = 8,  /* otherwise strip */
	MODEL_CCW           = 16
};

#endif
