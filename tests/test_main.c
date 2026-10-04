/* tests/test_main.c */
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "xkcd.h"
#include "history.h"
#include "wrap.h"
#include "selector.h"
#include "autorange.h"
#include "countdown.h"
#include "jsonstrip.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
#define RUN(t) do { printf("- %s\n", #t); t(); } while (0)

static const char SAMPLE[] =
  "{\"month\": \"3\", \"num\": 353, \"link\": \"\", \"year\": \"2007\", \"news\": \"\", "
  "\"safe_title\": \"Python\", \"transcript\": \"[[ Guy 1 is talking ]]\\nGuy: You're flying!\", "
  "\"alt\": \"I wrote 20 short programs in Python yesterday.  It was wonderful.  Perl, I'm leaving you.\", "
  "\"img\": \"https://imgs.xkcd.com/comics/python.png\", \"title\": \"Python\", \"day\": \"5\"}";

static void test_json_basic(void) {
    char s[64]; long n;
    CHECK(json_get_long(SAMPLE, SAMPLE + sizeof SAMPLE - 1, "num", &n) && n == 353);
    CHECK(json_get_string(SAMPLE, SAMPLE + sizeof SAMPLE - 1, "img", s, sizeof s));
    CHECK(strcmp(s, "https://imgs.xkcd.com/comics/python.png") == 0);
    CHECK(!json_get_string(SAMPLE, SAMPLE + sizeof SAMPLE - 1, "missing", s, sizeof s));
    /* "title" must not match inside "safe_title" */
    CHECK(json_get_string(SAMPLE, SAMPLE + sizeof SAMPLE - 1, "title", s, sizeof s) && strcmp(s, "Python") == 0);
}

static void test_json_unescape(void) {
    const char j[] = "{\"alt\": \"a\\\"b\\\\c\\nd \\u2019 \\u00e9 \\u4e00\"}";
    char s[64];
    CHECK(json_get_string(j, j + sizeof j - 1, "alt", s, sizeof s));
    CHECK(strcmp(s, "a\"b\\c d ' \xe9 ?") == 0);   /* \n -> space, U+2019 -> ', U+00E9 -> Latin-1, CJK -> ? */
}

static void test_json_c1(void) {
    const char j[] = "{\"alt\": \"a\\u0085b\\u009fc\\u00a0\"}";
    char s[16];
    CHECK(json_get_string(j, j + sizeof j - 1, "alt", s, sizeof s));
    CHECK(strcmp(s, "a?b?c\xa0") == 0);
}

static void test_json_truncates(void) {
    char s[8];
    CHECK(json_get_string(SAMPLE, SAMPLE + sizeof SAMPLE - 1, "alt", s, sizeof s));
    CHECK(strlen(s) == 7);
}

static void test_xkcd_parse(void) {
    xkcd_comic_t c;
    CHECK(xkcd_parse(SAMPLE, sizeof SAMPLE - 1, &c));
    CHECK(c.num == 353 && strcmp(c.title, "Python") == 0 && c.has_image);
    CHECK(strcmp(c.date, "2007-03-05") == 0);
    CHECK(!xkcd_parse("<html>404</html>", 16, &c));
}

static void test_xkcd_parse_no_image(void) {
    const char j[] = "{\"num\": 1608, \"title\": \"Hoverboard\", \"alt\": \"x\", "
                     "\"img\": \"https://imgs.xkcd.com/comics/\", \"year\":\"2015\",\"month\":\"11\",\"day\":\"25\"}";
    xkcd_comic_t c;
    CHECK(xkcd_parse(j, sizeof j - 1, &c));
    CHECK(!c.has_image);
    {   const char k[] = "{\"num\": 1, \"title\": \"t\", \"alt\": \"a\", \"img\": \"https://imgs.xkcd.com/comics/x.html\"}";
        CHECK(xkcd_parse(k, sizeof k - 1, &c) && !c.has_image); }
}

static void test_xkcd_urls(void) {
    char u[64];
    xkcd_info_url(0, u, sizeof u);   CHECK(strcmp(u, "https://xkcd.com/info.0.json") == 0);
    xkcd_info_url(614, u, sizeof u); CHECK(strcmp(u, "https://xkcd.com/614/info.0.json") == 0);
}

