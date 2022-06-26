/*
 * render.c : render world using openGL: this is the core of the rendering engine.
 *
 * VBO/VAO for chunks are allocated here.
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
#include "render.h"
#include "selection.h"
#include "particles.h"
#include "sign.h"
#include "skydome.h"
#include "entities.h"
#include "cartograph.h"
#include "waypoints.h"
#include "blockUpdate.h"
#include "meshBanks.h"
#include "tileticks.h"
#include "keybindings.h"
#include "undoredo.h"
#include "raycasting.h"
#include "nanovg.h"
#include "SIT.h"
#include "zlib.h" /* crc32 */
#include "globals.h"

struct RenderWorld_t render;

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

/* hack: toolbar for extended selection is assigned to block 255 in blocksTable.js */
static struct Item_t extendedSelItems[] = {
	{ID(255,0)}, {ID(255,1)}, {ID(255,2)}, {ID(255,3)}, {ID(255,4)}, {ID(255,5)}, {ID(255,6)}, {ID(255,7)}, {ID(255,8)},
	/* offhand slot */
	{ID(4000,0)},
};

void GLAPIENTRY debugGLError(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	STRPTR str, sev;
	TEXT   typeUnknown[64];
	switch (type) {
	case GL_DEBUG_TYPE_ERROR:               str = "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return; //str = "DEPRECATED_BEHAVIOR"; break;
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

	render.selection.selFlags &= ~(SEL_NOCURRENT|SEL_BLOCKPOS);
	if (item->id > 0 && (render.debugInfo & DEBUG_SELECTION) == 0 && render.selection.extra.entity == 0)
	{
		/* preview block */
		int8_t * offset;
		ItemID_t id = item->id;
		vec4     loc;

		/* pointing too far away */
		if ((render.selection.selFlags & SEL_POINTTO) == 0)
			return;

		if (! isBlockId(id))
		{
			/* check if this item is used to create a block */
			ItemDesc desc = itemGetById(item->id);
			if (desc == NULL || (id = desc->refBlock) == 0)
				goto highlight_bbox;
			id <<= 4;
		}

		struct BlockOrient_t info = {
			.pointToId = render.selection.extra.blockId,
			.direction = globals.direction,
			.side      = render.selection.extra.side,
			.topHalf   = render.selection.extra.topHalf,
			.yaw       = render.yaw
		};

		if (blockIds[id >> 4].placement > 0)
		{
			switch (blockAdjustPlacement(id, &info)) {
			case PLACEMENT_NONE:
				/* placement not possible, cancel everything */
				render.selection.selFlags |= SEL_NOCURRENT;
				return;
			case PLACEMENT_GROUND:
				/* check if ground is within 1 block reach */
				offset = cubeNormals + render.selection.extra.side * 4;
				loc[0] = render.selection.current[0] + offset[0];
				loc[1] = render.selection.current[1] - 1;
				loc[2] = render.selection.current[2] + offset[2];
				if (! blockIsSolidSide(mapGetBlockId(globals.level, loc, NULL), SIDE_TOP) /* || info.pointToId != 0*/)
				{
					render.selection.selFlags |= SEL_NOCURRENT;
					return;
				}
				break;
			case PLACEMENT_OK:
				if (blockIds[info.pointToId>>4].special != BLOCK_POT)
				{
					offset = cubeNormals + render.selection.extra.side * 4;
					loc[0] = render.selection.current[0] + offset[0];
					loc[1] = render.selection.current[1] + offset[1];
					loc[2] = render.selection.current[2] + offset[2];
					if (mapGetBlockId(globals.level, loc, NULL) != 0)
					{
						render.selection.selFlags |= SEL_NOCURRENT;
						return;
					}
				}
				else info.keepPos = 1;
			}
		}

		/* show a preview of what is going to be placed if left-clicked */
		BlockState b = blockGetById(render.selection.extra.blockId);
		if ((b->inventory & CATFLAGS) == DECO && b->type == QUAD)
			offset = cubeNormals + 5; /* 0,0,0 */
		else
			offset = cubeNormals + render.selection.extra.side * 4;
		int blockId = blockAdjustOrient(id, &info, render.selection.extra.inter);
		if (info.keepPos) offset = cubeNormals + 5; /* 0,0,0 */
		if ((render.selection.blockId & ~15) == (blockId & ~15))
		{
			id = render.selection.rotate90 & 7;
			switch (render.selection.rotate90 >> 4) {
			case VX+1: while (id > 0) blockId = blockRotateX90(blockId), id --; break;
			case VY+1: while (id > 0) blockId = blockRotateY90(blockId), id --; break;
			case VZ+1: while (id > 0) blockId = blockRotateZ90(blockId), id --;
			}
		}
		else render.selection.rotate90 = 0;

		#if 0
		static int oldBlock;
		if (oldBlock != blockId)
			fprintf(stderr, "blockId = %d:%d, side = %d, dir = %d\n", (blockId>>4) & 255, blockId & 15, info.side, info.direction), oldBlock = blockId;
		#endif

		loc[0] = render.selection.current[0] + offset[0];
		loc[1] = render.selection.current[1] + offset[1];
		loc[2] = render.selection.current[2] + offset[2];
		loc[3] = 255;
		render.selection.selFlags |= SEL_BLOCKPOS;
		memcpy(render.selection.blockPos, loc, sizeof loc);

		if (globals.showPreview == 0)
		{
			render.selection.blockId = blockId;
			goto highlight_bbox;
		}

		if (render.selection.blockId != blockId)
		{
			/* generate a mesh on the fly: performance is not really a concern here */
			render.selection.blockVtx = blockGenModel(render.vboPreview, blockId);
			render.selection.blockId  = blockId;
		}
		int vtx = render.selection.blockVtx;
		int wire = vtx >> 10;
		vtx &= 1023;

		glBindBuffer(GL_ARRAY_BUFFER, render.vboPreviewLoc);
		glBufferSubData(GL_ARRAY_BUFFER, 0, 16, loc);

		glFrontFace(GL_CCW);
		glUseProgram(render.shaderItems);
		glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);

		glBindVertexArray(render.vaoPreview);
		glDrawArrays(GL_TRIANGLES, 0, vtx);

		glDrawArrays(GL_LINES, vtx, wire);
	}
	else if (render.selection.extra.entity == 0) /* highlight bounding box instead */
	{
		static int locOffset;
		vec4 loc;
		highlight_bbox:
		loc[0] = render.selection.current[0];
		loc[1] = render.selection.current[1];
		loc[2] = render.selection.current[2];

		glUseProgram(render.selection.shader);
		glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);

		if (locOffset == 0)
			locOffset = glGetUniformLocation(render.selection.shader, "info");

		/* draw block bounding box */
		BlockState b   = blockGetById(render.selection.extra.blockId);
		VTXBBox    box = blockGetBBoxForVertex(b);
		int        flg = render.selection.extra.cnxFlags;
		int        off = 0;
		int        count;

		if (box)
		{
			if (render.selection.extra.special == BLOCK_DOOR_TOP) loc[VY] --;
			loc[3] = 1;
			glProgramUniform4fv(render.selection.shader, locOffset, 1, loc);
			glBindVertexArray(render.vaoBBox);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render.vboBBoxIdx);
			glFrontFace(GL_CCW);

			/* too complex to do in the vertex/geometry shader */
			static int lastId, lastFlag, lastCount;
			/* rearrange element array on the fly: performance does not really matter here */
			if (lastId != b->id || lastFlag != flg)
			{
				count = lastCount = blockGenVertexBBox(b, box, flg, &render.vboBBoxVTX, ID(31,0), 0);
				lastId = b->id;
				lastFlag = flg;
			}
			else count = lastCount;

			/* filled polygon */
			glDepthMask(GL_FALSE);
			glDrawElements(GL_TRIANGLES, count & 0xffff, GL_UNSIGNED_SHORT, 0);

			loc[3] = 0;
			glProgramUniform4fv(render.selection.shader, locOffset, 1, loc);

			/* edge highlight */
			off += (count & 0xffff) * 2;
			count >>= 16;
			glDrawElements(GL_LINES, count, GL_UNSIGNED_SHORT, (APTR) off);

			/* hidden part of selection box */
			loc[3] = 2;
			glProgramUniform4fv(render.selection.shader, locOffset, 1, loc);
			glDepthFunc(GL_GEQUAL);
			glDrawElements(GL_LINES, count, GL_UNSIGNED_SHORT, (APTR) off);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glDepthFunc(GL_LEQUAL);
		}

		glDepthMask(GL_TRUE);
	}
	glBindVertexArray(0);
}

