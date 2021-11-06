/*
 * waypoints.h : public and private interface for managing map markers.
 *
 * written by T.Pierron, nov 2021
 */

#ifndef WAY_POINTS_H
#define WAY_POINTS_H

void wayPointsRead(void);
void wayPointsEdit(vec4 pos, float rotation[2]);


#ifdef WAY_POINTS_IMPL

typedef struct WayPoint_t *         WayPoint;

struct WayPointsPrivate_t
{
	SIT_Widget list;
	SIT_Widget delButton;
	SIT_Widget coords[3];
	vector_t   all;                 /* WayPoint_t */
	NBTFile_t  nbt;
	uint8_t    nbtModified;
	uint8_t    displayInWorld;
	int        nbtWaypoints;        /* WayPoints branch offset in NBT */
	float      curPos[3];           /* goto interface */
	float      rotation[2];
};

struct WayPoint_t
{
	TEXT    name[64];
	float   location[3];
	float   rotation[2];
	uint8_t color[4];
};

#endif
#endif