static void test_random_pick_skips_404(void) {
    unsigned long r;
    for (r = 0; r < 20000; ++r) {
        long p = xkcd_random_pick(r * 2654435761UL, 3000, 353);
        CHECK(p >= 1 && p <= 3000 && p != 404 && p != 353);
        if (fails) break;
    }
    CHECK(xkcd_random_pick(12345, 1, 0) == 1);
}

static void test_history(void) {
    history_t h; long id; int i;
    history_init(&h);
    CHECK(history_current(&h) == 0 && !history_back(&h, &id));
    for (i = 1; i <= 30; ++i) history_push(&h, i);
    CHECK(h.count == HISTORY_MAX && history_current(&h) == 30);
    for (i = 0; i < 24; ++i) CHECK(history_back(&h, &id));
    CHECK(id == 6 && !history_back(&h, &id));          /* oldest kept is 30-25+1 = 6 */
    CHECK(history_forward(&h, &id) && id == 7);
    history_push(&h, 99);                              /* drops 8..30 */
    CHECK(history_current(&h) == 99 && !history_forward(&h, &id));
    CHECK(history_back(&h, &id) && id == 7);
}

static void test_wrap(void) {
    char lines[4][81];
    int n = wrap_text("I wrote 20 short programs in Python yesterday. It was wonderful.", 20, lines, 4);
    CHECK(n == 4);
    CHECK(strcmp(lines[0], "I wrote 20 short") == 0);
    CHECK(strlen(lines[1]) <= 20);
    n = wrap_text("Supercalifragilisticexpialidocious", 10, lines, 4);
    CHECK(n == 4 && strcmp(lines[0], "Supercalif") == 0);   /* hard-split long words */
}

static void test_selectors(void) {
    char s[64];
    selector_main(s, sizeof s, 1); CHECK(strcmp(s, "fmt=ilbm,bits=4,w=624,h=150,colors=12,base=4,par=1:2") == 0);
    selector_main(s, sizeof s, 0); CHECK(strcmp(s, "fmt=ilbm,bits=4,w=624,h=110,colors=12,base=4,par=1:2") == 0);
    selector_zoom(s, sizeof s, 1); CHECK(strcmp(s, "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024") == 0);
    selector_zoom(s, sizeof s, 0); CHECK(strcmp(s, "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024") == 0);
}

#include "ilbm.h"
#include "netmap.h"
#include "ilbm_index.h"
#include "zoomscroll.h"
#include <stdlib.h>

/* 16x2, 2 planes, ByteRun1. Row0 plane0 = FF FF, plane1 = 00 00; Row1 plane0 = 00 0F (literal), plane1 = F0 00 */
static const unsigned char TINY[] = {
  'F','O','R','M', 0,0,0,70, 'I','L','B','M',   /* 4 + BMHD 28 + CMAP 20 + BODY 18 = 70; file = 78 bytes */
  'B','M','H','D', 0,0,0,20, 0,16, 0,2, 0,0, 0,0, 2, 0, 1, 0, 0,0, 1,1, 0,16, 0,2,
  'C','M','A','P', 0,0,0,12, 0,0,0, 255,255,255, 255,0,0, 0,0,255,
  'B','O','D','Y', 0,0,0,10, 0xFF,0xFF, 0xFF,0x00, 0x01,0x00,0x0F, 0x01,0xF0,0x00 };

static int decode_all(const unsigned char *src, unsigned long n, unsigned short chunk, unsigned char *pl0, unsigned char *pl1, ilbm_t *d)
{
    unsigned long off = 0; unsigned short used; int r = ILBM_NEED_MORE;
    unsigned char *planes[2]; planes[0] = pl0; planes[1] = pl1;
    ilbm_init(d);
    while (off < n) {
        unsigned short k = (unsigned short)((n - off) < chunk ? (n - off) : chunk);
        r = ilbm_feed(d, src + off, k, &used);
        off += used;
        if (r == ILBM_HEADER) ilbm_set_target(d, planes, 2, 2, 2);
        else if (r != ILBM_NEED_MORE) break;
    }
    return r;
}