Bool renderRotatePreview(int dir)
{
	uint8_t axis = render.selection.rotate90 >> 4;
	uint8_t r = (render.selection.rotate90 & 3) + dir;
	uint8_t try = 0;
	uint8_t facing = globals.direction & 1 ? 3 : 1;
	int blockId = render.selection.blockId;
	if (r == 4)  r = 0; else
	if (r > 127) r = 3;

	for (try = 0; try < 2; try ++)
	{
		if (axis == 0)
		{
			/* try VY first */
			blockId = blockRotateY90(blockId);
			if (blockId != render.selection.blockId)
			{
				axis = 2;
				goto accept;
			}

			/* try to rotate on X or Z axis */
			axis = facing;
		}
		else if (axis != 2)
		{
			axis = facing;
		}
		switch (axis) {
		case 1: blockId = blockRotateX90(blockId); break;
		case 2: blockId = blockRotateY90(blockId); break;
		case 3: blockId = blockRotateZ90(blockId); break;
		}

		if (blockId != render.selection.blockId)
		{
			accept:
			render.selection.blockVtx = blockGenModel(render.vboPreview, blockId);
			render.selection.blockId  = blockId;
			render.selection.rotate90 = (axis << 4) | r;
			return True;
		}
		else if (try == 0)
		{
			/* try another axis */
			r = dir < 0 ? 3 : 1;
			axis = 0;
		}
	}
	return False;
}

/* left click in off-hand mode: add selection point */
void renderSetSelectionPoint(int action)
{
	switch (action) {
	case RENDER_SEL_INIT:
		/* force block selection (avoid block preview) */
		render.debugInfo |= DEBUG_SELECTION;
		break;

	case RENDER_SEL_CLEAR:
		if ((render.debugInfo & DEBUG_SELECTION) == 0) break;
		if (globals.selPoints == 3)
			undoLog(LOG_SELECTION, selectionGetPoints());
		render.inventory->offhand &= ~(PLAYER_ALTPOINT | PLAYER_OFFHAND);
		render.debugInfo &= ~DEBUG_SELECTION;
		render.selection.selFlags &= ~SEL_MOVE;
		render.invCache ++;
		selectionCancel();
		break;

	case RENDER_SEL_AUTO:
		/* auto-select similar block */
		if (render.selection.selFlags & SEL_POINTTO)
		{
			if (render.selection.extra.entity > 0)
			{
				float pos[3];
				switch (render.selection.extra.side) {
				case SIDE_ENTITY:   entityGetPos(render.selection.extra.entity, pos); break;
				case SIDE_WAYPOINT: wayPointGetPos(render.selection.extra.entity, pos); break;
				default: return;
				}
				selectionSelect(pos, render.scale);
			}
			else selectionAutoSelect(render.selection.current, render.scale);
			render.invCache ++;
		}
		break;

	case RENDER_SEL_AUTOMOVE:
		/* move clone selection to where mouse is pointing */
		render.debugInfo |= DEBUG_SELECTION;
		render.selection.selFlags |= SEL_MOVE;
		break;

	case RENDER_SEL_STOPMOVE:
		render.selection.selFlags &= ~SEL_MOVE;
		break;

	case RENDER_SEL_ADDPT:
		/* click on a block */
		if ((render.selection.selFlags & SEL_POINTTO) == 0)
			break;

		selectionSetPoint(render.scale, render.selection.current,
			(render.inventory->offhand & PLAYER_ALTPOINT) == 0 ? SEL_POINT_1 : SEL_POINT_2);
		if ((globals.selPoints & 3) == 3)
			render.invCache ++;
		else
			render.inventory->offhand ^= PLAYER_ALTPOINT;
	}
}

void renderSetSelection(int32_t points[6])
{
	render.debugInfo |= DEBUG_SELECTION;
	render.inventory->offhand = 1;
	render.invCache ++;
	selectionSetPoint(render.scale, (vec4) {points[0], points[1], points[2]}, SEL_POINT_1);
	selectionSetPoint(render.scale, (vec4) {points[3], points[4], points[5]}, SEL_POINT_2);
}

void inventorySetTooltip(SIT_Widget toolTip, Item item, STRPTR extra);

/* from mouse pos mx, my: pickup block pointed at this location using ray casting */
void renderPointToBlock(int mx, int my)
{
	if (mx < 0) mx = render.mouseX, my = render.mouseY;

	vec4 dir = {render.inventory->x - 26 * render.scale, globals.height - 22 * render.scale, 208 * render.scale};

	if (dir[VX] <= mx && mx <= dir[VX] + dir[2] && my > dir[VY])
	{
		/* hovering toolbar slot: show tooltip of item in slot */
		Item hover;
		int  item;
		if (mx >= render.inventory->x)
		{
			item = (mx - render.inventory->x) / (20*render.scale);
			if (item > 8) item = 8;

			if ((globals.selPoints & 3) != 3)
				hover = &render.inventory->items[item];
			else
				hover = &extendedSelItems[item];
		}
		else hover = &extendedSelItems[9], item = 9;

		render.selection.selFlags &= ~(SEL_POINTTO | SEL_NOCURRENT);
		render.selection.selFlags |= SEL_OFFHAND;
		render.inventory->offhand |= PLAYER_TOOLBAR;
		render.inventory->hoverSlot = item;

		if (hover->id > 0 && hover != render.toolbarItem)
		{
			TEXT message[256];
			render.toolbarItem = hover;
			renderShowBlockInfo(True, DEBUG_BLOCK);
			if (item == 9)
			{
				snprintf(message, sizeof message,
					LANG("Switch to extended selection <b>(shortcut: %s)</b><br>"
					     "Click to select which point to set/change <b>(shortcut: 0 [zero])</b>."),
					keyGetText(KBD_SWITCH_OFFHAND)
				);
				SIT_SetValues(render.blockInfo, SIT_Title, message, NULL);
			}
			else
			{
				snprintf(message, sizeof message, "<br><b>%s: %c</b>", LANG("shortcut"), '1' + item);
				inventorySetTooltip(render.blockInfo, hover, message);
			}
		}
		if (hover->id == 0)
		{
			renderShowBlockInfo(False, DEBUG_BLOCK);
			render.toolbarItem = NULL;
		}
	}
	else /* use raycasting to get block being pointed at */
	{
		if (render.selection.selFlags & SEL_OFFHAND)
		{
			/* was hovering toolbar: clear tooltip */
			render.selection.selFlags &= ~SEL_OFFHAND;
			renderShowBlockInfo(False, DEBUG_BLOCK);
			render.inventory->offhand &= ~PLAYER_TOOLBAR;
			render.toolbarItem = NULL;
		}

		/* this method has been ripped off from: https://stackoverflow.com/questions/2093096/implementing-ray-picking */
		vec4 clip = {mx * 2. / globals.width - 1, 1 - my * 2. / globals.height, 0, 1};

		matMultByVec(dir, globals.matInvMVP, clip);

		dir[VX] = dir[VX] / dir[VT] - render.camera[VX];
		dir[VY] = dir[VY] / dir[VT] - render.camera[VY];
		dir[VZ] = dir[VZ] / dir[VT] - render.camera[VZ];

		if (mapPointToObject(globals.level, render.camera, dir, render.selection.current, &render.selection.extra))
			render.selection.selFlags |= SEL_POINTTO;
		else
			render.selection.selFlags &= ~(SEL_POINTTO | SEL_NOCURRENT);

		if (render.selection.selFlags & SEL_MOVE)
			selectionSetClonePt(render.selection.current, render.selection.extra.side);

		/* user want to place/pickup an item: update action indicator according to what is pointed */
		if (render.debugInfo & DEBUG_SHOWITEM)
		{
			uint8_t shouldDisplayPreview =
				render.selection.extra.side == SIDE_ENTITY ? PREVIEW_PICKUP :
				render.previewItemId > 0 && render.selection.extra.side == SIDE_TOP ? PREVIEW_BLOCK : PREVIEW_NOTHING;
			if (shouldDisplayPreview != render.previewItem)
			{
				/* selection type has changed */
				switch (render.previewItem = shouldDisplayPreview) {
				case PREVIEW_NOTHING:
					worldItemDeletePreview();
					SIT_SetValues(render.blockInfo, SIT_Visible, False, NULL);
					break;
				case PREVIEW_PICKUP:
					worldItemDeletePreview();
					SIT_SetValues(render.blockInfo, SIT_Visible, True, SIT_Title, "Pick-up item", NULL);
					break;
				case PREVIEW_BLOCK:
					worldItemPreview(render.camera, render.selection.extra.inter, render.previewItemId);
					SIT_SetValues(render.blockInfo, SIT_Visible, True, SIT_Title, "Place item here", NULL);
				}
			}
			else if (render.previewItem == PREVIEW_BLOCK)
			{
				worldItemUpdatePreviewPos(render.camera, render.selection.extra.inter);
			}
		}
	}

	render.mouseX = mx;
	render.mouseY = my;
}

