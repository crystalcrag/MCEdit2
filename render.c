/*
 * render.c : render world using openGL: this is the heart of the rendering engine.
 *
 * VBO/VAO are allocated here.
 *
 * written by T.Pierron, july 2020
 */

#define RENDER_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "blocks.h"
#include "render.h"
#include "items.h"
#include "particles.h"
#include "sign.h"
#include "skydome.h"
#include "entities.h"
#include "nanovg.h"
#include "SIT.h"

struct RenderWorld_t render;
extern double        curTime;
static ListHead      meshBanks;    /* MeshBuffer */
static ListHead      alphaBanks;   /* MeshBuffer */
static ListHead      gpuBanks;     /* GPUBank */

/* fixed shading per face (somewhat copied from minecraft): S, E, N, W, T, B */
static float shading[] = {
	0.9, 0, 0, 0, /* 16 bytes alignment requirement for a float, really? */
	0.8, 0, 0, 0,
	0.9, 0, 0, 0,
	0.8, 0, 0, 0,
	1.0, 0, 0, 0,
	0.7, 0, 0, 0,
};

static float invShading[] = { /* inventory shading for 3d blocks */
	0.65, 0, 0, 1,
	0.75, 0, 0, 0,
	0.65, 0, 0, 0,
	0.75, 0, 0, 0,
	1.0,  0, 0, 0,
	0.75, 0, 0, 0,
};

int8_t normals[] = { /* normal per face */
	 0,  0,  1, 0,
	 1,  0,  0, 0,
	 0,  0, -1, 0,
	-1,  0,  0, 0,
	 0,  1,  0, 0,
	 0, -1,  0, 0
};

void GLAPIENTRY debugGLError(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	STRPTR str, sev;
	TEXT   typeUnknown[64];
	switch (type) {
	case GL_DEBUG_TYPE_ERROR:               str = "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: str = "DEPRECATED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  str = "UNDEFINED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_PORTABILITY:         str = "PORTABILITY"; break;
	case GL_DEBUG_TYPE_PERFORMANCE:         str = "PERFORMANCE"; break;
	case GL_DEBUG_TYPE_OTHER:               str = "OTHER"; break;
	default:                                sprintf(str = typeUnknown, "TYPE:%d", type);
	}
    switch (severity){
    case GL_DEBUG_SEVERITY_LOW:    sev = "LOW"; break;
    case GL_DEBUG_SEVERITY_MEDIUM: sev = "MEDIUM"; break;
    case GL_DEBUG_SEVERITY_HIGH:   sev = "HIGH"; break;
    default:                       return; /* info stuff, don't care */
    }
	fprintf(stderr, "src: %d, id: %d, type: %s, sev: %s, %s\n", source, id, str, sev, message);
}

/* render what's being currently selected */
static void renderSelection(void)
{
	Item item = &render.inventory->items[render.inventory->selected];

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	render.selection.sel &= ~SEL_NOCURRENT;
	if (item->id > 0 && (render.debugInfo & DEBUG_SELECTION) == 0)
	{
		/* preview block */
		int8_t * offset;
		int      id = item->id;
		vec4     loc;

		if (id >= ID(256, 0))
		{
			ItemDesc desc = itemGetById(item->id);
			if (desc == NULL || (id = desc->refBlock) == 0)
				goto highlight_bbox;
			id <<= 4;
		}

		struct BlockOrient_t info = {
			.pointToId = render.selection.extra.blockId,
			.direction = render.direction,
			.side      = render.selection.extra.side,
			.topHalf   = render.selection.extra.topHalf,
			.yaw       = render.yaw
		};

		if (blockIds[id >> 4].placement > 0)
		{
			switch (blockAdjustPlacement(id, &info)) {
			case PLACEMENT_NONE:
				/* placement not possible, cancel everything */
				render.selection.sel |= SEL_NOCURRENT;
				return;
			case PLACEMENT_GROUND:
				/* check if ground is within 1 block reach */
				offset = normals + render.selection.extra.side * 4;
				loc[0] = render.selection.current[0] + offset[0];
				loc[1] = render.selection.current[1] - 1;
				loc[2] = render.selection.current[2] + offset[2];
				if (! blockIsSolidSide(mapGetBlockId(render.level, loc, NULL), SIDE_TOP) || info.pointToId != 0)
				{
					render.selection.sel |= SEL_NOCURRENT;
					return;
				}
				break;
			case PLACEMENT_OK:
				offset = normals + render.selection.extra.side * 4;
				loc[0] = render.selection.current[0] + offset[0];
				loc[1] = render.selection.current[1] + offset[1];
				loc[2] = render.selection.current[2] + offset[2];
				if (mapGetBlockId(render.level, loc, NULL) != 0)
				{
					render.selection.sel |= SEL_NOCURRENT;
					return;
				}
			}
		}

		/* show a preview of what is going to be placed if left-clicked */
		BlockState b = blockGetById(render.selection.extra.blockId);
		if ((b->inventory & CATFLAGS) == DECO && b->type == QUAD)
		{
			offset = normals + 5;
		}
		else offset = normals + render.selection.extra.side * 4;
		int blockId = blockAdjustOrient(id, &info, render.selection.extra.inter);
		if (info.keepPos) offset = normals + 5;

		#if 1
		static int oldBlock;
		if (oldBlock != blockId)
			fprintf(stderr, "blockId = %d:%d, side = %d, dir = %d\n", (blockId>>4) & 255, blockId & 15, info.side, info.direction), oldBlock = blockId;
		#endif

		if (render.selection.blockId != blockId)
		{
			/* generate a mesh on the fly: performance is not really a concern here */
			render.selection.blockVtx = blockGenModel(render.vboPreview, blockId);
			render.selection.blockId  = blockId;
		}
		loc[0] = render.selection.current[0] + offset[0];
		loc[1] = render.selection.current[1] + offset[1];
		loc[2] = render.selection.current[2] + offset[2];
		loc[3] = 255;
		memcpy(render.selection.blockPos, loc, sizeof loc);
		int vtx = render.selection.blockVtx;
		int wire = vtx >> 10;
		vtx &= 1023;

		glBindBuffer(GL_ARRAY_BUFFER, render.vboPreviewLoc);
		glBufferSubData(GL_ARRAY_BUFFER, 0, 16, loc);
		//float alpha = 1;
		//glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof (mat4) + 16 * 7 + 4, sizeof alpha, &alpha);

		glFrontFace(GL_CCW);
		glUseProgram(render.shaderBlocks);
		glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

		glBindVertexArray(render.vaoPreview);
		glDrawArrays(GL_TRIANGLES, 0, vtx);

		glDrawArrays(GL_LINES, vtx, wire);
		//alpha = 0;
		//glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof (mat4) + 16 * 7 + 4, sizeof alpha, &alpha);
	}
	else /* highlight bounding box instead */
	{
		vec4 loc;
		highlight_bbox:
		loc[0] = render.selection.current[0];
		loc[1] = render.selection.current[1];
		loc[2] = render.selection.current[2];

		glUseProgram(render.selection.shader);
		glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

		/* draw block bounding box */
		BlockState b   = blockGetById(render.selection.extra.blockId);
		VTXBBox    box = blockGetBBox(b); if (! box) return;
		int        off = 0;
		int        flg = render.selection.extra.cnxFlags;
		int        count;

		if (render.selection.extra.special == BLOCK_DOOR_TOP) loc[VY] --;
		loc[3] = 1 + 4;
		setShaderValue(render.selection.shader, "info", 4, loc);

		glBindVertexArray(render.vaoBBox);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.vboBBoxIdx);
		glFrontFace(GL_CCW);

		/* too complex to do in the vertex/geometry shader */
		static int lastId, lastFlag, lastCount;
		/* rearrange element array on the fly: performance does not really matter here */
		if (lastId != b->id || lastFlag != flg)
		{
			count = lastCount = blockGenVertexBBox(b, box, flg, &render.vboBBoxVTX);
			lastId = b->id;
			lastFlag = flg;
			//fprintf(stderr, "block = %s, cnx = %d\n", b->name, flg);
		}
		else count = lastCount;

		/* filled polygon */
		glDepthMask(GL_FALSE);
		glDrawElements(GL_TRIANGLES, count & 0xffff, GL_UNSIGNED_SHORT, 0);

		loc[3] -= 4;
		setShaderValue(render.selection.shader, "info", 4, loc);

		/* edge highlight */
		off += (count & 0xffff) * 2;
		count >>= 16;
		glDrawElements(GL_LINES, count, GL_UNSIGNED_SHORT, (APTR) off);

		/* hidden part of selection box */
		loc[3] += 8;
		setShaderValue(render.selection.shader, "info", 4, loc);
		glDepthFunc(GL_GEQUAL);
		glDrawElements(GL_LINES, count, GL_UNSIGNED_SHORT, (APTR) off);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
	}
	glBindVertexArray(0);
}

