/* src/selector.c */
#include <stdio.h>
#include "selector.h"

void selector_main(char *out, unsigned short max, int pal)
{
    snprintf(out, max, "fmt=ilbm,bits=4,w=%d,h=%d,colors=12,base=4,par=1:2", MAIN_IMG_W, pal ? MAIN_IMG_H_PAL : MAIN_IMG_H_NTSC);
}

void selector_zoom(char *out, unsigned short max, int pal)
{
    snprintf(out, max, "fmt=ilbm,bits=4,w=640,h=%d,colors=16,base=0", pal ? 512 : 400);
}