MapExtraData renderGetSelectedBlock(vec4 pos, int * blockModel)
{
	if ((render.selection.selFlags & (SEL_POINTTO|SEL_NOCURRENT)) == SEL_POINTTO)
	{
		if (pos)
		{
			Item item = &render.inventory->items[render.inventory->selected];
			memcpy(pos, item->id > 0 && (render.debugInfo & DEBUG_SELECTION) == 0 && (render.selection.selFlags & SEL_BLOCKPOS) ?
				render.selection.blockPos : render.selection.current, sizeof (vec4));
		}
		if (blockModel)
			*blockModel = render.selection.blockId;

		return &render.selection.extra;
	}
	return NULL;
}

/* SITE_OnResize on root widget */
static int renderGetSize(SIT_Widget w, APTR cd, APTR ud)
{
	if (globals.level == NULL)
		return 0;

	render.scale = globals.width / (3 * 182.f) * ITEMSCALE;
	render.inventory->update ++;

	/* aspect ratio (needed by particle.gsh and waypoints) */
	shading[SHADING_VPWIDTH]  = globals.width;
	shading[SHADING_VPHEIGHT] = globals.height;
	matPerspective(render.matPerspective, globals.fieldOfVision, globals.width / (float) globals.height, NEAR_PLANE, FAR_PLANE);
	glViewport(0, 0, globals.width, globals.height);

	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, 16, shading);

	return 1;
}

/* init a Uniform Buffer Object to quickly transmit data to shaders */
int renderInitUBO(void)
{
	/* normals vector for cube as ordered in blocks.vsh */
	static float normals[] = {
		0,0,1,1,  1,0,0,1,  0,0,-1,1,  -1,0,0,1,  0,1,0,1,  0,-1,0,1
	};

	/*
	 * uniform buffer object: shared among all shaders (uniformBlock.glsl):
	 * mat4 projMatrix;
	 * mat4 MVP;
	 * vec4 lookAt;
	 * vec4 camera;
	 * vec4 sunDir;
	 * vec4 normals[6];
	 * vec4 shading[6];
	 */
	GLuint buffer;
	vec4   sunDir;
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, buffer);
	glBufferData(GL_UNIFORM_BUFFER, UBO_TOTAL_SIZE, NULL, GL_STATIC_DRAW);
	skydomeGetSunPos(sunDir);

	/* these should rarely change */
	shading[SHADING_FOGDIST] = globals.distanceFOG ? globals.renderDist * 16 + 8 : 0;
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_NORMALS, sizeof normals, normals);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET, sizeof shading, shading);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SUNDIR_OFFSET, sizeof (vec4), sunDir);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	return buffer;
}

/* extract under water tile: need a separate image to have repeat flag on texture */
static APTR renderGetUnderWater(DATA8 * data, int * width, int * height, int bpp)
{
	int   tileSz     = *width / 32;
	int   tileStride = tileSz * bpp;
	int   imgStride  = *width * bpp;
	DATA8 source     = *data;
	DATA8 waterTile  = malloc(tileStride * tileSz);
	DATA8 dest;

	int i;
	for (source += UNDERWATER_TILE_X * tileStride + UNDERWATER_TILE_Y * tileSz * imgStride, dest = waterTile, i = tileSz;
	     i > 0; memcpy(dest, source, tileStride), i --, dest += tileStride, source += imgStride);

	render.nvgWater = nvgCreateImageRGBA(globals.nvgCtx, tileSz, tileSz, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST, waterTile);
	free(waterTile);

	return blockPostProcessTexture;
}


