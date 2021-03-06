/*
 * items.c: manage items from MC (all the stuff from items.png).
 *
 * Written by T.Pierron, oct 2020
 */

#define ITEMS_IMPL
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <zlib.h>
#include "items.h"
#include "SIT.h"
#include "blocks.h"
#include "NBT2.h"

static struct ItemsState_t items;

STRPTR stringAddPool(STRPTR string, int extra);

Bool itemCreate(const char * file, STRPTR * keys, int line)
{
	struct ItemDesc_t item;
	STRPTR value;

	memset(&item, 0, sizeof item);

	value = jsonValue(keys, "id");

	if (! IsDef(value) || atoi(value) < 256)
	{
		SIT_Log(SIT_ERROR, "%s: missing or invalid id on line %d\n", file, line);
		return False;
	}

	item.id = ITEMID(atoi(value), 0);
	item.name = stringAddPool(jsonValue(keys, "name"), 0);

	value = jsonValue(keys, "state");
	if (value) item.id |= atoi(value);

	value = jsonValue(keys, "durability");
	if (value)
	{
		switch (FindInList("DIAMOND,IRON,STONE,WOOD,GOLD", value, 0)) {
		case 0:  item.durability = 1561; break;
		case 1:  item.durability = 250; break;
		case 2:  item.durability = 131; break;
		case 3:  item.durability = 59; break;
		case 4:  item.durability = 32; break;
		default: item.durability = atoi(value);
		}
	}

	value = jsonValue(keys, "stack");
	item.stack = value ? atoi(value) : 1;

	value = jsonValue(keys, "cat");
	if (value)
	{
		int cat = FindInList("ALLCAT,BUILD,DECO,REDSTONE,CROPS,RAILS", value, 0);
		if (cat < 0)
		{
			SIT_Log(SIT_ERROR, "%s: unknown inventory category '%s' on line %d\n", file, value, line);
			return False;
		}
		item.category = cat;
	}

	value = jsonValue(keys, "tex");
	if (value && value[0] == '[')
	{
		item.texU = strtoul(value+1, &value, 10); if (*value == ',') value ++;
		item.texV = strtoul(value,   &value, 10);
	}
	else
	{
		SIT_Log(SIT_ERROR, "%s: missing texture coord for item %s on line %d\n", file, item.name, line);
		return False;
	}

	value = jsonValue(keys, "createBlock");
	if (value)
	{
		/* can't use hash table yet: it is not allocated yet */
		Block b;
		int   i;
		for (i = 1, b = blockIds + 1; i < 256 && b->id; b ++, i ++)
			if (strcmp(b->tech, value) == 0) break;
		if (i < 256)
			item.refBlock = i;
		else
			fprintf(stderr, "unknown block '%s'\n", value);
	}

	value = jsonValue(keys, "createTileEntity");
	if (value)
		item.tile = stringAddPool(value, 0);

	value = jsonValue(keys, "tech");
	item.tech = stringAddPool(value ? value : item.name, 0);
	for (value = item.tech; *value; value ++)
	{
		uint8_t chr = *value;
		if ('A' <= chr && chr <= 'Z')
			*value = chr - 'A' + 'a';
		else if (chr == ' ')
			*value = '_';
	}

	/* check for misspelled properties */
	while (*keys)
	{
		if (FindInList("id,state,name,tex,cat,stack,durability,tech,createBlock,createTileEntity", *keys, 0) < 0)
		{
			SIT_Log(SIT_ERROR, "%s: unknown property %s on line %d\n", file, *keys, line);
			return False;
		}
		keys += 2;
	}

	/* everything seems ok, alloc item */
	#define POOLITEMS       128
	#define POOLMASK        (POOLITEMS-1)
	if ((items.count & POOLMASK) == 0)
	{
		/* keep this table contiguous */
		items.table = realloc(items.table, (items.count + POOLITEMS) * sizeof *items.table);
		if (items.table == NULL) return False;
	}

	ItemDesc desc = items.table + items.count;
	memcpy(desc, &item, sizeof *desc);
	items.count ++;
	#undef POOLSTATES
	#undef POOLMASK

	return True;
}

static void itemHashAdd(ItemHash table, int max, STRPTR name, ItemID_t id)
{
	if (! IsDef(name)) return;

	ItemHash hash, last;
	uint32_t crc = crc32(0, name, strlen(name));
	int      index = crc % max;

	for (last = NULL; ; )
	{
		hash = table + index;
		if (hash->crc)
		{
			if (hash->next && last == NULL)
				index = hash->next - 1;
			else if (last)
				index ++;
			else
				index ++, last = hash;
			if (index >= max)
				index = 0;
		}
		else break;
	}
	if (last)
		last->next = index + 1;
	hash->crc = crc;
	hash->id  = id;
}

