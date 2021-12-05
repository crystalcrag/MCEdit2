/*
 * waypoints.c : handle user-placed map markers (read from disk and render in world), as well as user
 *               interface to manage them.
 *
 * markers are stored <world_folder>/mcedit_waypoints.dat, see https://gist.github.com/Podshot/c30d9b980cde4e7485d6
 * for format of this file.
 *
 * written by T.Pierron, Nov 2021
 */

#define WAY_POINTS_IMPL
#include <glad.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <malloc.h>
#include "render.h"
#include "SIT.h"
#include "waypoints.h"
#include "MCEdit.h"
#include "globals.h"
#include "NBT2.h"
#include "nanovg.h"
#include "extra.h"
#include "ColorChooser.c"


struct WayPointsPrivate_t waypoints;

/*
 * manage NBT file
 */

static void wayPointRandColor(uint8_t colors[4])
{
	uint16_t hsv[3];
	hsv[0] = RandRange(0, 360);
	hsv[1] = 50 + RandRange(0, 50);
	hsv[2] = 50 + RandRange(0, 50);
	HSV_to_RGB(hsv, colors);
	colors[3] = 255;
}

/* map must be opened in globals.level before using this function */
void wayPointsRead(void)
{
	TEXT path[128];

	CopyString(path, globals.level->path, sizeof path);
	AddPart(path, "../mcedit_waypoints.dat", sizeof path);
	vector_init(waypoints.all, sizeof (struct WayPoint_t));

	NBT_Parse(&waypoints.nbt, path);

	/* check if there is any way points defined yed */
	waypoints.nbtWaypoints = NBT_FindNode(&waypoints.nbt, 0, "Waypoints");

	// NBT_Dump(&waypoints.nbt, 0, 0, 0);

	if (waypoints.nbtWaypoints >= 0)
	{
		NBTIter_t iter;
		int       offset;

		NBT_InitIter(&waypoints.nbt, waypoints.nbtWaypoints, &iter);

		while ((offset = NBT_Iter(&iter)) >= 0)
		{
			NBTIter_t waypoint;
			WayPoint  wp;
			int       i;
			wp = vector_nth(&waypoints.all, waypoints.all.count);
			waypoints.listDirty = 1;
			memset(wp, 0, sizeof *wp);
			/* might not be defined */
			wayPointRandColor(wp->color);
			NBT_IterCompound(&waypoint, waypoints.nbt.mem + offset);
			while ((i = NBT_Iter(&waypoint)) >= 0)
			{
				DATA8 mem = NBT_Payload(&waypoints.nbt, offset + i);
				switch (FindInList("Name,Coordinates,Rotation,Color", waypoint.name, 0)) {
				case 0: CopyString(wp->name, mem, sizeof wp->name); break;
				case 1: memcpy(wp->location, mem, sizeof wp->location); break;
				case 2: memcpy(wp->rotation, mem, sizeof wp->rotation); break;
				case 3: memcpy(wp->color,    mem, sizeof wp->color);
				}
			}
		}
	}
	waypoints.displayInWorld = NBT_GetInt(&waypoints.nbt, NBT_FindNode(&waypoints.nbt, 0, "DisplayInWorld"), 0 /* default value */);
}

/* get origin of waypoint */
void wayPointGetPos(int id, float ret[3])
{
	if (0 < id && id <= waypoints.all.count)
	{
		WayPoint wp = vector_nth(&waypoints.all, id-1);
		memcpy(ret, wp->location, sizeof wp->location);
	}
	else memset(ret, 0, 12);
}

static void wayPointsAddToList(WayPoint);