/* init static tables and objects */
Bool renderInitStatic(void)
{
	#ifdef DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(debugGLError, NULL);
	#endif

	Bool compiled =
	(render.shaderBlocks     = createGLSLProgram("blocks.vsh",    "blocks.fsh", "blocks.gsh")) &&
	(render.shaderItems      = createGLSLProgram("items.vsh",     "items.fsh", NULL)) &&
	(render.selection.shader = createGLSLProgram("selection.vsh", "selection.fsh", NULL));

	if (! compiled)
		return False;

	if (! wayPointsInitStatic())
		return False;

	if (! raycastInitStatic())
		return False;

	/* init VBO for vboInventoryLoc, vboPreview, vboPreviewLoc, vboInventory */
	glGenBuffers(5, &render.vboInventoryMDAI);

	/* will init vaoInventory, vaoBBox, vaoPreview */
	glGenVertexArrays(3, &render.vaoInventory);

	/* allocate some vbo to display chunk boundary */
	debugInit();
	/* init all the static tables */
	chunkInitStatic();
	halfBlockInit();
	if (! particlesInit())
		return False;
	if (! jsonParse(RESDIR "blocksTable.js", blockCreate))
		return False;
	if (! jsonParse(RESDIR "itemsTable.js", itemCreate))
		return False;
	if (! skydomeInit())
		return False;
	itemInitHash();
	blockParseConnectedTexture();
	blockParseBoundingBox();
	blockParseInventory(render.vboInventory);
	selectionInitStatic(render.selection.shader);
	if (! entityInitStatic())
		return False;

	render.uniformTime = glGetUniformLocation(render.shaderBlocks, "timeMS");
	render.uniformOverlay = glGetUniformLocation(render.shaderBlocks, "underWater");

	/* load main texture file (note: will require some tables from earlier static init functions) */
	render.texBlock = textureLoad(RESDIR, "terrain.png", 1, renderGetUnderWater);

	/* texture used by fog */
	glGenTextures(1, &render.texSky);
	glBindTexture(GL_TEXTURE_2D, render.texSky);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SKYDOME_FBO_SIZE, SKYDOME_FBO_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	/* will be rendered in a off-screen FBO */
	glGenFramebuffers(1, &render.fboSky);
	glBindFramebuffer(GL_FRAMEBUFFER, render.fboSky);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render.texSky, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	/* inventory item VAO/VBO */
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

	/* vao for preview: will also use shader from inventory models */
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

	/* vao for bbox highlight */
	glGenBuffers(2, &render.vboBBoxVTX);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxVTX);
	/* 8 vertices per bbox, 12 bytes per vertex (3 floats), 20 max bounding box */
	glBufferData(GL_ARRAY_BUFFER, 8 * 20 * 20, NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxIdx);
	/* indirect buffer: 36 for faces, 24 for lines, 2 bytes per index, 20 sets max */
	glBufferData(GL_ARRAY_BUFFER, (24 + 36) * 2 * 20, NULL, GL_STATIC_DRAW);

	glBindVertexArray(render.vaoBBox);
	glBindBuffer(GL_ARRAY_BUFFER, render.vboBBoxVTX);
	/* 3 for vertex, 2 for tex coord */
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (APTR) 12);
	glEnableVertexAttribArray(1);
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
		matRotate(render.matInventoryItem, M_PI_4+M_PI, VY);
		matRotate(temp, M_PI_4/2, VX);
		matMult(render.matInventoryItem, temp, render.matInventoryItem);
	}

	/* pre-conpute perspective projection matrix */
	shading[SHADING_VPWIDTH]    = globals.width;
	shading[SHADING_VPHEIGHT]   = globals.height;
	shading[SHADING_BRIGHTNESS] = globals.brightness == 101 ? 1 : globals.brightness * 0.007f;
	matPerspective(render.matPerspective, globals.fieldOfVision, globals.width / (float) globals.height, NEAR_PLANE, FAR_PLANE);

	globals.uboShader = renderInitUBO();
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BUFFER_INDEX, globals.uboShader);

	/* HUD resources */
	render.compass = nvgCreateImage(globals.nvgCtx, RESDIR INTERFACE "compass.png", 0);
	render.debugFont = nvgFindFont(globals.nvgCtx, "sans-serif"); /* created by SITGL */
	render.nvgTerrain = nvgCreateImage(globals.nvgCtx, (APTR) render.texBlock, NVG_IMAGE_NEAREST | NVG_IMAGE_GLTEX);

	/* scale of inventory items (182 = px width of inventory bar) */
	render.scale = globals.width / (3 * 182.f) * ITEMSCALE;
	SIT_AddCallback(globals.app, SITE_OnResize, renderGetSize, NULL);

	render.blockInfo = SIT_CreateWidget("blockinfo", SIT_TOOLTIP, globals.app,
		SIT_ToolTipAnchor, SITV_TooltipFollowMouse,
		SIT_DelayTime,     SITV_TooltipManualTrigger,
		SIT_DisplayTime,   100000,
		NULL
	);

	static float biomeColor[] = {0.411765, 0.768627, 0.294118};
	setShaderValue(render.shaderBlocks, "biomeColor", 3, biomeColor);

	/* don't need it yet */
	SIT_ExtractDialog(render.blockInfo);

	/* will need to measure some stuff before hand */
	return signInitStatic(render.debugFont);
}

void renderSetInventory(Inventory inventory)
{
	render.inventory = inventory;
	if (inventory->texture == 0)
		inventory->texture = nvgCreateImage(globals.nvgCtx, RESDIR "widgets.png", NVG_IMAGE_NEAREST);
    inventory->update = 1;
}

void renderSetCompassOffset(float offset)
{
	render.compassOffset = offset > 0 ? globals.width - offset : 0;
}

Map renderInitWorld(STRPTR path, int renderDist)
{
	Map ret = mapInitFromPath(path, renderDist);
	if (ret)
	{
		render.debug = 0;
		render.camera[VX] = ret->cx;
		render.camera[VY] = ret->cy + PLAYER_HEIGHT;
		render.camera[VZ] = ret->cz;
		SIT_InsertDialog(render.blockInfo);
		if (render.libWnd)
			SIT_InsertDialog(render.libWnd);
		SIT_SetValues(render.blockInfo,
			SIT_ToolTipAnchor, globals.lockMouse ? SITV_TooltipFixed : SITV_TooltipFollowMouse,
			SIT_X, globals.width  >> 1,
			SIT_Y, globals.height >> 1,
			NULL
		);
		if (globals.extraDist > 0)
			raycastInitMap(ret);
		return ret;
	}
	return NULL;
}

void mapUpdateDelAll(void);

/* lots of stuff to free */
void renderCloseWorld(void)
{
	/* these first 3 calls will free 90% of memory usage */
	meshCloseAll(globals.level);
	mapFreeAll(globals.level);
	raycastFreeAll();
	globals.level = NULL;

	/* these are small bits and pieces */
	particleDelAll();
	entityNukeAll();
	signDelAll();
	cartoDelAll();
	selectionCancel();
	mapUpdateDelAll();
	undoDelAll();
	updateClearAll();
	wayPointsClose();

	/* keep some SITGL elements */
	render.libWnd = SIT_GetById(globals.app, "selcopy");
	/* copied selection: allow user to quickly copy/paste selection between worlds */
	if (render.libWnd) SIT_ExtractDialog(render.libWnd);
	SIT_ExtractDialog(render.blockInfo);
	SIT_Nuke(SITV_NukeCtrl);

	globals.modifCount = 0;
	globals.selPoints = 0;
	globals.inEditBox = 0;

	render.message.chrLen = 0;
	render.invCache = 0;
	render.invCount = 0;
	render.invExt = 0;
	render.toolbarItem = 0;
	render.selWnd = NULL;
	render.editWnd = NULL;
	render.selection.selFlags = 0;

	SIT_ExtractDialog(render.blockInfo);
	SIT_Nuke(SITV_NukeCtrl);
}


/* show limits of chunk player is currently in */
void renderToggleDebug(int what)
{
	render.debug ^= what;

	switch (what) {
	case RENDER_DEBUG_BRIGHT:
		shading[SHADING_BRIGHTNESS] = globals.brightness == 101 ? 1 : globals.brightness * 0.007f;
		glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET+16, 16, shading+4);
		break;

	case RENDER_FRAME_ADVANCE:
		if (! render.freeze.text[0])
		{
			NVGcontext * vg = globals.nvgCtx;
			nvgFontFaceId(vg, render.debugFont);
			nvgFontSize(vg, FONTSIZE);
			render.freeze.chrLen = snprintf(render.freeze.text, sizeof render.freeze.text,
				LANG("Use '%s' to advance next frame, Esc to exit this mode"), keyGetText(KBD_FRAME_ADVANCE));
			render.freeze.pxLen = nvgTextBounds(vg, 0, 0, render.freeze.text, render.freeze.text + render.freeze.chrLen, NULL);
		}
	}
}

/* print info from VBO */
void renderDebugBlock(void)
{
	/* no stderr in release build anyway (NBT explorer would be nice though) */
	#ifdef DEBUG
	if (render.selection.extra.entity > 0)
		entityDebug(render.selection.extra.entity);
	else if (render.selection.selFlags & SEL_POINTTO)
		debugBlockVertex(render.selection.current, render.selection.extra.side);
	#endif
}

void renderResetViewport(void)
{
	glViewport(0, 0, globals.width, globals.height);
}