/* store item/block name into a hash for faster association between name and id */
void itemInitHash(void)
{
	int i;
	items.hashSize   = roundToUpperPrime(items.count + 256);
	items.hashIdSize = roundToUpperPrime(items.count);
	items.hashByName = calloc(sizeof *items.hashByName, items.hashSize + items.hashIdSize);
	items.hashById   = items.hashByName + items.hashSize;

	/* first: add block id */
	for (i = 0; i < 256; i ++)
		itemHashAdd(items.hashByName, items.hashSize, blockIds[i].tech, ID(i, 0));

	/* second: items */
	for (i = 0; i < items.count; i ++)
	{
		ItemDesc ref = items.table + i;
		if (ITEMMETA(ref->id) == 0 && ref->name)
			itemHashAdd(items.hashByName, items.hashSize, ref->tech, ref->id);

		TEXT id[16];
		sprintf(id, "%d", ref->id);
		itemHashAdd(items.hashById, items.hashIdSize, id, i);
	}
}

int itemGetCount(void)
{
	return items.count;
}

/* will return leftovers that don't fit the max stack count */
int itemAddCount(Item dest, int add)
{
	ItemDesc desc = itemGetById(dest->id);
	int      max  = desc ? desc->stack : 64;
	int      old  = dest->count;

	dest->count += add;
	if (dest->count > max)
		add = dest->count - max, dest->count = max;
	else
		add = 0;
	dest->added = dest->count - old;

	/* leftovers */
	return add;
}

/* get item id that creates the given block id */
ItemID_t itemCanCreateBlock(ItemID_t blockId, STRPTR * name)
{
	int i, id = blockId >> 4;
	for (i = 0; i < items.count; i ++)
	{
		ItemDesc desc = items.table + i;
		if (desc->refBlock == id)
		{
			if (name) *name = desc->name;
			return desc->id;
		}
	}
	return blockId;
}

float itemDurability(Item item)
{
	ItemDesc desc = itemGetById(item->id);

	if (desc)
	{
		int dura = desc->durability;
		if (item->uses > dura)
			return -1;

		return ((dura - item->uses) * 16 / dura) * 0.0625;
	}
	else return 1;
}

int itemMaxDurability(ItemID_t id)
{
	if (isBlockId(id))
		return -1;

	ItemDesc desc = itemGetById(id);

	if (desc == NULL || desc->durability == 0)
		return -1;

	return desc->durability;
}

ItemID_t itemGetByName(STRPTR name, Bool forInventory)
{
	if (name == NULL)
		return 0;
	if ('0' <= name[0] && name[0] <= '9')
	{
		/* older versions used numeric id directly */
		int id = atoi(name);
		/* back then, there was less than 256 block types */
		return id < 256 ? ID(id, 0) : ITEMID(id, 0);
	}

	if (strncasecmp(name, "minecraft:", 10) == 0)
		name += 10;

	ItemHash hash;
	uint32_t crc;
	STRPTR   data;
	int      val;

	name = STRDUPA(name);
	for (data = name; ; data ++)
	{
		uint8_t chr = *data;
		if (chr == ':') { *data++ = 0; break; }
		if (chr == 0) break;
		/* force lower case */
		if ('A' <= chr && chr <= 'Z')
			*data = chr - 'A' + 'a';
	}

	crc  = crc32(0, name, strlen(name));
	val  = *data ? atoi(data) : 0;
	hash = items.hashByName + crc % items.hashSize;

	for (;;)
	{
		if (hash->crc == crc)
		{
			/*
			 * some block id have a dedicated item id instead of reusing the block (and have the same tech name)
			 * block like cauldron, repeater, doors, ... besides, the block id have no inventory model.
			 */
			if (! isBlockId(hash->id) || blockIds[hash->id >> 4].inventory > 0 || ! forInventory)
				return hash->id | val;
		}
		if (hash->next > 0)
			hash = items.hashByName + hash->next - 1;
		else
			return 0;
	}
}

