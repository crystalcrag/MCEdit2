/*
 * quadtree.c : space partioning based on a quad tree.
 *
 * inspired by https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/introduction-to-octrees-r3529/
 *
 * the purpose of this module is to be able to quickly enumerate entities that intersect a 3d AABB:
 * see doc/internals.html for a quick overview of how this module works.
 *
 * written by T.Pierron, dec 2021.
 */

#define ENTITY_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "entities.h"

#define MIN_SIZE     1
#define QUAD_BATCH   96

/* private datatypes */
struct QuadBatch_t
{
	struct QuadBatch_t * next;
	struct QuadTree_t batch[QUAD_BATCH];
	uint32_t usage[QUAD_BATCH/32];
	uint8_t count;
};

struct QuadSelect_t
{
	Entity * list;
	int      count, max;
};

static struct QuadBatch_t    qmem;
static struct QuadTree_t *   qroot;
static struct QuadSelect_t   qselected;
typedef struct QuadBatch_t * QuadBatch;

static QuadTree quadTreeAlloc(void)
{
	QuadBatch * prev;
	QuadBatch   mem;
	/* alloc the node in batch, but we need to avoid relocating pointers (no realloc()) */
	for (mem = &qmem, prev = NULL; mem->count == QUAD_BATCH; prev = &qmem.next, mem = mem->next);
	if (mem == NULL)
	{
		mem = malloc(sizeof *mem);
		if (mem == NULL) return NULL;
		memset(mem->usage, 0, sizeof mem->usage);
		mem->count = 0;
		mem->next = NULL;
		*prev = mem;
	}
	mem->count ++;
	QuadTree tree = mem->batch + mapFirstFree(mem->usage, QUAD_BATCH/32);
	memset(tree, 0, sizeof *tree);
	return tree;
}

static void quadTreeFree(QuadTree node)
{
	QuadBatch mem;
	for (mem = &qmem; mem; mem = mem->next)
	{
		if (mem->batch <= node && node < EOT(mem->batch))
		{
			int slot = node - mem->batch;
			mem->usage[slot>>5] &= ~(1 << (slot & 31));
			mem->count --;
			break;
		}
	}
}

/* initial size does not really matter, quad tree will be readjusted if needed (shrinked or enlarged) */
void quadTreeInit(int x, int z, int size)
{
	QuadTree tree = qroot = quadTreeAlloc();

	/* get next power of 2 */
	-- size;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size ++;

	/* use chunk boundary: less likely that an entity is split between these */
	tree->x = (x & ~15) - (size >> 1);
	tree->z = (z & ~15) - (size >> 1);
	tree->size = size;

	/* pre-allocate some space */
	qselected.list = malloc(sizeof *qselected.list * 32);
	qselected.max  = 32;

	fprintf(stderr, "quad tree size = %d\n", size);
}

/* start from scratch (will have to call quadTreeInit() first) */
void quadTreeClear(void)
{
	QuadBatch mem;
	for (mem = &qmem; mem; mem = mem->next)
	{
		mem->count = 0;
		memset(mem->usage, 0, sizeof mem->usage);
	}
	free(qselected.list);
	memset(&qselected, 0, sizeof qselected);
	qroot = NULL;
}

