/* src/zoomscroll.h — scroll arithmetic for the Zoom view and the image-buffer growth policy. */
#ifndef XKCD_ZOOMSCROLL_H
#define XKCD_ZOOMSCROLL_H
#define ZS_LINE      16
#define ZS_OVERLAP   32
#define ZS_MIN_THUMB 8
typedef struct { int img_h, view_h, top; } zs_t;
void zs_init(zs_t *z, int img_h, int view_h);
int  zs_scrollable(const zs_t *z);
int  zs_max_top(const zs_t *z);
int  zs_page(const zs_t *z);
int  zs_scroll(zs_t *z, int delta);
void zs_exposed(int applied, int view_h, int *first, int *count);
void zs_thumb(const zs_t *z, int track_h, int *y, int *h);
unsigned long bufgrow_next(unsigned long size, unsigned long need, unsigned long cap);
#endif
