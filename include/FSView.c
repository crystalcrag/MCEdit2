/*
 * FSView.c : simplified file manager view to load or save files.
 *
 * note: if the macro FSVIEW_HASCOPY is defined before compiling that file, you'll be able to move/copy
 *       files using Ctrl+C/X/V, similar to Windows explorer. This feature alone is about 40% of the total
 *       code of this module.
 *
 * Written by T.Pierron, sep 2021.
 */

#include <stdio.h>
#include <malloc.h>
#include <stdarg.h>
#include "SIT.h"

typedef struct FSView_t *     FSView;
typedef struct FSItem_t *     FSItem;
typedef struct FStack_t *     FStack;
typedef struct FSStat_t *     FSStat;

struct FSView_t
{
	SIT_Widget   list;
	SIT_Widget   select, rename, del;
	SIT_Widget   search, dir, save;
	SIT_CallProc create, notify;
	ListHead     files;
	uint8_t      flags;
	uint8_t      operation;
	uint8_t      forceRename;
	STRPTR       copyFiles;
	STRPTR       ext;
	uint16_t     copyMax;
	uint16_t     copyUsage;
	uint16_t     copyCount;
	TEXT         curDir[256];
};

struct FSItem_t
{
	ListNode   node;
	SIT_Widget preview;
	uint8_t    type;
	uint8_t    visible;
	uint8_t    todel;
	uint8_t    haspreview;
	int        size;
	TEXT       name[1];
};

struct FStack_t
{
	FStack      next, prev;
	ScanDirData args;
};

struct FSStat_t
{
	int      dirs, files;
	int      rename, overwrite;
	uint64_t total;
};

static int FSProcessMenu(SIT_Widget w, APTR cd, APTR ud);
static int FSSaveAs(SIT_Widget w, APTR cd, APTR ud);

static SIT_Accel accels[] = {
	#ifdef FSVIEW_HASCOPY
	{SITK_FlagCapture + SITK_FlagCtrl + 'C', SITE_OnActivate, NULL, FSProcessMenu},
	{SITK_FlagCapture + SITK_FlagCtrl + 'X', SITE_OnActivate, NULL, FSProcessMenu},
	{SITK_FlagCapture + SITK_FlagCtrl + 'V', SITE_OnActivate, NULL, FSProcessMenu},
	#endif
	{SITK_FlagCapture + SITK_FlagCtrl + 'F', SITE_OnActivate, NULL, FSProcessMenu},
	{SITK_FlagCapture + SITK_FlagCtrl + 'R', SITE_OnActivate, "rename"},
	{SITK_BackSpace, SITE_OnActivate, "parent"},
	{SITK_Delete,    SITE_OnActivate, "ko"},
	#ifdef FSVIEW_CAPTUREESC
	{SITK_FlagCapture + SITK_Escape, SITE_OnClose},
	#endif
	{0}
};

void FSViewScan(FSView view, STRPTR path)
{
	ScanDirData args;
	FSItem item;

	/* delete previous entries */
	SIT_ListDeleteRow(view->list, DeleteAllRows);
	while ((item = (FSItem) ListRemHead(&view->files)))
		free(item);

	if (ScanDirInit(&args, path))
	{
		FSItem dir = NULL;
		int lastDir = 0;
		do
		{
			item = calloc(sizeof *item + strlen(args.name), 1);
			item->visible = 1;
			item->type = args.isDir;
			item->size = args.size;

			if (args.isDir == 0)
				ListAddTail(&view->files, &item->node);
			else if (dir)
				ListInsert(&view->files, &item->node, &dir->node), dir = item;
			else
				ListAddHead(&view->files, &item->node), dir = item;
			strcpy(item->name, args.name);

			SIT_Widget td = args.isDir ?
				SIT_ListInsertControlIntoCell(view->list, SIT_ListInsertItem(view->list, lastDir ++, item, SITV_TDSubChild), 0) :
				SIT_ListInsertControlIntoCell(view->list, SIT_ListInsertItem(view->list, -1, item, SITV_TDSubChild), 0);

			view->create(td, view->curDir, item);

			SIT_ListFinishInsertControl(view->list);
		}
		while (ScanDirNext(&args));
	}
}

static int FSFilterItems(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud;
	FSItem item;
	TEXT   search[32];
	int    row;
	/* convert to lowercase */
	CopyString(search, cd, sizeof search);
	StrToLower(search, -1);
	if (search[0] == 0)
	{
		/* show everything */
		for (item = HEAD(view->files), row = 0; item; NEXT(item), row ++)
		{
			if (! item->visible)
				SIT_ListSetRowVisibility(view->list, row, item->visible = 1);
		}
	}
	else for (item = HEAD(view->files), row = 0; item; NEXT(item), row ++)
	{
		/* no need to bother with a more complicated sub-string search */
		DATA8 p;
		char  match = 0;
		for (p = item->name; *p; p ++)
		{
			uint8_t chr = *p;
			if ('A' <= chr && chr <= 'Z') chr += 32;
			if (search[0] == chr)
			{
				DATA8 s, s2;
				for (s = search + 1, s2 = p + 1; *s; s ++, s2 ++)
				{
					chr = *s2;
					if ('A' <= chr && chr <= 'Z') chr += 32;
					if (chr != *s) break;
				}
				if (*s == 0) { match = 1; break; }
			}
		}
		if (item->visible != match)
		{
			item->visible = match;
			SIT_ListSetRowVisibility(view->list, row, match);
		}
	}
	return 1;
}

