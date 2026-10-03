/* src/ilbm.h */
#ifndef XKCD_ILBM_H
#define XKCD_ILBM_H
typedef struct { unsigned short w, h; unsigned char planes, compression; unsigned char cmap[32][3]; unsigned char ncolors; } ilbm_info_t;
enum { ILBM_NEED_MORE = 0, ILBM_HEADER = 1, ILBM_DONE = 2, ILBM_ERROR = -1 };
typedef struct ilbm_s {
    ilbm_info_t info;
    unsigned char state, hdr[8], hdrn;
    unsigned long chunk_left;             /* bytes left in current chunk (excluding pad) */
    unsigned char pad, have_bmhd, bmhd[20], bmhdn;
    unsigned short cmapn;
    unsigned char *planes[8], nplanes_dst;
    unsigned short bpr_dst, max_rows, row, plane, col, src_bpr;
    unsigned char op_left, op_run;        /* ByteRun1: bytes still to emit; 1 = waiting for/using run value */
} ilbm_t;
void ilbm_init(ilbm_t *d);
int  ilbm_feed(ilbm_t *d, const unsigned char *p, unsigned short n, unsigned short *used);
const ilbm_info_t *ilbm_info(const ilbm_t *d);
void ilbm_set_target(ilbm_t *d, unsigned char **planes, unsigned char nplanes_dst, unsigned short bpr, unsigned short max_rows);
unsigned short ilbm_rows_done(const ilbm_t *d);
#endif
