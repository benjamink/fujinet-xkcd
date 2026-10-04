/* src/amiga/timer.c - V33-safe: CreatePort/CreateExtIO from amiga.lib

   The countdown runs against a system-clock deadline (TR_GETSYSTIME), so it stays on time while a
   dialog or Zoom leaves ticks unserviced: the next poll simply finds less time left, or none. Each
   tick is armed to land when the shown whole-second value changes. */
#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include "countdown.h"
#include "timer.h"

static struct MsgPort *port, *sys_port;
static struct timerequest *tr, *sys_tr;     /* tr: ticks (SendIO); sys_tr: TR_GETSYSTIME (DoIO) */
static int open_ok, sys_ok, pending;
static unsigned long dl_s, dl_us, cap_us;

int timer_open(void)
{
    if (!(port = CreatePort(0, 0)) || !(sys_port = CreatePort(0, 0))) { timer_close(); return 0; }
    if (!(tr = (struct timerequest *)CreateExtIO(port, sizeof *tr)) ||
        !(sys_tr = (struct timerequest *)CreateExtIO(sys_port, sizeof *sys_tr))) { timer_close(); return 0; }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0) != 0) { timer_close(); return 0; }
    open_ok = 1;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)sys_tr, 0) != 0) { timer_close(); return 0; }
    sys_ok = 1;
    return 1;
}

static void now(unsigned long *s, unsigned long *us)
{
    sys_tr->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)sys_tr);
    *s = sys_tr->tr_time.tv_secs; *us = sys_tr->tr_time.tv_micro;
}

static unsigned long left_us(void)
{
    unsigned long s, us;
    now(&s, &us);
    return countdown_left_us(dl_s, dl_us, s, us, cap_us);
}

static void arm(unsigned long us)
{
    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_secs = us / COUNTDOWN_US; tr->tr_time.tv_micro = us % COUNTDOWN_US;
    SendIO((struct IORequest *)tr);
    pending = 1;
}

void timer_start(unsigned long seconds)
{
    timer_abort();
    now(&dl_s, &dl_us);
    dl_s += seconds;
    cap_us = seconds * COUNTDOWN_US;    /* a clock set back can never show more than the interval */
    arm(countdown_wait_us(cap_us));
}
void timer_abort(void)
{
    if (!pending) return;
    if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
    WaitIO((struct IORequest *)tr);     /* only reached for a request that SendIO sent */
    pending = 0;
}
unsigned long timer_sigmask(void) { return port ? 1UL << port->mp_SigBit : 0; }
int timer_poll(void)
{
    unsigned long left;
    if (!pending || !CheckIO((struct IORequest *)tr)) return TIMER_IDLE;
    WaitIO((struct IORequest *)tr);
    pending = 0;
    if (!(left = left_us())) return TIMER_DONE;
    arm(countdown_wait_us(left));
    return TIMER_TICK;
}
unsigned short timer_secs_left(void) { return pending ? countdown_shown(left_us()) : 0; }
void timer_close(void)
{
    timer_abort();
    if (sys_ok) CloseDevice((struct IORequest *)sys_tr);
    if (open_ok) CloseDevice((struct IORequest *)tr);
    if (sys_tr) DeleteExtIO((struct IORequest *)sys_tr);
    if (tr) DeleteExtIO((struct IORequest *)tr);
    if (sys_port) DeletePort(sys_port);
    if (port) DeletePort(port);
    tr = sys_tr = 0; port = sys_port = 0; open_ok = sys_ok = 0;
}
