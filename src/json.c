/* src/json.c — flat-object extractor for xkcd info.0.json; matches "key" only as a whole key */
#include <string.h>
#include "json.h"

static const char *find_value(const char *p, const char *end, const char *key)
{
    size_t kl = strlen(key);
    while (p < end) {
        if (*p == '"' && (size_t)(end - p) > kl + 1 && memcmp(p + 1, key, kl) == 0 && p[kl + 1] == '"') {
            const char *q = p + kl + 2;
            while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) ++q;
            if (q < end && *q == ':') {
                ++q;
                while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) ++q;
                return q;
            }
        }
        if (*p == '"') {                       /* skip over any string so we never match inside values */
            ++p;
            while (p < end && *p != '"') { if (*p == '\\') ++p; ++p; }
        }
        ++p;
    }
    return 0;
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static char map_cp(unsigned cp)
{
    if (cp >= 0x80 && cp <= 0x9F) return '?';      /* C1 controls */
    if (cp < 0x100) return (char)cp;
    switch (cp) {
    case 0x2018: case 0x2019: case 0x2032: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: return '-';
    case 0x2026: return '.';
    default: return '?';
    }
}

int json_get_string(const char *buf, const char *end, const char *key, char *out, unsigned short max)
{
    const char *p = find_value(buf, end, key);
    unsigned short n = 0;
    if (!p || p >= end || *p != '"' || max == 0) return 0;
    ++p;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char e = *p++;
            switch (e) {
            case 'n': case 'r': case 't': c = ' '; break;
            case 'u': {
                unsigned cp = 0; int i, v;
                for (i = 0; i < 4 && p < end && (v = hexv(*p)) >= 0; ++i, ++p) cp = (cp << 4) | (unsigned)v;
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end && p[0] == '\\' && p[1] == 'u') p += 6; /* drop low surrogate */
                c = (cp >= 0xD800 && cp <= 0xDFFF) ? '?' : map_cp(cp);
                break; }
            default: c = e; break;               /* \" \\ \/ */
            }
        }
        if (n + 1 < max) out[n++] = c;
    }
    out[n] = 0;
    return 1;
}

int json_get_long(const char *buf, const char *end, const char *key, long *out)
{
    const char *p = find_value(buf, end, key);
    long v = 0; int any = 0, quoted = 0;
    if (!p) return 0;
    if (*p == '"') { quoted = 1; ++p; }
    while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); any = 1; }
    if (quoted && (p >= end || *p != '"')) return 0;
    if (!any) return 0;
    *out = v;
    return 1;
}