static void test_ilbm_tiny_any_chunking(void)
{
    unsigned short chunk;
    for (chunk = 1; chunk <= sizeof TINY; ++chunk) {
        unsigned char p0[4] = {0}, p1[4] = {0}; ilbm_t d;
        int r = decode_all(TINY, sizeof TINY, chunk, p0, p1, &d);
        CHECK(r == ILBM_DONE);
        CHECK(ilbm_info(&d)->w == 16 && ilbm_info(&d)->h == 2 && ilbm_info(&d)->planes == 2);
        CHECK(ilbm_info(&d)->ncolors == 4 && ilbm_info(&d)->cmap[2][0] == 255);
        CHECK(p0[0] == 0xFF && p0[1] == 0xFF && p0[2] == 0x00 && p0[3] == 0x0F);
        CHECK(p1[0] == 0x00 && p1[1] == 0x00 && p1[2] == 0xF0 && p1[3] == 0x00);
        if (fails) break;
    }
}

static void test_ilbm_truncated_body(void)
{
    unsigned char p0[4] = {0}, p1[4] = {0}; ilbm_t d;
    int r = decode_all(TINY, sizeof TINY - 3, 7, p0, p1, &d);
    CHECK(r == ILBM_NEED_MORE);              /* caller sees not-done; row 0 drawn */
    CHECK(ilbm_rows_done(&d) == 1);
    CHECK(p0[0] == 0xFF);
}

static void test_ilbm_rejects_garbage(void)
{
    const unsigned char bad[] = "<html><body>404</body></html>";
    unsigned short used; ilbm_t d;
    ilbm_init(&d);
    CHECK(ilbm_feed(&d, bad, sizeof bad - 1, &used) == ILBM_ERROR);
}

static void test_ilbm_fixture(void)
{
    FILE *f = fopen("tests/fixtures/python_640x400x16.iff", "rb");
    unsigned char *buf, **planes; long n; int i, r = ILBM_NEED_MORE; ilbm_t d;
    if (!f) { printf("  (fixture missing, skipped)\n"); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n); CHECK(fread(buf, 1, (size_t)n, f) == (size_t)n); fclose(f);
    planes = malloc(4 * sizeof *planes);
    for (i = 0; i < 4; ++i) planes[i] = calloc(80, 512);
    {   unsigned long off = 0; unsigned short used;
        ilbm_init(&d);
        while (off < (unsigned long)n) {
            r = ilbm_feed(&d, buf + off, (unsigned short)(((unsigned long)n - off) > 512 ? 512 : ((unsigned long)n - off)), &used);
            off += used;
            if (r == ILBM_HEADER) ilbm_set_target(&d, planes, 4, 80, 512);
            else if (r != ILBM_NEED_MORE) break;
        }
    }
    CHECK(r == ILBM_DONE);
    CHECK(ilbm_rows_done(&d) == ilbm_info(&d)->h);
    for (i = 0; i < 4; ++i) free(planes[i]);
    free(planes); free(buf);
}

/* ---- ilbm_index ---- */
static unsigned char *load_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*len);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); fclose(f); return 0; }
    fclose(f);
    return b;
}

/* Reference: decode a whole ILBM with the streaming decoder into planes[np] of bpr x h. */
static unsigned short stream_decode(const unsigned char *src, unsigned long n, unsigned char **planes,
                                    unsigned char np, unsigned short bpr, unsigned short h)
{
    ilbm_t d; unsigned long off = 0; unsigned short used; int r = ILBM_NEED_MORE;
    ilbm_init(&d);
    while (off < n) {
        unsigned short k = (unsigned short)((n - off) > 512UL ? 512UL : (n - off));
        r = ilbm_feed(&d, src + off, k, &used);
        off += used;
        if (r == ILBM_HEADER) ilbm_set_target(&d, planes, np, bpr, h);
        else if (r != ILBM_NEED_MORE) break;
    }
    return ilbm_rows_done(&d);
}