/* selection changed in list view */
static int FSSelectItem(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud;
	Bool ena = cd != NULL;
	if (view->flags & FSVIEW_SAVE)
	{
		FSItem item = cd;
		if (item) SIT_SetValues(view->save, SIT_Title, item->name, NULL);
	}
	else
	{
		SIT_SetValues(view->select, SIT_Enabled, ena, NULL);
		SIT_SetValues(view->rename, SIT_Enabled, ena, NULL);
		SIT_SetValues(view->del,    SIT_Enabled, ena, NULL);
	}
	return 1;
}

/* double-clicked on an item in list view */
static int FSUseItem(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud;
	FSItem item = NULL;
	int    row;
	SIT_GetValues(view->list, SIT_SelectedIndex, &row, NULL);
	SIT_GetValues(view->list, SIT_RowTag(row), &item, NULL);
	if (item == NULL) return 0;
	if (item->type == 1)
	{
		/* directory */
		if (AddPart(view->curDir, item->name, sizeof view->curDir))
		{
			SIT_SetValues(view->dir, SIT_Title, view->curDir, NULL);
			FSViewScan(view, view->curDir);
		}
		else SIT_Log(SIT_WARN, "Path is too long (%d characters, max: %d), sorry about that :-/\n",
			strlen(view->curDir) + strlen(item->name) + 1, sizeof view->curDir - 1);
	}
	else if (view->flags & FSVIEW_SAVE)
	{
		FSSaveAs(w, cd, ud);
	}
	else
	{
		/* provide a full path for convenience */
		int    max = strlen(view->curDir) + strlen(item->name) + 2;
		STRPTR full = alloca(max);
		strcpy(full, view->curDir);
		AddPart(full, item->name, max);
		view->notify(w, view, full);
	}
	return 1;
}

static int FSParentDir(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud;
	if (ParentDir(view->curDir) >= 0)
	{
		SIT_SetValues(view->dir, SIT_Title, view->curDir, NULL);
		FSViewScan(view, view->curDir);
		/* select the directory we came from */
		STRPTR select = strchr(view->curDir, 0) + 1;
		FSItem dir;
		int    row;
		for (dir = HEAD(view->files), row = 0; dir && strcasecmp(dir->name, select); NEXT(dir), row ++);
		SIT_SetValues(view->list, SIT_RowSel(row), True, NULL);
	}
	return 1;
}


static int FSCloseDialog(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_CloseDialog(w);
	return 1;
}

/* ask a question to the user with Yes/No as possible answer */
static void FSYesNo(FSView view, STRPTR msg, SIT_CallProc cb, Bool yesNo)
{
	SIT_Widget ask = SIT_CreateWidget("ask.bg", SIT_DIALOG, view->list,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		#ifdef FSVIEW_CAPTUREESC
		SIT_AccelTable,   EOT(accels) - 2,
		#endif
		SIT_Style,        "padding: 1em",
		NULL
	);

	SIT_CreateWidgets(ask, "<label name=label title=", msg, ">");

	if (yesNo)
	{
		SIT_CreateWidgets(ask,
			"<button name=ok title=Yes top=WIDGET,label,0.8em buttonType=", SITV_DefaultButton, ">"
			"<button name=ko title=No top=OPPOSITE,ok right=FORM buttonType=", SITV_CancelButton, ">"
		);
		SIT_SetAttributes(ask, "<ok right=WIDGET,ko,1em>");
	}
	else /* only a "no" button */
	{
		SIT_CreateWidgets(ask, "<button name=ok right=FORM title=Ok top=WIDGET,label,0.8em buttonType=", SITV_DefaultButton, ">");
		cb = FSCloseDialog;
	}
	SIT_AddCallback(SIT_GetById(ask, "ok"), SITE_OnActivate, cb, view);
	SIT_ManageWidget(ask);
}


#ifdef FSVIEW_HASCOPY
/* need to check recursively: expensive, but better be safe than sorry :-/ */
static void FSCheckDir(STRPTR dst, STRPTR src, FSStat stat, int max, STRPTR conflict, int maxc)
{
	FStack scan = alloca(sizeof *scan);

	scan->next = scan->prev = NULL;
	if (ScanDirInit(&scan->args, src))
	{
		for (;;)
		{
			if (dst)
			{
				AddPart(dst, scan->args.name, max);
				Bool isDir = IsDir(dst);
				if (isDir != scan->args.isDir)
				{
					stat->rename ++;
				}
				else if (! isDir)
				{
					stat->overwrite ++;
					if (stat->overwrite < 5)
					{
						int j;
						j = StrCat(conflict, maxc, 0, "&#x25cf; ");
						j = StrCat(conflict, maxc, j, scan->args.name);
						j = StrCat(conflict, maxc, j, "<br>");
					}
				}
			}

			if (scan->args.isDir)
			{
				stat->dirs ++;
				/* recursive scan */
				if (! AddPart(src, scan->args.name, max))
				{
					/* fatal error: path too long :-/ */
					stat->overwrite = strlen(src) + strlen(scan->args.name) + 1;
					stat->files = stat->dirs = -1;
					while (scan)
					{
						ScanDirCancel(&scan->args);
						scan = scan->prev;
					}
					return;
				}
				if (dst)
					AddPart(dst, scan->args.name, max);

				if (scan->next == NULL)
				{
					FStack top = alloca(sizeof *scan);
					scan->next = top;
					top->prev = scan;
					top->next = NULL;
				}
				scan = scan->next;
				if (ScanDirInit(&scan->args, src))
				{
					/* empty directory */
					ParentDir(src);
					if (dst) ParentDir(dst);
				}
			}
			else stat->files ++, stat->total += scan->args.size;

			while (! ScanDirNext(&scan->args))
			{
				/* no more entries in this directory */
				if (scan->prev == NULL) return;
				ParentDir(src);
				if (dst) ParentDir(dst);
				scan = scan->prev;
			}
		}
	}
}

