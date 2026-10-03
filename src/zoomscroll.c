/* src/zoomscroll.c */
#include "zoomscroll.h"

void zs_init(zs_t *z, int img_h, int view_h)
{
    z->img_h = img_h;
    z->view_h = view_h;
    z->top = 0;
}

int zs_scrollable(const zs_t *z) { return z->img_h > z->view_h; }

int zs_max_top(const zs_t *z)
{
    int m = z->img_h - z->view_h;
    return m > 0 ? m : 0;
}

int zs_page(const zs_t *z)
{
    int p = z->view_h - ZS_OVERLAP;
    return p < ZS_LINE ? ZS_LINE : p;
}

int zs_scroll(zs_t *z, int delta)
{
    int t = z->top + delta, m = zs_max_top(z);
    if (t < 0) t = 0;
    if (t > m) t = m;
    delta = t - z->top;
    z->top = t;
    return delta;
}

void zs_exposed(int applied, int view_h, int *first, int *count)
{
    if (applied == 0) { *first = 0; *count = 0; return; }
    if (applied >= view_h || -applied >= view_h) { *first = 0; *count = view_h; return; }
    if (applied > 0) { *first = view_h - applied; *count = applied; }
    else             { *first = 0; *count = -applied; }
}

void zs_thumb(const zs_t *z, int track_h, int *y, int *h)
{
    long th;
    if (!zs_scrollable(z)) { *y = 0; *h = track_h; return; }
    th = (long)track_h * z->view_h / z->img_h;
    if (th < ZS_MIN_THUMB) th = ZS_MIN_THUMB;
    if (th > track_h) th = track_h;
    *h = (int)th;
    *y = (int)((long)(track_h - th) * z->top / zs_max_top(z));
}

unsigned long bufgrow_next(unsigned long size, unsigned long need, unsigned long cap)
{
    unsigned long n = size ? size : 1;
    if (need > cap) return 0;
    while (n < need) {
        if (n > cap / 2) return cap;   /* n*2 would exceed cap: stop, never overflow */
        n *= 2;
    }
    return n > cap ? cap : n;
}