/* get technical name of a block that can be saved in NBT */
STRPTR itemGetTechName(ItemID_t itemId, STRPTR out, int max, Bool addMeta)
{
	STRPTR tech = NULL;
	int i = 0, meta = 0;
	CopyString(out, "minecraft:", max);
	if (! isBlockId(itemId))
	{
		if (addMeta)
			meta = ITEMMETA(itemId);
		else
			itemId &= ~(ITEMID_FLAG-1);
		ItemDesc desc = itemGetById(itemId);
		if (! desc)
			/* try without meta */
			desc = itemGetById(ITEMID(ITEMNUM(itemId), 0));

		if (desc) tech = desc->tech;
	}
	else
	{
		if (addMeta)
			meta = itemId & 15;
		else
			itemId &= ~15;
		tech = blockIds[itemId>>4].tech;
	}

	if (tech) i = StrCat(out, max, i, tech);
	else      i = StrCat(out, max, i, "unknown");

	if (meta > 0)
	{
		TEXT data[12];
		sprintf(data, ":%d", meta);
		StrCat(out, max, i, data);
	}
	return out;
}

ItemDesc itemGetById(ItemID_t id)
{
	ItemHash hash;
	uint32_t crc;
	TEXT     idstr[16];

	if (isBlockId(id))
		return NULL;

	sprintf(idstr, "%d", id);

	crc  = crc32(0, idstr, strlen(idstr));
	hash = items.hashById + crc % items.hashIdSize;

	for (;;)
	{
		if (hash->crc == crc)
			return items.table + hash->id;

		if (hash->next > 0)
			hash = items.hashById + hash->next - 1;
		else
			return NULL;
	}
}

/* register a callback when item is used by player */
Bool itemRegisterUse(STRPTR tech, UseItem_t cb)
{
	ItemID_t id = itemGetByName(tech, True);

	if (id > 0)
	{
		ItemDesc desc = itemGetById(id);
		desc->use = cb;
		return True;
	}
	return False;
}

Bool itemUse(ItemID_t id, vec4 pos, int pointToId)
{
	ItemDesc desc = itemGetById(id);
	if (desc && desc->use)
		return desc->use(id, pos, pointToId);
	return False;
}

ItemDesc itemGetByIndex(int i)
{
	return i < items.count ? items.table + i : NULL;
}

/* function will suppose buffer has enough space */
int itemGetInventoryByCat(Item buffer, int cat)
{
	BlockState state;
	Item item = buffer;

	/* scan block states */
	for (state = blockGetById(ID(1, 0)); state < blockLast; state ++)
	{
		uint8_t bcat = state->inventory & CATFLAGS;
		if (cat > 0 ? bcat != cat : bcat == 0 || bcat == FILLBY)
			continue;

		if (buffer)
		{
			item->id = state->id;
			item->count = 1;
			item->uses = 0;
		}
		item ++;
	}
	/* also include items */
	int i;
	for (i = 0; i < items.count; i ++)
	{
		ItemDesc desc = items.table + i;
		if (cat > 0 ? desc->category == cat : desc->name != NULL)
		{
			if (buffer)
			{
				item->id = desc->id;
				item->count = 1;
				item->uses = 0;
			}
			item ++;
		}
	}

	return item - buffer;
}

ItemID_t itemHasModel(Item item)
{
	ItemID_t ret = item->id;
	if (item->id > 0)
	{
		if (isBlockId(item->id))
		{
			BlockState state = blockGetById(item->id);
			if (state->inventory == 0)
				return itemCanCreateBlock(item->id, NULL);
		}
	}
	return ret;
}

/*
 * generate a mesh from an item suitable for entity shader.
 * note: <vertex> can be NULL to first get number of vertices to alloc.
 *
 * model will be oriented in the XZ plane, facing top.
 */

#define QUAD_VERTEX      (INT_PER_VERTEX * 6)

static void itemGenQuad(DATA16 out, int x1, int z1, int x2, int z2, int norm, DATA8 texUV)
{
	static uint8_t texCoords[] = { /* S, E, N, W, T, B */
		0,0,    0,0,    1,0,    1,0,
		0,1,    0,1,    0,0,    0,0,
		1,0,    1,0,    0,0,    0,0,
		0,0,    0,0,    0,1,    0,1,
		0,0,    0,1,    1,1,    1,0,
		0,0,    0,1,    1,1,    1,0,
	};
	DATA8 index, tex;
	int   i, U1, V1, U2, V2;
	/* XXX U,V will only work if blockTexResol is 16 */
	U1 = texUV[0] * 16 + x1;
	V1 = texUV[1] * 16 + z1;
	U2 = U1 + x2 - x1;
	V2 = V1 + z2 - z1;
	if (norm == SIDE_SOUTH) V1 --;
	if (norm == SIDE_EAST)  U1 --;
	for (i = 0, index = cubeIndices + norm * 4, norm <<= 3, tex = texCoords + norm; i < 4; i ++, out += INT_PER_VERTEX, index ++, tex += 2)
	{
		DATA8 point = cubeVertex + index[0];
		out[VX] = ((point[VX] ? x2 : x1) * BASEVTX) / blockTexResol + ORIGINVTX;
		out[VZ] = ((point[VZ] ? z2 : z1) * BASEVTX) / blockTexResol + ORIGINVTX;
		out[VY] = (point[VY]  ? BASEVTX/24 : -BASEVTX/24) + ORIGINVTX;
		{
			int V = tex[1] ? V2 : V1;
			out[3] = (tex[0] ? U2 : U1) | ((V & ~7) << 6);
			out[4] = (V & 7) | norm;
		}
		//fprintf(stderr, "%c: %g, %g, %g - %d, %d\n", "SENWTB"[norm>>3], (out[VX] - ORIGINVTX) * (1./BASEVTX), (out[VY] - ORIGINVTX) * (1./BASEVTX),
		//	(out[VZ] - ORIGINVTX) * (1./BASEVTX), GET_UCOORD(out), GET_VCOORD(out));
	}
	/* convert to triangles */
	memcpy(out,   out - 20, BYTES_PER_VERTEX);
	memcpy(out+5, out - 10, BYTES_PER_VERTEX);
}

