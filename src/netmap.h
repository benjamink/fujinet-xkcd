/* src/netmap.h - portable error mapping for the streamed-image fetch. */
#ifndef XKCD_NETMAP_H
#define XKCD_NETMAP_H
enum { NET_OK = 0, NET_ERR_NODEVICE = 0x80, NET_ERR_NOTFOUND, NET_ERR_PARSE, NET_ERR_HTTP,
       NET_ERR_TOOBIG, NET_ERR_CONVERT, NET_ERR_PARTIAL, NET_ERR_NOMEM, NET_ERR_NOIMAGE };

/* fujinet-nio-lib passes the firmware status byte through unchanged
   (StatusCode::InvalidRequest = 2, ::Unsupported = 8), so these equal FN_ERR_INVALID / FN_ERR_UNSUPPORTED.
   net.c asserts that at compile time. */
#define NETMAP_FN_OK          0x00
#define NETMAP_FN_INVALID     0x02
#define NETMAP_FN_UNSUPPORTED 0x08

/* Maps the end state of a streamed image fetch to a NET_* code (or a raw fn error that has no mapping).
   fn_err      last fn_read result (NETMAP_FN_OK if the stream ended cleanly / EOF)
   ilbm_r      last ilbm_feed result (ILBM_*)
   header_fail non-zero if the header callback returned 0
   rows_done   rows decoded so far; h = image height from the header (0 if no header seen) */
unsigned char netmap_image(unsigned char fn_err, int ilbm_r, int header_fail,
                           unsigned short rows_done, unsigned short h);

/* Maps a failure of the open call itself (before any read). The app's selectors are fixed and valid, so
   FN_ERR_INVALID here means the firmware does not know translation type 4: NET_ERR_NOIMAGE.
   The selectors are fixed and valid, and the img URLs come from the xkcd JSON, which in practice is always
   https://imgs.xkcd.com/..., so no other cause of InvalidRequest at Open is expected.
   Other errors map as in netmap_image with nothing read. (INVALID from a *read* stays NET_ERR_CONVERT.) */
unsigned char netmap_open_error(unsigned char fn_err);
#endif
