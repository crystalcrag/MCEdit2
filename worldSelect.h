/*
 * worldSelect.c: public function to handle GAMELOOP_WORLDSELECT from main.c.
 *
 * written by T.Pierron, dec 2021
 */

#ifndef MC_WORLDSELECT
#define MC_WORLDSELECT


int optionsQuickAccess(SIT_Widget, APTR cd, APTR ud);
int optionsExit(SIT_Widget, APTR cd, APTR ud);


#ifdef WORLDSELECT_IMPL
struct WorldSelect_t
{
	SIT_Widget options, brightval;
	SIT_Widget enterKey, worlds;
	SIT_Widget curKey, capture;
	int        sensitivity;
	int        guiScale, lockMouse;
	int        curKeySym, curKeyMod;
	int        renderDist, autoEdit;
	int        fov, fps, fog;
	int        brightness, fullScreen;
	int        compassSize, showPreview;
	int        curTab;
};
#endif
#endif
