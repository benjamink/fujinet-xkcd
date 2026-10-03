/* src/amiga/net.h */
#ifndef XKCD_NET_H
#define XKCD_NET_H
#include "xkcd.h"
#include "ilbm.h"
#include "netmap.h"     /* NET_OK, NET_ERR_* */

unsigned char net_fetch_comic(long num, xkcd_comic_t *out);          /* num<=0 => latest */
/* Streams the translated ILBM. cb is called once with the info; it must return the target planes
   (or 0 to abort) by filling the given arrays. Returns NET_OK, or NET_ERR_PARTIAL if rows were drawn
   before a failure. */
typedef int (*net_header_cb)(void *ctx, const ilbm_info_t *info, unsigned char **planes,
                             unsigned char *nplanes, unsigned short *bpr, unsigned short *max_rows);
unsigned char net_fetch_image(const char *img_url, const char *selector, net_header_cb cb, void *ctx);
const char *net_error(unsigned char err, long num);   /* user-facing text; num used for "Comic #N does not exist" */
void net_shutdown(void);
#endif