static void check_ranges_match(const unsigned char *src, unsigned long n, unsigned short step)
{
    ilbm_doc_t doc; unsigned long *idx; unsigned char *ref[8], *got[8];
    unsigned short h, bpr, first, cnt; unsigned char np, p;
    CHECK(ilbm_doc_parse(&doc, src, n, 0, 0) == ILBMX_OK);
    h = doc.info.h; np = doc.info.planes; bpr = doc.src_bpr;
    idx = malloc(sizeof *idx * h);
    CHECK(ilbm_doc_parse(&doc, src, n, idx, h) == ILBMX_OK);
    CHECK(doc.rows == h);
    for (p = 0; p < np; ++p) { ref[p] = calloc(bpr, h); got[p] = calloc(bpr, h); }
    CHECK(stream_decode(src, n, ref, np, bpr, h) == h);
    for (first = 0; first < h; first = (unsigned short)(first + step)) {
        for (cnt = 1; first + cnt <= h; cnt = (unsigned short)(cnt * 2 + 1)) {
            for (p = 0; p < np; ++p) memset(got[p], 0xAA, (size_t)bpr * h);
            ilbm_doc_decode_rows(&doc, first, cnt, got, np, bpr, first);
            for (p = 0; p < np; ++p) {
                size_t k, lo = (size_t)first * bpr, hi = (size_t)(first + cnt) * bpr;
                CHECK(memcmp(got[p] + lo, ref[p] + lo, hi - lo) == 0);
                for (k = 0; k < (size_t)bpr * h; ++k)
                    if ((k < lo || k >= hi) && got[p][k] != 0xAA) { CHECK(!"write outside requested rows"); break; }
            }
            if (fails) goto out;
        }
    }
out:
    for (p = 0; p < np; ++p) { free(ref[p]); free(got[p]); }
    free(idx);
}

static void test_ilbmx_tiny_ranges(void) { check_ranges_match(TINY, sizeof TINY, 1); }

/* TINY with an uncompressed BODY (compression 0, 8 bytes). */
static void test_ilbmx_tiny_uncompressed(void)
{
    static const unsigned char rows[8] = { 0xFF,0xFF, 0x00,0x00, 0x00,0x0F, 0xF0,0x00 };
    unsigned char raw[76];
    memcpy(raw, TINY, 68);
    raw[7] = 68;                 /* FORM len: 4 + 28 + 20 + 16 */
    CHECK(raw[30] == 1);         /* BMHD compression byte */
    raw[30] = 0;
    CHECK(raw[60] == 'B' && raw[67] == 0x0A);
    raw[67] = 8;                 /* BODY len */
    memcpy(raw + 68, rows, 8);
    check_ranges_match(raw, sizeof raw, 1);
}

/* Chunk lengths from the wire must never wrap pointer arithmetic (32-bit on the 68000). */
static void test_ilbmx_huge_chunk_len(void)
{
    static const unsigned char lens[2][4] = { {0xFF,0xFF,0xFF,0xF8}, {0xFF,0xFF,0xFF,0xE6} };
    unsigned i; ilbm_doc_t doc; unsigned long idx[2];
    for (i = 0; i < 2; ++i) {
        unsigned char b[64];
        memset(b, 0, sizeof b);
        memcpy(b, "FORM", 4); b[7] = 56; memcpy(b + 8, "ILBM", 4);
        memcpy(b + 12, "JUNK", 4); memcpy(b + 16, lens[i], 4);
        /* No BMHD seen before the walk stops: invalid. */
        CHECK(ilbm_doc_parse(&doc, b, sizeof b, 0, 0) == ILBMX_ERROR);
        CHECK(ilbm_doc_parse(&doc, b, sizeof b, idx, 2) == ILBMX_ERROR);
    }
    {   /* Valid BMHD, then JUNK with a huge length, then a BODY that is never reached. */
        unsigned char b[sizeof TINY + 16];
        memcpy(b, TINY, 12 + 28);                       /* FORM, ILBM, BMHD */
        memcpy(b + 40, "JUNK", 4); memcpy(b + 44, lens[0], 4);
        memset(b + 48, 0, 4);
        memcpy(b + 52, TINY + 60, sizeof TINY - 60);    /* BODY header + data */
        /* Header is valid, the walk stops at JUNK: OK with no BODY found; index call is PARTIAL. */
        CHECK(ilbm_doc_parse(&doc, b, 52 + (sizeof TINY - 60), 0, 0) == ILBMX_OK);
        CHECK(doc.body == 0 && doc.body_len == 0);
        CHECK(ilbm_doc_parse(&doc, b, 52 + (sizeof TINY - 60), idx, 2) == ILBMX_PARTIAL);
        CHECK(doc.rows == 0);
    }
}

