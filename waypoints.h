/*
 * waypoints.h : public and private interface for managing map markers.
 *
 * written by T.Pierron, nov 2021
 */

#ifndef WAY_POINTS_H
#define WAY_POINTS_H

void wayPointsRead(void);
void wayPointsEdit(vec4 pos, float rotation[2]);
Bool wayPointsInit(void);
void wayPointsRender(vec4 camera);
int  wayPointRaypick(vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos);
void wayPointInfo(int id, STRPTR msg, int max);

#ifdef WAY_POINTS_IMPL

typedef struct WayPoint_t *         WayPoint;

struct WayPointsPrivate_t
{
	SIT_Widget list;
	SIT_Widget delButton;
	SIT_Widget coords[3];
	vector_t   all;                /* WayPoint_t */
	NBTFile_t  nbt;
	uint8_t    nbtModified;        /* NBT waypoints file need to be saved */
	uint8_t    listDirty;          /* need to update GL VBO */
	uint8_t    glCount;            /* nb. of waypoints actually rendered (max: 255) */
	int        displayInWorld;
	int        nbtWaypoints;       /* WayPoints branch offset in NBT */
	float      curPos[3];          /* goto interface */
	float      rotation[2];
	int        vao, vbo, shader;   /* GL stuff */
	float      lastPos[3];         /* reduce VBO sorting */
	int        lastHover;          /* waypoint id */
};

struct WayPoint_t
{
	TEXT    name[64];
	float   location[3];
	float   rotation[2];
	uint8_t color[4];
	int     glIndex;
};

#define WAYPOINTS_BEAM_SZ          0.5f
#define WAYPOINTS_VBO_SIZE         20
#define WAYPOINTS_MAX              255   /* note: in visible range */

#endif
#endif
