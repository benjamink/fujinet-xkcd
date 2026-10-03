#include <proto/dos.h>
#include <hardware/custom.h>
#include "rng.h"
extern struct Custom custom;
static unsigned long s = 2463534242UL;
void rng_seed(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    s ^= ((unsigned long)ds.ds_Tick << 16) ^ (unsigned long)ds.ds_Minute ^ (unsigned long)custom.vhposr;
    if (!s) s = 2463534242UL;
}
unsigned long rng_next(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
