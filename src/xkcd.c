/* src/xkcd.c */
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "xkcd.h"

void xkcd_info_url(long num, char *out, unsigned short max)
{
    if (num <= 0) snprintf(out, max, "https://xkcd.com/info.0.json");
    else snprintf(out, max, "https://xkcd.com/%ld/info.0.json", num);
}

static int ends_with_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf), i;
    if (a < b) return 0;
    for (i = 0; i < b; ++i) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (x != y) return 0;
    }
    return 1;
}

int xkcd_parse(const char *buf, unsigned short len, xkcd_comic_t *c)
{
    const char *end = buf + len;
    long y = 0, m = 0, d = 0;
    memset(c, 0, sizeof *c);
    if (!json_get_long(buf, end, "num", &c->num) || c->num <= 0) return 0;
    if (!json_get_string(buf, end, "safe_title", c->title, sizeof c->title))
        json_get_string(buf, end, "title", c->title, sizeof c->title);
    json_get_string(buf, end, "alt", c->alt, sizeof c->alt);
    json_get_string(buf, end, "img", c->img, sizeof c->img);
    if (json_get_long(buf, end, "year", &y) && json_get_long(buf, end, "month", &m) && json_get_long(buf, end, "day", &d))
        snprintf(c->date, sizeof c->date, "%04ld-%02ld-%02ld", y, m, d);
    c->has_image = (unsigned char)(strlen(c->img) < XKCD_URL_MAX - 1 &&      /* a full buffer means the URL was cut off */
                                   (ends_with_ci(c->img, ".png") || ends_with_ci(c->img, ".jpg") ||
                                   ends_with_ci(c->img, ".jpeg") || ends_with_ci(c->img, ".gif")));
    return 1;
}

long xkcd_random_pick(unsigned long r, long latest, long avoid)
{
    long p, n;
    if (latest <= 1) return 1;
    p = (long)(r % (unsigned long)latest) + 1;
    for (n = 0; n < latest; ++n, p = p % latest + 1)    /* walk forward past 404 / the current comic */
        if (p != 404 && p != avoid) return p;
    return 1;
}
