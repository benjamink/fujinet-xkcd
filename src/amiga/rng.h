/* src/amiga/rng.h */
#ifndef XKCD_RNG_H
#define XKCD_RNG_H
void rng_seed(void);                 /* DateStamp ticks ^ VHPOSR */
unsigned long rng_next(void);        /* xorshift32 */
#endif
