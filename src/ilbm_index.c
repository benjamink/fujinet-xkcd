/* src/ilbm_index.c */
#include <string.h>
#include "ilbm_index.h"

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

static int parse_header(ilbm_doc_t *d, const unsigned char *buf, unsigned long len)
{
    unsigned long pos = 12;
    int have_bmhd = 0;

    memset(d, 0, sizeof *d);
    if (len < 12 || memcmp(buf, "FORM", 4) != 0 || memcmp(buf + 8, "ILBM", 4) != 0)
        return ILBMX_ERROR;
    while (len - pos >= 8) {
        const unsigned char *ck = buf + pos;
        unsigned long clen = be32(ck + 4);
        unsigned long data = pos + 8;
        if (memcmp(ck, "BMHD", 4) == 0) {
            const unsigned char *b = buf + data;
            if (clen != 20 || len - data < 20) return ILBMX_ERROR;
            d->info.w = (unsigned short)((b[0] << 8) | b[1]);
            d->info.h = (unsigned short)((b[2] << 8) | b[3]);
            d->info.planes = b[8];
            d->info.compression = b[10];
            if (!d->info.w || !d->info.h || !d->info.planes || d->info.planes > 8 || d->info.compression > 1)
                return ILBMX_ERROR;
            have_bmhd = 1;
        } else if (memcmp(ck, "CMAP", 4) == 0) {
            unsigned long n = clen, i;
            if (n > 96) n = 96;
            if (n > len - data) n = len - data;
            for (i = 0; i < n; ++i) d->info.cmap[i / 3][i % 3] = buf[data + i];
            d->info.ncolors = (unsigned char)(n / 3);
        } else if (memcmp(ck, "BODY", 4) == 0) {
            if (!have_bmhd) return ILBMX_ERROR;
            d->body = buf + data;
            d->body_len = len - data;
            if (d->body_len > clen) d->body_len = clen;
            break;
        }
        if (clen > len - data) break;       /* truncated or bogus length: stop walking (no wrap) */
        pos = data + clen;
        if ((clen & 1) && pos < len) ++pos;
    }
    if (!have_bmhd) return ILBMX_ERROR;
    d->src_bpr = (unsigned short)(((d->info.w + 15) / 16) * 2);
    return ILBMX_OK;
}

/* Skip one plane row starting at *pos. Returns 1 complete, 0 buffer ends first, -1 malformed. */
static int skip_plane_row(const ilbm_doc_t *d, unsigned long *pos)
{
    unsigned long p = *pos;
    unsigned short out = 0;

    if (d->info.compression == 0) {
        if (p + d->src_bpr > d->body_len) return 0;
        *pos = p + d->src_bpr;
        return 1;
    }
    while (out < d->src_bpr) {
        signed char c;
        if (p >= d->body_len) return 0;
        c = (signed char)d->body[p++];
        if (c >= 0) {
            unsigned short n = (unsigned short)(c + 1);
            if (out + n > d->src_bpr) return -1;
            if (p + n > d->body_len) return 0;
            p += n;
            out = (unsigned short)(out + n);
        } else if (c != -128) {
            unsigned short n = (unsigned short)(1 - c);
            if (out + n > d->src_bpr) return -1;
            if (p >= d->body_len) return 0;
            ++p;
            out = (unsigned short)(out + n);
        }
    }
    *pos = p;
    return 1;
}

int ilbm_doc_parse(ilbm_doc_t *d, const unsigned char *buf, unsigned long len,
                   unsigned long *row_off, unsigned short max_rows)
{
    unsigned long pos = 0;
    unsigned short want, r;
    unsigned char p;

    if (parse_header(d, buf, len) != ILBMX_OK) return ILBMX_ERROR;
    if (!row_off) return ILBMX_OK;
    d->row_off = row_off;
    want = d->info.h < max_rows ? d->info.h : max_rows;
    if (!d->body) return want ? ILBMX_PARTIAL : ILBMX_OK;
    for (r = 0; r < want; ++r) {
        unsigned long start = pos;
        for (p = 0; p < d->info.planes; ++p) {
            int k = skip_plane_row(d, &pos);
            if (k < 0) return ILBMX_ERROR;
            if (k == 0) return ILBMX_PARTIAL;
        }
        row_off[r] = start;
        d->rows = (unsigned short)(r + 1);
    }
    return ILBMX_OK;
}

static void decode_plane_row(const ilbm_doc_t *d, unsigned long *pos, unsigned char *dst, unsigned short bpr)
{
    unsigned long p = *pos;
    unsigned short out = 0;

    if (d->info.compression == 0) {
        for (; out < d->src_bpr; ++out, ++p)
            if (dst && out < bpr) dst[out] = d->body[p];
        *pos = p;
        return;
    }
    while (out < d->src_bpr) {
        signed char c = (signed char)d->body[p++];
        if (c >= 0) {
            unsigned short n = (unsigned short)(c + 1);
            for (; n; --n, ++p, ++out)
                if (dst && out < bpr) dst[out] = d->body[p];
        } else if (c != -128) {
            unsigned short n = (unsigned short)(1 - c);
            unsigned char v = d->body[p++];
            for (; n; --n, ++out)
                if (dst && out < bpr) dst[out] = v;
        }
    }
    *pos = p;
}

void ilbm_doc_decode_rows(const ilbm_doc_t *d, unsigned short first, unsigned short count,
                          unsigned char **planes, unsigned char nplanes_dst,
                          unsigned short bpr, unsigned short dst_row0)
{
    unsigned short i;
    unsigned char p;

    for (i = 0; i < count; ++i) {
        unsigned short r = (unsigned short)(first + i);
        unsigned long pos;
        if (r >= d->rows) return;
        pos = d->row_off[r];
        for (p = 0; p < d->info.planes; ++p) {
            unsigned char *dst = p < nplanes_dst
                ? planes[p] + (unsigned long)(dst_row0 + i) * bpr
                : 0;
            decode_plane_row(d, &pos, dst, bpr);
        }
    }
}
