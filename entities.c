/*
 * entities.c : manage the list of active entity surrounding the player.
 *
 * written by T.Pierron, apr 2021.
 */

#define ENTITY_IMPL
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "entities.h"
#include "blocks.h"
#include "maps.h"
#include "glad.h"

struct EntitiesPrivate_t entities;

extern int blockInvModelCube(DATA16 ret, BlockState b, DATA8 texCoord);

/* init opengl buffer and models */
Bool entityInitStatic(void)
{
	glGenVertexArrays(1, &entities.vao);
	glGenBuffers(3, &entities.vbo); /* + vboLoc and vboMDAI */

	/* entities will use floats as for their vertices */
	glBindVertexArray(entities.vao);
	glBindBuffer(GL_ARRAY_BUFFER, entities.vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERTEX_SIZE, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, VERTEX_SIZE, (void *) 12);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, entities.vboLoc);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);

	entities.shader = createGLSLProgram("entities.vsh", "entities.fsh", NULL);
	if (! entities.shader)
		return False;

	struct BlockState_t unknownEntity = {
		0, CUBE, 0, NULL, 31,13,31,13,31,13,31,13,31,13,31,13
	};

	uint16_t buffer[36 * INT_PER_VERTEX];
	extern uint8_t texCoord[];

	memset(entities.models, 0xff, sizeof entities.models);

	blockInvModelCube(buffer, &unknownEntity, texCoord);

	glBindBuffer(GL_ARRAY_BUFFER, entities.vbo);
	glBufferData(GL_ARRAY_BUFFER, 36 * VERTEX_SIZE, NULL, GL_STATIC_DRAW);
	float * vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	DATA16  tex    = (DATA16) (vertex + 3);
	DATA16  buf    = buffer;
	int     i;

	/* convert block vertex into entity vertex (this operation is lossless) */
	for (i = 0; i < 36; i ++, buf += INT_PER_VERTEX, vertex += VERTEX_SIZE/4, tex += VERTEX_SIZE/2)
	{
		vertex[0] = (buf[0] - BASEVTX) * (1./BASEVTX);
		vertex[1] = (buf[1] - BASEVTX) * (1./BASEVTX);
		vertex[2] = (buf[2] - BASEVTX) * (1./BASEVTX);
		uint16_t U = GET_UCOORD(buf);
		uint16_t V = GET_VCOORD(buf);
		tex[0] = U | (V << 9);
		tex[1] = GET_NORMAL(buf) | ((V & 0x180) >> 4) | 64;
	}
	entities.models[0] = 0;
	entities.vertices[0] = 36;

	glUnmapBuffer(GL_ARRAY_BUFFER);

	return True;
}

static Entity entityAlloc(void)
{
	int count = entities.count;
	int max   = entities.max;
	if (count == max)
	{
		entities.max = max += ENTITY_BATCH;
		Entity list = realloc(entities.list, max * sizeof *list + (max >> 5) * 4);
		if (list)
		{
			entities.list = list;
			/* move usage flags at end of buffer */
			count >>= 5;
			memmove(entities.usage = (DATA32) (list + max), list + (count<<5), count * 4);
			memset(entities.usage + count, 0, 4 * (ENTITY_BATCH >> 5));
			max = count + 1;
		}
		else return NULL;
	}
	else max >>= 5;
	entities.count ++;
	return entities.list + mapFirstFree(entities.usage, max);
}

/* extract information from NBT records */
void entityParse(Chunk c, NBTFile nbt, int offset)
{
	/* <offset> points to a TAG_List_Compound of entities */
	NBTIter_t list;
	int       prev = -1;
	NBT_InitIter(nbt, offset, &list);
	while ((offset = NBT_Iter(&list)) >= 0)
	{
		STRPTR id;
		float  pos[6];
		int    off;

		/* iterate over the properties of one entity */
		NBTIter_t entity;
		NBT_InitIter(nbt, offset, &entity);
		memset(pos, 0, sizeof pos); id = NULL;
		while ((off = NBT_Iter(&entity)) >= 0)
		{
			switch (FindInList("Pos,Motion,id", entity.name, 0)) {
			case 0: NBT_ConvertToFloat(nbt, off, pos, 3); break;
			case 1: NBT_ConvertToFloat(nbt, off, pos+3, 3); break;
			case 2: id = NBT_Payload(nbt, off);
			}
		}
		if (id && !(pos[0] == 0 && pos[1] == 0 && pos[2] == 0))
		{
			Entity entity = entityAlloc();

			if (prev >= 0)
				entities.list[prev].next = entity - entities.list;
			prev = entity - entities.list;
			if (c->entityList == ENTITY_END)
				c->entityList = prev;

			memcpy(entity->pos, pos, sizeof pos);
			entity->modelId = ENTITY_UNKNOWN;
			entity->tile = nbt->mem + offset;
			entity->next = ENTITY_END;
			entity->select = 0;
			entities.dirty = 1;
		}
	}
}

