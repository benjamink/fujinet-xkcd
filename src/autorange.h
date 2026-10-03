/* src/autorange.h - auto-refresh interval range and the slider (PropInfo pot) mapping */
#ifndef XKCD_AUTORANGE_H
#define XKCD_AUTORANGE_H
#define AUTO_MIN 10
#define AUTO_MAX 600
#define AUTO_DEFAULT 60
unsigned short auto_from_pot(unsigned short pot);      /* 0..0xFFFF -> 10..600 step 10 */
unsigned short auto_to_pot(unsigned short secs);
unsigned short auto_clamp(long secs);
#endif
