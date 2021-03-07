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

	item.id = atoi(value);
	item.name = stringAddPool(jsonValue(keys, "name"), 0);
	if (item.id < 256)
	{
		SIT_Log(SIT_ERROR, "%s: invalid item id %d on line %d\n", file, item.id, line);
		return False;
	}
	value = jsonValue(keys, "state");
	item.id <<= 4;
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
		item.refBlock = atoi(value);

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

	/* everything seems, alloc item */
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

static void itemHashAdd(ItemHash table, int max, STRPTR name, int id)
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
		itemHashAdd(items.hashByName, items.hashSize, blockIds[i].tech, i);

	/* second: items */
	for (i = 0; i < items.count; i ++)
	{
		ItemDesc ref = items.table + i;
		if ((ref->id & 15) == 0 && ref->name)
			itemHashAdd(items.hashByName, items.hashSize, ref->tech, ref->id >> 4);

		TEXT id[16];
		sprintf(id, "%d", ref->id);
//		fprintf(stderr, "item %s: %s\n", ref->name, id);
		itemHashAdd(items.hashById, items.hashIdSize, id, i);
	}
}

int itemGetCount(void)
{
	return items.count;
}

int itemAddCount(Item dest, int add)
{
	ItemDesc desc = itemGetById(dest->id);
	int      max  = desc ? desc->stack : 64;

	dest->count += add;
	if (dest->count > max)
		add = dest->count - max, dest->count = max;
	else
		add = 0;

	/* leftovers */
	return add;
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

int itemMaxDurability(Item item)
{
	if (item->id < ID(256,0))
		return -1;

	ItemDesc desc = itemGetById(item->id);

	if (desc == NULL || desc->durability == 0)
		return -1;

	return desc->durability;
}

int itemGetByName(STRPTR name, Bool forInventory)
{
	if ('0' <= name[0] && name[0] <= '9')
	{
		/* older versions used numeric id directly */
		return atoi(name) << 4;
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
			if (hash->id >= 256 || blockIds[hash->id].inventory > 0)
				return (hash->id << 4) | val;
		}
		if (hash->next > 0)
			hash = items.hashByName + hash->next - 1;
		else
			return -1;
	}
}

/* note: out must be NULL terminated before calling this function */
STRPTR itemGetTechName(int itemId, STRPTR out, int max)
{
	STRPTR tech = NULL;
	int i = StrCat(out, max, 0, "minecraft:");
	if (itemId >= ID(256,0))
	{
		ItemDesc desc = itemGetById(itemId & ~15);

		if (desc) tech = desc->tech;
	}
	else
	{
		BlockState b = blockGetById(itemId);

		if (b && b->inventory == BBOX_NONE) itemId &= ~15;
		tech = blockIds[itemId>>4].tech;
	}

	if (tech) i = StrCat(out, max, i, tech);
	else      i = StrCat(out, max, i, "unknown");

	if (itemId & 15)
	{
		TEXT data[4];
		sprintf(data, ":%d", itemId & 15);
		StrCat(out, max, i, data);
	}
	return out;
}

ItemDesc itemGetById(int id)
{
	ItemHash hash;
	uint32_t crc;
	TEXT     idstr[6];

	if (id < ID(256, 0))
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
	for (state = blockGetByIdData(1, 0); state->id < ID(255,0); state ++)
	{
		uint8_t bcat = state->inventory & CATFLAGS;
		if (cat > 0 ? bcat != cat : bcat == 0)
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
			case 0: level = NBT_ToInt(&file, off2, 0); break;
			case 1: id    = NBT_ToInt(&file, off2, 0);
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