/* from mouse pos mx, my: pickup block pointed at this location using ray casting */
void renderPointToBlock(int mx, int my)
{
	if (mx < 0) mx = render.mouseX, my = render.mouseY;
	/* this method has been ripped off from: https://stackoverflow.com/questions/2093096/implementing-ray-picking */
	vec4 clip = {mx * 2. / render.width - 1, 1 - my * 2. / render.height, 0, 1};
	vec4 dir;

	matMultByVec(dir, render.matInvMVP, clip);

	dir[VX] = dir[VX] / dir[VT] - render.camera[VX];
	dir[VY] = dir[VY] / dir[VT] - render.camera[VY];
	dir[VZ] = dir[VZ] / dir[VT] - render.camera[VZ];

	/* XXX why is the yaw/pitch ray picking off compared to a MVP matrix ??? */
	//if (mapPointToBlock(render.level, render.camera, &render.yaw, NULL, render.selection.current, &render.selection.extra))
	if (mapPointToBlock(render.level, render.camera, NULL, dir, render.selection.current, &render.selection.extra))
		render.selection.sel |= SEL_CURRENT;
	else
		render.selection.sel &= ~(SEL_CURRENT | SEL_NOCURRENT);

	render.mouseX = mx;
	render.mouseY = my;
}

MapExtraData renderGetSelectedBlock(vec4 pos, int * blockModel)
{
	if ((render.selection.sel & (SEL_CURRENT|SEL_NOCURRENT)) == SEL_CURRENT)
	{
		if (pos)
		{
			Item item = &render.inventory->items[render.inventory->selected];
			memcpy(pos, item->id > 0 && (render.debugInfo & DEBUG_SELECTION) == 0 ? render.selection.blockPos : render.selection.current, sizeof (vec4));
		}
		if (blockModel)
		{
			*blockModel = render.selection.blockId;
		}
		return &render.selection.extra;
	}
	return NULL;
}

/* SITE_OnResize on root widget */
static int renderGetSize(SIT_Widget w, APTR cd, APTR ud)
{
	float * size = cd;
	render.width  = size[0];
	render.height = size[1];
	/* aspect ratio (needed by particle.gsh) */
	shading[1] = render.width / (float) render.height;
	matPerspective(render.matPerspective, DEF_FOV, shading[1], NEAR_PLANE, 1000);
	glViewport(0, 0, render.width, render.height);

	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, 16, shading);

	return 1;
}