static void quadTreeInsert(QuadTree root, Entity item)
{
	if ((root->items == NULL && root->nbLeaf == 0) || root->size <= MIN_SIZE)
	{
		insert:
		item->qnode = root;
		item->qnext = root->items;
		root->items = item;
	}
	else
	{
		float x  = root->x;
		float z  = root->z;
		float sz = root->size * 0.5f;
		float x2 = x + sz;
		float z2 = z + sz;
		float scale = ENTITY_SCALE(item);
		float szEntX = item->szx * scale;
		float szEntZ = item->szz * scale;
		uint8_t quadrant = 0;

		if (item->pos[VX] + szEntX <  x2) ; else
		if (item->pos[VX] - szEntX >= x2) quadrant |= 1; else { item->enflags |= ENFLAG_OVERLAP; goto insert; } /* overlap multiple quadrants */
		if (item->pos[VZ] + szEntZ <  z2) ; else
		if (item->pos[VZ] - szEntZ >= z2) quadrant |= 2; else { item->enflags |= ENFLAG_OVERLAP; goto insert; }

		QuadTree child = root->quadrants[quadrant];
		uint8_t  check = 0;
		if (child == NULL)
		{
			child = quadTreeAlloc();
			child->x = x + (quadrant & 1 ? sz : 0);
			child->z = z + (quadrant & 2 ? sz : 0);
			child->size = sz;
			child->parent = root;
			root->quadrants[quadrant] = child;
			root->nbLeaf ++;
			check = root->nbLeaf == 1;
		}
		item->enflags &= ~ENFLAG_OVERLAP;
		/* check if we can push the item deeper */
		if (check)
		{
			/* is about to be split: check if we need to move items into quadrant */
			Entity node, next, * prev;
			for (node = next = root->items, prev = &root->items; node; node = next)
			{
				next = node->qnext;
				if (node->enflags & ENFLAG_OVERLAP)
				{
					/* cannot be pushed down */
					prev = &node->qnext;
					continue;
				}
				/* unlink and reinsert */
				*prev = node->qnext;
				quadTreeInsert(root, node);
			}
		}
		quadTreeInsert(child, item);
	}
}

static Entity quadTreeRemoveItem(QuadTree root, Entity item)
{
	Entity   prune = NULL, * prev;
	QuadTree node  = item->qnode;

	/* remove <item> for its quad tree node */
	for (prev = &node->items, prune = *prev; prune != item; prev = &prune->qnext, prune = prune->qnext);
	*prev = item->qnext;
	prune = NULL;
	while (node != root && node->items == NULL && node->nbLeaf == 0)
	{
		/* prune some branch */
		node = node->parent;
		uint8_t i;
		for (i = 0; i < 4; i ++)
		{
			if (node->quadrants[i] == item->qnode)
			{
				node->quadrants[i] = NULL;
				quadTreeFree(item->qnode);
				node->nbLeaf --;
				item->qnode = node;
				/* if only one leaf remains with 1 item in it: prune it too */
				if (! prune && node->nbLeaf == 1 && node->items == NULL)
				{
					for (i = 0; i < 4 && node->quadrants[i] == NULL; i ++);
					QuadTree sub = node->quadrants[i];
					if (sub->nbLeaf == 0 && sub->items->qnext == NULL)
						prune = sub->items;
				}
				break;
			}
		}
	}
	return prune;
}

/* fuse leaf nodes into higher level quadrant to keep the tree mostly balanced */
static void quadTreePrune(QuadTree root, Entity item)
{
	QuadTree sub, prev;
	Entity   insert = NULL;

	for (sub = prev = item->qnode->parent; sub != root && sub->nbLeaf == 1 && sub->items == NULL; prev = sub, sub = sub->parent)
	{
		uint8_t i;
		for (i = 0; i < 4 && sub->quadrants[i] == NULL; i ++);
		QuadTree quadrant = sub->quadrants[i];
		/* one item and no leaves */
		if (quadrant->nbLeaf == 0)
		{
			Entity first = quadrant->items;
			if (first == NULL)
				;
			else if (first->qnext == NULL)
				insert = first, quadrant->items = NULL;
			else break;
			sub->nbLeaf --;
			quadTreeFree(quadrant);
			sub->quadrants[i] = NULL;
		}
	}
	if (insert)
	{
		insert->qnext = prev->items;
		prev->items = insert;
		insert->qnode = prev;
	}
}

/* high-level interface for removing an item */
void quadTreeDeleteItem(Entity item)
{
	/* not every entity are in quad tree (temporary ones aren't) */
	if ((item->enflags & ENFLAG_INQUADTREE) == 0)
		return;

	QuadTree root  = qroot;
	Entity   prune = quadTreeRemoveItem(root, item);

	if (prune)
		quadTreePrune(root, prune);

	/* also check if we can prune top-level */
	while (root->nbLeaf == 1 && root->items == NULL)
	{
		int i;
		for (i = 0; i < 4 && root->quadrants[i] == NULL; i ++);
		QuadTree quadrant = root->quadrants[i];
		quadTreeFree(root);
		root = qroot = quadrant;
		root->parent = NULL;
		fprintf(stderr, "quad tree size = %d\n", root->size);
	}
}