static void test_ilbmx_fixtures(void)
{
    static const char *files[] = { "tests/fixtures/python_640x400x16.iff", "tests/fixtures/2636_zoom_gray4.iff" };
    unsigned i;
    for (i = 0; i < 2; ++i) {
        long n = 0; unsigned char *b = load_file(files[i], &n);
        CHECK(b != 0);
        if (!b) continue;
        check_ranges_match(b, (unsigned long)n, 37);
        free(b);
    }
}

static void test_ilbmx_header_only(void)
{
    ilbm_doc_t doc;
    CHECK(ilbm_doc_parse(&doc, TINY, sizeof TINY, 0, 0) == ILBMX_OK);
    CHECK(doc.info.w == 16 && doc.info.h == 2 && doc.info.planes == 2 && doc.src_bpr == 2);
    CHECK(doc.info.ncolors == 4 && doc.body != 0 && doc.body_len == 10 && doc.rows == 0);
}

static void test_ilbmx_truncated(void)
{
    ilbm_doc_t doc; unsigned long idx[2];
    /* Drop the last 3 bytes: row 1 plane 1 is incomplete, so only row 0 is indexed. */
    CHECK(ilbm_doc_parse(&doc, TINY, sizeof TINY - 3, idx, 2) == ILBMX_PARTIAL);
    CHECK(doc.rows == 1);
    /* Header complete, BODY header present but no body bytes. */
    CHECK(ilbm_doc_parse(&doc, TINY, sizeof TINY - 10, idx, 2) == ILBMX_PARTIAL);
    CHECK(doc.rows == 0);
    /* Cut inside the BMHD: header invalid. */
    CHECK(ilbm_doc_parse(&doc, TINY, 30, idx, 2) == ILBMX_ERROR);
}

static void test_ilbmx_garbage(void)
{
    ilbm_doc_t doc; unsigned long idx[2];
    const unsigned char html[] = "<html><body>404</body></html>";
    CHECK(ilbm_doc_parse(&doc, html, sizeof html - 1, idx, 2) == ILBMX_ERROR);
    CHECK(ilbm_doc_parse(&doc, html, sizeof html - 1, 0, 0) == ILBMX_ERROR);
}

static void test_ilbmx_overflow_rejected(void)
{
    unsigned char bad[sizeof TINY]; ilbm_doc_t doc; unsigned long idx[2];
    memcpy(bad, TINY, sizeof TINY);
    /* First BODY control byte (offset 68): 0xFF = run of 2 -> make it 0xFD = run of 4 > src_bpr 2. */
    CHECK(bad[68] == 0xFF);
    bad[68] = 0xFD;
    CHECK(ilbm_doc_parse(&doc, bad, sizeof bad, idx, 2) == ILBMX_ERROR);
}

static void test_ilbmx_clip_and_discard(void)
{
    ilbm_doc_t doc; unsigned long idx[2]; unsigned char p0[4], *pl[1];
    CHECK(ilbm_doc_parse(&doc, TINY, sizeof TINY, idx, 2) == ILBMX_OK);
    memset(p0, 0x55, sizeof p0); pl[0] = p0;
    /* Only plane 0 wanted, destination 1 byte per row: plane 1 discarded, col 1 clipped. */
    ilbm_doc_decode_rows(&doc, 0, 2, pl, 1, 1, 0);
    CHECK(p0[0] == 0xFF && p0[1] == 0x00);
    CHECK(p0[2] == 0x55 && p0[3] == 0x55);
    /* Asking past the indexed rows stops at doc.rows. */
    memset(p0, 0x55, sizeof p0);
    ilbm_doc_decode_rows(&doc, 1, 5, pl, 1, 2, 0);
    CHECK(p0[0] == 0x00 && p0[1] == 0x0F && p0[2] == 0x55);
}