/* file/directory has been created in current view: add to the list */
static void FSInsertListItem(FSView view, STRPTR full)
{
	STRPTR name = BaseName(full);
	FSItem item = calloc(sizeof *item + strlen(name), 1);
	FSItem ins  = HEAD(view->files);
	int    row  = 0;
	int    res  = -1;
	item->visible = 1;
	item->type = IsDir(full);
	strcpy(item->name, name);

	if (item->type == 0)
	{
		/* sort after directories */
		item->size = FileSize(full);
		while (ins && ins->type == 1)
			NEXT(ins), row ++;
	}

	/* lexicographic sort in the list */
	while (ins && (res = strcasecmp(ins->name, name)) < 0)
		NEXT(ins), row ++;

	if (res == 0)
	{
		/* overwritten item: need to remove the old one */
		FSItem next = (FSItem) ins->node.ln_Next;
		ListRemove(&view->files, &ins->node);
		SIT_ListDeleteRow(view->list, row);
		free(ins);
		ins = next;
	}

	if (ins == NULL)
		ListAddTail(&view->files, &item->node);
	else
		ListInsert(&view->files, &item->node, ins->node.ln_Prev);

	SIT_Widget td = SIT_ListInsertControlIntoCell(view->list, SIT_ListInsertItem(view->list, row, item, SITV_TDSubChild), 0);

	view->create(td, view->curDir, item);

	SIT_ListFinishInsertControl(view->list);
}

/* auto-rename file/dir to avoid conflict */
static Bool FSAutoRename(FSView view, STRPTR dst, STRPTR src, Bool isDir)
{
	STRPTR file = BaseName(dst);
	STRPTR ext  = strrchr(file, '.');
	int    count = 1;

	if (ext) file = ext, ext = STRDUPA(ext);
	else     file = strchr(file, 0), ext = "";

	do {
		sprintf(file, " (%d)%s", count, ext);
		count ++;
	} while (FileExists(dst));

	if (view->operation == 'c' ? !FileCopy(src, dst, True) : !FileRename(src, dst, True))
		return False;

	FSInsertListItem(view, dst);
	return True;
}

static Bool FSOperation(FSView view, STRPTR dst, STRPTR src, Bool isDir)
{
	if (view->operation == 'c' ? !FileCopy(src, dst, True) : !FileRename(src, dst, True))
		return False;
	FSInsertListItem(view, dst);
	return True;
}

/* perform the actual move/copy */
static void FSDoPaste(FSView view, FSStat stat)
{
	TEXT dst[512];
	TEXT src[512];
	int  slen, dlen, i, pos;

	CopyString(dst, view->curDir,    sizeof dst);
	CopyString(src, view->copyFiles, sizeof src);
	dlen = strlen(dst);
	slen = strlen(src);

	for (pos = slen + 1, i = view->copyCount; i > 0; i --)
	{
		STRPTR file = view->copyFiles + pos;
		AddPart(dst, file, sizeof dst);
		AddPart(src, file, sizeof src);

		if (FileExists(src))
		{
			Bool isDirSrc = IsDir(src);
			if (FileExists(dst))
			{
				/* conflict: check what can be done: overwrite or rename */
				Bool isDirDst = IsDir(dst);

				if (isDirDst != isDirSrc)
				{
					/* can't overwrite: has to be renamed */
					if (! FSAutoRename(view, dst, src, isDirSrc))
					{
						fatal_error:
						snprintf(dst, sizeof dst, "error %s '%s' to '%s':<br><br>%s", view->operation == 'c' ? "copying" : "moving", src, dst, GetError());
						FSYesNo(view, dst, NULL, False);
						return;
					}
				}
				else if (isDirDst)
				{
					/* same dir: need to check resursively */
					if (IsSameFile(src, dst))
					{
						/* overwrite with the same directory: will have to duplicate the whole content */
						if (view->operation == 'c')
							FSAutoRename(view, dst, src, True);
						else
							goto next_file; /* nothing to do */
					}
					/* else merge directories content */

					//FSCheckDir(dst, src, &stat, sizeof src, conflict, sizeof conflict);
				}
				else if (! IsSameFile(src, dst))
				{
					/* src is a file: move or copy as needed (overwriting dst if it exists) */
					if (view->forceRename ? ! FSAutoRename(view, dst, src, False) : ! FSOperation(view, dst, src, False))
						goto fatal_error;
				}
				else if (view->operation == 'c')
				{
					if (! FSAutoRename(view, dst, src, False))
						goto fatal_error;
				}
				/* else do nothing */
			}
			else if (isDirSrc)
			{
				//FSCheckDir(NULL, src, &stat, sizeof src, NULL, 0);
			}
			else if (! FSOperation(view, dst, src, False))
			{
				goto fatal_error;
			}
		}
		/* else src file deleted in the meantime: not a big deal */

		next_file:
		pos += strlen(file) + 1;
		dst[dlen] = 0;
		src[slen] = 0;
	}
}