/* init static tables and objects */
Bool renderInitStatic(int width, int height, APTR sitRoot)
{
	vec4 lightPos = {0, 1, 1, 1};

	#ifdef DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(debugGLError, NULL);
	#endif

	Bool compiled =
	(render.shaderParticles  = createGLSLProgram("particles.vsh", "particles.fsh", "particles.gsh")) &&
	(render.shaderBlocks     = createGLSLProgram("blocks.vsh",    "blocks.fsh", NULL)) &&
	(render.selection.shader = createGLSLProgram("selection.vsh", "selection.fsh", NULL));

	if (! compiled)
		return False;

	/* init VBO for vboInventoryLoc, vboPreview, vboPreviewLoc, vboInventory, vboParticles */
	glGenBuffers(6, &render.vboInventoryMDAI);

	/* will init vaoInventory, vaoBBox, vaoPreview and vaoParticles */
	glGenVertexArrays(4, &render.vaoInventory);

	/* init all the static tables */
	mapInitStatic();
	chunkInitStatic();
	halfBlockInit();
	// playerInitPickup(&render.pickup);
	if (! jsonParse(RESDIR "blocksTable.js", blockCreate))
		return False;
	if (! jsonParse(RESDIR "itemsTable.js", itemCreate))
		return False;
	if (! skydomeInit(render.matMVP))
		return False;
	itemInitHash();
	blockParseConnectedTexture();
	blockParseBoundingBox();
	blockParseInventory(render.vboInventory);
	particlesInit(render.vboParticles);
	if (! entityInitStatic())
		return False;

	/* main texture file */
	render.texBlock = textureLoad(RESDIR, "terrain.png", 1, blockPostProcessTexture);

	memcpy(render.lightPos, lightPos, sizeof lightPos);

	/* inventory item: will use same shaders as block models, therefore same VAO (but different VBO) */
	glBindVertexArray(render.vaoInventory);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventory);
	glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryLoc);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);

	/* vao for preview: will also use shader from block models */
	glBindVertexArray(render.vaoPreview);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboPreview);
	glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboPreviewLoc);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);

	/* vao for particles */
	glBindVertexArray(render.vaoParticles);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboParticles);
	glBufferData(GL_ARRAY_BUFFER, PARTICLES_VBO_SIZE * PARTICLES_MAX, NULL, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, PARTICLES_VBO_SIZE, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_INT, PARTICLES_VBO_SIZE, (void *) 12);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	/* vao for bbox highlight */
	glGenBuffers(2, &render.vboBBoxVTX);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxVTX);
	/* 8 vertices per bbox, 12 bytes per vertex (3 floats), 20 max bounding box */
	glBufferData(GL_ARRAY_BUFFER, 8 * 6 * 20, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxIdx);
	/* indirect buffer: 36 for faces, 24 for lines, 2 bytes per index, 20 sets max */
	glBufferData(GL_ARRAY_BUFFER, (24 + 36) * 2 * 20, NULL, GL_STATIC_DRAW);

	glBindVertexArray(render.vaoBBox);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxVTX);
	glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, 0, 0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);


	/* XXX pre-alloc memory for inventory items: MDAI will fail if we use glBufferData() instead of glBufferSubData() */
	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryMDAI);
	glBufferData(GL_ARRAY_BUFFER, 16 * 100, NULL, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryLoc);
	glBufferData(GL_ARRAY_BUFFER, 16 * 100, NULL, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, render.vboPreviewLoc);
	glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_DYNAMIC_DRAW); /* yep, that's it: only one item to draw */

	glBindBuffer(GL_ARRAY_BUFFER, render.vboPreview);
	glBufferData(GL_ARRAY_BUFFER, 300 * BYTES_PER_VERTEX, NULL, GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	/* model matrix for inventory items */
	{
		mat4 temp;
		matRotate(render.matInventoryItem, -M_PI_4-M_PI_2, 1);
		matRotate(temp, -M_PI_4/2, 0);
		matMult(render.matInventoryItem, temp, render.matInventoryItem);
	}

	/* pre-conpute perspective projection matrix */
	render.width = width;
	render.height = height;
	shading[1] = render.width / (float) render.height;
	matPerspective(render.matPerspective, DEF_FOV, shading[1], NEAR_PLANE, 1000);

	/* normals vector for cube as ordered in blocks.vsh */
	static float normals[] = {
		0,0,1,1,  1,0,0,1,  0,0,-1,1,  -1,0,0,1,  0,1,0,1,  0,-1,0,1
	};

	/*
	 * uniform buffer object: shared among all shaders (uniformBlock.glsl):
	 * mat4 projMatrix;
	 * mat4 mvMatrix;
	 * vec4 lightPos;
	 * vec4 normals[6];
	 * float shading[6];
	 */
	glGenBuffers(1, &render.uboShader);
	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);
	glBufferData(GL_UNIFORM_BUFFER, sizeof (mat4) * 2 + sizeof (vec4) * (2+6+6), NULL, GL_STATIC_DRAW);

	/* these should rarely change */
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);
	glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof (mat4) + 2 * sizeof (vec4), sizeof normals, normals);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, sizeof shading, shading);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BUFFER_INDEX, render.uboShader);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	/* HUD resources */
	SIT_GetValues(sitRoot, SIT_NVGcontext, &render.nvgCtx, NULL);
	render.sitRoot = sitRoot;
	render.compass = nvgCreateImage(render.nvgCtx, RESDIR INTERFACE "compass.png", 0);
	render.debugFont = nvgFindFont(render.nvgCtx, "sans-serif"); /* created by SITGL */
	render.nvgTerrain = nvgCreateImage(render.nvgCtx, (APTR) render.texBlock, NVG_IMAGE_NEAREST | NVG_IMAGE_GLTEX);

	/* scale of inventory items (182 = px width of inventory bar) */
	render.scale = render.width / (3 * 182.) * ITEMSCALE;
	SIT_AddCallback(sitRoot, SITE_OnResize, renderGetSize, NULL);

	/* will need to measure some stuff before hand */
	return signInitStatic(render.nvgCtx, render.debugFont);
}

void renderSetInventory(Inventory inventory)
{
	render.inventory = inventory;
	if (inventory->texture == 0)
		inventory->texture = nvgCreateImage(render.nvgCtx, RESDIR "widgets.png", NVG_IMAGE_NEAREST);
    inventory->update = 1;
}

#if 0
static void renderPickup(void)
{
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);

	glUseProgram(render.shaderCust);
	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), render.pickup.model);

	vec4 loc = {0, 0, 0, 255};
	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryLoc);
	glBufferData(GL_ARRAY_BUFFER, sizeof loc, loc, GL_STATIC_DRAW);

	int size = blockInvGetModelSize(render.pickup.blockId);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);
	glBindVertexArray(render.vaoInventory);
	glDrawArrays(GL_TRIANGLES, size & 0xfffff, size >> 20);
	glBindVertexArray(0);
}
#endif

Map renderInitWorld(STRPTR path, int renderDist)
{
	render.level = mapInitFromPath(path, renderDist);
	if (render.level)
	{
		render.debug = 0;
		render.camera[VX] = render.level->cx;
		render.camera[VY] = render.level->cy + PLAYER_HEIGHT;
		render.camera[VZ] = render.level->cz;

		/* actually just allocate some vbo */
		debugToggle(render.level);

		return render.level;
	}
	return NULL;
}

/* show limits of chunk player is currently in */
void renderToggleDebug(int what)
{
	render.debug ^= what;
}

/* print info from VBO */
void renderDebugBlock(void)
{
	debugBlockVertex(render.level, &render.selection);
}

/* screen size changed */
void renderResize(int width, int height)
{
	render.width  = width;
	render.height = height;
	render.scale = render.width / (3 * 182.) * ITEMSCALE;
	render.inventory->update ++;
}

void renderResetViewport(void)
{
	glViewport(0, 0, render.width, render.height);
}

/* view matrix change (pos and/or angle) */
void renderSetViewMat(vec4 pos, vec4 lookat, float * yawPitch)
{
	float old[3];
	memcpy(old, render.camera, sizeof old);
	render.camera[VX] = pos[VX];
	render.camera[VY] = pos[VY] + PLAYER_HEIGHT;
	render.camera[VZ] = pos[VZ];

	mapMoveCenter(render.level, old, render.camera);

	matLookAt(render.matModel, render.camera[VX], render.camera[VY], render.camera[VZ], lookat[VX], lookat[VY], lookat[VZ], 0, 1, 0);
	/* must be same as the one used in the vertex shader */
	matMult(render.matMVP, render.matPerspective, render.matModel);
	/* we will need that matrix sooner or later */
	matInverse(render.matInvMVP, render.matMVP);

	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_CAMERA_OFFFSET, sizeof (vec4), render.camera);

	mapViewFrustum(render.level, render.matMVP, render.camera);
	render.yaw = yawPitch[0];
	render.pitch = yawPitch[1];
	render.direction = 1; /* east */
	if (M_PI_4      <= render.yaw && render.yaw <= M_PI_4 + M_PI_2) render.direction = 0; else /* south:  45 ~ 135 */
	if (M_PI+M_PI_4 <= render.yaw && render.yaw <= 2*M_PI-M_PI_4)   render.direction = 2; else /* north: 225 ~ 315 */
	if (M_PI-M_PI_4 <= render.yaw && render.yaw <= M_PI+M_PI_4)     render.direction = 3;      /* west:  135 ~ 225 */
}