/* view matrix change (pos and/or angle) */
void renderSetViewMat(vec4 pos, vec4 lookat, float * yawPitch)
{
	float old[3];
	memcpy(old, render.camera, sizeof old);
	render.camera[VX] = pos[VX];
	render.camera[VY] = pos[VY] + PLAYER_HEIGHT;
	render.camera[VZ] = pos[VZ];

	if (mapMoveCenter(globals.level, old, render.camera) && globals.extraDist > 0)
		raycastMoveCenter(globals.level, old, render.camera);

	matLookAt(render.matModel, render.camera, (float[3]) {lookat[VX], lookat[VY] + PLAYER_HEIGHT, lookat[VZ]}, (float[3]) {0, 1, 0}, render.nearPlane);
	vecAdd(render.nearPlane, render.nearPlane, render.camera);
	/* must be same as the one used in the vertex shader */
	matMult(globals.matMVP, render.matPerspective, render.matModel);
	/* we will need that matrix sooner or later */
	matInverse(globals.matInvMVP, globals.matMVP);

	old[VX] = lookat[VX] - pos[VX];
	old[VY] = lookat[VY] - pos[VY];
	old[VZ] = lookat[VZ] - pos[VZ];

	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_CAMERA_OFFSET, sizeof (vec4), render.camera);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_LOOKAT_OFFSET, sizeof old, old);

	uint8_t oldDir = globals.direction;
	render.setFrustum = 1;
	render.yaw = yawPitch[0];
	render.pitch = yawPitch[1];
	render.yawFull = yawPitch[2]; /* needed by underwater overlay */
	globals.direction = 1; /* east */
	if (M_PI_4f       <= render.yaw && render.yaw <= M_PI_4f+M_PI_2f) globals.direction = 0; else /* south:  45 ~ 135 */
	if (M_PIf+M_PI_4f <= render.yaw && render.yaw <= 2*M_PIf-M_PI_4f) globals.direction = 2; else /* north: 225 ~ 315 */
	if (M_PIf-M_PI_4f <= render.yaw && render.yaw <= M_PIf+M_PI_4f)   globals.direction = 3;      /* west:  135 ~ 225 */
	if (oldDir != globals.direction)
		selectionSetSize();
	if (globals.lockMouse)
		renderPointToBlock(globals.width >> 1, globals.height >> 1);
}

void renderShowBlockInfo(Bool show, int what)
{
	if (show)
	{
		render.debugInfo |= what;
		if (what & DEBUG_SHOWITEM)
		{
			Inventory inv = render.inventory;
			render.previewItemId = 0;
			render.previewItem = PREVIEW_NOTHING;
			if (inv->selected < 9)
				render.previewItemId = itemHasModel(inv->items + inv->selected);

			if (render.selection.extra.entity > 0 && render.selection.extra.side == SIDE_ENTITY)
			{
				/* pointing at an entity: pick it up if left-clicked */
				render.previewItem = PREVIEW_PICKUP;
				SIT_SetValues(render.blockInfo, SIT_Visible, True, SIT_Title, "Pick-up item", NULL);
			}
			/* check if it is possible to show an item */
			else if (render.previewItemId > 0 && render.selection.extra.side == SIDE_TOP)
			{
				worldItemPreview(render.camera, render.selection.extra.inter, render.previewItemId);
				render.previewItem = PREVIEW_BLOCK;
				SIT_SetValues(render.blockInfo, SIT_Visible, True, SIT_Title, "Place item here", NULL);
			}
			return;
		}
	}
	else
	{
		render.debugInfo &= ~what;
		SIT_SetValues(render.blockInfo, SIT_Visible, False, NULL);
		if (globals.selPoints & 3)
			render.debugInfo |= DEBUG_SELECTION;
		if (what & DEBUG_SHOWITEM)
		{
			render.previewItem = PREVIEW_NOTHING;
			render.previewItemId = 0;
			SIT_SetValues(render.blockInfo, SIT_Visible, False, NULL);
			worldItemDeletePreview();
		}
	}
}

