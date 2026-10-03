/* src/netmap.c */
#include "netmap.h"
#include "ilbm.h"

unsigned char netmap_image(unsigned char fn_err, int ilbm_r, int header_fail,
                           unsigned short rows_done, unsigned short h)
{
    if (header_fail) return NET_ERR_NOMEM;
    if (fn_err == NETMAP_FN_OK && ilbm_r == ILBM_DONE && h > 0 && rows_done >= h) return NET_OK;
    /* Anything else is a failure. Once pixels are on screen it is a partial image. */
    if (rows_done > 0) return NET_ERR_PARTIAL;
    if (fn_err == NETMAP_FN_UNSUPPORTED) return NET_ERR_TOOBIG;
    if (fn_err == NETMAP_FN_INVALID) return NET_ERR_CONVERT;
    if (fn_err != NETMAP_FN_OK) return fn_err;
    return NET_ERR_CONVERT;     /* EOF/garbage/short BODY with nothing drawn */
}

unsigned char netmap_open_error(unsigned char fn_err)
{
    if (fn_err == NETMAP_FN_INVALID) return NET_ERR_NOIMAGE;
    return netmap_image(fn_err, ILBM_NEED_MORE, 0, 0, 0);
}