static void test_netmap(void) {
    CHECK(netmap_image(0, ILBM_DONE, 0, 100, 100) == NET_OK);
    CHECK(netmap_image(0, ILBM_DONE, 0, 40, 100) == NET_ERR_PARTIAL);     /* BODY ended early */
    CHECK(netmap_image(0, ILBM_DONE, 0, 0, 100) == NET_ERR_CONVERT);
    CHECK(netmap_image(0, ILBM_NEED_MORE, 0, 10, 100) == NET_ERR_PARTIAL); /* EOF mid-image */
    CHECK(netmap_image(0, ILBM_NEED_MORE, 0, 0, 100) == NET_ERR_CONVERT);  /* EOF, nothing drawn */
    CHECK(netmap_image(0, ILBM_NEED_MORE, 0, 0, 0) == NET_ERR_CONVERT);    /* EOF before header */
    CHECK(netmap_image(0, ILBM_ERROR, 0, 0, 100) == NET_ERR_CONVERT);
    CHECK(netmap_image(0, ILBM_ERROR, 0, 5, 100) == NET_ERR_PARTIAL);
    CHECK(netmap_image(0, ILBM_HEADER, 1, 0, 100) == NET_ERR_NOMEM);
    CHECK(netmap_image(8, ILBM_NEED_MORE, 0, 0, 0) == NET_ERR_TOOBIG);     /* FN_ERR_UNSUPPORTED */
    CHECK(netmap_image(2, ILBM_NEED_MORE, 0, 0, 0) == NET_ERR_CONVERT);    /* FN_ERR_INVALID */
    CHECK(netmap_image(8, ILBM_NEED_MORE, 0, 7, 100) == NET_ERR_PARTIAL);
    CHECK(netmap_image(0x10, ILBM_NEED_MORE, 0, 0, 0) == 0x10);            /* transport passes through */
    CHECK(netmap_image(0x10, ILBM_NEED_MORE, 0, 9, 100) == NET_ERR_PARTIAL);
    CHECK(netmap_image(0x06, ILBM_NEED_MORE, 0, 0, 0) == 0x06);            /* timeout passes through */
}

static void test_netmap_open(void) {
    /* INVALID from the open call: firmware does not know translation type 4. */
    CHECK(netmap_open_error(NETMAP_FN_INVALID) == NET_ERR_NOIMAGE);
    CHECK(netmap_open_error(NETMAP_FN_UNSUPPORTED) == NET_ERR_TOOBIG);
    CHECK(netmap_open_error(0x10) == 0x10);                                /* transport passes through */
    CHECK(netmap_open_error(0x06) == 0x06);                                /* timeout passes through */
    /* INVALID from a read stays a conversion failure. */
    CHECK(netmap_image(NETMAP_FN_INVALID, ILBM_NEED_MORE, 0, 0, 0) == NET_ERR_CONVERT);
    CHECK(NET_ERR_NOIMAGE != NET_ERR_CONVERT);
}

static void test_autorange(void)
{
    unsigned short s;
    CHECK(auto_from_pot(0) == 10 && auto_from_pot(0xFFFF) == 600);
    for (s = 10; s <= 600; s += 10) CHECK(auto_from_pot(auto_to_pot(s)) == s);
    CHECK(auto_clamp(0) == 10 && auto_clamp(9999) == 600 && auto_clamp(64) == 60);
}

static void test_countdown(void)
{
    const unsigned long cap = 600 * COUNTDOWN_US;
    CHECK(countdown_left_us(100, 0, 100, 0, cap) == 0);                 /* exactly due */
    CHECK(countdown_left_us(100, 0, 101, 5, cap) == 0);                 /* overdue */
    CHECK(countdown_left_us(160, 0, 100, 0, cap) == 60 * COUNTDOWN_US);
    CHECK(countdown_left_us(160, 200, 159, 999900, cap) == 300);        /* borrow across seconds */
    CHECK(countdown_left_us(4000000000UL, 0, 1, 0, cap) == cap);       /* clock set back: saturate */
    CHECK(countdown_shown(60 * COUNTDOWN_US) == 60);
    CHECK(countdown_shown(59 * COUNTDOWN_US + 1) == 60);
    CHECK(countdown_shown(1) == 1 && countdown_shown(0) == 0);
    CHECK(countdown_wait_us(60 * COUNTDOWN_US) == COUNTDOWN_US);
    CHECK(countdown_wait_us(59 * COUNTDOWN_US + 250000) == 250000);
    CHECK(countdown_wait_us(0) == 0);
}

