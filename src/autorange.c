/* src/autorange.c - portable: 59 steps of 10 s between AUTO_MIN and AUTO_MAX */
#include "autorange.h"

unsigned short auto_clamp(long s)
{
    if (s < AUTO_MIN) return AUTO_MIN;
    if (s > AUTO_MAX) return AUTO_MAX;
    return (unsigned short)(((s + 5) / 10) * 10);
}

unsigned short auto_from_pot(unsigned short pot)
{
    return (unsigned short)(AUTO_MIN + ((pot * 59UL + 0x7FFF) / 0xFFFF) * 10);
}

unsigned short auto_to_pot(unsigned short secs)
{
    secs = auto_clamp(secs);
    return (unsigned short)(((secs - AUTO_MIN) / 10) * 0xFFFFUL / 59);
}