/* tooltip is about to be deleted, clear reference */
static int clearRef(SIT_Widget w, APTR cd, APTR ud)
{
	render.blockInfo = NULL;
	return 1;
}

void renderShowBlockInfo(Bool show, int what)
{
	if (show)
	{
		render.debugInfo |= what;
		if (what != DEBUG_BLOCK) return;
		if (! render.blockInfo)
		{
			render.blockInfo = SIT_CreateWidget("blockinfo", SIT_TOOLTIP, render.sitRoot,
				SIT_ToolTipAnchor, SITV_TooltipFollowMouse,
				SIT_DelayTime,     3600000,
				SIT_DisplayTime,   10000,
				NULL
			);
			SIT_AddCallback(render.blockInfo, SITE_OnFinalize, clearRef, NULL);
		}
		SIT_SetValues(render.blockInfo, SIT_Visible, True, SIT_DisplayTime, SITV_ResetTime, NULL);
	}
	else
	{
		render.debugInfo &= ~what;
		SIT_SetValues(render.blockInfo, SIT_Visible, False, NULL);
	}
}

static inline void renderParticles(void)
{
	int count = particlesAnimate(render.level, render.camera);
	if (count == 0) return;

//	fprintf(stderr, "particles = %d\n", count);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);

	glUseProgram(render.shaderParticles);
	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);
	glBindVertexArray(render.vaoParticles);

	setShaderValue(render.shaderParticles, "camera", 3, render.camera);

	glDrawArrays(GL_POINTS, 0, count);
	glDepthMask(GL_TRUE);
}

static void renderDrawItems(int count)
{
	mat4 ortho;

	matOrtho(ortho, 0, render.width, 0, render.height, 0, 100);

	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);

	glUseProgram(render.shaderBlocks);
	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

	/* change matrix model + projection and shading (slightly darker) */
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), ortho);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), render.matInventoryItem);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, sizeof invShading, invShading);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);
	glBindVertexArray(render.vaoInventory);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, render.vboInventoryMDAI);
	glMultiDrawArraysIndirect(GL_TRIANGLES, 0, count, 0);
	glBindVertexArray(0);

	/* restore original projection matrix */
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, sizeof shading, shading);
}

/* add extended info on inventory items: durability bar and/or stack count */
static void renderDrawExtInv(Item items, float scale, int count)
{
	/* need to add extra info on top of items */
	NVGcontext * vg = render.nvgCtx;
	int fh = roundf(scale * 0.4);
	int sz = roundf(scale * 0.0625);
	int i;

	nvgBeginFrame(vg, render.width, render.height, 1);
	nvgFontFaceId(vg, render.debugFont);
	nvgFontSize(vg, fh);
	nvgTextAlign(vg, NVG_ALIGN_TOP);

	for (i = 0; i < count; i ++, items ++)
	{
		int count = items->count;
		if (count > 1)
		{
			div_t res;
			if (count < 100)
				res = div(items->count, 10);
			else
				res.quot = res.rem = 9;

			TEXT number[] = {res.quot == 0 ? ' ' : res.quot + '0', res.rem + '0'};

			int x = roundf(items->x + scale - nvgTextBounds(vg, 0, 0, number, number+2, NULL));
			int y = render.height - items->y - fh;
			nvgFillColorRGBA8(vg, "\0\0\0\xff");
			nvgText(vg, x + 2, y + 2, number, number + 2);
			nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
			nvgText(vg, x, y, number, number + 2);
		}
		if (items->uses > 0)
		{
			nvgBeginPath(vg);
			float dura = itemDurability(items);
			int y = render.height - items->y - sz * 2;
			nvgRect(vg, items->x, y, scale, sz*2);
			nvgFillColorRGBA8(vg, "\0\0\0\xff");
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgFillColorRGBA8(vg, blockGetDurability(dura));
			nvgRect(vg, items->x, y, scale * (dura < 0 ? 1 : dura), sz);
			nvgFill(vg);
		}
	}
	nvgEndFrame(vg);
}

/* draw items in inventory */
static void renderInventoryItems(float scale)
{
	if (render.inventory->update != render.invCache)
	{
		MDAICmd   cmd;
		MDAICmd_t commands[MAXCOLINV];
		float     location[MAXCOLINV * 3];
		float *   loc;
		int       count, i, ext;

		/* inventory has changed: update all GL buffers */
		for (i = count = ext = 0, cmd = commands, loc = location; i < MAXCOLINV; i ++)
		{
			Item item = &render.inventory->items[i];
			int  size;
			if (item->id == 0) continue;
			if (item->id > ID(256, 0))
			{
				ItemDesc desc = itemGetById(item->id);
				size = blockInvGetModelSize(desc->glInvId);
			}
			else
			{
				BlockState b = blockGetById(item->id);
				size = blockInvGetModelSize(b->invId);
			}

			cmd->count = size >> 20;
			cmd->first = size & 0xfffff;
			cmd->instanceCount = 1;
			cmd->baseInstance = count;
			loc[0] = render.inventory->x + (i * 20 * scale);
			loc[1] = render.inventory->y;
			loc[2] = 16 * scale;
			item->x = roundf(loc[0]);
			item->y = roundf(loc[1]);

			if (item->count > 1 || item->uses > 0)
				ext = True;

			loc += 3;
			cmd ++;
			count ++;
		}

		glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryMDAI);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof *commands * count, commands);

		glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryLoc);
		glBufferSubData(GL_ARRAY_BUFFER, 0, count * 12, location);

		glBindBuffer(GL_ARRAY_BUFFER, 0);

		render.invCache = render.inventory->update;
		render.invCount = count;
		render.invExt   = ext;
    }

	if (render.invCount > 0)
	{
		renderDrawItems(render.invCount);
		if (render.invExt)
			renderDrawExtInv(render.inventory->items, 16 * scale, MAXCOLINV);
	}
}