/* high level insertion function */
void quadTreeInsertItem(Entity item)
{
	QuadTree root = qroot;
	float scale  = ENTITY_SCALE(item);
	float szEntX = item->szx * scale;
	float szEntZ = item->szz * scale;

	item->enflags |= ENFLAG_INQUADTREE;
	/* check if quadtree is big enough */
	for (;;)
	{
		uint8_t flags = 0, quadrant;
		float sz = root->size;
		if (item->pos[VX] + szEntX < root->x)      flags |= 1; else
		if (item->pos[VX] - szEntX > root->x + sz) flags |= 2;
		if (item->pos[VZ] + szEntZ < root->z)      flags |= 4; else
		if (item->pos[VZ] - szEntZ > root->z + sz) flags |= 8;

		if (flags > 0)
		{
			/* quad tree too small: add a top layer */
			float size = root->size;
			QuadTree super = quadTreeAlloc();
			quadrant = 0;
			root->parent = super;
			super->x = root->x;
			super->z = root->z;
			if (flags & 1)
				quadrant |= 1, super->x -= size;
			if (flags & 4)
				quadrant |= 2, super->z -= size;
			super->size = size * 2;
			super->quadrants[quadrant] = root;
			super->nbLeaf = 1;
			root = qroot = super;
			fprintf(stderr, "quad tree size = %d\n", root->size);
		}
		else break;
	}
	quadTreeInsert(root, item);
}

/* relocate one item in the quad tree */
void quadTreeChangePos(Entity item)
{
	if ((item->enflags & ENFLAG_INQUADTREE) == 0)
		return;

	QuadTree root = qroot;
	QuadTree node = item->qnode;
	float SZ = ENTITY_SCALE(item);
	float X2 = item->szx * SZ;
	float Z2 = item->szz * SZ;
	float X  = item->pos[VX] - X2;
	float Z  = item->pos[VZ] - Z2;
	X2 += item->pos[VX];
	Z2 += item->pos[VZ];

	/* check if it is outside of current tree */
	if (node->x <= X && X2 < node->x + node->size &&
	    node->z <= Z && Z2 < node->z + node->size)
	{
		if ((item->enflags & ENFLAG_OVERLAP) == 0)
			/* still in the box: nothing to change */
			return;

		float xm = node->x + (node->size >> 1);
		float zm = node->z + (node->size >> 1);

		/* check if item still overlaps border */
		if (X2 >= xm && X < xm) return; /* still overlap multiple quadrants */
		if (Z2 >= zm && Z < zm) return;
	}

	Entity prune = quadTreeRemoveItem(root, item);
	item->qnode = NULL;
	item->enflags &= ~ENFLAG_OVERLAP;
	quadTreeInsert(root, item);
	/* avoid useless dealloc/alloc */
	if (prune) quadTreePrune(root, prune);
}

/* that's the main purpose of this datatype: get all nodes that intersect <bbox> without having to scan everything */
static void quadTreeFindEntities(QuadTree root, float bbox[6], int filter)
{
	/* check for top-level node first */
	Entity item;
	for (item = root->items; item; item = item->qnext)
	{
		if (filter & ENFLAG_EQUALZERO ? (item->enflags & filter) == 0 : (item->enflags & filter) != 0)
			continue;

		float scale = ENTITY_SCALE(item);
		float SX = item->szx * scale;
		float SZ = item->szz * scale;
		float SY = item->szy * scale;
		float X  = item->pos[VX];
		float Y  = item->pos[VY];
		float Z  = item->pos[VZ];
		if (bbox[VX] < X+SX && bbox[VX+3] > X-SX &&
		    bbox[VY] < Y+SY && bbox[VY+3] > Y-SY &&
		    bbox[VZ] < Z+SZ && bbox[VZ+3] > Z-SZ)
		{
			/* intersecting bounding box */
			if (qselected.count == qselected.max)
			{
				qselected.max += 32;
				Entity * list = realloc(qselected.list, qselected.max * sizeof *qselected.list);
				if (! list) continue;
				qselected.list = list;
			}
			qselected.list[qselected.count++] = item;
		}
	}
	if (root->nbLeaf > 0)
	{
		static uint8_t flags[] = {0,0,0,0,0,1,2,3,0,4,8,12,0,5,10,15};
		float mx = root->x + (root->size >> 1);
		float mz = root->z + (root->size >> 1);
		int   check = 0;
		int   i;

		if (bbox[3] <  mx) check |= 1; else
		if (bbox[0] >= mx) check |= 2; else check |= 3;
		if (bbox[5] <  mz) check |= 4; else
		if (bbox[2] >= mz) check |= 8; else check |= 12;

		for (check = flags[check], i = 0; check; check >>= 1, i ++)
		{
			if ((check & 1) == 0) continue;
			QuadTree quadrant = root->quadrants[i];
			if (quadrant)
				quadTreeFindEntities(quadrant, bbox, filter);
		}
	}
}

