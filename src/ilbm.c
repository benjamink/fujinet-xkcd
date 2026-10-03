/* src/ilbm.c — byte-at-a-time state machine so any chunking works */
#include <string.h>
#include "ilbm.h"

enum { S_FORM, S_FORMTYPE, S_CHUNK_HDR, S_BMHD, S_CMAP, S_SKIP, S_PAD, S_BODY_WAIT, S_BODY, S_DONE, S_ERR };

static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

void ilbm_init(ilbm_t *d) { memset(d, 0, sizeof *d); d->state = S_FORM; }
const ilbm_info_t *ilbm_info(const ilbm_t *d) { return &d->info; }
unsigned short ilbm_rows_done(const ilbm_t *d) { return d->row; }

void ilbm_set_target(ilbm_t *d, unsigned char **planes, unsigned char n, unsigned short bpr, unsigned short max_rows)
{
    unsigned char i;
    for (i = 0; i < n && i < 8; ++i) d->planes[i] = planes[i];
    d->nplanes_dst = n; d->bpr_dst = bpr; d->max_rows = max_rows;
    d->src_bpr = (unsigned short)(((d->info.w + 15) / 16) * 2);
    d->state = S_BODY;
}

static void put(ilbm_t *d, unsigned char b)
{
    if (d->plane < d->nplanes_dst && d->row < d->max_rows && d->col < d->bpr_dst)
        d->planes[d->plane][(unsigned long)d->row * d->bpr_dst + d->col] = b;
    if (++d->col == d->src_bpr) {
        d->col = 0;
        if (++d->plane == d->info.planes) { d->plane = 0; ++d->row; }
    }
}

static void end_chunk(ilbm_t *d) { d->state = d->pad ? S_PAD : S_CHUNK_HDR; d->hdrn = 0; }

static void body_byte(ilbm_t *d, unsigned char b)
{
    if (d->info.compression == 0) { put(d, b); return; }
    if (d->op_left == 0) {                          /* control byte */
        signed char c = (signed char)b;
        if (c >= 0)        { d->op_run = 0; d->op_left = (unsigned char)(c + 1); }
        else if (c != -128) { d->op_run = 1; d->op_left = (unsigned char)(1 - c); }
        return;
    }
    if (d->op_run) { while (d->op_left) { put(d, b); --d->op_left; } }
    else           { put(d, b); --d->op_left; }
}

int ilbm_feed(ilbm_t *d, const unsigned char *p, unsigned short n, unsigned short *used)
{
    unsigned short i = 0;
    while (i < n && d->state != S_ERR && d->state != S_DONE) {
        unsigned char b = p[i];
        switch (d->state) {
        case S_FORM:                                     /* "FORM" + u32 length */
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn == 4 && memcmp(d->hdr, "FORM", 4) != 0) d->state = S_ERR;
            else if (d->hdrn == 8) { d->hdrn = 0; d->state = S_FORMTYPE; }
            break;
        case S_FORMTYPE:                                 /* "ILBM" */
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn == 4) { d->hdrn = 0; d->state = memcmp(d->hdr, "ILBM", 4) ? S_ERR : S_CHUNK_HDR; }
            break;
        case S_CHUNK_HDR:
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn < 8) break;
            d->hdrn = 0;
            d->chunk_left = be32(d->hdr + 4);
            d->pad = (unsigned char)(d->chunk_left & 1);
            if (!memcmp(d->hdr, "BMHD", 4))      { d->state = d->chunk_left == 20 ? S_BMHD : S_ERR; d->bmhdn = 0; }
            else if (!memcmp(d->hdr, "CMAP", 4)) { d->state = S_CMAP; d->cmapn = 0; if (!d->chunk_left) end_chunk(d); }
            else if (!memcmp(d->hdr, "BODY", 4)) { d->state = d->have_bmhd ? S_BODY_WAIT : S_ERR; }
            else if (d->chunk_left == 0)         end_chunk(d);
            else                                 d->state = S_SKIP;
            break;
        case S_BMHD:
            d->bmhd[d->bmhdn++] = b; ++i;
            if (d->bmhdn == 20) {
                d->info.w = (unsigned short)((d->bmhd[0] << 8) | d->bmhd[1]);
                d->info.h = (unsigned short)((d->bmhd[2] << 8) | d->bmhd[3]);
                d->info.planes = d->bmhd[8];
                d->info.compression = d->bmhd[10];
                if (!d->info.w || !d->info.h || !d->info.planes || d->info.planes > 8 || d->info.compression > 1)
                    d->state = S_ERR;
                else { d->have_bmhd = 1; end_chunk(d); }
            }
            break;
        case S_CMAP:
            if (d->cmapn < 96) d->info.cmap[d->cmapn / 3][d->cmapn % 3] = b;
            ++d->cmapn; ++i;
            if (--d->chunk_left == 0) { d->info.ncolors = (unsigned char)((d->cmapn > 96 ? 96 : d->cmapn) / 3); end_chunk(d); }
            break;
        case S_SKIP:
            ++i;
            if (--d->chunk_left == 0) end_chunk(d);
            break;
        case S_PAD:
            ++i; d->state = S_CHUNK_HDR; d->hdrn = 0;
            break;
        case S_BODY_WAIT:                                /* caller must ilbm_set_target() */
            *used = i; return ILBM_HEADER;
        case S_BODY:
            if (d->chunk_left == 0 || d->row >= d->info.h) { d->state = S_DONE; break; }
            ++i; --d->chunk_left;
            body_byte(d, b);
            if (d->row >= d->info.h) d->state = S_DONE;
            break;
        }
    }
    if (d->state == S_BODY_WAIT) { *used = i; return ILBM_HEADER; }
    if (d->state == S_BODY && (d->chunk_left == 0 || d->row >= d->info.h)) d->state = S_DONE;
    if (d->state == S_DONE) { *used = n; return ILBM_DONE; }   /* trailing bytes (pad/extra chunks) are ignored */
    *used = i;
    return d->state == S_ERR ? ILBM_ERROR : ILBM_NEED_MORE;
}