int itemGenMesh(ItemID_t blockId, DATA16 out)
{
	uint8_t texUV[2];
	DATA16  vertex = out;
	DATA8   bitmap = alloca(blockTexResol * blockTexResol);
	int     count  = 6;

	/* used by entities: a block that normally render as normal, we want it in item form */
	if ((blockId & ITEMID_FLAG) && ITEMNUM(blockId) < 256)
		blockId &= 0xffff;

	if (isBlockId(blockId))
	{
		BlockState state = blockGetById(blockId);
		texUV[0] = state->nzU;
		texUV[1] = state->nzV;
	}
	else
	{
		ItemDesc item = itemGetById(blockId);
		if (item)
		{
			texUV[0] = item->texU + ITEM_ADDTEXU;
			texUV[1] = item->texV + ITEM_ADDTEXV;
		}
		else return 0;
	}

	if (blockGetAlphaTex(bitmap, texUV[0], texUV[1]))
	{
		uint8_t i, j, max, rect[4];
		DATA8   src;

		//fprintf(stderr, "================+\n");
		//for (i = 0, src = bitmap; i < blockTexResol; i ++, fputs("|\n", stderr))
		//	for (j = 0; j < blockTexResol; j ++, src ++)
		//		putc(src[0] ? '#' : ' ', stderr);

		rect[0] = rect[1] = 255;
		rect[2] = rect[3] = 0;

		/* horizontal spans (ie: N/S bands) */
		for (i = 0, src = bitmap, max = blockTexResol-1; i < blockTexResol; src += blockTexResol, i ++)
		{
			uint8_t minN, maxN;
			uint8_t minS, maxS;
			DATA8   s;
			for (minN = minS = 255, maxN = maxS = 0, s = src, j = 0; j < blockTexResol; j ++, s ++)
			{
				if (s[0] == 0) continue;
				if (i == 0 || s[-blockTexResol] == 0) {
					if (minN > j) minN = j;
					if (maxN < j) maxN = j;
				}
				if (i == max || s[blockTexResol] == 0) {
					if (minS > j) minS = j;
					if (maxS < j) maxS = j;
				}
			}
			if (minN <= maxN)
			{
				maxN ++;
				if (rect[0] > minN) rect[0] = minN;
				if (rect[2] < maxN) rect[2] = maxN;
				if (vertex)
					itemGenQuad(vertex, minN, i, maxN, i, SIDE_NORTH, texUV), vertex += QUAD_VERTEX;
				count += 6;
			}
			if (minS <= maxS)
			{
				maxS ++;
				if (rect[0] > minS) rect[0] = minS;
				if (rect[2] < maxS) rect[2] = maxS;
				if (vertex)
					itemGenQuad(vertex, minS, i+1, maxS, i+1, SIDE_SOUTH, texUV), vertex += QUAD_VERTEX;
				count += 6;
			}
		}

		/* vertical spans (ie: E/W bands) */
		for (i = 0, src = bitmap, max = blockTexResol-1; i < blockTexResol; src ++, i ++)
		{
			uint8_t minW, maxW;
			uint8_t minE, maxE;
			DATA8   s;
			for (minE = minW = 255, maxE = maxW = 0, s = src, j = 0; j < blockTexResol; j ++, s += blockTexResol)
			{
				if (s[0] == 0) continue;
				if (i == 0 || s[-1] == 0) {
					if (minW > j) minW = j;
					if (maxW < j) maxW = j;
				}
				if (i == max || s[1] == 0) {
					if (minE > j) minE = j;
					if (maxE < j) maxE = j;
				}
			}
			if (minW <= maxW)
			{
				maxW ++;
				if (rect[1] > minW) rect[1] = minW;
				if (rect[3] < maxW) rect[3] = maxW;
				if (vertex)
					itemGenQuad(vertex, i, minW, i, maxW, SIDE_WEST, texUV), vertex += QUAD_VERTEX;
				count += 6;
			}
			if (minE <= maxE)
			{
				maxE ++;
				if (rect[1] > minE) rect[1] = minE;
				if (rect[3] < maxE) rect[3] = maxE;
				if (vertex)
					itemGenQuad(vertex, i+1, minE, i+1, maxE, SIDE_EAST, texUV), vertex += QUAD_VERTEX;
				count += 6;
			}
		}
		/* top and bottom quad */
		if (vertex)
		{
			itemGenQuad(vertex, rect[0], rect[1], rect[2], rect[3], SIDE_TOP, texUV);    vertex += QUAD_VERTEX;
			itemGenQuad(vertex, rect[0], rect[1], rect[2], rect[3], SIDE_BOTTOM, texUV); vertex += QUAD_VERTEX;

			/* need to shift vertex by -rect[0], -rect[1] */
			uint16_t dx = (rect[0] * BASEVTX) / blockTexResol;
			uint16_t dz = (rect[1] * BASEVTX) / blockTexResol;
			int nb;
			for (vertex = out, nb = count; nb > 0; nb --, vertex += INT_PER_VERTEX)
				vertex[VX] -= dx, vertex[VZ] -= dz;
		}
	}
	return count;
}

