/*
 * items.h: public function to deal with MC items
 *
 * Written by T.Pierron, oct 2020
 */

#ifndef MCITEMS_H
#define MCITEMS_H

#include "UtilityLibLite.h"

#define ITEMID(id, data)     ((id<<4)|data)
#define ITEMMETA(id)         (id & 15)
#define ITEM_ADDTEXU         16
#define ITEM_ADDTEXV         32
#define ITEM_INVALID_ID      ITEMID(451, 0)

typedef struct Item_t *         Item;
typedef struct Item_t           ItemBuf;
typedef struct ItemDesc_t *     ItemDesc;

ItemDesc itemGetById(int id);
ItemDesc itemGetByIndex(int i);
Bool     itemCreate(const char * file, STRPTR * keys, int line);
void     itemInitHash(void);
int      itemGetCount(void);
int      itemGetByName(STRPTR name, Bool forInventory);
int      itemAddCount(Item, int add);
int      itemMaxDurability(Item);
float    itemDurability(Item);
STRPTR   itemGetTechName(int id, STRPTR out, int max);
void     itemDecodeEnchants(DATA8 nbt, STRPTR title, int max);
int      itemGetInventoryByCat(Item buffer, int cat);

struct Item_t              /* for rendering */
{
	uint16_t id;           /* item or block id + metadata */
	uint16_t uses;         /* display durability bar */
	uint16_t count;        /* stack count */
	uint16_t x, y;         /* position on screen (relative to bottom left) */
	uint8_t  slot;         /* used to manage items dragged over inventory */
	uint8_t  added;        /* items temporarily added to count (drag item) */
	DATA8    extra;        /* NBT fragment */
};

struct ItemDesc_t          /* from itemTable.js */
{
	int      id;           /* item id (>256) */
	STRPTR   name;         /* human readable name */
	uint8_t  stack;        /* max items in a stack */
	uint8_t  category;     /* for creative inventory */
	uint8_t  texU;         /* texture location in items.png */
	uint8_t  texV;
	uint16_t next;
	uint16_t durability;
	uint16_t refBlock;     /* blockId+meta this item is for */
	STRPTR   tech;         /* technical name */
	STRPTR   tile;
	uint16_t glInvId;      /* vbo slot for inventory */
	uint16_t altStates;
};

#ifdef ITEMS_IMPL
typedef struct ItemHash_t *    ItemHash;

struct ItemHash_t          /* easy access of items by name */
{
	uint32_t crc;
	uint16_t next;
	uint16_t id;
};


struct ItemsState_t        /* global structure */
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