static int FSForceRename(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget diag;
	FSView     view;
	FSStat     stat;
	APTR       rename;
	view = ud;
	SIT_GetValues(w, SIT_UserData, &rename, SIT_Parent, &diag, NULL);
	SIT_GetValues(diag, SIT_UserData, &stat, NULL); stat = memcpy(alloca(sizeof *stat), stat, sizeof *stat);
	SIT_CloseDialog(w);
	view->forceRename = (int) rename;
	FSDoPaste(view, stat);
	return 1;
}

static void FSAsk(FSView view, FSStat stat, ...)
{
	static TEXT suffix[] = " What do you want to do ?<br><br>Note: nothing have been %s yet";
	va_list args;
	STRPTR  msg, label;
	int     len, i;

	i = 0;
	len = strlen(suffix) + 10;
	for (va_start(args, stat); (msg = va_arg(args, STRPTR)); len += strlen(msg)); label = alloca(len); label[0] = 0;
	for (va_start(args, stat); (msg = va_arg(args, STRPTR)); i = StrCat(label, len, i, msg));

	sprintf(label + i, suffix, view->operation == 'c' ? "copied" : "moved");

	SIT_Widget diag = SIT_CreateWidget("ask.bg", SIT_DIALOG + SIT_EXTRA(sizeof *stat), view->list,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		#ifdef FSVIEW_CAPTUREESC
		SIT_AccelTable,   EOT(accels) - 2,
		#endif
		SIT_Style,        "padding: 1em",
		NULL
	);

	SIT_CreateWidgets(diag,
		"<label name=label title=", label, ">"
		"<button name=ren title=Rename top=WIDGET,label,0.5em userData=1 buttonType=", SITV_DefaultButton, ">"
		"<button name=over title=Overwrite top=OPPOSITE,ren>"
		"<button name=ko title=Cancel top=OPPOSITE,ren right=FORM buttonType=", SITV_CancelButton, ">"
	);
	SIT_SetAttributes(diag, "<ren right=WIDGET,over,1em><over right=WIDGET,ko,1em>");

	FSStat mem;
	SIT_GetValues(diag, SIT_UserData, &mem, NULL);
	memcpy(mem, stat, sizeof *mem);

	SIT_AddCallback(SIT_GetById(diag, "ren"),  SITE_OnActivate, FSForceRename, view);
	SIT_AddCallback(SIT_GetById(diag, "over"), SITE_OnActivate, FSForceRename, view);

	SIT_ManageWidget(diag);
}

/* copy/move files that have been copied to "clipboard" */
static void FSPasteOperation(FSView view)
{
	if (view->copyCount == 0) return;

	/*
	 * first check if there is any conflicts
	 */
	struct FSStat_t stat = {0};
	TEXT   dst[512];
	TEXT   src[512];
	TEXT   conflict[256];
	STRPTR lastFile;
	int    slen, dlen, i, pos, j;

	CopyString(dst, view->curDir,    sizeof dst);
	CopyString(src, view->copyFiles, sizeof src);
	dlen = strlen(dst);
	slen = strlen(src);
	view->forceRename = 0;

	/* this entire loop just looks for conflicts */
	for (pos = slen + 1, j = 0, i = view->copyCount, conflict[0] = 0, lastFile = NULL; i > 0; i --)
	{
		STRPTR file = view->copyFiles + pos;
		AddPart(dst, file, sizeof dst);
		AddPart(src, file, sizeof src);

		if (FileExists(src))
		{
			Bool isDirSrc = IsDir(src);
			if (isDirSrc) stat.dirs ++;
			else          stat.files ++;
			if (FileExists(dst))
			{
				/* conflict: check what can be done: overwrite or rename */
				Bool isDirDst = IsDir(dst);

				if (isDirDst != isDirSrc)
				{
					/* can't overwrite: has to be renamed */
					stat.rename ++;
				}
				else if (isDirDst)
				{
					/* same dir: need to check resursively */
					if (IsSameFile(src, dst))
					{
						/* overwrite with the same directory: will have to duplicate the whole content */
						stat.rename ++;
						if (view->operation == 'c')
							/* count the objects to copy */
							FSCheckDir(NULL, src, &stat, sizeof src, NULL, 0);
						/* else directory will simply be renamed */
					}
					else FSCheckDir(dst, src, &stat, sizeof src, conflict, sizeof conflict);

					if (stat.files < 0)
					{
						bail:
						SIT_Log(SIT_WARN, "Path is too long (%d characters, max: %d), sorry about that :-/\n",
							stat.overwrite, sizeof src - 1);
						return;
					}
				}
				else if (! IsSameFile(src, dst))
				{
					stat.overwrite ++;
					if (stat.overwrite < 5)
					{
						j = StrCat(conflict, sizeof conflict, j, "&#x25cf; ");
						j = StrCat(conflict, sizeof conflict, j, file);
						j = StrCat(conflict, sizeof conflict, j, "<br>");
					}
					lastFile = file;
				}
				else stat.rename ++;

			}
			else if (isDirSrc)
			{
				FSCheckDir(NULL, src, &stat, sizeof src, NULL, 0);
				if (stat.files < 0) goto bail;
			}
		}
		/* else src file deleted in the meantime: not a big deal */

		pos += strlen(file) + 1;
		dst[dlen] = 0;
		src[slen] = 0;
	}

	/*
	 * second: perform the operation, avert the user of anything unusual
	 */
	if (stat.overwrite > 0 && stat.rename == 0)
	{
		if (stat.overwrite > 5)
			snprintf(conflict, sizeof conflict - j, "&#x25cf; and %d more.", stat.overwrite - 5);

		if (stat.overwrite == 1)
			FSAsk(view, &stat, "The file '", lastFile, "' already exists in this directory.", NULL);
		else
			FSAsk(view, &stat, "The following files already exist in this directory:<br>", conflict, NULL);
	}
	else if (stat.files > 100)
	{
		FormatNumber((stat.total+1023) >> 10, src, sizeof src);
		sprintf(conflict, " %d files (%s Kb)", stat.files, src);
		FSAsk(view, &stat, "Are you sure you want to ", view->operation == 'c' ? "copy" : "move", conflict, " into this directory ?", NULL);
	}
	else /* no conflict or it can be resolved automatically */
	{
		// start copy/move
		FSDoPaste(view, &stat);
		//SIT_Log(SIT_WARN, "files: %d, dirs: %d, overwrite: %d, rename: %d, size: %d\n",
		//	stat.files, stat.dirs, stat.overwrite, stat.rename, (int) stat.total);
	}
}

