/* src/amiga/zoom.h */
#ifndef XKCD_ZOOM_H
#define XKCD_ZOOM_H
#include "xkcd.h"
void zoom_show(const xkcd_comic_t *c);   /* blocks until Esc (or error), restores main screen */
#endif
