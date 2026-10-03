/* src/amiga/net.c - FujiNet NIO access: xkcd JSON and streamed, translated ILBM. */
#include <stdio.h>
#include <proto/dos.h>
#include "fujinet-nio.h"
#include "net.h"
#include "jsonstrip.h"

#define JSON_WAITS  750         /* 15 s at Delay(1) = 1/50 s */
#define IMAGE_WAITS 3000        /* 60 s: FujiNet buffers the whole image, then converts before the first read */

/* The lib hands the firmware status byte through unchanged; netmap.c relies on it. */
typedef char assert_invalid_code[(FN_ERR_INVALID == NETMAP_FN_INVALID) ? 1 : -1];
typedef char assert_unsupported_code[(FN_ERR_UNSUPPORTED == NETMAP_FN_UNSUPPORTED) ? 1 : -1];

static unsigned char up;

static unsigned char ensure_up(void)
{
    unsigned char e;
    if (up) return FN_OK;
    e = fn_init();
    if (e == FN_ERR_NOT_FOUND) return NET_ERR_NODEVICE;
    if (e != FN_OK) return e;
    up = 1;
    return FN_OK;
}

/* Transport-level failures drop the session so the next call re-runs fn_init. */
static void note_fn_error(unsigned char e)
{
    if (e == FN_ERR_TRANSPORT || e == FN_ERR_IO || e == FN_ERR_TIMEOUT) net_shutdown();
}

static unsigned char finish(unsigned char e)
{
    note_fn_error(e);
    return e;
}

/* Reads one chunk with NOT_READY/BUSY polling. */
static unsigned char read_chunk(fn_handle_t h, uint32_t off, unsigned char *b, unsigned short max,
                                unsigned short *n, unsigned char *fl, int max_waits)
{
    int waits = 0;
    unsigned char e;
    for (;;) {
        *n = 0; *fl = 0;
        e = fn_read(h, off, b, max, n, fl);
        if (e != FN_ERR_NOT_READY && e != FN_ERR_BUSY) return e;
        if (++waits > max_waits) return FN_ERR_TIMEOUT;
        Delay(1);
    }
}

unsigned char net_fetch_comic(long num, xkcd_comic_t *out)
{
    static char buf[4096];
    static unsigned char raw[512];
    char url[64];
    fn_handle_t h;
    unsigned short n, total = 0, status = 0, want;
    uint32_t len = 0, off = 0;
    unsigned char fl, e;
    jsonstrip_t js;

    if ((e = ensure_up()) != FN_OK) return e;
    xkcd_info_url(num, url, sizeof url);
    if ((e = fn_open(&h, FN_METHOD_GET, url, FN_OPEN_FOLLOW_REDIR)) != FN_OK) return finish(e);
    jsonstrip_init(&js, buf, sizeof buf);
    for (;;) {
        want = sizeof raw;
        if (want > FN_MAX_CHUNK_SIZE) want = FN_MAX_CHUNK_SIZE;   /* the lib's frame limit */
        e = read_chunk(h, off, raw, want, &n, &fl, JSON_WAITS);
        if (e != FN_OK) break;
        off += n;
        /* Drops the long "transcript"/"news" values so alt and img always fit. */
        if (!jsonstrip_feed(&js, (const char *)raw, n)) { e = NET_ERR_PARSE; break; }
        if ((fl & FN_READ_EOF) || n == 0) break;
    }
    if (e == FN_OK && fn_info(h, &status, &len, &fl) == FN_OK && (fl & FN_INFO_HAS_STATUS)) {
        if (status == 404) e = NET_ERR_NOTFOUND;
        else if (status >= 400) e = NET_ERR_HTTP;
    }
    fn_close(h);
    if (e != FN_OK) return finish(e);
    total = jsonstrip_end(&js);
    return xkcd_parse(buf, total, out) ? NET_OK : NET_ERR_PARSE;
}

unsigned char net_fetch_image(const char *img_url, const char *selector, net_header_cb cb, void *ctx)
{
    static unsigned char buf[512];
    static ilbm_t dec;
    fn_handle_t h;
    uint32_t off = 0;
    unsigned short n, used, k;
    unsigned char fl, e;
    int r = ILBM_NEED_MORE, header_fail = 0, have_header = 0;

    if ((e = ensure_up()) != FN_OK) return e;
    e = fn_open_translated(&h, FN_METHOD_GET, img_url, FN_OPEN_FOLLOW_REDIR, FN_TRANSLATE_IMAGE, 0, selector);
    if (e != FN_OK) return netmap_open_error(finish(e));
    ilbm_init(&dec);
    while (r != ILBM_DONE && r != ILBM_ERROR && !header_fail) {
        e = read_chunk(h, off, buf, sizeof buf, &n, &fl, IMAGE_WAITS);
        if (e != FN_OK) break;
        off += n;
        for (k = 0; k < n && r != ILBM_DONE && r != ILBM_ERROR; k = (unsigned short)(k + used)) {
            r = ilbm_feed(&dec, buf + k, (unsigned short)(n - k), &used);
            if (r == ILBM_HEADER) {
                unsigned char *planes[8], np = 0;
                unsigned short bpr = 0, rows = 0;
                have_header = 1;
                if (!cb(ctx, ilbm_info(&dec), planes, &np, &bpr, &rows)) { header_fail = 1; break; }
                ilbm_set_target(&dec, planes, np, bpr, rows);
            }
        }
        if ((fl & FN_READ_EOF) || n == 0) break;
    }
    fn_close(h);
    note_fn_error(e);
    return netmap_image(e, r, header_fail, ilbm_rows_done(&dec), have_header ? ilbm_info(&dec)->h : 0);
}

const char *net_error(unsigned char e, long num)
{
    static char msg[48];
    switch (e) {
    case NET_ERR_NODEVICE: return "NIO driver not loaded (run fujinet-nio first)";
    case NET_ERR_NOTFOUND: snprintf(msg, sizeof msg, "Comic #%ld does not exist", num); return msg;
    case NET_ERR_PARSE:    return "Unexpected reply from xkcd.com";
    case NET_ERR_HTTP:     return "xkcd.com returned an error";
    case NET_ERR_TOOBIG:   return "Image too large for FujiNet to convert";
    case NET_ERR_CONVERT:  return "FujiNet could not convert this image";
    case NET_ERR_PARTIAL:  return "Image transfer interrupted";
    case NET_ERR_NOIMAGE:  return "FujiNet firmware lacks image conversion (update it)";
    case NET_ERR_NOMEM:    return "Not enough chip memory";
    case FN_ERR_TRANSPORT: return "No reply from FujiNet";
    case FN_ERR_TIMEOUT:   return "FujiNet timed out";
    default:               return fn_error_string(e);
    }
}

void net_shutdown(void) { if (up) { fn_shutdown(); up = 0; } }
