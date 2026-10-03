/* tests/test_main.c */
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "xkcd.h"
#include "history.h"
#include "wrap.h"
#include "selector.h"
#include "autorange.h"
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
    selector_zoom(s, sizeof s, 1); CHECK(strcmp(s, "fmt=ilbm,bits=4,w=640,h=512,colors=16,base=0") == 0);
    selector_zoom(s, sizeof s, 0); CHECK(strcmp(s, "fmt=ilbm,bits=4,w=640,h=400,colors=16,base=0") == 0);
}

#include "ilbm.h"
#include "netmap.h"
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

int main(void) {
    RUN(test_json_c1); RUN(test_jsonstrip_long_transcript); RUN(test_jsonstrip_key_inside_value);
    RUN(test_jsonstrip_overflow); RUN(test_xkcd_long_url);
    RUN(test_json_basic); RUN(test_json_unescape); RUN(test_json_truncates);
    RUN(test_xkcd_parse); RUN(test_xkcd_parse_no_image); RUN(test_xkcd_urls);
    RUN(test_random_pick_skips_404); RUN(test_history); RUN(test_wrap); RUN(test_selectors);
    RUN(test_ilbm_tiny_any_chunking); RUN(test_ilbm_truncated_body);
    RUN(test_ilbm_rejects_garbage); RUN(test_ilbm_fixture); RUN(test_netmap); RUN(test_netmap_open);
    RUN(test_autorange);
    printf(fails ? "%d FAILURES\n" : "ALL PASS\n", fails);
    return fails != 0;
}