/* render items at arbitrary location */
void renderItems(Item items, int count, float scale)
{
	glBindBuffer(GL_ARRAY_BUFFER, render.vboInventoryLoc);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, render.vboInventoryMDAI);
	float * loc = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	MDAICmd cmd = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);
	Bool    ext = False;
	Item    item;
	int     i;

	for (i = 0, item = items; i < count; i ++, item ++, cmd ++, loc += 3)
	{
		int glInvId;
		if (item->id < ID(256, 0))
		{
			BlockState b = blockGetById(item->id);
			glInvId = b->invId;
		}
		else
		{
			ItemDesc desc = itemGetById(item->id);
			if (desc == NULL)
				desc = itemGetById(ITEM_INVALID_ID);
			glInvId = desc->glInvId;
		}
		glInvId = blockInvGetModelSize(glInvId);

		cmd->count = glInvId >> 20;
		cmd->first = glInvId & 0xfffff;
		cmd->instanceCount = 1;
		cmd->baseInstance = i;

		loc[0] = item->x;
		loc[1] = item->y;
		loc[2] = scale;

		if (item->count > 1 || item->uses > 0)
			ext = True;
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);

	renderDrawItems(count);
	if (ext) renderDrawExtInv(items, scale, count);
}

/* setup the GL_DRAW_INDIRECT_BUFFER for glMultiDrawArraysIndirect() to draw 95% of the terrain */
static void renderPrepVisibleChunks(Map map)
{
	ChunkData cd;
	GPUBank   bank;
	MDAICmd   cmd;
	float *   loc;

	render.debugTotalTri = 0;

	/* prep all the terrain chunks we will need to render */
	for (bank = HEAD(gpuBanks); bank; NEXT(bank))
	{
		if (bank->vtxSize == 0) continue;
		bank->cmdTotal = 0;
		bank->cmdAlpha = 0;
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
		bank->locBuffer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		bank->cmdBuffer = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);

		for (cd = map->firstVisible; cd; cd = cd->visible)
		{
			if (cd->glBank != bank) continue; /* XXX sort by bank in frustum culling */

			Chunk  chunk = cd->chunk;
			GPUMem mem   = bank->usedList + cd->glSlot;
			int    size  = cd->glSize;
			int    alpha = cd->glAlpha;

			size -= alpha;
			if (size > 0 || alpha > 0)
			{
				int start = mem->offset / BYTES_PER_VERTEX;

				if (size > 0)
				{
					cmd = bank->cmdBuffer + bank->cmdTotal;
					cmd->count = size / BYTES_PER_VERTEX;
					cmd->instanceCount = 1;
					cmd->first = start;
					cmd->baseInstance = bank->cmdTotal; /* needed by glVertexAttribDivisor() */
					render.debugTotalTri += cmd->count;
					start += cmd->count;

					loc = bank->locBuffer + bank->cmdTotal * 3;
					loc[0] = chunk->X;
					loc[1] = cd->Y;
					loc[2] = chunk->Z;
					bank->cmdTotal ++;
				}
				if (alpha > 0)
				{
					bank->cmdAlpha ++;
					int i = bank->vtxSize - bank->cmdAlpha;
					cmd = bank->cmdBuffer + i;
					cmd->count = alpha / BYTES_PER_VERTEX;
					cmd->instanceCount = 1;
					cmd->first = start;
					cmd->baseInstance = i; /* needed by glVertexAttribDivisor() */

					loc = bank->locBuffer + i * 3;
					loc[0] = chunk->X;
					loc[1] = cd->Y;
					loc[2] = chunk->Z;
				}
			}
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
		bank->cmdBuffer = NULL;
		bank->locBuffer = NULL;
	}
}

static void renderText(NVGcontext * vg, int x, int y, STRPTR text, float a)
{
	STRPTR end = strchr(text, 0);
	nvgGlobalAlpha(vg, a);
	nvgFillColorRGBA8(vg, "\0\0\0\xff");
	nvgText(vg, x+2, y+2, text, end);
	nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
	nvgText(vg, x, y, text, end);
	nvgGlobalAlpha(vg, 1);
}


/* show tooltip near mouse cursor containing some info on the block selected */
void renderBlockInfo(SelBlock * sel)
{
	static vec4 pos;

	if (memcmp(pos, sel->current, sizeof pos))
	{
		int        id    = sel->extra.blockId;
		BlockState block = blockGetById(id);
		int        XYZ[] = {sel->current[0], sel->current[1], sel->current[2]};
		TEXT       msg[256];

		sprintf(msg, "X: %d <dim>(%d)</dim>\nY: %d <dim>(%d)</dim>\nZ: %d <dim>(%d)</dim>\n%s <dim>(%d:%d)</dim>",
			XYZ[0], XYZ[0] & 15, XYZ[1], XYZ[1] & 15, XYZ[2], XYZ[2] & 15, block->name, id>>4, id&15);

		SIT_SetValues(render.blockInfo, SIT_Title, msg, SIT_DisplayTime, SITV_ResetTime, NULL);
	}
}

/*
 * Frustum debug
 */
void renderFrustum(Bool snapshot)
{
	static int vaoFrustum;
	static int vboFrustum;
	static int vboFrustumLoc;
	static int vboCount;

	Map map = render.level;

	if (vaoFrustum == 0)
	{
		glGenVertexArrays(1, &vaoFrustum);
		glGenBuffers(1, &vboFrustum);
		glGenBuffers(1, &vboFrustumLoc);

		glBindVertexArray(vaoFrustum);
		glBindBuffer(GL_ARRAY_BUFFER, vboFrustum);
		glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, vboFrustumLoc);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);
	}
	if (snapshot)
	{
		static uint8_t edges[] = {
			0, 1,   1, 5,   5, 4,   4, 0,
			3, 2,   2, 6,   6, 7,   7, 3,
			0, 3,   1, 2,   5, 6,   4, 7,
		};
		extern uint8_t vertex[];

		ChunkData cd;
		DATA16    vtx;
		int       nb;

		for (cd = map->firstVisible, nb = 0; cd; cd = cd->visible, nb ++);
		glBindBuffer(GL_ARRAY_BUFFER, vboFrustum);
		glBufferData(GL_ARRAY_BUFFER, 24 * BYTES_PER_VERTEX, NULL, GL_STATIC_DRAW);
		vtx = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		int i;
		for (i = 0; i < DIM(edges); i ++, vtx += INT_PER_VERTEX)
		{
			DATA8 p = &vertex[edges[i] * 3];
			vtx[0] = VERTEX(p[0]*16);
			vtx[1] = VERTEX(p[1]*16);
			vtx[2] = VERTEX(p[2]*16);
			SET_UVCOORD(vtx, 31*16+8, 8);
			vtx[4] |= 0xff << 8;
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, vboFrustumLoc);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, nb * 12, NULL, GL_STATIC_DRAW);
		float * loc = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);
		for (cd = map->firstVisible, vboCount = 0; cd; cd = cd->visible, loc += 3, vboCount ++)
		{
			Chunk chunk = cd->chunk;
			loc[0] = chunk->X;
			loc[1] = cd->Y;
			loc[2] = chunk->Z;
		}
		glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	}
	if (vboCount > 0)
	{
		glBindVertexArray(vaoFrustum);
		glDrawArraysInstanced(GL_LINES, 0, 24, vboCount);
	}
}