static void renderDrawItems(int count)
{
	mat4 ortho;

	matOrtho(ortho, 0, globals.width, 0, globals.height, 0, 100);

	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_STENCIL_TEST);

	glUseProgram(render.shaderItems);
	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);

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
	NVGcontext * vg = globals.nvgCtx;
	int fh = roundf(scale * 0.4f);
	int sz = roundf(scale * 0.0625f);
	int i;

	nvgBeginFrame(vg, globals.width, globals.height, 1);
	nvgFontFaceId(vg, render.debugFont);
	nvgFontSize(vg, fh);
	nvgTextAlign(vg, NVG_ALIGN_TOP);

	for (i = 0; i < count; i ++, items ++)
	{
		int stack = items->count;
		if (stack > 1)
		{
			div_t res;
			if (stack < 100)
				res = div(stack, 10);
			else
				res.quot = res.rem = 9; /* more than 100 in a stack :-/ cap it out at 99 */

			TEXT number[] = {res.quot == 0 ? ' ' : res.quot + '0', res.rem + '0'};

			int x = roundf(items->x + scale - nvgTextBounds(vg, 0, 0, number, number+2, NULL));
			int y = globals.height - items->y - fh;
			nvgFillColorRGBAS8(vg, "\0\0\0\xff");
			nvgText(vg, x + 2, y + 2, number, number + 2);
			nvgFillColorRGBAS8(vg, "\xff\xff\xff\xff");
			nvgText(vg, x, y, number, number + 2);
		}
		if (items->uses > 0)
		{
			nvgBeginPath(vg);
			float dura = itemDurability(items);
			int y = globals.height - items->y - sz * 2;
			nvgRect(vg, items->x, y, scale, sz*2);
			nvgFillColorRGBAS8(vg, "\0\0\0\xff");
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
		Item      item;
		MDAICmd   cmd;
		MDAICmd_t commands[MAXCOLINV];
		float     location[MAXCOLINV * 3];
		float *   loc;
		int       count, i, ext;

		item = (globals.selPoints & 3) == 3 ? extendedSelItems : render.inventory->items;

		/* inventory has changed: update all GL buffers */
		for (i = count = ext = 0, cmd = commands, loc = location; i < MAXCOLINV; i ++, item ++)
		{
			int size;
			if (item->id == 0) continue;
			if (isBlockId(item->id))
			{
				BlockState b = blockGetById(item->id);
				size = blockInvGetModelSize(b->invId);
			}
			else
			{
				ItemDesc desc = itemGetById(item->id);
				size = desc ? blockInvGetModelSize(desc->glInvId) : 0;
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

/* render items at arbitrary location (used by interface) */
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
		int size;
		if (isBlockId(item->id))
		{
			BlockState b = blockGetById(item->id);
			size = blockInvGetModelSize(b->invId);
		}
		else
		{
			ItemDesc desc = itemGetById(item->id);
			if (desc == NULL) continue;
			size = blockInvGetModelSize(desc->glInvId);
		}

		cmd->count = size >> 20;
		cmd->first = size & 0xfffff;
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

static int compare(const void * item1, const void * item2)
{
	return * (DATA32) item2 - * (DATA32) item1;
}

/*
 * sort alpha transpareny vertices: while costly to do that on the CPU, this operation should not be
 * done every frame.
 */
static inline void renderSortVertex(GPUBank bank, ChunkData cd)
{
	cd->yaw = render.yaw;
	cd->pitch = render.pitch;

	GPUMem mem = bank->usedList + cd->glSlot;
	DATA32 vtx, src1, src2, dist;
	int    count = cd->glAlpha / VERTEX_DATA_SIZE, i;

	/* pre-compute distance of vertices: they are not that cheap to compute */
	dist = count <= 512 ? alloca(count * 8) : malloc(count * 8);
	float X = cd->chunk->X - render.camera[0];
	float Z = cd->chunk->Z - render.camera[2];
	float Y = cd->Y        - render.camera[1];

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bank->vboTerrain);
	/* glSize == size of all vertices (in bytes), glAlpha == amount of alpha vertices (at the end) */
	DATA32 vertex = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, mem->offset + (cd->glSize - cd->glAlpha), cd->glAlpha, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);

	for (vtx = vertex, src2 = dist, i = 0; i < count; i ++, src2 += 2, vtx += VERTEX_INT_SIZE)
	{
		#define VTX(x)     ((x) - ORIGINVTX) * (1./BASEVTX)
		float dx = VTX((bitfieldExtract(vtx[1], 16, 16) +
		                bitfieldExtract(vtx[3],  0, 16)) >> 1);
		float dy = VTX((bitfieldExtract(vtx[2],  0, 16) +
		                bitfieldExtract(vtx[3], 16, 16)) >> 1);
		float dz = VTX((bitfieldExtract(vtx[2], 16, 16) +
		                bitfieldExtract(vtx[4], 16, 16)) >> 1);

		dx += X; dy += Y; dz += Z;
		/* qsort() doesn't want float return value, so convert to fixed point */
		src2[0] = (dx*dx + dy*dy + dz*dz) * 1024;
		src2[1] = i;
		#undef VTX
	}

	/* can't sort 2 arrays at the same time: sort the cheapest first */
	qsort(dist, count, 8, compare);

	/* then move quads from the vertex array */
	for (i = 0, src1 = dist; i < count; i ++, src1 += 2)
	{
		if (src1[1] != i)
		{
			uint8_t  tmpbuf[VERTEX_DATA_SIZE];
			uint16_t loc;
			DATA32   cur, tmp;

			/* need to be moved */
			memcpy(tmpbuf, cur = vertex + i * VERTEX_INT_SIZE, VERTEX_DATA_SIZE);
			memcpy(cur, tmp = vertex + src1[1] * VERTEX_INT_SIZE, VERTEX_DATA_SIZE);
			for (loc = src1[1] * 2 + 1, cur = tmp; dist[loc] != i; cur = tmp)
			{
				uint16_t pos = dist[loc];
				memcpy(cur, tmp = vertex + pos * VERTEX_INT_SIZE, VERTEX_DATA_SIZE);
				dist[loc] = loc >> 1;
				loc = pos * 2 + 1;
			}
			dist[loc] = loc >> 1;
			memcpy(cur, tmpbuf, VERTEX_DATA_SIZE);
		}
	}
	if (count > 512)
		free(dist);

	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

static inline Bool renderHasPlayerMoved(Map map, ChunkData cd)
{
	int off = CHUNK_POS2OFFSET(cd->chunk, render.camera);

	if (map->curOffset != off)
	{
		map->curOffset = off;
		return True;
	}
	return False;
}

/* setup the GL_DRAW_INDIRECT_BUFFER for glMultiDrawArraysIndirect() to draw 95% of the terrain */
static void renderPrepVisibleChunks(Map map)
{
	ChunkData cd, player;
	GPUBank   bank;
	MDAICmd   cmd;
	float *   loc;
	int       dx, dy, dz;

	render.debugTotalTri = 0;

	int Y = CPOS(render.camera[1]);
	player = (0 <= Y && Y < map->center->maxy ? map->center->layer[Y] : NULL);

	if (map->mapArea < 0)
		/* brush: chunks always starts at 0,0,0; map->c{x,y,z} is location of brush in world coord */
		dx = map->cx, dy = map->cy, dz = map->cz;
	else
		dx = dy = dz = 0;

	/* prep all the terrain chunks we will need to render */
	for (bank = HEAD(map->gpuBanks); bank; NEXT(bank))
	{
		if (bank->vtxSize == 0) continue;
		bank->cmdTotal = 0;
		bank->cmdAlpha = 0;
		int alphaIndex = bank->vtxSize - 1;
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
			int    start = mem->offset / VERTEX_DATA_SIZE;

			size -= alpha;
			if (size > 0)
			{
				cmd = bank->cmdBuffer + bank->cmdTotal;
				cmd->count = (cd->cdFlags & CDFLAG_DISCARDABLE ? size - cd->glDiscard : size) / VERTEX_DATA_SIZE;
				cmd->instanceCount = 1;
				cmd->first = start;
				cmd->baseInstance = bank->cmdTotal; /* needed by glVertexAttribDivisor() */
				render.debugTotalTri += cmd->count;
				start += size / VERTEX_DATA_SIZE;

				loc = bank->locBuffer + bank->cmdTotal * (VERTEX_INSTANCE/4);
				loc[0] = dx + chunk->X;
				loc[1] = dy + cd->Y;
				loc[2] = dz + chunk->Z;
				loc[3] = cd->cdFlags >> 5;
				bank->cmdTotal ++;
			}
			/* alpha chunks needs to be drawn from far to near */
			if (alpha > 0)
			{
				bank->cmdAlpha ++;
				cmd = bank->cmdBuffer + alphaIndex;
				cmd->count = alpha / VERTEX_DATA_SIZE;
				cmd->instanceCount = 1;
				cmd->first = start;
				cmd->baseInstance = alphaIndex; /* needed by glVertexAttribDivisor() */
				render.debugTotalTri += cmd->count;

				loc = bank->locBuffer + alphaIndex * (VERTEX_INSTANCE/4);
				loc[0] = dx + chunk->X;
				loc[1] = dy + cd->Y;
				loc[2] = dz + chunk->Z;
				/* alpha don't have fog quads */
				loc[3] = 0;
				alphaIndex --;

				/* check if we need to sort vertex: this is costly but should not be done very often */
				if ((cd->cdFlags & CDFLAG_NOALPHASORT) == 0)
				{
					if ((fabsf(render.yaw - cd->yaw) > M_PI_4f && fabsf(render.yaw - cd->yaw - 2*M_PIf) > M_PI_4f) ||
						 fabsf(render.pitch - cd->pitch) > M_PI_4f ||
						 (player == cd && renderHasPlayerMoved(map, cd)))
					{
						//fprintf(stderr, "sorting chunk %d, %d, %d: %d quads\n", cd->chunk->X, cd->Y, cd->chunk->Z, cd->glAlpha / 28);
						renderSortVertex(bank, cd);
					}
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
	nvgFillColorRGBAS8(vg, "\0\0\0\xff");
	nvgText(vg, x+2, y+2, text, end);
	nvgFillColorRGBAS8(vg, "\xff\xff\xff\xff");
	nvgText(vg, x, y, text, end);
	nvgGlobalAlpha(vg, 1);
}


/* show tooltip near mouse cursor containing some info on the block selected */
void renderBlockInfo(SelBlock_t * sel)
{
	int curCRC;
	if (sel->extra.entity == 0)
	{
		/* pointing to a block: get blockId */
		curCRC = crc32(crc32(0, (DATA8) &sel->extra.blockId, sizeof sel->extra.blockId), (DATA8) sel->current, 3 * sizeof (float));
	}
	else curCRC = entityGetCRC(sel->extra.entity);
	if (curCRC != render.oldBlockCRC)
	{
		TEXT msg[256];
		render.oldBlockCRC = curCRC;
		if (sel->extra.entity == 0)
		{
			int id = sel->extra.blockId;
			int XYZ[] = {sel->current[VX], sel->current[VY], sel->current[VZ]};

			if (sel->extra.special == BLOCK_BED)
			{
				/* color is encoded in tile entity :-/ */
				DATA8 tile = chunkGetTileEntity(sel->extra.cd, sel->extra.offset);
				if (tile)
				{
					struct NBTFile_t nbt = {.mem = tile};
					id &= ~15;
					id |= NBT_GetInt(&nbt, NBT_FindNode(&nbt, 0, "color"), 14);
				}
			}

			sprintf(msg, "X: %d <dim>(%d)</dim>\nY: %d <dim>(%d)</dim>\nZ: %d <dim>(%d)</dim>\n%s <dim>(%d:%d)</dim>",
				XYZ[0], XYZ[0] & 15, XYZ[1], XYZ[1] & 15, XYZ[2], XYZ[2] & 15, blockGetById(id)->name, id>>4, id&15);
		}
		else switch (sel->extra.side) {
		case SIDE_ENTITY:     entityInfo(sel->extra.entity, msg, sizeof msg); break;
		case SIDE_WAYPOINT: wayPointInfo(sel->extra.entity, msg, sizeof msg); break;
		}

		SIT_SetValues(render.blockInfo,
			SIT_Title,       msg,
			SIT_Visible,     True,
			SIT_DisplayTime, SITV_ResetTime,
			NULL
		);
	}
	else SIT_SetValues(render.blockInfo, SIT_Visible, True, NULL);
}

/*
 * world rendering: per-frame shader magic happens here.
 */
void renderWorld(void)
{
	/* generate mesh we didn't have time to do before */
	if (meshReady(globals.level))
	{
		meshGenerate(globals.level);
		render.setFrustum = 1;
	}

	if (render.setFrustum)
	{
		/* do it as late as possible */
		double start = FrameGetTime();
		mapViewFrustum(globals.level, render.nearPlane);
		start = FrameGetTime() - start;
		render.frustumTime = start;
		render.underWater = mapIsPositionInLiquid(globals.level, render.camera);
		render.setFrustum = 0;
	}

	glProgramUniform1ui(render.shaderBlocks, render.uniformTime, globals.curTime);

	/* must be done before glViewport */
	signPrepare(render.camera);

	glClearColor(0, 0, 0, 0);
	glClear(GL_DEPTH_BUFFER_BIT);

	/* sky dome */
	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_MVMATRIX_OFFSET, sizeof (mat4), globals.matMVP);
	skydomeRender(render.fboSky, render.underWater);

	glViewport(0, 0, globals.width, globals.height);

//	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	/* render terrain block */
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);
	glFrontFace(GL_CCW);
	glDepthMask(GL_TRUE);

	glUseProgram(render.shaderBlocks);

	renderPrepVisibleChunks(globals.level);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, render.texSky);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);
	/* 20 times per second max */
	if (globals.curTime - render.animUpdate >= 50)
	{
		render.animUpdate = globals.curTime;
		textureAnimate();
	}

	GPUBank bank;
	glProgramUniform1ui(render.shaderBlocks, render.uniformOverlay, render.underWater);

	/* draw opaque main terrain */
	for (bank = HEAD(globals.level->gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdTotal > 0)
		{
			/* we have something to render from this bank */
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_POINTS, 0, bank->cmdTotal, 0);
		}
	}

	/* text signs and map in item frame */
	signRender();
	cartoRender();
	/* entities */
	glBindTexture(GL_TEXTURE_2D, render.texBlock);
	entityRender();

	/* raycasted chunks, between sky texture and current terrain */
	if (globals.extraDist > 0 && globals.raycastEnabled)
		raycastRender();

	/* translucent terrain */
	glUseProgram(render.shaderBlocks);
	for (bank = HEAD(globals.level->gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdAlpha > 0)
		{
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_POINTS, (void*)(bank->cmdTotal*16), bank->cmdAlpha, 0);
		}
	}

	/* particles */
	particlesRender();

	/* don't add any clutter to the render (ie: F1 view) */
	if (render.debugInfo & DEBUG_NOCLUTTER)
		return;

	/* in-game map marker */
	wayPointsRender(render.camera);

	/* show limit of chunk boundary where player is */
	if (render.debug & RENDER_DEBUG_CURCHUNK)
	{
		debugShowChunkBoundary(globals.level->center, CPOS(render.camera[VY]));
	}

	/* selection overlay */
	if (render.selection.selFlags && render.previewItem == PREVIEW_NOTHING)
		renderSelection();

	if (globals.selPoints)
	{
		glUseProgram(render.selection.shader);
		glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
		selectionRender();
	}
	glBindVertexArray(0);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	//debugRenderCaveGraph();

	NVGcontext * vg = globals.nvgCtx;
	nvgBeginFrame(vg, globals.width, globals.height, 1);
	nvgFontFaceId(vg, render.debugFont);
	nvgFontSize(vg, FONTSIZE);
	nvgTextAlign(vg, NVG_ALIGN_TOP);

	if (render.underWater)
	{
		/* draw underwater overlay */
		float size = MAX(globals.width, globals.height) * 0.5f;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, globals.width, globals.height);
		nvgFillPaint(vg, nvgImagePattern(vg, render.yawFull * globals.width * -0.2f, render.pitch * globals.height * 0.2f, size, size, 0, render.nvgWater, 0.6));
		nvgFill(vg);
	}

	if ((render.debug & RENDER_DEBUG_NOHUD) == 0)
	{
		/* draw the compass */
		float scale;

		if (globals.compassSize > 0)
		{
			scale = globals.height * globals.compassSize * (render.compassOffset > 0 ? 0.11f : 0.15f);
			nvgSave(vg);
			nvgTranslate(vg, globals.width - scale - render.compassOffset, scale); scale -= 20;
			nvgRotate(vg, M_PIf - render.yaw);
			nvgBeginPath(vg);
			nvgRect(vg, -scale, -scale, scale*2, scale*2);
			nvgFillPaint(vg, nvgImagePattern(vg, -scale, -scale, scale*2, scale*2, 0, render.compass, 1));
			nvgFill(vg);
			nvgRestore(vg);
		}

		/* draw inventory bar */
		scale = render.scale;
		render.inventory->x = (globals.width - scale * 182) * 0.5f + 3 * scale;
		render.inventory->y = 3 * scale;
		nvgSave(vg);
		nvgScale(vg, scale, scale);
		nvgTranslate(vg, globals.width / (2 * scale) - 182 / 2, globals.height / scale - 22);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, 182, 22);
		nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, 253, 24, 0, render.inventory->texture, 1));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRect(vg, -26, 0, 22, 22);
		nvgFillPaint(vg, nvgImagePattern(vg, render.inventory->offhand & 2 ? -208-26-23 : -208-26, 0, 253, 24, 0, render.inventory->texture, 1));
		nvgFill(vg);

		int pos = render.inventory->offhand & 1 ? -26 : render.inventory->selected * 20 - 1;

		/* selected slot */
		nvgBeginPath(vg);
		nvgRect(vg, pos, -1, 24, 24);
		nvgFillPaint(vg, nvgImagePattern(vg, pos - 183, -1, 253, 24, 0, render.inventory->texture, 1));
		nvgFill(vg);
		nvgRestore(vg);

		/* message above inventory bar */
		switch (render.inventory->infoState) {
		case INFO_INV_INIT:
			render.inventory->infoX = (globals.width - nvgTextBounds(vg, 0, 0, render.inventory->infoTxt, NULL, NULL)) / 2;
			render.inventory->infoTime = globals.curTime + INFO_INV_DURATION * 1000;
			render.inventory->infoState = INFO_INV_SHOW;
			// no break;
		case INFO_INV_SHOW:
			renderText(vg, render.inventory->infoX, globals.height - 35 * scale, render.inventory->infoTxt, 1);
			if (globals.curTime > render.inventory->infoTime)
			{
				render.inventory->infoState = INFO_INV_FADE;
				render.inventory->infoTime += INFO_INV_FADEOUT * 1000;
			}
			break;
		case INFO_INV_FADE:
			renderText(vg, render.inventory->infoX, globals.height - 35 * scale, render.inventory->infoTxt,
				(render.inventory->infoTime - globals.curTime) / (INFO_INV_FADEOUT * 1000.));
			if (globals.curTime > render.inventory->infoTime)
				render.inventory->infoState = INFO_INV_NONE;
		}

		/* unsaved edit message */
		if (render.message.chrLen > 0)
		{
			nvgFontSize(vg, FONTSIZE_MSG);
			nvgBeginPath(vg);
			nvgRect(vg, FONTSIZE, globals.height - FONTSIZE * 2, render.message.pxLen + FONTSIZE_MSG, FONTSIZE);
			nvgFillColorRGBAS8(vg, "\0\0\0\xaa");
			nvgFill(vg);
			nvgFillColorRGBAS8(vg, "\xff\xff\xff\xff");
			nvgText(vg, FONTSIZE+FONTSIZE_MSG/2, globals.height - FONTSIZE*2+(FONTSIZE-FONTSIZE_MSG)/2, render.message.text,
				render.message.text + render.message.chrLen);
		}

		if (globals.lockMouse)
		{
			/* mouse cursor is hidden, therefore draw a crosshair */
			float x = globals.width  >> 1;
			float y = globals.height >> 1;
			nvgSave(vg);
			nvgBeginPath(vg);
			nvgGlobalCompositeBlendFuncSeparate(vg, NVG_ONE_MINUS_DST_COLOR, NVG_ZERO, NVG_ONE, NVG_ZERO);
			nvgStrokeColor(vg, nvgRGB(255,255,255));
			nvgStrokeWidth(vg, 7.5);
			nvgMoveTo(vg, x, y - 30);
			nvgLineTo(vg, x, y + 30);
			nvgMoveTo(vg, x - 30, y);
			nvgLineTo(vg, x + 30, y);
			nvgStroke(vg);
			nvgRestore(vg);
		}
	}

	/* debug info */
	if (render.debug & RENDER_DEBUG_CURCHUNK)
	{
		debugCoord(vg, render.camera, render.debugTotalTri);
		entityRenderBBox();
		//quadTreeDebug(globals.nvgCtx);
	}

	if (render.debug & RENDER_FRAME_ADVANCE)
	{
		/* show a message of why time is frozen */
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, globals.width, FONTSIZE + 10);
		nvgFillColorRGBA8(vg, "\0\0\0\x7f");
		nvgFill(vg);
		nvgFillColorRGBA8(vg, "\xff\xff\xff\xff");
		nvgFontSize(vg, FONTSIZE);
		nvgText(vg, (globals.width - render.freeze.pxLen) >> 1, 5, render.freeze.text, render.freeze.text + render.freeze.chrLen);
	}

	nvgEndFrame(vg);

	/* inventory items needs to be rendered after nanovg commands */
	if ((render.debug & RENDER_DEBUG_NOHUD) == 0)
		renderInventoryItems(render.scale);

	if (render.debugInfo & DEBUG_BLOCK)
	{
		if (render.selection.extra.entity > 0 || (render.selection.selFlags & SEL_POINTTO))
			/* tooltip about block being pointed at */
			renderBlockInfo(&render.selection);
		else
			SIT_SetValues(render.blockInfo, SIT_Visible, False, NULL);
	}
}

