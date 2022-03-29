/*
 * sign.c : logic to render text of signs (post or standing): draw the text into a GL texture and
 *          use that texture as a decal on the sign. Texture will contain up to 128 signs (8x16),
 *          so lots of boilerplate code will ne needed to keep track of everything.
 *
 *          note: the sign model itself is handled in the chunk meshing phase.
 *
 * Written by T.Pierron, jan 2021
 */

#define SIGN_IMPL
#include <glad.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "nanovg.h"
#include "nanovg_gl_utils.h"
#include "sign.h"
#include "maps.h"
#include "render.h"
#include "blocks.h"
#include "undoredo.h"
#include "NBT2.h"
#include "globals.h"

/*
 * TileEntity associated with block (292 bytes):
 *    TAG_String("Text4"): {"text":"XYZ DEF GHI KLM"} [52]
 *    TAG_String("Text3"): {"text":"hello world!"} [40]
 *    TAG_String("Text2"): {"text":"iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"} [76]
 *    TAG_String("id"): minecraft:sign [28]
 *    TAG_String("Text1"): {"text":"wwwwwwwwwwwwwww"} [44]
 *    TAG_Int("x"): -174 [16]
 *    TAG_Int("y"): 73 [16]
 *    TAG_Int("z"): -45 [16]
 */

static struct SignPrivate_t signs;
#if 0
#define RGBA(hex)    (hex >> 16), (hex >> 8) & 0xff, (hex & 0xff), 0xff
/* hmm, maybe later ... */
static uint8_t colors[] = {
	RGBA(0x000000),
	RGBA(0x0000AA),
	RGBA(0x00AA00),
	RGBA(0x00AAAA),
	RGBA(0xAA0000),
	RGBA(0xAA00AA),
	RGBA(0xFFAA00),
	RGBA(0xAAAAAA),
	RGBA(0x555555),
	RGBA(0x5555FF),
	RGBA(0x55FF55),
	RGBA(0x55FFFF),
	RGBA(0xFF5555),
	RGBA(0xFF55FF),
	RGBA(0xFFFF55),
	RGBA(0xFFFFFF),
	RGBA(0xDDD605),
};
#undef RGBA
#endif

char signMinText[] = "wwwwwwwwwwwwwww";

Bool signInitStatic(int font)
{
	signs.shader = createGLSLProgram("decals.vsh", "decals.fsh", NULL);

	if (! signs.shader)
		return False;

	/* since we are only rendering quad, count will always be 6 for all glMultiDrawArrays() objects */
	int i;
	for (i = 0; i < BANK_MAX; i ++)
		signs.mdaCount[i] = 6;

	signs.font = font;

	void cartoInitStatic(int shader, int * mdaCount);
	cartoInitStatic(signs.shader, signs.mdaCount);

	return True;
}

/* where writable texture face is located in terrain.png (tile coord) */
#define SIGN_TEX_X      15
#define SIGN_TEX_Y      2

/* get the vertices where the text will be drawn on */
void signFillVertex(int blockId, float pt[6], int uv[4])
{
	BlockState b = blockGetById(blockId);

	if (b->custModel)
	{
		DATA16 vertex = b->custModel;
		int    count;

		/* grab the vertex and exact uv tex of face that has a UV that are within SIGN_TEX_XY */
		for (count = vertex[-1]; count > 0; count -= 6, vertex += 6 * INT_PER_VERTEX)
		{
			int U1 = GET_UCOORD(vertex);
			int V1 = GET_VCOORD(vertex);

			if (SIGN_TEX_X*16 <= U1 && U1 <= SIGN_TEX_X*16+16 &&
			    SIGN_TEX_Y*16 <= V1 && V1 <= SIGN_TEX_Y*16+16)
			{
				int i;
				/* somewhat hack: take the first face */
				if (pt)
				{
					for (i = 0; i < 3; i ++)
					{
						pt[i]   = (vertex[i]    - ORIGINVTX) * (1. / BASEVTX);
						pt[i+3] = (vertex[10+i] - ORIGINVTX) * (1. / BASEVTX);
					}
				}
				if (uv)
				{
					uv[0] = GET_UCOORD(vertex);
					uv[1] = GET_VCOORD(vertex);
					uv[2] = GET_UCOORD(vertex+INT_PER_VERTEX*2);
					uv[3] = GET_VCOORD(vertex+INT_PER_VERTEX*2);
				}
				break;
			}
		}
	}
}