/*
 * world rendering: per-frame shader magic happens here.
 */
void renderWorld(void)
{
	/* generate mesh we didn't have time to do before */
	if (render.level->genList.lh_Head)
	{
		mapGenerateMesh(render.level);
		mapViewFrustum(render.level, render.matMVP, render.camera);
	}

	/* must be done before glViewport */
	signPrepare(render.camera);

	glViewport(0, 0, render.width, render.height);
	glClearColor(0.5, 0.5, 0.8, 1);
	glClear(GL_DEPTH_BUFFER_BIT);

	/* sky dome */
	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), render.matModel);
	skydomeRender();

	glBindBuffer(GL_UNIFORM_BUFFER, render.uboShader);

//	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	/* render terrain block */
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glFrontFace(GL_CCW);
	glDepthMask(GL_TRUE);

	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), render.matModel);
	glUseProgram(render.shaderBlocks);

	static float biomeColor[] = {0.411765, 0.768627, 0.294118};
	setShaderValue(render.shaderBlocks, "biomeColor", 3, biomeColor);

	renderPrepVisibleChunks(render.level);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);

	/* first pass: main terrain */
	GPUBank bank;
	for (bank = HEAD(gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdTotal > 0)
		{
			/* we have something to render from this bank */
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_TRIANGLES, 0, bank->cmdTotal, 0);
		}
	}

	/* second: entities */
	entityRender();

	/* third pass: translucent terrain */
	glUseProgram(render.shaderBlocks);
	glDepthMask(GL_FALSE);
	for (bank = HEAD(gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdAlpha > 0)
		{
			/* we have something to render from this bank */
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_TRIANGLES, (void*)(bank->cmdTotal*16), bank->cmdAlpha, 0);
		}
	}
	glDepthMask(GL_TRUE);

	/* show limit of chunk boundary where player is */
	if (render.debug & RENDER_DEBUG_CURCHUNK)
	{
		Map       map  = render.level;
		ChunkData air  = map->air;
		Chunk     cur  = map->center;
		int       max  = cur->maxy;
		GPUBank   bank = air->glBank;
		int       off  = bank->usedList[air->glSlot].offset / BYTES_PER_VERTEX;
		MDAICmd   cmd;
		float *   loc;
		int       i;

		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
		loc = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		cmd = glMapBuffer(GL_DRAW_INDIRECT_BUFFER, GL_WRITE_ONLY);

		for (i = 0; i < max; cmd ++, loc += 3, i ++)
		{
			cmd->count = air->glSize / BYTES_PER_VERTEX;
			cmd->instanceCount = 1;
			cmd->first = off;
			cmd->baseInstance = i; /* needed by glVertexAttribDivisor() */

			loc[0] = cur->X;
			loc[1] = i*16;
			loc[2] = cur->Z;
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
		glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);

		glBindVertexArray(bank->vaoTerrain);
		glMultiDrawArraysIndirect(GL_LINES, 0, max, 0);
	}
	if (render.debug & RENDER_DEBUG_FRUSTUM)
	{
		renderFrustum(False);
	}

	/* text signs */
	signRender();
	glBindTexture(GL_TEXTURE_2D, render.texBlock);

	/* particles */
	renderParticles();

	/* selection overlay */
	if (render.selection.sel)
		renderSelection();

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	/* draw the compass */
	float scale = render.height * 0.15;
	APTR  vg = render.nvgCtx;

	nvgBeginFrame(vg, render.width, render.height, 1);
	nvgFontFaceId(vg, render.debugFont);
	nvgFontSize(vg, FONTSIZE);
	nvgTextAlign(vg, NVG_ALIGN_TOP);
	nvgSave(vg);
	nvgTranslate(vg, render.width - scale, scale); scale -= 20;
	nvgRotate(vg, M_PI - render.yaw);
	nvgBeginPath(vg);
	nvgRect(vg, -scale, -scale, scale*2, scale*2);
	nvgFillPaint(vg, nvgImagePattern(vg, -scale, -scale, scale*2, scale*2, 0, render.compass, 1));
	nvgFill(vg);
	nvgRestore(vg);

	/* draw inventory bar */
	scale = render.scale;
	render.inventory->x = (render.width - scale * 182) * 0.5 + 3 * scale;
	render.inventory->y = 3 * scale;
	nvgSave(vg);
	nvgScale(vg, scale, scale);
	nvgTranslate(vg, render.width / (2 * scale) - 182 / 2, render.height / scale - 22);
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, 182, 22);
	nvgFillPaint(vg, nvgImagePattern(render.nvgCtx, 0, 0, 230, 24, 0, render.inventory->texture, 1));
	nvgFill(vg);

	int pos = render.inventory->selected * 20 - 1;

	nvgBeginPath(vg);
	nvgRect(vg, pos, -1, 24, 24);
	nvgFillPaint(vg, nvgImagePattern(vg, pos - 183, -1, 230, 24, 0, render.inventory->texture, 1));
	nvgFill(vg);
	nvgRestore(vg);

	/* message above inventory bar */
	switch (render.inventory->infoState) {
	case INFO_INV_INIT:
		render.inventory->infoX = (render.width - nvgTextBounds(vg, 0, 0, render.inventory->infoTxt, NULL, NULL)) / 2;
		render.inventory->infoTime = curTime + INFO_INV_DURATION * 1000;
		render.inventory->infoState = INFO_INV_SHOW;
		// no break;
	case INFO_INV_SHOW:
		renderText(vg, render.inventory->infoX, render.height - 35 * scale, render.inventory->infoTxt, 1);
		if (curTime > render.inventory->infoTime)
		{
			render.inventory->infoState = INFO_INV_FADE;
			render.inventory->infoTime += INFO_INV_FADEOUT * 1000;
		}
		break;
	case INFO_INV_FADE:
		renderText(vg, render.inventory->infoX, render.height - 35 * scale, render.inventory->infoTxt,
			(render.inventory->infoTime - curTime) / (INFO_INV_FADEOUT * 1000.));
		if (curTime > render.inventory->infoTime)
			render.inventory->infoState = INFO_INV_NONE;
	}

	/* unsaved edit message */
	if (render.message.chrLen > 0)
	{
		nvgFontSize(vg, FONTSIZE_MSG);
		nvgBeginPath(vg);
		nvgRect(vg, FONTSIZE, render.height - FONTSIZE * 2, render.message.pxLen + FONTSIZE_MSG, FONTSIZE);
		nvgFillColorRGBA8(vg, "\0\0\0\xaa");
		nvgFill(vg);
		nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
		nvgText(vg, FONTSIZE+FONTSIZE_MSG/2, render.height - FONTSIZE*2+(FONTSIZE-FONTSIZE_MSG)/2, render.message.text, render.message.text + render.message.chrLen);
	}

	/* debug info */
	if (render.debug)
	{
		debugCoord(vg, render.camera, render.debugTotalTri/3);
	}
	nvgEndFrame(vg);

	/* inventory items needs to be rendered after nanovg commands */
	renderInventoryItems(scale);

	if ((render.selection.sel & SEL_CURRENT) && (render.debugInfo & DEBUG_BLOCK))
	{
		/* tooltip about block being pointed at */
		renderBlockInfo(&render.selection);
	}
}