/* Runs src through jsonstrip in chunk-size steps; returns output length or -1 on overflow. */
static int strip_run(const char *src, size_t n, size_t chunk, char *out, unsigned short cap) {
    jsonstrip_t js; size_t i;
    jsonstrip_init(&js, out, cap);
    for (i = 0; i < n; i += chunk)
        if (!jsonstrip_feed(&js, src + i, (unsigned short)(n - i < chunk ? n - i : chunk))) return -1;
    return jsonstrip_end(&js);
}

static void test_jsonstrip_long_transcript(void) {
    static char src[20000], out[4096];
    static const size_t chunks[] = { 1, 7, 512 };
    size_t k, n, i;
    n = (size_t)sprintf(src, "{\"month\": \"1\", \"num\": 802, \"news\": \"");
    for (i = 0; i < 100; ++i) n += (size_t)sprintf(src + n, "n\\\"ews ");
    n += (size_t)sprintf(src + n, "\", \"safe_title\": \"Big\", \"transcript\": \"");
    for (i = 0; i < 1000; ++i) n += (size_t)sprintf(src + n, "line \\\"%03d\\\"\\\\\\n", (int)(i % 1000));
    n += (size_t)sprintf(src + n, "\", \"alt\": \"The alt \\\"text\\\"\", "
        "\"img\": \"https://imgs.xkcd.com/comics/big.png\", \"title\": \"Big\", \"day\": \"5\"}");
    CHECK(n > 10000);
    for (k = 0; k < 3; ++k) {
        xkcd_comic_t c;
        int len = strip_run(src, n, chunks[k], out, sizeof out);
        CHECK(len > 0 && len < 600);
        CHECK(xkcd_parse(out, (unsigned short)len, &c));
        CHECK(c.num == 802 && strcmp(c.alt, "The alt \"text\"") == 0);
        CHECK(strcmp(c.img, "https://imgs.xkcd.com/comics/big.png") == 0 && c.has_image);
        CHECK(strstr(out, "\"transcript\": \"\"") != 0 && strstr(out, "\"news\": \"\"") != 0);
    }
}

static void test_jsonstrip_key_inside_value(void) {
    const char j[] = "{\"num\": 5, \"alt\": \"the \\\"transcript\\\": \\\"x\\\" key\", "
                     "\"title\": \"transcript\", \"img\": \"a.png\"}";
    char out[256]; xkcd_comic_t c; size_t k;
    static const size_t chunks[] = { 1, 3, 512 };
    for (k = 0; k < 3; ++k) {
        int len = strip_run(j, sizeof j - 1, chunks[k], out, sizeof out);
        CHECK(len == (int)(sizeof j - 1) && memcmp(out, j, (size_t)len) == 0);   /* unchanged */
        CHECK(xkcd_parse(out, (unsigned short)len, &c) && strcmp(c.title, "transcript") == 0);
    }
}

static void test_jsonstrip_overflow(void) {
    char out[16];
    CHECK(strip_run(SAMPLE, sizeof SAMPLE - 1, 512, out, sizeof out) == -1);
}

static void test_xkcd_long_url(void) {
    char j[400], url[320]; xkcd_comic_t c; int i;
    for (i = 0; i < 200; ++i) url[i] = 'a';
    strcpy(url + 192, "/abc.png");   /* 200 chars */
    sprintf(j, "{\"num\": 9, \"title\": \"T\", \"img\": \"%s\"}", url);
    CHECK(strlen(url) == 200);
    CHECK(xkcd_parse(j, (unsigned short)strlen(j), &c) && c.has_image && strcmp(c.img, url) == 0);
    for (i = 0; i < 300; ++i) url[i] = 'a';           /* longer than the buffer: truncated, so no image */
    strcpy(url + 292, "/abc.png");
    sprintf(j, "{\"num\": 9, \"title\": \"T\", \"img\": \"%s\"}", url);
    CHECK(xkcd_parse(j, (unsigned short)strlen(j), &c) && !c.has_image);
}