/* text is stored in JSON, really ?! */
static int signParseText(DATA8 dest, int max, DATA8 json)
{
	/* do it quick'n dirty */
	DATA8 text = strstr(json, "\"text\":");
	if (text && text[7] == '\"')
	{
		/* it's not like NBT allow storing arbitrary datatypes like JSON does */
		return jsonParseString(dest, text + 8, max);
	}
	int i;
	/* get the whole text (older version (<1.8 ?) didn't use json */
	for (i = 0; i < max && json[0]; i ++, json ++, dest ++)
		dest[0] = json[0];
	if (i < max) dest[0] = 0;
	else         dest[-1] = 0;
	return i;
}

/* convert sign text into a user-editable string for a multi-line text edit */
void signGetText(vec4 pos, DATA8 text, int max)
{
	int XYZ[] = {pos[0], pos[1], pos[2]};
	int i, j, nb;

	text[0] = 0;
	for (i = nb = 0, text[0] = 0; i < signs.count; i ++)
	{
		SignText sign = signs.list + i;
		if (memcmp(sign->XYZ, XYZ, sizeof XYZ) == 0)
		{
			for (j = 0; j < 4; j ++)
			{
				if (sign->text[j] > 0)
					nb += signParseText(text + nb, max - nb, sign->tile + sign->text[j]);
				if (nb < max)
					text[nb] = '\n', nb ++;
			}
			/* remove unnecessary newlines at the end */
			while (nb > 0 && text[nb-1] == '\n') nb --;
			text[nb] = 0;
			break;
		}
	}
}

/* extract all the information we need from NBT */
static void signParseEntity(SignText sign)
{
	struct NBTFile_t nbt = {.mem = sign->tile};
	struct NBTIter_t iter;
	int i;

	memset(sign->text, 0, sizeof sign->text);
	memset(sign->XYZ,  0, sizeof sign->XYZ);
	if (! nbt.mem) return;
	NBT_IterCompound(&iter, nbt.mem);
	while ((i = NBT_Iter(&iter)) >= 0)
	{
		switch (iter.name[0]) {
		case 't': case 'T':
			if (strncasecmp(iter.name + 1, "ext", 3) == 0)
			{
				int id = iter.name[4] - '1';
				if (0 <= id && id <= 3)
					sign->text[id] = (DATA8) NBT_Payload(&nbt, i) - nbt.mem;
			}
			break;
		case 'x': case 'X': sign->XYZ[0] = NBT_GetInt(&nbt, i, 0); break;
		case 'y': case 'Y': sign->XYZ[1] = NBT_GetInt(&nbt, i, 0); break;
		case 'z': case 'Z': sign->XYZ[2] = NBT_GetInt(&nbt, i, 0); break;
		}
	}

	/* check if sign is empty: avoid allocating off-screen slot if it is */
	for (i = 0, sign->empty = False; i < 4; i ++)
	{
		/* would have been easier if that crap wasn't stored in JSON */
		uint8_t text[128];
		int len = sign->text[i];
		if (len == 0) continue;
		if (signParseText(text, sizeof text, sign->tile + len) > 0)
			/* not empty */
			return;
	}
	sign->empty = True;
}

