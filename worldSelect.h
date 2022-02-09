/*
 * worldSelect.c: public function to handle GAMELOOP_WORLDSELECT from main.c.
 *
 * written by T.Pierron, dec 2021
 */

#ifndef MC_WORLDSELECT
#define MC_WORLDSELECT


SIT_Widget optionsQuickAccess(void);
int        optionsExit(SIT_Widget, APTR cd, APTR ud);
void       mceditYesNo(SIT_Widget parent, STRPTR msg, SIT_CallProc cb, Bool yesNo);


#ifdef WORLDSELECT_IMPL
struct WorldSelect_t
{
	SIT_Widget options, brightval;
	SIT_Widget enterKey, worlds;
	SIT_Widget curKey, capture;
	SIT_Widget fileSelect, dirSelect;
	int        sensitivity;
	int        guiScale, lockMouse;
	int        curKeySym, curKeyMod;
	int        renderDist, autoEdit;
	int        fov, fps, fog;
	int        brightness, fullScreen;
	int        compassSize, showPreview;
	int        curTab;
};

typedef struct WorldInfo_t * WorldInfo;

struct WorldInfo_t
{
	int64_t timestamp;
	uint8_t folder[1];
};

#endif
#endif