/* mostly used to render cloned selection */
void renderDrawMap(Map map)
{
	GPUBank bank;
	renderPrepVisibleChunks(map);

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);
	glFrontFace(GL_CCW);
	glDepthMask(GL_TRUE);
	glUseProgram(render.shaderBlocks);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, render.texBlock);

	for (bank = HEAD(map->gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdTotal > 0)
		{
			/* we have something to render from this bank */
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_POINTS, 0, bank->cmdTotal, 0);
		}
	}

	/* second: entities */
	if (BRUSH_ENTITIES(map))
		entityCopyRender(BRUSH_ENTITIES(map));

	/* third pass: translucent terrain */
	for (bank = HEAD(map->gpuBanks); bank; NEXT(bank))
	{
		if (bank->cmdAlpha > 0)
		{
			/* we have something to render from this bank */
			glBindVertexArray(bank->vaoTerrain);
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bank->vboMDAI);
			glMultiDrawArraysIndirect(GL_POINTS, (void*)(bank->cmdTotal*16), bank->cmdAlpha, 0);
		}
	}
	/* it can be reset by libraryGenThumb() */
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BUFFER_INDEX, globals.uboShader);
}

/*
 * map update actions
 */
void renderAddModif(void)
{
	/* new chunks might have been created */
	nvgFontFaceId(globals.nvgCtx, render.debugFont);
	nvgFontSize(globals.nvgCtx, FONTSIZE_MSG);
	globals.modifCount ++;
	render.message.chrLen = sprintf(render.message.text, LangStrPlural(globals.modifCount, DLANG("%d unsaved edit"), DLANG("%d unsaved edits")), globals.modifCount);
	render.message.pxLen  = nvgTextBounds(globals.nvgCtx, 0, 0, render.message.text, render.message.text + render.message.chrLen, NULL);
}