static void FSAddItem(FSView view, STRPTR name)
{
	int bytes = strlen(name) + 1;
	int init  = 0;
	if (view->copyCount == 0)
	{
		view->copyUsage = 0;
		bytes += init = strlen(view->curDir) + 1;
	}

	if (view->copyUsage + bytes > view->copyMax)
	{
		int max = (view->copyUsage + bytes + 255) & ~255;   if (max > 65535) return;
		STRPTR buffer = realloc(view->copyFiles, max);      if (buffer == NULL) return;

		view->copyFiles = buffer;
		view->copyMax   = max;
	}
	if (init > 0)
	{
		memcpy(view->copyFiles, view->curDir, init);
		view->copyUsage = init;
		bytes -= init;
	}
	memcpy(view->copyFiles + view->copyUsage, name, bytes);
	view->copyUsage += bytes;
	view->copyCount ++;
}
#endif

static int FSProcessMenu(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view;
	int    row;
	/* move/copy file across directories using Ctrl+C/V/X */
	SIT_Widget focus = SIT_GetFocus();
	if (focus)
	{
		/* don't hi-jack edit box editing capabilities */
		SIT_GetValues(focus, SIT_CtrlType, &row, NULL);
		if (row == SIT_EDITBOX) return 0;
	}
	SIT_GetValues(w, SIT_UserData, &view, NULL);
	if (view->flags & FSVIEW_SAVE) return 0;
	switch ((int) cd & 0xff) {
	#ifdef FSVIEW_HASCOPY
	case 'c':
	case 'x':
		view->operation = (int) cd & 0xff;
		view->copyCount = 0;
		FSItem item;
		for (item = HEAD(view->files), row = 0; item; row ++, NEXT(item))
		{
			int select;
			SIT_GetValues(view->list, SIT_RowSel(row), &select, NULL);
			/* ignore directories */
			if (select && item->type == 0) FSAddItem(view, item->name);
		}
		break;
	case 'v':
		FSPasteOperation(view);
		break;
	#endif
	case 'f':
		SIT_SetFocus(view->search);
	}

	return 1;
}

static int FSDoRenameItem(SIT_Widget w, APTR cd, APTR ud)
{
	FSItem item;
	FSView view;
	STRPTR name;
	SIT_GetValues(ud, SIT_Title, &name, SIT_UserData, &item, NULL);
	SIT_GetValues(w, SIT_UserData, &view, NULL);
	int    len = strlen(view->curDir)+1;
	STRPTR src = alloca(len + strlen(item->name) + 1);
	STRPTR dst = alloca(len + strlen(name) + 1);
	memcpy(src, view->curDir, len); AddPart(src, item->name, 1e6);
	memcpy(dst, view->curDir, len); AddPart(dst, name, 1e6);

	if (FileRename(src, dst, False))
	{
		/* not the most efficient way to update the UI, but performance doesn't matter at all at this point */
		FSItem renamed = calloc(sizeof *item + strlen(name), 1);
		*renamed = *item;
		strcpy(renamed->name, name);
		SIT_GetValues(view->list, SIT_SelectedIndex, &len, NULL);
		SIT_Widget td = SIT_ListInsertControlIntoCell(view->list, SIT_ListInsertItem(view->list, len, renamed, SITV_TDSubChild), 0);
		view->create(td, view->curDir, renamed);
		SIT_ListFinishInsertControl(view->list);
		SIT_ListDeleteRow(view->list, len+1);
		SIT_SetValues(view->list, SIT_SelectedIndex, len, NULL);
		ListInsert(&view->files, &renamed->node, &item->node);
		ListRemove(&view->files, &item->node);
		free(item);
		SIT_CloseDialog(w);
	}
	else SIT_Log(SIT_WARN, "Fail to rename '%s' into '%s': %s", item->name, name, GetError());
	return 1;
}

