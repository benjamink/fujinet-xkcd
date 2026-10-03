/* src/xkcd.h */
#ifndef XKCD_XKCD_H
#define XKCD_XKCD_H
#define XKCD_TITLE_MAX 96
#define XKCD_ALT_MAX   512
#define XKCD_URL_MAX   256
typedef struct {
    long num;
    char title[XKCD_TITLE_MAX];
    char alt[XKCD_ALT_MAX];
    char img[XKCD_URL_MAX];
    char date[12];
    unsigned char has_image;
} xkcd_comic_t;
void xkcd_info_url(long num, char *out, unsigned short max);
int  xkcd_parse(const char *buf, unsigned short len, xkcd_comic_t *c);
long xkcd_random_pick(unsigned long r, long latest, long avoid);
#endif
