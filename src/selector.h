/* src/selector.h */
#ifndef XKCD_SELECTOR_H
#define XKCD_SELECTOR_H
#define MAIN_IMG_W      624
#define MAIN_IMG_H_PAL  150
#define MAIN_IMG_H_NTSC 110
void selector_main(char *out, unsigned short max, int pal);
void selector_zoom(char *out, unsigned short max, int pal);
#endif
