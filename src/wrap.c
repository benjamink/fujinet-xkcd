/* src/wrap.c */
#include <string.h>
#include "wrap.h"

int wrap_text(const char *s, unsigned char cols, char lines[][81], int max_lines)
{
    int n = 0;
    if (cols > 80) cols = 80;
    while (*s && n < max_lines) {
        size_t len = strlen(s), take, cut;
        while (*s == ' ') { ++s; --len; }
        if (!*s) break;
        if (len <= cols) take = len;
        else {
            cut = cols;
            while (cut > 0 && s[cut] != ' ') --cut;
            take = cut ? cut : cols;                     /* hard split a word longer than a line */
        }
        memcpy(lines[n], s, take);
        while (take && lines[n][take - 1] == ' ') --take;
        lines[n][take] = 0;
        s += take;
        ++n;
    }
    return n;
}