/* update off-screen texture for a given sign */
static void signUpdateBank(SignText sign)
{
	static char ellipsis[] = "...";

	int      slot = sign->bank;
	SignBank bank = signs.banks + (slot & 0xff);
	NVGCTX   vg   = globals.nvgCtx;
	float    x, y, ellipse;

	/*
	 * /!\ despite origin of coordinate system of nanovg being top left corner, texture
	 * inside FBO will fliped vertically :-/
	 */
	nvgluBindFramebuffer(bank->nvgFBO);
	glViewport(0, 0, SIGN_WIDTH * BANK_WIDTH, SIGN_HEIGHT * BANK_HEIGHT);
	nvgBeginFrame(vg, SIGN_WIDTH * BANK_WIDTH, SIGN_HEIGHT * BANK_HEIGHT, 1);

	slot >>= 8;
	x = (slot &  7) * SIGN_WIDTH;
	y = (slot >> 3) * SIGN_HEIGHT;

	nvgFontFaceId(vg, signs.font);
	nvgFontSize(vg, SIGN_HEIGHT/4);
	nvgTextAlign(vg, NVG_ALIGN_TOP | NVG_ALIGN_LEFT);
	nvgFillColorRGBAS8(vg, "\0\0\0\xff");
	/* clear remain of previous sign if any */
	nvgBeginPath(vg);
	nvgRect(vg, x, y, SIGN_WIDTH, SIGN_HEIGHT);
	nvgFill(vg);

	/* now we can draw the text */
	nvgFillColorRGBAS8(vg, "\xff\0\0\xff");
	ellipse = nvgTextBounds(vg, 0, 0, ellipsis, ellipsis + 3, NULL);

	int i;
	for (i = 0; i < 4; i ++, y += SIGN_HEIGHT / 4)
	{
		uint8_t text[128];
		int len = sign->text[i];
		if (len == 0) continue;
		len = signParseText(text, sizeof text, sign->tile + len);

		if (len > 0)
		{
			float width = nvgTextBounds(vg, 0, 0, text, text + len, NULL);
			if (width > SIGN_WIDTH)
			{
				/* too big: reduce font size a bit */
				int fontsz = SIGN_HEIGHT/4 * SIGN_WIDTH / width;
				if (fontsz < 10) fontsz = 10;
				/*
				 * note: don't try to use nvgScale, the result will be blurry mess due to stb_truetype
				 *       anti-aliasing, that will be blurred with nvGscale(), that will be blurred by GL_LINEAR.
				 */
				nvgFontSize(vg, fontsz);
				width = nvgTextBounds(vg, 0, 0, text, text + len, NULL);
				float ypos = y + (SIGN_HEIGHT/4 - fontsz) * 0.5f;
				if (width > SIGN_WIDTH)
				{
					/* argh, still too big: write text with ellipsis */
					float pos;
					len = nvgTextFit(vg, text, text + len, SIGN_WIDTH - ellipse, &width);
					pos = nvgText(vg, x + (SIGN_WIDTH - width - ellipse) * 0.5f, ypos, text, text + len);
					nvgText(vg, pos, ypos, ellipsis, ellipsis + 3);
				}
				else nvgText(vg, x + (SIGN_WIDTH - width) * 0.5f, ypos, text, text + len);
				nvgFontSize(vg, SIGN_HEIGHT/4);
			}
			else nvgText(vg, x + (SIGN_WIDTH - width) * 0.5f, y, text, text + len);
		}
	}

	nvgEndFrame(vg);
	nvgluBindFramebuffer(NULL);
	glViewport(0, 0, globals.width, globals.height);
}


/* sign has been modified, re-generate tile entity and texture */
void signSetText(Chunk chunk, vec4 pos, DATA8 msg)
{
	STRPTR text[4], tile;
	int    XYZ[] = {pos[0], pos[1], pos[2]};
	int    i;

	Split(text, (STRPTR) msg, DIM(text), '\n');

	for (i = 0; i < 4; i ++)
	{
		tile = text[i];
		if (IsDef(tile))
		{
			/* has to convert this crap back to json */
			STRPTR buffer = text[i] = alloca(16 + strlen(tile) + StrCount(tile, '\\') + StrCount(tile, '\"'));
			for (strcpy(buffer, "\"text\":\""), buffer += 8; *tile; tile ++)
			{
				uint8_t chr = *tile;
				switch (chr) {
				case '\\':
				case '\"': *buffer++ = '\\'; // no break;
				default:   *buffer++ = chr;
				}
			}
			buffer[0] = '\"';
			buffer[1] = 0;
		}
		else text[i] = NULL;
	}

	SignText sign;
	for (i = signs.count, sign = signs.list; i > 0; i --, sign ++)
	{
		if (memcmp(sign->XYZ, XYZ, sizeof XYZ) == 0)
		{
			/* update tile entity and back buffer */
			ChunkData cd = chunk->layer[XYZ[1]>>4];
			if (cd == NULL) break;
			XYZ[0] &= 15;
			XYZ[2] &= 15;

			struct BlockIter_t iter = {.blockIds = cd->blockIds, .offset = CHUNK_BLOCK_POS(XYZ[0], XYZ[2], XYZ[1]&15)};
			int blockId = getBlockId(&iter);
			undoLog(LOG_BLOCK, blockId, chunkGetTileEntity(chunk, XYZ), cd, iter.offset);
			tile = blockCreateTileEntity(blockId, pos, text);
			if (tile == NULL) break;
			chunkAddTileEntity(chunk, XYZ, tile);
			chunkMarkForUpdate(chunk, CHUNK_NBT_TILEENTITIES);
			sign->tile = tile;
			signParseEntity(sign);

			if (sign->bank >= 0)
				signUpdateBank(sign);
			else if (! sign->empty)
				signs.listDirty = 1;

			break;
		}
	}
}

