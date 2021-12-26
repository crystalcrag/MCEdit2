/*
 * minecarts.h: public function to handle different types of minecarts
 *
 * written by T.Pierron, dec 2021
 */

#ifndef MC_MINECARTS_H
#define MC_MINECARTS_H

Bool minecartTryUsing(ItemID_t itemId, vec4 pos, int pointToBlock);
int  minecartParse(NBTFile file, Entity entity);


enum
{
	RAILS_NS,
	RAILS_EW,
	RAILS_ASCE,
	RAILS_ASCW,
	RAILS_ASCN,
	RAILS_ASCS,
	RAILS_CURVED_SE,
	RAILS_CURVED_SW,
	RAILS_CURBED_NW,
	RAILS_CURSET_NE,
};

#endif