/* src/amiga/dialogs.h */
#ifndef XKCD_DIALOGS_H
#define XKCD_DIALOGS_H
enum { DLG_CANCEL = 0, DLG_START = 1, DLG_STOP = 2 };
#include "autorange.h"   /* AUTO_MIN, AUTO_MAX, AUTO_DEFAULT */
int dlg_fetch_id(long *id, long latest);                  /* 1 = OK with *id set, 0 = cancel */
int dlg_auto_refresh(unsigned short *secs, int running);  /* DLG_*; *secs updated only on START */
#endif
