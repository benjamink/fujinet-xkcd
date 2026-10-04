/* src/countdown.c - portable: no clock access, just the arithmetic */
#include "countdown.h"

unsigned long countdown_left_us(unsigned long dl_s, unsigned long dl_us,
                                unsigned long now_s, unsigned long now_us, unsigned long cap_us)
{
    unsigned long ds;
    if (now_s > dl_s || (now_s == dl_s && now_us >= dl_us)) return 0;
    ds = dl_s - now_s;
    if (ds > cap_us / COUNTDOWN_US + 1) return cap_us;      /* also keeps ds * 10^6 from overflowing */
    ds = ds * COUNTDOWN_US + dl_us - now_us;                /* unsigned wrap cancels when now_us > dl_us */
    return ds > cap_us ? cap_us : ds;
}

unsigned short countdown_shown(unsigned long left_us)
{
    return (unsigned short)((left_us + COUNTDOWN_US - 1) / COUNTDOWN_US);
}

unsigned long countdown_wait_us(unsigned long left_us)
{
    unsigned long r = left_us % COUNTDOWN_US;
    return r ? r : (left_us ? COUNTDOWN_US : 0);
}