static int FSMakeDir(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view;
	STRPTR name;
	SIT_GetValues(ud, SIT_Title, &name, NULL);
	SIT_GetValues(w, SIT_UserData, &view, NULL);

	int    len = strlen(view->curDir)+1;
	STRPTR dir = alloca(len + strlen(name) + 1);
	memcpy(dir, view->curDir, len);
	AddPart(dir, name, 1e6);

	if (CreatePath(dir, False) == NULL)
	{
		FSItem ins;
		int    row;
		for (row = 0, ins = HEAD(view->files); ins && ins->type == 1 && strcasecmp(ins->name, name) < 0; NEXT(ins), row ++);

		FSItem item = calloc(sizeof *item + strlen(name), 1);
		item->type = 1;
		strcpy(item->name, name);
		SIT_Widget td = SIT_ListInsertControlIntoCell(view->list, SIT_ListInsertItem(view->list, row, item, SITV_TDSubChild), 0);
		view->create(td, view->curDir, item);
		SIT_ListFinishInsertControl(view->list);
		SIT_SetValues(view->list, SIT_SelectedIndex, row, NULL);
		if (ins) ListInsert(&view->files, &item->node, ins->node.ln_Prev);
		else     ListAddHead(&view->files, &item->node);
		SIT_CloseDialog(w);
	}
	else SIT_Log(SIT_WARN, "Fail to create '%s': %s", name, GetError());

	return 1;
}

/* ask for a new name first */
static int FSRenameItem(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget ask = SIT_CreateWidget("ask.bg", SIT_DIALOG, w,
		SIT_DialogStyles, SITV_Plain | SITV_Modal | SITV_Movable,
		#ifdef FSVIEW_CAPTUREESC
		SIT_AccelTable,   EOT(accels) - 2,
		#endif
		NULL
	);

	FSView view = ud;
	FSItem item;
	APTR   cb;
	int    row;
	SIT_GetValues(w, SIT_UserData, &item, NULL);
	if (item == NULL)
	{
		SIT_GetValues(view->list, SIT_SelectedIndex, &row, NULL);
		SIT_GetValues(view->list, SIT_RowTag(row), &item, NULL);
		cb = FSDoRenameItem;
	}
	else
	{
		item = alloca(sizeof *item);
		item->type = 1;
		item->name[0] = 0;
		cb = FSMakeDir;
	}

	SIT_CreateWidgets(ask,
		"<label name=label title=", item->type == 0 ? "Enter a new name for this file:" :
			"Enter a new name for this directory:", ">"
		"<editbox name=fname top=WIDGET,label,0.5em width=20em editLength=128 title=", item->name, "userData=", item, ">"
		"<button name=ok title=Rename top=WIDGET,fname,0.5em buttonType=", SITV_DefaultButton, "userData=", view, ">"
		"<button name=ko title=Cancel top=OPPOSITE,ok right=FORM buttonType=", SITV_CancelButton, ">"
	);
	SIT_SetAttributes(ask, "<ok right=WIDGET,ko,0.5em>");
	SIT_Widget edit = SIT_GetById(ask, "fname");
	SIT_AddCallback(SIT_GetById(ask, "ok"), SITE_OnActivate, cb, edit);

	SIT_ManageWidget(ask);
	SIT_SetFocus(edit);
	return 1;
}

/* delete the whole content of a directory */
static void FSDeleteDir(STRPTR buffer, int max)
{
	/* simulate recursivity */
	FStack dirs = alloca(sizeof *dirs);
	dirs->prev = NULL;
	dirs->next = NULL;
	if (ScanDirInit(&dirs->args, buffer))
	{
		int len = strlen(buffer);
		for (;;)
		{
			AddPart(buffer, dirs->args.name, sizeof buffer);
			if (! DeleteDOS(buffer) && dirs->args.isDir)
			{
				/* can't delete a non-empty directory */
				if (dirs->next == NULL)
				{
					FStack top = alloca(sizeof *top);
					top->prev = dirs;
					top->next = NULL;
					dirs->next = top;
					dirs = top;
				}
				else dirs = dirs->next;
				ScanDirInit(&dirs->args, buffer);
				len = strlen(buffer);
				continue;
			}
			buffer[len] = 0;
			while (! ScanDirNext(&dirs->args))
			{
				/* now directory should be empty */
				DeleteDOS(buffer);
				if (dirs->prev == NULL) return;
				len = ParentDir(buffer);
				dirs = dirs->prev;
			}
		}
	}
	else DeleteDOS(buffer); /* empty dir */
}

/* user pressed 'Ok' on confirmation dialog */
static int FSDoDeleteItems(SIT_Widget w, APTR cd, APTR ud)
{
	FSItem item, prev;
	FSView view = ud;
	TEXT   buffer[512];
	int    row, len;
	SIT_CloseDialog(w);
	SIT_GetValues(view->list, SIT_ItemCount, &row, NULL);
	CopyString(buffer, view->curDir, sizeof buffer);

	for (item = prev = TAIL(view->files), len = strlen(buffer), row --; item; item = prev, row --)
	{
		PREV(prev);
		if (item->todel == 0) continue;

		if (! AddPart(buffer, item->name, sizeof buffer))
			continue;

		if (item->type == 1) FSDeleteDir(buffer, sizeof buffer);
		else DeleteDOS(buffer);
		buffer[len] = 0;

		SIT_ListDeleteRow(view->list, row);
		ListRemove(&view->files, &item->node);
		free(item);
	}
	return 1;
}