static int wayPointsAdd(SIT_Widget w, APTR cd, APTR ud)
{
	NBTFile_t nbt = {.max = 256};
	uint8_t   colors[4];

	nbt.mem = alloca(256);

	NBT_Add(&nbt,
		TAG_String,     "Name", "",
		TAG_Int,        "Dimension", 0,
		TAG_List_Float, "Coordinates", 3 | NBT_WithInit, waypoints.curPos,
		TAG_List_Float, "Rotation", 2 | NBT_WithInit, waypoints.rotation,
		TAG_List_Byte,  "Color", 4 | NBT_WithInit, colors,
		TAG_Compound_End
	);

	if (waypoints.nbtWaypoints < 0)
	{
		NBT_Add(&waypoints.nbt,
			TAG_Compound, "",
				TAG_Int,           "DisplayInWorld", waypoints.displayInWorld,
				TAG_List_Compound, "Waypoints", 0,
			TAG_Compound_End
		);
		waypoints.nbtWaypoints = NBT_FindNode(&waypoints.nbt, 0, "Waypoints");
	}

	NBT_Insert(&waypoints.nbt, "Waypoints", TAG_List_Compound | TAG_InsertAtEnd, &nbt);

	//NBT_Dump(&waypoints.nbt, 0, 0, 0);

	WayPoint wp = vector_nth(&waypoints.all, waypoints.all.count);
	memcpy(wp->location, waypoints.curPos, sizeof wp->location);
	memcpy(wp->rotation, waypoints.rotation, sizeof waypoints.rotation);
	memcpy(wp->color, colors, sizeof wp->color);
	wp->name[0] = 0;
	wp->glIndex = -1;
	waypoints.nbtModified = True;
	waypoints.listDirty = 1;

	if (waypoints.all.count > 1)
	{
		wayPointsAddToList(wp);
		SIT_SetValues(waypoints.list, SIT_SelectedIndex, waypoints.all.count - 1, NULL);
		SIT_SetValues(w, SIT_Enabled, False, NULL);
	}
	else
	{
		SIT_CloseDialog(w);
		SIT_Exit(1);
	}
	return 1;
}

static int wayPointsGetNth(int nth)
{
	NBTIter_t iter;
	NBT_InitIter(&waypoints.nbt, waypoints.nbtWaypoints, &iter);
	int offset = -1;
	while ((offset = NBT_Iter(&iter)) >= 0 && nth > 0)
		nth --;
	return offset;
}

static void vector_del(vector v, int nth, int count)
{
	int   size = v->itemsize;
	DATA8 mem = v->buffer + nth * size;
	memmove(mem, mem + count * size, (v->count - nth - count) * size);
	v->count -= count;
}

static int wayPointsDel(SIT_Widget w, APTR cd, APTR ud)
{
	int nth;
	SIT_GetValues(waypoints.list, SIT_SelectedIndex, &nth, NULL);
	if (nth >= 0 && NBT_Delete(&waypoints.nbt, waypoints.nbtWaypoints, nth))
	{
		waypoints.nbtModified = True;
		waypoints.listDirty = 1;
		vector_del(&waypoints.all, nth, 1);
		SIT_ListDeleteRow(waypoints.list, nth);
	}
	return 1;
}


/*
 * user interface to handle way points
 */

static void wayPointsAddToList(WayPoint wp)
{
	TEXT coord[64];
	TEXT dist[16];
	vec4 dir;

	/* show distance from camera */
	vecSub(dir, waypoints.curPos, wp->location);
	dir[VT] = vecLength(dir);
	if (dir[VT] < 10)       strcpy(dist, "Nearby");
	else if (dir[VT] < 500) sprintf(dist, "%dm", (int) dir[VT]);
	else                    sprintf(dist, "%.1fkm", round((double) dir[VT] / 1000));

	snprintf(coord, sizeof coord, "%d, %d, %d", (int) wp->location[VX], (int) wp->location[VY], (int) wp->location[VZ]);
	SIT_ListInsertItem(waypoints.list, -1, NULL, "", wp->name[0] ? wp->name : "Unnamed", coord, dist);
}

/* teleport to selected location in <goto> popup */
static int wayPointsGoto(SIT_Widget w, APTR cd, APTR ud)
{
	/* ud is player vec4 position: don't modify until user confirms its choice */
	memcpy(waypoints.playerPos, waypoints.curPos, sizeof waypoints.curPos);
	memcpy(waypoints.playerRotation, waypoints.rotation, sizeof waypoints.rotation);
	SIT_CloseDialog(w);
	SIT_Exit(1);
	return 1;
}