/*
 * map update actions
 */
void renderAddModif(void)
{
	/* new chunks might have been created */
	mapViewFrustum(render.level, render.matMVP, render.camera);
	nvgFontSize(render.nvgCtx, FONTSIZE_MSG);
	render.modifCount ++;
	render.message.chrLen = sprintf(render.message.text, LangStrPlural(NULL, render.modifCount, "%d unsaved edit", "%d unsaved edits"), render.modifCount);
	render.message.pxLen  = nvgTextBounds(render.nvgCtx, 0, 0, render.message.text, render.message.text + render.message.chrLen, NULL);
}

void renderAllSaved(void)
{
	render.modifCount = 0;
	render.message.chrLen = 0;
}

int renderGetTerrain(int size[2])
{
	if (size) size[0] = 512, size[1] = 1024;
	return render.nvgTerrain;
}

/*
 * this part is to manage vertex buffer on the GPU.
 *
 * store the mesh of one sub-chunk in mem before transfering to GPU
 */
#define MAX_MESH_CHUNK         64*1024
static MeshBuffer renderAllocMeshBuf(ListHead * head)
{
	MeshBuffer mesh = malloc(sizeof *mesh + MAX_MESH_CHUNK);
	if (! mesh) return NULL;
	memset(mesh, 0, sizeof *mesh);
	ListAddTail(head, &mesh->node);
	return mesh;
}

void renderInitBuffer(ChunkData cd)
{
	MeshBuffer mesh;
	/* typical sub-chunk is usually below 64Kb of mesh data */
	if (meshBanks.lh_Head == NULL)
	{
		/* since we are using a 5 bytes per vertex, we will only use at most 65535 bytes */
		mesh = renderAllocMeshBuf(&alphaBanks);
		mesh = renderAllocMeshBuf(&meshBanks);
	}
	else mesh = HEAD(meshBanks);
	mesh->chunk = cd;

	for (mesh = HEAD(meshBanks);  mesh; mesh->usage = 0, NEXT(mesh));
	for (mesh = HEAD(alphaBanks); mesh; mesh->usage = 0, NEXT(mesh));
}

/* partial mesh data; note: <size> is at most 4096 */
void renderFlush(WriteBuffer buffer)
{
	ListHead * first = buffer->alpha ? &alphaBanks : &meshBanks;
	int        size  = (DATA8) buffer->cur - (DATA8) buffer->start;
	MeshBuffer list;

	for (list = HEAD(*first); list; NEXT(list))
	{
		if (list->usage + size <= MAX_MESH_CHUNK)
		{
			memcpy(list->buffer + list->usage, buffer->start, size);
			list->usage += size;
			break;
		}
		else if (list->node.ln_Next == NULL)
		{
			/* insert from head of list */
			list = renderAllocMeshBuf(first);
			memcpy(list->buffer, buffer->start, size);
			list->usage = size;
			break;
		}
	}
	buffer->cur = buffer->start;
}

/*
 * store a compressed mesh into the GPU mem and keep track of where it is, in ChunkData
 * this is basically a custom allocator:
 * - allocating in a bank where there is no free list is O(1)
 * - if there are free list, it is O(N), where N = number of free list.
 */
static int renderStoreArrays(ChunkData cd, int size)
{
	GPUBank bank;
	GPUMem  mem;

	if (size == 0)
	{
		if (cd->glBank)
		{
			renderFreeArray(cd);
			cd->glBank = NULL;
		}
		return -1;
	}

	for (bank = HEAD(gpuBanks); bank && bank->memAvail <= bank->memUsed + size /* bank is full */; NEXT(bank));

	if (bank == NULL)
	{
		bank = calloc(sizeof *bank, 1);
		bank->memAvail = MEMPOOL;
		bank->maxItems = MEMITEM;
		bank->usedList = calloc(sizeof *mem, MEMITEM);
		bank->firstFree = -1;

		glGenVertexArrays(1, &bank->vaoTerrain);
		/* will also init vboLocation and vboMDAI */
		glGenBuffers(3, &bank->vboTerrain);

		/* pre-configure terrain VAO: 5 bytes per vertex */
		glBindVertexArray(bank->vaoTerrain);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);
		/* this will allocate memory on the GPU: mem chunks of 20Mb */
		glBufferData(GL_ARRAY_BUFFER, MEMPOOL, NULL, GL_STATIC_DRAW);
		glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, 0);
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, BYTES_PER_VERTEX, (void *) 6);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
		glVertexAttribPointer(2, 3, GL_FLOAT, 0, 0, 0);
		glEnableVertexAttribArray(2);
		glVertexAttribDivisor(2, 1);

		checkOpenGLError("renderStoreArrays");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		ListAddTail(&gpuBanks, &bank->node);
	}

	/* check for free space in the bank */
	int i, * prev;
	for (i = bank->firstFree, prev = &bank->firstFree, mem = bank->usedList; i >= 0; )
	{
		GPUMem block = mem + i;
		/* first place available */
		if (size <= - block->size)
		{
			/* check if we can split memory block */
			GPUMem next = block + 1;
			if (i+1 < bank->nbItem && next->size == 0)
			{
				next->block.next = block->block.next;
				next->size = block->size + size;
				next->offset = block->offset + size;
				*prev = next->size == 0 ? block->block.next : i+1;
				block->size = size;
			}
			else /* use all memory from the block */
			{
				*prev = block->block.next;
				block->size = -block->size;
			}
			mem = block;
			goto found;
		}
		prev = &block->block.next;
		i = *prev;
	}
	/* no free block big enough: alloc at the end */
	if (bank->nbItem == bank->maxItems)
	{
		/* not enough items */
		i = (bank->maxItems + MEMITEM) * sizeof *mem;
		mem = realloc(mem, i);
		if (mem)
		{
			memset(mem + bank->maxItems, 0, MEMITEM * sizeof *mem);
			bank->maxItems += MEMITEM;
			bank->usedList = mem;
		}
		else { fprintf(stderr, "alloc %d failed: aborting\n", i); return -1; }
	}
	i = bank->nbItem;
	bank->nbItem ++;
	mem += i;
	mem->size = size;
	mem->offset = bank->memUsed;
	bank->memUsed += size;

	found:
	mem->block.cd = cd;
	cd->glSlot = i;
	cd->glSize = size;
	cd->glBank = bank;