/* ask confirmation before deleting anything */
static int FSDeleteItem(SIT_Widget w, APTR cd, APTR ud)
{
	ScanDirData args;
	FSView view = ud;
	FSItem item;
	STRPTR fname;
	int    row;
	TEXT   warn[256];
	int    nbDir = 0, nbFiles = 0, size = 0;

	if (view->flags & FSVIEW_MULTISEL)
	{
		int i;
		for (item = HEAD(view->files), i = 0; item; NEXT(item), i ++)
		{
			SIT_GetValues(view->list, SIT_RowSel(i), &row, NULL);
			item->todel = row;
			if (row == 0) continue;
			if (item->type == 1) nbDir ++;
			else nbFiles ++, size += item->size;
		}

		if (nbFiles + nbDir > 1)
		{
			row = snprintf(warn, sizeof warn, "Are you sure you want to <u>permanently</u> delete ");
			goto list_objects;
		}
		/* else single item selected */
	}
	SIT_GetValues(view->list, SIT_SelectedIndex, &row, NULL);
	SIT_GetValues(view->list, SIT_RowTag(row), &item, NULL);

	fname = alloca(strlen(view->curDir) + strlen(item->name) + 2);
	strcpy(fname, view->curDir);
	AddPart(fname, item->name, 1e6);
	item->todel = 1;

	if (item->type == 1)
	{
		/* directory: check what's inside first */
		nbDir = nbFiles = 0;
		if (ScanDirInit(&args, fname))
		{
			do
			{
				/* process args */
				if (args.isDir) nbDir ++;
				else nbFiles ++, size += args.size;
			}
			while (ScanDirNext(&args));
		}
		if (nbDir == 0 && nbFiles == 0)
		{
			/* directory is empty: don't bother poping a dialog */
			FSDoDeleteItems(w, NULL, view);
			return 1;
		}
		row = snprintf(warn, sizeof warn,
			"<b>Are you sure you want to <u>permanently</u> delete the directory '%s' ?</b><br><br>"
			"It contains ", item->name
		);
		list_objects:
		if (nbDir > 0)
		{
			TEXT msg[16];
			if (nbDir > 1) sprintf(msg, "%d directories", nbDir);
			else strcpy(msg, "1 directory");
			row = StrCat(warn, sizeof warn, row, msg);
		}
		if (nbFiles > 0)
		{
			if (nbDir > 0) row = StrCat(warn, sizeof warn, row, " and ");
			TEXT msg[32];
			TEXT sizebuf[10];
			FormatNumber(sizebuf, sizeof sizebuf, "%d", (size + 1023) >> 10);
			sprintf(msg, "%d %s (%s Kb)", nbFiles, nbFiles > 1 ? "files" : "file", sizebuf);
			row = StrCat(warn, sizeof warn, row, msg);
		}
	}
	else snprintf(warn, sizeof warn, "<b>Are you sure you want to <u>permanently</u> delete the file '%s' ?</b>", item->name);

	FSYesNo(view, warn, FSDoDeleteItems, True);
	return 1;
}

static int FSChooseName(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_CloseDialog(w);
	FSSaveAs(NULL, NULL, ud);
	return 1;
}

/* check if file exists */
static int FSSaveAs(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud;
	STRPTR name, full;
	int    max;
	SIT_GetValues(view->save, SIT_Title, &name, NULL);

	max = strlen(view->curDir) + strlen(name) + 2;
	if (view->ext) max += strlen(view->ext) + 1;
	CopyString(full = alloca(max), view->curDir, max);
	AddPart(full, name, max);

	/* clear invalid characters */
	SafePath(name = BaseName(full));

	/* empty filename: not allowed */
	if (name[0] == 0) return 1;

	if (view->ext)
	{
		/* add extension if it is missing: need to do this before checking file overwrite */
		STRPTR ext = strrchr(name, '.');
		if (ext == NULL || strcasecmp(ext+1, view->ext))
		{
			ext = view->ext; ext[-1] = '.';
			StrCat(name, max, 0, ext - 1); ext[-1] = 0;
		}
	}

	if (IsDir(full))
	{
		FSYesNo(view, "A directory with the same name already exists: use a different name.", NULL, False);
	}
	else if (w && FileExists(full)) /* w == NULL means user confirmed overwrite */
	{
		static TEXT format[] = "Are you sure you want to overwrite the file '%s' ?";

		max = strlen(format) + strlen(name);
		sprintf(full = alloca(max), format, name);

		FSYesNo(view, full, FSChooseName, True);
	}
	else view->notify(w, view, full);

	return 1;
}

static int FSFinalize(SIT_Widget w, APTR cd, APTR ud)
{
	FSView view = ud; /* allocated with SIT_DIALOG, no need to free() */
	FSItem item, next;
	for (item = next = HEAD(view->files); item; NEXT(next), free(item), item = next);
	free(view->copyFiles);
	return 1;
}