static void test_zs_tall_image(void)
{
    zs_t z; int f, c;
    zs_init(&z, 1024, 512);
    CHECK(zs_scrollable(&z) && zs_max_top(&z) == 512 && zs_page(&z) == 480);
    CHECK(zs_scroll(&z, ZS_LINE) == 16 && z.top == 16);
    CHECK(zs_scroll(&z, -100) == -16 && z.top == 0);
    CHECK(zs_scroll(&z, 10000) == 512 && z.top == 512);
    CHECK(zs_scroll(&z, 1) == 0 && z.top == 512);
    zs_exposed(16, 512, &f, &c);  CHECK(f == 496 && c == 16);
    zs_exposed(-16, 512, &f, &c); CHECK(f == 0 && c == 16);
    zs_exposed(0, 512, &f, &c);   CHECK(c == 0);
}

static void test_zs_exposed_full(void)
{
    int f, c;
    zs_exposed(512, 512, &f, &c);  CHECK(f == 0 && c == 512);
    zs_exposed(-600, 512, &f, &c); CHECK(f == 0 && c == 512);
}

static void test_zs_short_image(void)
{
    zs_t z; int y, h;
    zs_init(&z, 300, 512);
    CHECK(!zs_scrollable(&z) && zs_max_top(&z) == 0);
    CHECK(zs_scroll(&z, 50) == 0 && z.top == 0);
    zs_thumb(&z, 512, &y, &h); CHECK(y == 0 && h == 512);
}

static void test_zs_thumb(void)
{
    zs_t z; int y, h;
    zs_init(&z, 1024, 512);
    zs_thumb(&z, 512, &y, &h); CHECK(h == 256 && y == 0);
    zs_scroll(&z, 512);
    zs_thumb(&z, 512, &y, &h); CHECK(h == 256 && y == 256);
    zs_init(&z, 100000, 400);
    zs_thumb(&z, 400, &y, &h); CHECK(h == ZS_MIN_THUMB);
}

static void test_bufgrow(void)
{
    CHECK(bufgrow_next(16384, 16385, 262144) == 32768);
    CHECK(bufgrow_next(16384, 70000, 262144) == 131072);
    CHECK(bufgrow_next(131072, 200000, 262144) == 262144);
    CHECK(bufgrow_next(262144, 262145, 262144) == 0);
    CHECK(bufgrow_next(0, 5, 262144) == 8);                  /* size 0 starts at 1 */
    CHECK(bufgrow_next(1000, 10, 262144) == 1000);           /* already big enough */
    CHECK(bufgrow_next(1UL << 31, 0xFFFFFFFFUL, 0xFFFFFFFFUL) == 0xFFFFFFFFUL); /* no wrap */
}

int main(void) {
    RUN(test_json_c1); RUN(test_jsonstrip_long_transcript); RUN(test_jsonstrip_key_inside_value);
    RUN(test_jsonstrip_overflow); RUN(test_xkcd_long_url);
    RUN(test_json_basic); RUN(test_json_unescape); RUN(test_json_truncates);
    RUN(test_xkcd_parse); RUN(test_xkcd_parse_no_image); RUN(test_xkcd_urls);
    RUN(test_random_pick_skips_404); RUN(test_history); RUN(test_wrap); RUN(test_selectors);
    RUN(test_ilbm_tiny_any_chunking); RUN(test_ilbm_truncated_body);
    RUN(test_ilbm_rejects_garbage); RUN(test_ilbm_fixture); RUN(test_netmap); RUN(test_netmap_open);
    RUN(test_autorange); RUN(test_countdown);
    RUN(test_ilbmx_tiny_ranges); RUN(test_ilbmx_tiny_uncompressed); RUN(test_ilbmx_huge_chunk_len); RUN(test_ilbmx_fixtures); RUN(test_ilbmx_header_only);
    RUN(test_ilbmx_truncated); RUN(test_ilbmx_garbage); RUN(test_ilbmx_overflow_rejected);
    RUN(test_ilbmx_clip_and_discard);
    RUN(test_zs_tall_image); RUN(test_zs_exposed_full); RUN(test_zs_short_image);
    RUN(test_zs_thumb); RUN(test_bufgrow);
    printf(fails ? "%d FAILURES\n" : "ALL PASS\n", fails);
    return fails != 0;
}