/* SITE_OnPaint on listbox cells */
static int wayPointsPaintCell(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnCellPaint * ocp = cd;
	NVGcontext * vg;
	WayPoint wp = vector_nth(&waypoints.all, ocp->rowColumn >> 8);

	switch (ocp->rowColumn & 0xff) {
	case 0: /* way point color */
		SIT_GetValues(w, SIT_NVGcontext, &vg, NULL);
		nvgFillColor(vg, nvgRGB(wp->color[0], wp->color[1], wp->color[2]));
		nvgBeginPath(vg);
		float max = fminf(ocp->LTWH[2], ocp->LTWH[3]) - 4;
		nvgRect(vg, ocp->LTWH[0] + (ocp->LTWH[2] - max) * 0.5f, ocp->LTWH[1] + 2, max, max);
		nvgFill(vg);
		return 1;
	case 1: /* name - slightly dimmer if unset */
		if (wp->name[0] == 0)
			memcpy(ocp->fgColor, "\xcc\xcc\xcc\xff", 4);
	}
	return 0;
}

/* ColorChooser confirm callback */
static int wayPointsSetColor(SIT_Widget w, APTR cd, APTR ud)
{
	WayPoint wp = ud;
	memcpy(wp->color, cd, 4);

	int offset = wayPointsGetNth(wp - (WayPoint) waypoints.all.buffer);
	if (offset > 0 && NBT_AddOrUpdateKey(&waypoints.nbt, "Color", TAG_List_Byte | TAG_ListSize(4), wp->color, offset) > 0)
	{
		waypoints.nbtModified = True;
		if (wp->glIndex >= 0)
		{
			/* update GL buffers if this waypoint is displayed */
			glBindBuffer(GL_ARRAY_BUFFER, waypoints.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, wp->glIndex * WAYPOINTS_VBO_SIZE + 12, 3, wp->color);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
	}
	return 1;
}

/* SITE_OnBlur */
static int wayPointsFinishEdit(SIT_Widget w, APTR cd, APTR ud)
{
	if (! waypoints.cancelEdit)
	{
		WayPoint wp = ud;
		STRPTR   name;
		APTR     type;
		int      offset;

		SIT_GetValues(w, SIT_Title, &name, SIT_UserData, &type, NULL);
		offset = wayPointsGetNth(wp - (WayPoint) waypoints.all.buffer);

		/* modify in-memory list */
		if (type == (APTR) 2)
		{
			if (name[0] == 0)
			{
				memcpy(wp->location, waypoints.playerPos, 12);
				memcpy(wp->rotation, waypoints.playerRotation, 8);
			}
			else
			{
				/* try to get 3 numbers */
				int i;
				for (i = 0; i < 3; i ++)
				{
					STRPTR end;
					float val = strtof(name, &end);
					if (end > name) wp->location[i] = val;
					else break;
					if (*name == ',') name ++;
				}
			}
			/* modify listbox */
			TEXT coord[64];
			snprintf(coord, sizeof coord, "%d, %d, %d", (int) wp->location[VX], (int) wp->location[VY], (int) wp->location[VZ]);
			SIT_ListSetCell(waypoints.list, wp - (WayPoint) waypoints.all.buffer, 2, DontChangePtr, DontChange, coord);

			/* modify NBT */
			if (offset > 0)
			{
				if (NBT_AddOrUpdateKey(&waypoints.nbt, "Rotation",    TAG_List_Float | TAG_ListSize(8),  wp->rotation, offset) > 0 &&
				    NBT_AddOrUpdateKey(&waypoints.nbt, "Coordinates", TAG_List_Float | TAG_ListSize(12), wp->location, offset) > 0)
					waypoints.nbtModified = True;
			}
		}
		else
		{
			CopyString(wp->name, name, sizeof wp->name);
			/* modify listbox */
			SIT_ListSetCell(waypoints.list, wp - (WayPoint) waypoints.all.buffer, 1, DontChangePtr, DontChange, wp->name);

			/* modify NBT */
			if (offset > 0 && NBT_AddOrUpdateKey(&waypoints.nbt, "Name", TAG_String, name, offset) > 0)
				waypoints.nbtModified = True;
		}

		// NBT_Dump(&waypoints.nbt, 0, 0, 0);

		/* don't go here until a new edit box has been allocated */
		waypoints.cancelEdit = 1;
	}
	SIT_RemoveWidget(w);
	return 1;
}

/* SITE_OnRawKey handler from temporary editbox */
static int wayPointsAcceptEdit(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;
	if (msg->keycode == SITK_Return)
	{
		waypoints.cancelEdit = 0;
		wayPointsFinishEdit(w, NULL, ud);
		return 1;
	}
	else if (msg->keycode == SITK_Escape)
	{
		/* remove widgets will cause an OnBlur event */
		waypoints.cancelEdit = 1;
		SIT_RemoveWidget(w);
		return 1;
	}
	return 0;
}

/* handle right-click on waypoints list */
static int wayPointsClick(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	float rect[4];
	int   click;
	if (msg->state == SITOM_ButtonPressed && msg->button == SITOM_ButtonRight)
	{
		click = SIT_ListGetItemOver(w, rect, msg->x, msg->y, w);
		if (click >= 0)
		{
			WayPoint wp = vector_nth(&waypoints.all, click >> 8);
			TEXT     coord[64];
			int      padding[4];
			float    left, top;

			click &= 0xff;
			switch (click) {
			case 0: /* color value: show a color chooser */
				w = CCOpen(w, wp->color, wayPointsSetColor, wp, 50 - (int) SIT_EmToReal(w, SITV_Em(1)));
				SIT_SetValues(w,
					SIT_Left, SITV_AttachForm, NULL, (int) rect[0] - 50,
					SIT_Top,  SITV_AttachForm, NULL, (int) rect[3] + 5,
					NULL
				);
				SIT_ManageWidget(w);
				break;
			case 2: /* location: in-place edit box */
				snprintf(coord, sizeof coord, "%d, %d, %d", (int) wp->location[VX], (int) wp->location[VY], (int) wp->location[VZ]);
				// no break;
			case 1: /* name: same */
				SIT_GetValues(w, SIT_Parent, &w, NULL);
				SIT_GetValues(w, SIT_X, &left, SIT_Y, &top, SIT_Padding, padding, NULL);
				waypoints.cancelEdit = 0;
				w = SIT_CreateWidget("editname", SIT_EDITBOX, w,
					/* cannot edit wp->name directly: we want this to be cancellable */
					SIT_Title,      click == 2 ? coord : wp->name,
					SIT_EditLength, sizeof wp->name,
					SIT_Left,       SITV_AttachForm, NULL, (int) (rect[0] - left) - padding[0],
					SIT_Top,        SITV_AttachForm, NULL, (int) (rect[1] - top)  - padding[1] - 1,
					SIT_Width,      (int) (rect[2] - rect[0] - 2),
					SIT_Height,     (int) (rect[3] - rect[1] - 3),
					SIT_Style,      "border: 0; padding: 0",
					SIT_UserData,   (APTR) click,
					NULL
				);
				if (click == 2)
					SIT_SetValues(w, SIT_PlaceHolder, "Set to player pos if empty", NULL);
				SIT_SetFocus(w);
				SIT_AddCallback(w, SITE_OnBlur,   wayPointsFinishEdit, wp);
				SIT_AddCallback(w, SITE_OnRawKey, wayPointsAcceptEdit, wp);
				break;
			default:
				return 0;
			}
			return 1;
		}
	}
	return 0;
}

/* OnClose */
extern int wayPointsExit(SIT_Widget w, APTR cd, APTR ud)
{
	if (waypoints.nbtModified)
	{
		/* save on exit no matter what: not a critical file */
		TEXT path[128];
		CopyString(path, globals.level->path, sizeof path);
		AddPart(path, "../mcedit_waypoints.dat", sizeof path);
		if (NBT_Save(&waypoints.nbt, path, NULL, NULL) > 0)
			waypoints.nbtModified = False;
	}
	return 1;
}

/* SITE_OnChange on list box */
static int wayPointsSelect(SIT_Widget w, APTR cd, APTR ud)
{
	int nth;
	SIT_GetValues(w, SIT_SelectedIndex, &nth, NULL);
	SIT_SetValues(waypoints.delButton, SIT_Enabled, nth >= 0, NULL);
	if (nth >= 0)
	{
		WayPoint wp = vector_nth(&waypoints.all, nth);
		memcpy(waypoints.curPos, wp->location, 12);
		memcpy(waypoints.rotation, wp->rotation, 8);
		/* since we just modified curValue behind the back of SITGL, we need to notify the widgets */
		SIT_SetValues(waypoints.coords[0], SIT_Title, NULL, NULL);
		SIT_SetValues(waypoints.coords[1], SIT_Title, NULL, NULL);
		SIT_SetValues(waypoints.coords[2], SIT_Title, NULL, NULL);
	}
	return 1;
}

/* SITE_OnActivate on checkbox */
static int wayPointsDisplayed(SIT_Widget w, APTR cd, APTR ud)
{
	/* stored at the root of NBT */
	NBT_AddOrUpdateKey(&waypoints.nbt, ".DisplayInWorld", TAG_Int, &waypoints.displayInWorld, 0);
	waypoints.nbtModified = True;
	return 1;
}

extern int mcuiExitWnd(SIT_Widget w, APTR cd, APTR ud);

/* waypoints editing/goto location interface */
void wayPointsEdit(vec4 pos, float rotation[2])
{
	SIT_Widget diag = SIT_CreateWidget("goto.bg", SIT_DIALOG, globals.app,
		SIT_DialogStyles, SITV_Plain | SITV_Movable,
		NULL
	);
	memcpy(waypoints.curPos, pos, 12);
	memcpy(waypoints.rotation, rotation, 8);
	waypoints.playerPos = pos;
	waypoints.playerRotation = rotation;

	SIT_CreateWidgets(diag,
		"<label name=title.big title='Enter the coordinates you want to jump to:' left=", SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, ">"
		"<editbox name=X roundTo=2 editType=", SITV_Float, "width=8em curValue=", waypoints.curPos, "top=WIDGET,title,1em buddyLabel=", "X:", NULL, ">"
		"<editbox name=Y roundTo=2 editType=", SITV_Float, "width=8em curValue=", waypoints.curPos+1, "top=WIDGET,title,1em buddyLabel=", "Y:", NULL, ">"
		"<editbox name=Z roundTo=2 editType=", SITV_Float, "width=8em curValue=", waypoints.curPos+2, "top=WIDGET,title,1em buddyLabel=", "Z:", NULL, ">"
	);
	SIT_AddCallback(diag, SITE_OnFinalize, wayPointsExit, NULL);

	SIT_Widget top;
	/* show a list of waypoints if there is at least one */
	if (waypoints.all.count > 0)
	{
		SIT_CreateWidgets(diag,
			"<button name=ok title=Goto top=MIDDLE,Z left=WIDGET,Z,0.5em>"
			"<label name=msg title='<b>Available waypoints:</b> (right-click to edit)' left=",
				SITV_AttachPosition, SITV_AttachPos(50), SITV_OffsetCenter, "top=WIDGET,X,0.5em>"
			"<listbox name=list columnNames='\tName\tLocation\tDist.' left=FORM top=WIDGET,msg,0.5em right=FORM height=10em cellPaint=", wayPointsPaintCell, ">"
		);
		top = waypoints.list = SIT_GetById(diag, "list");
		SIT_ListSetColumn(top, 0, SIT_EmToReal(diag, SITV_Em(1.5)), 'L', DontChangePtr);

		WayPoint wp;
		int i;
		for (wp = vector_first(waypoints.all), i = waypoints.all.count; i > 0; wp ++, i --)
			wayPointsAddToList(wp);

		SIT_AddCallback(top, SITE_OnClick,    wayPointsClick,  NULL);
		SIT_AddCallback(top, SITE_OnChange,   wayPointsSelect, NULL);
		SIT_AddCallback(top, SITE_OnActivate, wayPointsGoto,   NULL);
	}
	else /* simplified interface */
	{
		top = SIT_GetById(diag, "X");
		waypoints.list = NULL;
	}

	SIT_CreateWidgets(diag,
		"<button name=add title='Add marker' top=", SITV_AttachWidget, top, SITV_Em(0.5), ">"
		"<button name=del title='Delete' top=OPPOSITE,add left=WIDGET,add,0.8em enabled=0>"
	);
	SIT_SetAttributes(diag,
		"<bY left=WIDGET,X,1em><bZ left=WIDGET,Y,1em>"
	);
	waypoints.delButton = SIT_GetById(diag, "del");
	waypoints.coords[0] = SIT_GetById(diag, "X");
	waypoints.coords[1] = SIT_GetById(diag, "Y");
	waypoints.coords[2] = SIT_GetById(diag, "Z");
	if (waypoints.all.count > 0)
	{
		SIT_CreateWidgets(diag,
			"<button name=render buttonType=", SITV_CheckBox, "curValue=", &waypoints.displayInWorld,
			"title='Show in world' left=WIDGET,del,0.5em top=MIDDLE,add>"
			"<button name=done title=Done top=OPPOSITE,add right=FORM buttonType=", SITV_DefaultButton, ">"
		);
		SIT_AddCallback(SIT_GetById(diag, "render"), SITE_OnActivate, wayPointsDisplayed, NULL);
		SIT_AddCallback(SIT_GetById(diag, "done"),   SITE_OnActivate, mcuiExitWnd, NULL);
		if (waypoints.lastHover > 0)
		{
			int nth = waypoints.lastHover-1;
			SIT_SetValues(top, SIT_MakeVisible, nth, SIT_SelectedIndex, nth, NULL);
		}
	}
	else
	{
		SIT_SetValues(waypoints.delButton, SIT_Visible, 0, NULL);
		SIT_CreateWidgets(diag,
			"<button name=ko title=Cancel top=OPPOSITE,add right=FORM buttonType=", SITV_CancelButton, ">"
			"<button name=ok title=Goto right=WIDGET,ko,0.5em top=OPPOSITE,add buttonType=", SITV_DefaultButton, ">"
		);
	}

	SIT_AddCallback(SIT_GetById(diag, "ok"),  SITE_OnActivate, wayPointsGoto, NULL);
	SIT_AddCallback(SIT_GetById(diag, "add"), SITE_OnActivate, wayPointsAdd, NULL);
	SIT_AddCallback(SIT_GetById(diag, "ko"),  SITE_OnActivate, mcuiExitWnd, NULL);
	SIT_AddCallback(waypoints.delButton,      SITE_OnActivate, wayPointsDel, NULL);

	SIT_ManageWidget(diag);
}

/* tooltip displayed in 3d view */
void wayPointInfo(int id, STRPTR msg, int max)
{
	WayPoint wp = vector_nth(&waypoints.all, id-1);
	int i;

	msg[0] = 0;
	i = StrCat(msg, max, 0, "<b>Waypoint:</b> ");
	if (wp->name[0])
	{
		i = StrCat(msg, max, i, wp->name);
		i = StrCat(msg, max, i, "<br>");
	}
	snprintf(msg + i, max - i, "<ench>%d, %d, %d</ench><br>Ctrl+G to edit", (int) wp->location[0], (int) wp->location[1], (int) wp->location[2]);
}

/*
 * openGL rendering part
 */

static void wayPointSetAlpha(int nth, int alpha)
{
	WayPoint wp = vector_nth(&waypoints.all, nth);
	wp->color[3] = alpha;
	glBindBuffer(GL_ARRAY_BUFFER, waypoints.vbo);
	glBufferSubData(GL_ARRAY_BUFFER, wp->glIndex * WAYPOINTS_VBO_SIZE + 12, 4, wp->color);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

int intersectRayPlane(vec4 P0, vec4 u, vec4 V0, vec norm, vec4 I);

/* find the waypoint hovered using position <camera> and direction vector <dir> */
int wayPointRaypick(vec4 dir, vec4 camera, vec4 cur, vec4 ret_pos)
{
	if (waypoints.displayInWorld == 0 || waypoints.all.count == 0)
		return 0;

	WayPoint wp;
	vec4     normal;
	float    maxDist = cur ? vecDistSquare(camera, cur) : 1e6f;
	int      wpId = 0;
	int      i, max;

	/* normal of waypoints: always facing player */
	normal[VX] = cosf(globals.yawPitch[0]);
	normal[VY] = 0;
	normal[VZ] = sinf(globals.yawPitch[0]);

	for (wp = vector_first(waypoints.all), i = 0, max = waypoints.all.count; i < max; i ++, wp ++)
	{
		if (wp->glIndex < 0) continue;
		vec4 inter;

		/* check if waypoint is behind camera (only care about their distance in the XZ plane) */
		inter[VX] = camera[VX] + normal[VX] * 0.5f - wp->location[VX];
		inter[VZ] = camera[VZ] + normal[VZ] * 0.5f - wp->location[VZ];
		inter[VY] = 0;
		/* dot product, but 2d only */
		if (vecDotProduct(inter, normal) > 0)
			continue;

		/* this function should always return 1 */
		if (intersectRayPlane(camera, dir, wp->location, normal, inter) == 1)
		{
			/* we are just interested in <inter> actually */
			vec4 CA;
			vecSub(CA, wp->location, inter);
			float dist = (CA[VX] * CA[VX] + CA[VZ] * CA[VZ]);

			if (dist > (WAYPOINTS_BEAM_SZ/2) * (WAYPOINTS_BEAM_SZ/2) /* 0.0625 */ || inter[VY] < wp->location[VY] - WAYPOINTS_BEAM_SZ/2)
				continue;

			/* need to check if there is a closer one though */
			dist = vecDistSquare(wp->location, camera);
			if (dist < maxDist)
				wpId = i + 1, maxDist = dist;
		}
	}
	if (waypoints.lastHover != wpId)
	{
		if (waypoints.lastHover > 0)
			wayPointSetAlpha(waypoints.lastHover-1, 0.3*255);

		if (wpId > 0)
			wayPointSetAlpha(wpId-1, 0.5*255);

		waypoints.lastHover = wpId;
	}
	return wpId;
}

Bool wayPointsInit(void)
{
	waypoints.shader = createGLSLProgram("waypoints.vsh", "waypoints.fsh", "waypoints.gsh");
	if (! waypoints.shader)
		/* error message already displayed */
		return False;

	/* very similar to particles */
	glGenVertexArrays(1, &waypoints.vao);
	glGenBuffers(1, &waypoints.vbo);

	glBindVertexArray(waypoints.vao);
	glBindBuffer(GL_ARRAY_BUFFER, waypoints.vbo);
	glBufferData(GL_ARRAY_BUFFER, WAYPOINTS_VBO_SIZE * WAYPOINTS_MAX * 2, NULL, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, WAYPOINTS_VBO_SIZE, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_INT, WAYPOINTS_VBO_SIZE, (void *) 12);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return True;
}

/* pretty cheap to compare */
static int wayPointsSort(const void * item1, const void * item2)
{
	return ((int32_t *)item2)[4] - ((int32_t *)item1)[4];
}

void wayPointsRender(vec4 camera)
{
	if (waypoints.all.count == 0 || waypoints.displayInWorld == 0)
		return;

	vec4 diff;
	vecSub(diff, camera, waypoints.lastPos);
	if (fabsf(diff[VX]) > 8 || fabsf(diff[VZ]) > 0)
		waypoints.listDirty = 1;

	if (waypoints.listDirty)
	{
		glBindBuffer(GL_ARRAY_BUFFER, waypoints.vbo);
		float *  vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
		float *  buffer = vertex;
		int32_t  maxDist = globals.level->maxDist * 16;
		int      i, max;
		WayPoint wp;

		for (wp = vector_first(waypoints.all), i = 0, max = waypoints.all.count, waypoints.glCount = 0, maxDist *= maxDist; i < max; i ++, wp ++)
		{
			/* don't care about frustum culling: there are too little of these to bother */
			int32_t dist = vecDistSquare(wp->location, camera);
			if (dist < maxDist)
			{
				vertex[0] = i;
				memcpy(vertex+3, wp->color, 4);
				memcpy(vertex+4, &dist, 4);
				((DATA8)vertex)[15] = 0.3*255;
				vertex += WAYPOINTS_VBO_SIZE/4;
				waypoints.glCount ++;
				if (waypoints.glCount == WAYPOINTS_MAX) break;
			}
			else wp->glIndex = -1;
		}
		/* they will be rendered with alpha: need to be sorted from back to front */
		qsort(buffer, waypoints.glCount, WAYPOINTS_VBO_SIZE, wayPointsSort);
		/* glIndex can only be set once the array has been sorted */
		for (i = 0, max = waypoints.glCount, vertex = buffer; i < max; i ++, vertex += WAYPOINTS_VBO_SIZE/4)
		{
			wp = vector_nth(&waypoints.all, vertex[0]);
			memcpy(vertex, wp->location, 12);
			wp->glIndex = i;
		}

		glUnmapBuffer(GL_ARRAY_BUFFER);
		waypoints.listDirty = 0;
		memcpy(waypoints.lastPos, camera, 12);
	}

	if (waypoints.glCount > 0)
	{
		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glUseProgram(waypoints.shader);
		glBindVertexArray(waypoints.vao);
		/* geometry shader will convert points to quad */
		glDrawArrays(GL_POINTS, 0, waypoints.glCount);
		glDepthMask(GL_TRUE);
		glBindVertexArray(0);
	}
}
