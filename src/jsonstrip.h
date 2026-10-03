/* src/jsonstrip.h - streaming JSON filter: drops the string value of "transcript" and "news". */
#ifndef XKCD_JSONSTRIP_H
#define XKCD_JSONSTRIP_H
typedef struct {
    char *out;
    unsigned short cap, len;
    unsigned char in_str, esc, drop, cand, mt, mn, arm, expect_key, overflow;
    unsigned char klen;
} jsonstrip_t;
/* out must hold cap bytes; one is reserved for a terminating NUL written by jsonstrip_end. */
void jsonstrip_init(jsonstrip_t *s, char *out, unsigned short cap);
/* Feeds a chunk. Returns 0 once the output buffer has overflowed (output then incomplete). */
int  jsonstrip_feed(jsonstrip_t *s, const char *in, unsigned short n);
/* NUL-terminates and returns the output length. */
unsigned short jsonstrip_end(jsonstrip_t *s);
#endif
