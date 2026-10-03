/* src/amiga/ui.h - main screen: custom hires screen, backdrop window, xkcd menu, buttons */
#ifndef XKCD_UI_H
#define XKCD_UI_H
#include "xkcd.h"
#include "ilbm.h"

struct Screen;
struct Window;
struct BitMap;

enum { UI_NONE, UI_PREV, UI_NEXT, UI_ZOOM, UI_FETCH_ID, UI_AUTO, UI_QUIT };

int  ui_open(void);                 /* 1 ok; opens screen+window */
void ui_close(void);
struct Screen *ui_screen(void);
struct Window *ui_window(void);
int  ui_is_pal(void);
unsigned long ui_sigmask(void);
int  ui_poll(void);                 /* drains IDCMP, returns first UI_* action */
void ui_show_comic(const xkcd_comic_t *c);     /* title bar, caption, clears image area */
void ui_status(const char *msg);               /* bottom status line */
/* auto_on: 0 = off; otherwise the refresh interval in seconds, shown as "Auto: Ns" */
void ui_set_buttons(int can_prev, int auto_on);
int  ui_image_header_cb(void *ctx, const ilbm_info_t *i, unsigned char **planes, unsigned char *np,
                        unsigned short *bpr, unsigned short *rows);   /* centres image in box, sets pens 4-15 */
/* Shared word-aligned placement: centres info's image in the box (x 0..box_w-1, rows top..top+box_h-1)
   of bm. Fills depth plane pointers, bpr and rows so the decoder never writes outside the box or the
   bitmap. Returns 0 (nothing filled) if the image cannot be placed. Used by the main view and Zoom. */
int  ui_place_image(const struct BitMap *bm, int depth, int box_w, int top, int box_h,
                    const ilbm_info_t *info, unsigned char **planes, unsigned char *np,
                    unsigned short *bpr, unsigned short *rows);
void ui_image_message(const char *msg);        /* centred text in image box ("No static image...") */
#endif
