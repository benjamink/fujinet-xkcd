/* src/amiga/timer.c - V33-safe: CreatePort/CreateExtIO from amiga.lib */
#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include "timer.h"

static struct MsgPort *port;
static struct timerequest *tr;
static int open_ok, pending;

int timer_open(void)
{
    if (!(port = CreatePort(0, 0))) return 0;
    if (!(tr = (struct timerequest *)CreateExtIO(port, sizeof *tr))) { timer_close(); return 0; }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0) != 0) { timer_close(); return 0; }
    open_ok = 1;
    return 1;
}
void timer_start(unsigned long seconds)
{
    timer_abort();
    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_secs = seconds; tr->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)tr);
    pending = 1;
}
void timer_abort(void)
{
    if (!pending) return;
    if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
    WaitIO((struct IORequest *)tr);     /* only reached for a request that SendIO sent */
    pending = 0;
}
unsigned long timer_sigmask(void) { return port ? 1UL << port->mp_SigBit : 0; }
int timer_fired(void)
{
    if (!pending || !CheckIO((struct IORequest *)tr)) return 0;
    WaitIO((struct IORequest *)tr);
    pending = 0;
    return 1;
}
void timer_close(void)
{
    timer_abort();
    if (open_ok) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteExtIO((struct IORequest *)tr);
    if (port) DeletePort(port);
    tr = 0; port = 0; open_ok = 0;
}