SIT_Widget FSInit(SIT_Widget app, STRPTR path, int options, SIT_CallProc create, SIT_CallProc select)
{
	FSView view;
	/* these callbacks are not optional: this module is useless without it */
	if (create == NULL || select == NULL) return NULL;
	if (IsRelativePath(path))
	{
		/* convert to full path */
		STRPTR full, cwd;
		SIT_GetValues(app, SIT_CurrentDir, &cwd, NULL);
		full = alloca(strlen(cwd) + strlen(path) + 2);
		strcpy(full, cwd);
		AddPart(full, path, 1e6);
		path = full;
	}

	if (strlen(path) >= sizeof view->curDir - 1)
	{
		SIT_Log(SIT_WARN, "Path is too long (%d characters, max: %d), sorry about that :-/\n",
			strlen(path), sizeof view->curDir - 1);
		return NULL;
	}

	SIT_Widget diag = SIT_CreateWidget("fsview.bg", SIT_DIALOG + SIT_EXTRA(sizeof (struct FSView_t)), app,
		SIT_DialogStyles, SITV_Plain,
		SIT_AccelTable,   accels,
		SIT_Right,        SITV_AttachForm, NULL, SITV_Em(2),
		SIT_Top,          SITV_AttachForm, NULL, SITV_Em(2),
		SIT_Left,         SITV_AttachForm, NULL, SITV_Em(2),
		SIT_Bottom,       SITV_AttachForm, NULL, SITV_Em(4),
		NULL
	);
	SIT_GetValues(diag, SIT_UserData, &view, NULL);
	SIT_Widget w;
	if (options & FSVIEW_SAVE)
	{
		SIT_CreateWidgets(diag,
			"<button name=label.save title='Save as:' buttonType=", SITV_DefaultButton, ">"
			"<editbox name=filter.save editLength=128 maxUndo=256 width=16em left=WIDGET,label,0.5em>"
			"<label name=in title=in: style='font-weight: bold' left=WIDGET,filter,0.5em top=MIDDLE,filter>"
		);
		options &= ~(FSVIEW_HASRENAME | FSVIEW_HASDELETE | FSVIEW_MULTISEL);
		view->save = SIT_GetById(diag, "filter");
		SIT_AddCallback(SIT_GetById(diag, "label"), SITE_OnActivate, FSSaveAs, view);
	}
	else
	{
		w = SIT_CreateWidget("select", SIT_BUTTON, diag,
			SIT_Title,   "Use",
			SIT_Enabled, False,
			NULL
		);
		if (options & FSVIEW_HASRENAME)
		{
			w = SIT_CreateWidget("rename", SIT_BUTTON, diag,
				SIT_Title,   "Rename",
				SIT_Enabled, False,
				SIT_Left,    SITV_AttachWidget, w, SITV_Em(0.5),
				NULL
			);
		}
		if (options & FSVIEW_HASDELETE)
		{
			w = SIT_CreateWidget("ko", SIT_BUTTON, diag,
				SIT_Title,   "Delete",
				SIT_Left,    SITV_AttachWidget, w, SITV_Em(0.5),
				SIT_Enabled, False,
				NULL
			);
		}

		SIT_CreateWidgets(diag,
			"<label name=label title=Find: left=", SITV_AttachWidget, w, SITV_Em(1.5), ">"
			"<editbox name=filter editLength=32 maxUndo=128 width=8em left=WIDGET,label,0.3em>"
		);
	}
	if (options & FSVIEW_HASMAKEDIR)
	{
		w = SIT_CreateWidget("newdir", SIT_BUTTON, diag,
			SIT_Title,    "<pchar src=newdir.png>",
			SIT_UserData, 1,
			NULL
		);
	}
	else w = NULL;
	CopyString(view->curDir, path, sizeof view->curDir);
	/* extension to add if it is missing */
	view->ext = strchr(view->curDir, '\t');
	if (view->ext) *view->ext++ = 0;
	SIT_CreateWidgets(diag,
		"<button name=parent title=", "<pchar src=parent.png>", ">"
		"<button name=exit title=X right=FORM buttonType=", SITV_CancelButton, ">"
		"<label name=dir overflow=", SITV_EllipsisLeft, "title=", view->curDir, "left=WIDGET,filter,0.5em right=WIDGET,parent,0.5em"
		" top=MIDDLE,filter style='white-space: nowrap'>"
		"<listbox columnNames=X name=filesys left=FORM right=FORM viewMode=", SITV_ListViewIcon, "listBoxFlags=", (options & FSVIEW_MULTISEL ? SITV_SelectMultiple : 0),
		" top=WIDGET,filter,0.5em bottom=FORM>"
	);
	SIT_SetAttributes(diag, "<label top=MIDDLE,filter><parent right=WIDGET,exit,0.5em><newdir right=WIDGET,parent,0.5em>");

	view->list = SIT_GetById(diag, "filesys");

	SIT_SetValues(view->list, SIT_UserData, view, NULL);

	view->flags  = options;
	view->select = SIT_GetById(diag, "select");
	view->rename = SIT_GetById(diag, "rename");
	view->del    = SIT_GetById(diag, "ko");
	view->dir    = SIT_GetById(diag, "dir");
	view->create = create;
	view->notify = select;
	view->search = SIT_GetById(diag, "filter");
	if (w) SIT_SetValues(view->dir, SIT_RightObject, w, NULL);
	FSViewScan(view, view->curDir);

	if (options & FSVIEW_SAVE)
		SIT_SetValues(view->dir, SIT_LeftObject, SIT_GetById(diag, "in"), NULL);
	else
		SIT_AddCallback(view->search, SITE_OnChange, FSFilterItems, view);

	SIT_AddCallback(diag,         SITE_OnFinalize, FSFinalize,    view);
	SIT_AddCallback(view->list,   SITE_OnChange,   FSSelectItem,  view);
	SIT_AddCallback(view->list,   SITE_OnActivate, FSUseItem,     view);
	SIT_AddCallback(view->select, SITE_OnActivate, FSUseItem,     view);
	SIT_AddCallback(view->rename, SITE_OnActivate, FSRenameItem,  view);
	SIT_AddCallback(view->del,    SITE_OnActivate, FSDeleteItem,  view);
	SIT_AddCallback(SIT_GetById(diag, "parent"), SITE_OnActivate, FSParentDir, view);
	SIT_AddCallback(SIT_GetById(diag, "newdir"), SITE_OnActivate, FSRenameItem, view);

	SIT_ManageWidget(diag);
	SIT_SetFocus(options & FSVIEW_SAVE ? view->search : view->list);

	return diag;
}
