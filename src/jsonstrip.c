/* src/jsonstrip.c - see jsonstrip.h. Keys are kept; only the string value of the
 * "transcript" and "news" keys is replaced by "" so the output stays valid JSON. */
#include "jsonstrip.h"

static const char K_T[] = "transcript";
static const char K_N[] = "news";

void jsonstrip_init(jsonstrip_t *s, char *out, unsigned short cap)
{
    s->out = out; s->cap = cap; s->len = 0;
    s->in_str = s->esc = s->drop = s->cand = s->mt = s->mn = s->arm = s->overflow = 0;
    s->expect_key = 1; s->klen = 0;
}

static void put(jsonstrip_t *s, char c)
{
    if (s->len + 1 < s->cap) s->out[s->len++] = c;
    else s->overflow = 1;
}

static void key_char(jsonstrip_t *s, char c)
{
    if (s->mt && (s->klen >= sizeof K_T - 1 || K_T[s->klen] != c)) s->mt = 0;
    if (s->mn && (s->klen >= sizeof K_N - 1 || K_N[s->klen] != c)) s->mn = 0;
    if (s->klen < 255) s->klen++;
}

int jsonstrip_feed(jsonstrip_t *s, const char *in, unsigned short n)
{
    unsigned short i;
    for (i = 0; i < n; ++i) {
        char c = in[i];
        if (s->drop) {                              /* inside a discarded value */
            if (s->esc) s->esc = 0;
            else if (c == '\\') s->esc = 1;
            else if (c == '"') { s->drop = 0; put(s, '"'); }
            continue;
        }
        if (s->in_str) {
            put(s, c);
            if (s->esc) { s->esc = 0; if (s->cand) s->mt = s->mn = 0; }
            else if (c == '\\') s->esc = 1;
            else if (c == '"') {
                s->in_str = 0;
                if (s->cand && ((s->mt && s->klen == sizeof K_T - 1) || (s->mn && s->klen == sizeof K_N - 1)))
                    s->arm = 1;
            } else if (s->cand) key_char(s, c);
            continue;
        }
        put(s, c);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '"') {
            if (s->arm == 2) {                      /* value of a stripped key: emit "" and skip the rest */
                s->drop = 1; s->arm = 0; s->esc = 0;
                continue;
            }
            s->arm = 0;
            s->in_str = 1; s->esc = 0;
            s->cand = s->expect_key; s->mt = s->mn = 1; s->klen = 0;
            s->expect_key = 0;
            continue;
        }
        if (c == ':' && s->arm == 1) { s->arm = 2; continue; }
        s->arm = 0;
        if (c == '{' || c == ',') s->expect_key = 1;
        else s->expect_key = 0;
    }
    return !s->overflow;
}

unsigned short jsonstrip_end(jsonstrip_t *s)
{
    if (s->cap) s->out[s->len < s->cap ? s->len : s->cap - 1] = 0;
    return s->len;
}