Entity * quadTreeIntersect(float bbox[6], int * count, int filter)
{
	qselected.count = 0;
	quadTreeFindEntities(qroot, bbox, filter);
	*count = qselected.count;
	return qselected.list;
}

#ifdef DEBUG
/* render on screen quad tree with entity location */
#define MARGIN    20
#include "nanovg.h"
#include "globals.h"
#include "selection.h"
static void quadTreeRender(QuadTree root, APTR vg, float bbox[4])
{
	float x = (root->x - bbox[0]) * bbox[2] + MARGIN;
	float z = (root->z - bbox[1]) * bbox[3] + MARGIN;
	nvgStrokeColorRGBA8(vg, "\x20\x20\x20\xff");
	nvgBeginPath(vg);
	nvgRect(vg, x, z, root->size * bbox[2], root->size * bbox[3]);
	nvgStroke(vg);
	int i;

	nvgStrokeColorRGBA8(vg, "\xff\xff\xff\xff");
	Entity item;
	for (item = root->items; item; item = item->qnext)
	{
		float scale = ENTITY_SCALE(item);
		float SX = item->szx * scale;
		float SZ = item->szz * scale;
		nvgBeginPath(vg);
		nvgRect(vg, (item->pos[VX] - SX - bbox[0]) * bbox[2] + MARGIN, (item->pos[VZ] - SZ - bbox[1]) * bbox[3] + MARGIN,
			SX * 2 * bbox[2], SZ * 2 * bbox[3]);
		nvgStroke(vg);
	}

	if (root->nbLeaf > 0)
	for (i = 0; i < 4; i ++)
	{
		if (! root->quadrants[i])
		{
			nvgStrokeColorRGBA8(vg, "\xff\x20\x20\xff");
			nvgBeginPath(vg);
			float szx = (root->size >> 1) * bbox[2];
			float szz = (root->size >> 1) * bbox[3];
			float x2 = x + (i & 1 ? szx : 0);
			float z2 = z + (i & 2 ? szz : 0);
			nvgMoveTo(vg, x2, z2);
			nvgLineTo(vg, x2 + szx, z2 + szz);
			nvgMoveTo(vg, x2 + szx, z2);
			nvgLineTo(vg, x2, z2 + szz);
			nvgStroke(vg);
		}
		else quadTreeRender(root->quadrants[i], vg, bbox);
	}
}

void quadTreeDebug(APTR vg)
{
	float bbox[4] = {
		qroot->x, qroot->z,
		(globals.width - 2*MARGIN) / (float) qroot->size,
		(globals.height - 2*MARGIN) / (float) qroot->size
	};
	quadTreeRender(qroot, vg, bbox);
	if ((globals.selPoints&3) == 3)
	{
		int points[6];
		selectionGetRange(points, True);
		nvgStrokeColorRGBA8(vg, "\xff\xff\x20\xff");
		nvgBeginPath(vg);
		nvgRect(vg, (points[0] - bbox[0]) * bbox[2] + MARGIN, (points[2] - bbox[1]) * bbox[3] + MARGIN,
			points[3] * bbox[2], points[5] * bbox[3]);
		nvgStroke(vg);
	}
}
#endif
