/* src/history.c — linear array, oldest at [0]; pos indexes the comic on screen */
#include <string.h>
#include "history.h"

void history_init(history_t *h) { memset(h, 0, sizeof *h); }

void history_push(history_t *h, long id)
{
    if (h->count) h->count = (unsigned char)(h->pos + 1);   /* forget forward entries */
    if (h->count == HISTORY_MAX) {
        memmove(h->ids, h->ids + 1, (HISTORY_MAX - 1) * sizeof h->ids[0]);
        --h->count;
    }
    h->ids[h->count] = id;
    h->pos = h->count++;
}

int history_back(history_t *h, long *id)
{
    if (!h->count || h->pos == 0) return 0;
    *id = h->ids[--h->pos];
    return 1;
}

int history_forward(history_t *h, long *id)
{
    if (!h->count || h->pos + 1 >= h->count) return 0;
    *id = h->ids[++h->pos];
    return 1;
}

long history_current(const history_t *h) { return h->count ? h->ids[h->pos] : 0; }