/* an operation was undone (called from undoredo.c) */
void renderCancelModif(void)
{
	/* hmm, should not happen */
	if (globals.modifCount == 0) return;

	globals.modifCount --;
	if (globals.modifCount > 0)
	{
		globals.modifCount --;
		renderAddModif();
	}
	else render.message.chrLen = 0;
}

void renderAllSaved(void)
{
	globals.modifCount = 0;
	render.message.chrLen = 0;
}

int renderGetTerrain(int size[2], int * texId)
{
	if (size) size[0] = 512, size[1] = 1024;
	if (texId) texId[0] = render.texBlock;
	return render.nvgTerrain;
}

void renderResetFrustum(void)
{
	static int oldDist;
	int dist = globals.distanceFOG ? globals.renderDist * 16 + 8 : 0;
	if (oldDist != dist)
	{
		shading[SHADING_FOGDIST] = dist;
		oldDist = 0;
		glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
		glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET+SHADING_FOGDIST*4, sizeof (float), shading + SHADING_FOGDIST);
	}
	render.setFrustum = 1;
}

/* SIT_Nuke is about to be called */
void renderSaveRestoreState(Bool save)
{
	if (save)
	{
		/* this will avoid recreating everything */
		render.selWnd  = SIT_GetById(globals.app, "selection"); /* selection nudge */
		render.libWnd  = SIT_GetById(globals.app, "selcopy");   /* copied selection window */
		render.editWnd = SIT_GetById(globals.app, "editbrush");
		if (render.selWnd)  SIT_ExtractDialog(render.selWnd);
		if (render.libWnd)  SIT_ExtractDialog(render.libWnd);
		if (render.editWnd) SIT_ExtractDialog(render.editWnd);
		SIT_ExtractDialog(render.blockInfo);
	}
	else
	{
		render.oldBlockCRC = 0;
		if (render.selWnd)  SIT_InsertDialog(render.selWnd),  render.selWnd = NULL;
		if (render.libWnd)  SIT_InsertDialog(render.libWnd),  render.libWnd = NULL;
		if (render.editWnd) SIT_InsertDialog(render.editWnd), render.editWnd = NULL;
		SIT_InsertDialog(render.blockInfo);
	}
}

/* will use the value from globals :-/ */
void renderSetFOV(float fov)
{
	matPerspective(render.matPerspective, fov, globals.width / (float) globals.height, NEAR_PLANE, FAR_PLANE);
	/* must be same as the one used in the vertex shader */
	matMult(globals.matMVP, render.matPerspective, render.matModel);
	/* we will need that matrix sooner or later */
	matInverse(globals.matInvMVP, globals.matMVP);

	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof (mat4), render.matPerspective);

	render.setFrustum = 1;
}

void renderSetFOG(int fogEnabled)
{
	shading[SHADING_FOGDIST] = fogEnabled ? globals.renderDist * 16 + 8 : 0;
	glBindBuffer(GL_UNIFORM_BUFFER, globals.uboShader);
	glBufferSubData(GL_UNIFORM_BUFFER, UBO_SHADING_OFFSET+SHADING_FOGDIST*4, sizeof (float), shading + SHADING_FOGDIST);
}