/* keep all signs in a list, but don't render anything yet */
int signAddToList(int blockId, ChunkData cd, int offset, int prev, uint8_t light)
{
	struct SignText_t sign = {.bank = -1, .light = light};
	int i;

	sign.tile = chunkGetTileEntityFromOffset(cd->chunk, cd->Y, offset);

	if (sign.tile)
	{
		/* extract all the information we will need to render the sign from NBT */
		signParseEntity(&sign);
	}
	else /* no tile entity at this location: not good, but allow editing anyway */
	{
		sign.XYZ[VX] = cd->chunk->X + (offset & 15);
		sign.XYZ[VZ] = cd->chunk->Z + ((offset >> 4) & 15);
		sign.XYZ[VY] = cd->Y + (offset >> 8);
		sign.empty = True;
	}

	//fprintf(stderr, "%d/%d. adding sign at %d, %d, %d: %p\n", signs.count, signs.max, sign.XYZ[0], sign.XYZ[1], sign.XYZ[2], tile);

	/* check if it is already in the list */
	int first = prev;
	if (prev >= 0)
	{
		for (;;)
		{
			SignText ptr = signs.list + prev;
			if (memcmp(ptr->XYZ, sign.XYZ, sizeof sign.XYZ) == 0)
				/* already in the list: assume no changes (there is a dedicated function for this: signSetText()) */
				return first;
			if (ptr->next == 0) break;
			prev = ptr->next;
		}
	}

	if (signs.count == signs.max)
	{
		int old = signs.max;
		int max = old + 32;
		signs.list = realloc(signs.list, max * sizeof (struct SignText_t) + (max >> 5) * 4);

		/* move some buffers to keep them contiguous */
		signs.usage = (DATA32) (signs.list + max);
		signs.max = max;
		if (old > 0)
		{
			/* usage buffers (in signs.list) */
			memmove(signs.usage, signs.list + old, 4 * (old >> 5));
		}
		signs.usage[old>>5] = 0;
	}

	/* check for a free place */
	i = mapFirstFree(signs.usage, signs.max >> 5);

	signFillVertex(blockId, sign.pt1, NULL);
	signs.list[i] = sign;
	signs.count ++;
	signs.listDirty = 1;

	if (prev >= 0)
		signs.list[prev].next = i;

	return first >= 0 ? first : i;
}

/* chunk freed */
void signDel(DATA8 tile)
{
	SignText sign;
	int      i;
	for (i = 0, sign = signs.list; i < signs.count && sign->tile != tile; sign ++, i ++);
	if (i < signs.count)
	{
		int slot = sign->bank;
		sign->tile = NULL;
		signs.usage[i >> 5] ^= 1 << (i & 31);
		signs.count --;

		if (slot >= 0)
		{
			SignBank bank = signs.banks + (slot & 0xff);

			/* free slot from bank */
			slot >>= 8;
			bank->usage[slot >> 5] ^= 1 << (slot & 31);
			bank->inBank --;
			if (bank->inBank == 0)
			{
				/* only delete what's expensive */
				nvgluDeleteFramebuffer(bank->nvgFBO);
				bank->nvgFBO = NULL;
			}

			signs.listDirty = True;
		}
	}
}

/* map being closed */
void signDelAll(void)
{
	SignBank bank;
	int i;
	for (i = signs.maxBank, bank = signs.banks; i > 0; bank ++, i --)
	{
		glDeleteBuffers(1, &bank->vbo);
		glDeleteVertexArrays(1, &bank->vao);
		free(bank->mdaFirst);
		if (bank->nvgFBO)
			nvgluDeleteFramebuffer(bank->nvgFBO);
	}

	free(signs.banks);
	free(signs.list);
	memset(&signs, 0, offsetp(struct SignPrivate_t *, mdaCount));
}

