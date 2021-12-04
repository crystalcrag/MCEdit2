/*
 * items.h: public function to deal with MC items
 *
 * Written by T.Pierron, oct 2020
 */

#ifndef MCITEMS_H
#define MCITEMS_H

#include "utils.h"
#include "blocks.h"

/*
 * item ids uses 16bits for state and up to 13bis for id: filled_maps requires a lot of states, as well
 * as potions. 16 states like blocks is not enough. For more extended information use Item_t struct.
 */
#define ITEMID(id, data)       (((id-256) << 17) | ITEMID_FLAG | (data))
#define ITEMMETA(id)           (id & (ITEMID_FLAG-1))
#define ITEMNUM(id)            (((id) >> 17) + 256)
#define ITEM_ADDTEXU           16
#define ITEM_ADDTEXV           32

typedef struct Item_t *        Item;
typedef struct Item_t          ItemBuf;
typedef struct ItemDesc_t *    ItemDesc;

ItemDesc itemGetById(ItemID_t id);
ItemDesc itemGetByIndex(int i);
Bool     itemCreate(const char * file, STRPTR * keys, int line);
void     itemInitHash(void);
int      itemGetCount(void);
ItemID_t itemGetByName(STRPTR name, Bool forInventory);
int      itemAddCount(Item, int add);
int      itemMaxDurability(ItemID_t);
float    itemDurability(Item);
ItemID_t itemHasModel(Item);
STRPTR   itemGetTechName(ItemID_t id, STRPTR out, int max, Bool addMeta);
void     itemDecodeEnchants(DATA8 nbt, STRPTR title, int max);
int      itemGetInventoryByCat(Item buffer, int cat);
ItemID_t itemCanCreateBlock(ItemID_t blockId, STRPTR * name);
int      itemGenMesh(ItemID_t blockId, DATA16 vertex);

struct Item_t                  /* for rendering */
{
	ItemID_t id;               /* item or block id + metadata */
	uint16_t uses;             /* display durability bar */
	uint16_t count;            /* stack count */
	uint16_t x, y;             /* position on screen (relative to bottom left) */
	uint8_t  slot;             /* used to manage items dragged over inventory */
	uint8_t  added;            /* items temporarily added to count (drag item) */
	DATA8    extra;            /* NBT fragment (tile entity) */
};

struct ItemDesc_t              /* from itemTable.js */
{
	ItemID_t id;               /* item id */
	STRPTR   name;             /* human readable name */
	uint8_t  stack;            /* max items in a stack */
	uint8_t  category;         /* for creative inventory */
	uint8_t  texU;             /* texture location in items.png */
	uint8_t  texV;
	uint16_t next;
	uint16_t durability;
	uint16_t refBlock;         /* blockId this item is for */
	uint16_t glInvId;          /* vbo slot for inventory */
	STRPTR   tech;             /* technical name */
	STRPTR   tile;
};

#ifdef ITEMS_IMPL
typedef struct ItemHash_t *    ItemHash;

struct ItemHash_t              /* easy access of items by name */
{
	uint32_t crc;
	uint32_t next;
	ItemID_t id;
};


struct ItemsState_t            /* global structure */
{
	ItemDesc table;
	ItemHash hashByName;
	ItemHash hashById;
	int      count;
	int      hashSize;
	int      hashIdSize;
};

#endif

#endif