static int entitySetSelection(Entity entity)
{
	int sel = entity ? entity - entities.list + 1 : 0;
	if (entities.selected != sel)
	{
		if (entities.vboLoc)
		{
			float val = 255;
			glBindBuffer(GL_ARRAY_BUFFER, entities.vboLoc);
			if (entities.selected > 0)
				glBufferSubData(GL_ARRAY_BUFFER, entities.selected * 16 - 4, 4, &val),
				entities.list[entities.selected-1].select = 0;
			val = 511;
			if (sel > 0)
				glBufferSubData(GL_ARRAY_BUFFER, sel * 16 - 4, 4, &val);
		}
		if (entity) entity->select = 1;
		entities.selected = sel;
	}
	return sel;
}

static void entityGetBBoxForFace(Entity entity, int side, float points[6], vec4 norm)
{
	extern uint8_t vertex[];
	extern uint8_t cubeIndices[];
	extern int8_t  normals[];
	DATA8 pt = vertex + cubeIndices[side * 4 + 1];
	points[VX] = pt[0] - 0.5 + entity->pos[VX];
	points[VY] = pt[1] - 0.5 + entity->pos[VY];
	points[VZ] = pt[2] - 0.5 + entity->pos[VZ];

	pt = vertex + cubeIndices[side * 4 + 3];
	points[VX+3] = pt[0] - 0.5 + entity->pos[VX];
	points[VY+3] = pt[1] - 0.5 + entity->pos[VY];
	points[VZ+3] = pt[2] - 0.5 + entity->pos[VZ];

	if (points[0] > points[3]) norm[0] = points[0], points[0] = points[3], points[3] = norm[0];
	if (points[2] > points[5]) norm[0] = points[2], points[2] = points[5], points[5] = norm[0];

	int8_t * normal = normals + side * 4;
	norm[VX] = normal[VX];
	norm[VY] = normal[VY];
	norm[VZ] = normal[VZ];
	norm[VT] = 1;
}

int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);

/* check if vector <dir> intersects an entity bounding box (from position <camera>) */
int entityRaycast(Chunk c, vec4 dir, vec4 camera, vec4 cur)
{
	float maxDist = cur ? vecDistSquare(camera, cur) * 1.5 : 1e6;
	int   flags = (dir[VX] < 0 ? 2 : 8) | (dir[VY] < 0 ? 16 : 32) | (dir[VZ] < 0 ? 1 : 4);

	if (c->entityList == ENTITY_END || ! entities.list)
	{
		if (entities.selected > 0)
			entitySetSelection(NULL);
		return 0;
	}
	Entity list = list = entities.list + c->entityList;
	for (;;)
	{
		if (vecDistSquare(camera, list->pos) < maxDist)
		{
			float points[6];
			vec4  norm, inter;
			int   i;
			/* assume rectangular bounding box (not necessarily axis aligned though) */
			for (i = 0; i < 6; i ++)
			{
				/* XXX need to rely on cross-product instead */
				if ((flags & (1 << i)) == 0) continue;

				entityGetBBoxForFace(list, i, points, norm);
				if (intersectRayPlane(camera, dir, points, norm, inter))
				{
					/* check if points is contained within the face */
					if (points[VX] <= inter[VX] && inter[VX] <= points[VX+3] &&
					    points[VY] <= inter[VY] && inter[VY] <= points[VY+3] &&
					    points[VZ] <= inter[VZ] && inter[VZ] <= points[VZ+3])
					{
						/* intersect */
						return entitySetSelection(list);
					}
				}
			}
		}
		if (list->next == ENTITY_END)
		{
			if (entities.selected > 0)
				entitySetSelection(NULL);
			return 0;
		}
		list = entities.list + list->next;
	}
}

/* chunk <c> is about to be unloaded, remove all entities references we have here */
void entityUnload(Chunk c)
{
	//
}

void entityAnimate(void)
{
	//
}

void entityRender(void)
{
	if (entities.dirty)
	{
		/* rebuild vboLoc and vboMDAI */
		glBindBuffer(GL_ARRAY_BUFFER, entities.vboLoc);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, entities.vboMDAI);

		if (entities.mdaiCount < entities.max)
		{
			/* realloc buffers */
			glBufferData(GL_ARRAY_BUFFER, entities.max * 16, NULL, GL_STATIC_DRAW);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, entities.max * 16, NULL, GL_STATIC_DRAW);
			entities.mdaiCount = entities.max;
			fprintf(stderr, "realloc VBO %d\n", entities.max);
		}

		float * loc = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		MDAICmd cmd = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);
		Entity  ent = entities.list;
		int     i;
		for (i = 0; i < entities.count; ent ++)
		{
			if (ent->tile == NULL) continue;
			memcpy(loc, ent->pos, 12);
			loc[3] = ent->select ? 511 : 255; /* skylight/blocklight */
			int model = ent->modelId;
			cmd->baseInstance = i;
			cmd->count = entities.vertices[model];
			cmd->first = entities.models[model];
			cmd->instanceCount = 1;
			i ++; loc += 4; cmd ++;
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
		entities.dirty = 0;
	}
	if (entities.count > 0)
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glUseProgram(entities.shader);
		glBindVertexArray(entities.vao);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, entities.vboMDAI);
		glMultiDrawArraysIndirect(GL_TRIANGLES, 0, entities.count, 0);
	}
}