//	fprintf(stderr, "mem used: %d (+%d)\n", bank->memUsed, size);

	return mem->offset;
}

/* mark memory occupied by the vertex array as free */
void renderFreeArray(ChunkData cd)
{
	int     pos  = cd->glSlot;
	GPUBank bank = cd->glBank;
	GPUMem  mem  = bank->usedList + pos;
	int     item = bank->nbItem - 1;
	GPUMem  todel;
	int     nb;

	cd->glBank = NULL;

	if (pos > 0 && mem[-1].size <= 0)
	{
		/* merge with previous slot */
		GPUMem prev = mem - 1;
		uint8_t merge = 0;
		while (prev->size == 0) prev --;
		if (prev->size > 0)
		{
			prev[1].offset = prev->offset + prev->size;
			prev ++;
			merge = 1;
		}
		prev->size -= mem->size;
		mem->size   = 0;
		nb = pos + 1;
		/* check if we need to merge with next slot too */
		if (nb <= item && bank->usedList[nb].size < 0)
		{
			mem ++;
			prev->size += mem->size;
			prev->block.next = mem->block.next;
			mem->size = 0;
			while (nb <= item && mem->size == 0) mem ++, nb ++;
		}
		if (nb > item || merge)
			/* need to collapse free list */
			mem = prev, pos = prev - bank->usedList;
		else
			/* linked list already setup */
			return;
	}
	else if (pos < item && mem[1].size < 0)
	{
		/* merge with next slot */
		GPUMem next = mem + 1;
		mem->size = next->size - mem->size;
		mem->block.next = next->block.next;
		next->size = 0;
	}
	else mem->size = -mem->size;

	for (todel = mem + 1, nb = pos + 1; nb <= item && todel->size == 0; todel ++, nb ++);
	if (nb <= item) nb = todel - mem, todel = NULL;
	else nb = todel - mem, bank->memUsed += mem->size;

	if (bank->firstFree >= 0)
	{
		GPUMem link;
		int    next, i;
		int *  prev;
		for (i = bank->firstFree, prev = &bank->firstFree; i >= 0; i = next)
		{
			link = bank->usedList + i;
			next = link->block.next;
			if (i >= pos) break;
			prev = &link->block.next;
			if (next > pos || next < 0) break;
		}
		if (todel)
			bank->nbItem -= nb, pos = -1;
		else if (pos > (i = *prev) || i >= pos+nb)
			mem->block.next = i;
		*prev = pos;
	}
	else if (todel)
	{
		/* last item being deleted: discard all GPUMem block */
		bank->nbItem -= nb;
	}
	else bank->firstFree = pos, mem->block.next = -1;
}

/* about to build command list for glMultiDrawArraysIndirect() */
void renderClearBank(void)
{
	GPUBank bank;
	for (bank = HEAD(gpuBanks); bank; bank->vtxSize = 0, bank->cmdTotal = 0, NEXT(bank));
}

/* number of sub-chunk we will have to render: will define the size of the command list */
void renderAddToBank(ChunkData cd)
{
	GPUBank bank = cd->glBank;

	if (cd->glSize - cd->glAlpha > 0) bank->vtxSize ++;
	if (cd->glAlpha > 0) bank->vtxSize ++;
}

/* alloc command list buffer on the GPU */
void renderAllocCmdBuffer(void)
{
	GPUBank bank;
	for (bank = HEAD(gpuBanks); bank; NEXT(bank))
	{
		/* avoid reallocating this buffer: it is used quite a lot (changed every frame) */
		int count = (bank->vtxSize + 1023) & ~1023;

		if (bank->vboLocSize < count)
		{
			/* be sure we have enough mem on GPU for command buffer */
			bank->vboLocSize = count;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vboLocation);
			glBufferData(GL_ARRAY_BUFFER, count * 12, NULL, GL_STATIC_DRAW);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glBufferData(GL_DRAW_INDIRECT_BUFFER, count * 16, NULL, GL_STATIC_DRAW);
		}
	}
}

/* transfer mesh to GPU */
void renderFinishMesh(void)
{
	MeshBuffer list;
	int        size, alpha;
	int        total, offset;
	ChunkData  cd;
	GPUBank    bank;

	for (list = HEAD(meshBanks), cd = list->chunk, size = 0; list; size += list->usage, NEXT(list));
	for (list = HEAD(alphaBanks), alpha = 0; list; alpha += list->usage, NEXT(list));

	total = size + alpha;
	//if (total == 0) fprintf(stderr, "chunk %3d, %2d, %3d: %6d bytes\n", cd->chunk->X, cd->Y, cd->chunk->Z, total);
	bank = cd->glBank;
	if (bank)
	{
		GPUMem mem = bank->usedList + cd->glSlot;
		if (total > mem->size)
		{
			/* not enough space: need to "free" previous mesh before */
			renderFreeArray(cd);
			offset = renderStoreArrays(cd, total);
		}
		else offset = mem->offset, cd->glSize = total; /* reuse mem segment */
	}
	else offset = renderStoreArrays(cd, total);

	if (offset >= 0)
	{
		bank = cd->glBank;
		cd->glAlpha = alpha;
		/* and finally copy the data to the GPU */
		glBindBuffer(GL_ARRAY_BUFFER, bank->vboTerrain);

		/* first: opaque */
		for (list = HEAD(meshBanks); list; offset += list->usage, NEXT(list))
			glBufferSubData(GL_ARRAY_BUFFER, offset, list->usage, list->buffer);

		/* then alpha: will be rendered in a separate pass */
		for (list = HEAD(alphaBanks); list; offset += list->usage, NEXT(list))
			glBufferSubData(GL_ARRAY_BUFFER, offset, list->usage, list->buffer);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

void renderDebugBank(void)
{
	GPUBank bank;

	for (bank = HEAD(gpuBanks); bank; NEXT(bank))
	{
		fprintf(stderr, "bank: mem = %d/%dK, items: %d/%d, vtxSize: %d\n", bank->memUsed>>10, bank->memAvail>>10,
			bank->nbItem, bank->maxItems, bank->vtxSize);

		#if 1
		GPUMem mem;
		int    i;

		for (mem = bank->usedList, i = bank->nbItem; i > 0; i --, mem ++)
		{
			fprintf(stderr, " - mem: %d at %d\n", mem->size, mem->offset);
		}
		#endif
	}
}
