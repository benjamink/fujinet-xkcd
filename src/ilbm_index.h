/* src/ilbm_index.h — random access to an in-memory IFF ILBM: header, per-row index, row-range decode. */
#ifndef XKCD_ILBM_INDEX_H
#define XKCD_ILBM_INDEX_H
#include "ilbm.h"

enum { ILBMX_OK = 0, ILBMX_PARTIAL = 1, ILBMX_ERROR = -1 };

typedef struct {
    ilbm_info_t info;
    const unsigned char *body;   /* first BODY data byte, or 0 if no BODY yet */
    unsigned long body_len;      /* BODY bytes actually present in the buffer */
    unsigned long *row_off;      /* caller array: offset into body of each indexed row */
    unsigned short rows;         /* complete rows indexed */
    unsigned short src_bpr;      /* ((w + 15) / 16) * 2 */
} ilbm_doc_t;

/* row_off == 0: header only. Otherwise index up to min(h, max_rows) rows.
   Returns ILBMX_OK, ILBMX_PARTIAL (buffer ends early; d->rows complete rows), or ILBMX_ERROR. */
int  ilbm_doc_parse(ilbm_doc_t *d, const unsigned char *buf, unsigned long len,
                    unsigned long *row_off, unsigned short max_rows);

/* Decode image rows first..first+count-1 (stopping at d->rows) into destination rows dst_row0..;
   planes >= nplanes_dst are discarded, bytes at col >= bpr are not written. */
void ilbm_doc_decode_rows(const ilbm_doc_t *d, unsigned short first, unsigned short count,
                          unsigned char **planes, unsigned char nplanes_dst,
                          unsigned short bpr, unsigned short dst_row0);
#endif