/* render the sign into off-screen texture using nanovg */
static void signAddToBank(SignText sign)
{
	SignBank bank;
	int      i, slot;

	for (i = 0, slot = -1, bank = signs.banks; i < signs.maxBank; i ++, bank ++)
	{
		if (bank->inBank < BANK_MAX)
		{
			slot = mapFirstFree(bank->usage, DIM(bank->usage));
			if (slot >= 0) break;
		}
	}

	if (slot < 0)
	{
		signs.maxBank ++;
		bank = realloc(signs.banks, signs.maxBank * sizeof *signs.banks);
		if (bank == NULL) return;
		signs.banks = bank;
		bank += signs.maxBank - 1;
		memset(bank, 0, sizeof *bank);
		bank->mdaFirst = malloc(4 * BANK_MAX);
		bank->usage[0] = 1;
		slot = 0;
	}

	if (bank->vbo == 0)
	{
		glGenBuffers(1, &bank->vbo);
		glGenVertexArrays(1, &bank->vao);

		glBindVertexArray(bank->vao);
		glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
		glBufferData(GL_ARRAY_BUFFER, 16 * 6 * BANK_MAX, NULL, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 4, GL_FLOAT, 0, 0, 0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	if (bank->nvgFBO == NULL)
		bank->nvgFBO = nvgluCreateFramebuffer(globals.nvgCtx, SIGN_WIDTH * BANK_WIDTH, SIGN_HEIGHT * BANK_HEIGHT, NVG_IMAGE_MASK);

	bank->inBank ++;
	bank->update = 1;
	sign->bank = (slot << 8) | i;

	signUpdateBank(sign);
}

/* render (offscreen) just what is close enough to the camera */
void signPrepare(vec4 camera)
{
	SignText sign;
	SignBank bank;
	int      count;
	int      pos[] = {camera[0], camera[1], camera[2]};

	/* same block: don't redo the whole thing again */
	if (! signs.listDirty && memcmp(signs.curXYZ, pos, sizeof pos) == 0)
		return;

	memcpy(signs.curXYZ, pos, sizeof pos);
	signs.toRender = 0;
	signs.listDirty = 0;

	for (bank = signs.banks, count = signs.maxBank; count > 0; bank->inMDA = 0, bank->update = 0, count --, bank ++);

	for (sign = signs.list, count = signs.count; count > 0; count --, sign ++)
	{
		if (sign->tile == NULL || sign->empty) continue;
		int dx = sign->XYZ[0] - pos[0];
		int dy = sign->XYZ[1] - pos[1];
		int dz = sign->XYZ[2] - pos[2];

		if (dx*dx + dy*dy + dz*dz < SIGN_MAX_DIST*SIGN_MAX_DIST)
		{
			/* generate one quad per sign: need 6 vec4 */
			static uint8_t vtx[] = {
				0,1,2,10,   0,4,2,11,   3,4,5,12,
				3,1,5,13,   0,1,2,10,   3,4,5,12
			};
			static float addMeta[] = {
				0, -(1<<14), (1<<10)-(1<<14), 1<<10
			};
			float vertices[4*6], meta;
			uint8_t slot;
			/* include in rendering */
			if (sign->bank < 0)
			{
				/* not yet rendered: do it now */
				signAddToBank(sign);
			}
			/* nanovg texture will be fliped vertically: modify vertical slot accordingly here */
			slot = sign->bank >> 8;
			bank = signs.banks + (sign->bank & 0xff);
			meta = ((BANK_HEIGHT - slot / BANK_WIDTH) << 14) | ((slot & (BANK_WIDTH-1)) << 10) | sign->light;

			int i;
			for (i = 0; i < DIM(vtx); i ++)
			{
				uint8_t id = vtx[i];
				vertices[i] = id >= 10 ? meta + addMeta[id-10] : sign->XYZ[i&3] + sign->pt1[id];
			}
			i = slot * 6;
			glBindBuffer(GL_ARRAY_BUFFER, bank->vbo);
			glBufferSubData(GL_ARRAY_BUFFER, i * 16, sizeof vertices, vertices);
			bank->mdaFirst[bank->inMDA++] = i;
			signs.toRender ++;
		}
	}

	/* update mipmap if texture has changed */
	for (bank = signs.banks, count = signs.maxBank; count > 0; count --, bank ++)
	{
		if (bank->update)
		{
			glBindTexture(GL_TEXTURE_2D, bank->nvgFBO->texture);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	}
}

void signRender(void)
{
	if (signs.toRender == 0)
		return;

	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glFrontFace(GL_CCW);
	glActiveTexture(GL_TEXTURE0);
	/* sign text have same coordinates than the model: offset depth values to prevent z-fighting */
	glPolygonOffset(-5.0, -5.0);

	glUseProgram(signs.shader);

	SignBank bank;
	int      count;
	for (bank = signs.banks, count = signs.maxBank; count > 0; count --, bank ++)
	{
		if (bank->inMDA == 0) continue;
		glBindVertexArray(bank->vao);
		glBindTexture(GL_TEXTURE_2D, bank->nvgFBO->texture);
		glMultiDrawArrays(GL_TRIANGLES, bank->mdaFirst, signs.mdaCount, bank->inMDA);
	}
	glDisable(GL_POLYGON_OFFSET_FILL);
}