/* Q'n'D enchantment decoder */
struct Enchant_t
{
	int    id;
	STRPTR name;
	int    max;
};

static struct Enchant_t enchantments[] = {
	{0, "Protection", 4},
	{1, "Fire Protection", 4},
	{2, "Feather Falling", 4},
	{3, "Blast Protection", 4},
	{4, "Blast Protection", 4},
	{5, "Respiration", 3},
	{6, "Aqua Affinity", 1},
	{7, "Thorns", 3},
	{8, "Depth Strider", 3},
	{9, "Frost Walker", 2},
	{10, "Curse of Binding", 1},
	{16, "Sharpness", 5},
	{17, "Smite", 5},
	{18, "Bane of Arthropods", 5},
	{19, "Knockback", 2},
	{20, "Fire Aspect", 2},
	{21, "Looting", 3},
	{22, "Sweeping Edge", 3},
	{32, "Efficiency", 5},
	{33, "Silk Touch", 1},
	{34, "Unbreaking", 3},
	{35, "Fortune", 3},
	{48, "Power", 5},
	{49, "Punch", 2},
	{50, "Flame", 1},
	{51, "Infinity", 1},
	{61, "Luck of the Sea", 3},
	{62, "Lure", 3},
	{65, "Loyalty", 3},
	{66, "Impaling", 5},
	{67, "Riptide", 3},
	{68, "Channeling", 1},
	{70, "Mending", 1},
	{71, "Curse of Vanishing", 1},
};

void itemDecodeEnchants(DATA8 nbt, STRPTR title, int max)
{
	int       tag  = 0, off;
	NBTFile_t file = {.mem = nbt};
	NBTIter_t iter;

	NBT_InitIter(&file, 0, &iter);

	while ((off = NBT_Iter(&iter)) >= 0)
	{
		int id = 0, level = 0, off2;
		NBTIter_t sub;
		NBT_InitIter(&file, off, &sub);
		while ((off2 = NBT_Iter(&sub)) >= 0)
		{
			switch (FindInList("lvl,id", sub.name, 0)) {
			case 0: level = NBT_GetInt(&file, off2, 0); break;
			case 1: id    = NBT_GetInt(&file, off2, 0);
			}
		}
		if (id > 0 && level > 0)
		{
			int pos;
			for (pos = 0; pos < DIM(enchantments); pos ++)
			{
				static STRPTR roman[] = {" I", " II", " III", " IV", " V"};
				if (enchantments[pos].id != id) continue;
				id = StrCat(title, max, 0, "<br>");
				if (tag == 0) id = StrCat(title, max, id, "<ench>"), tag ++;
				id = StrCat(title, max, id, enchantments[pos].name);
				if (level > 5)
				{
					/* using hacking tools, are we? */
					TEXT num[10];
					sprintf(num, "%d", level);
					StrCat(title, max, id, num);
				}
				else if (level > 0)
				{
					StrCat(title, max, id, roman[level-1]);
				}
				break;
			}
		}
	}
	if (tag == 1)
		StrCat(title, max, 0, "</ench>");
}
