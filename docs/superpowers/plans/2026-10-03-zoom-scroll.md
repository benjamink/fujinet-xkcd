# Zoom Scrolling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zoom shows xkcd comics fitted to the 640-pixel screen width in crisp grey-4 and scrolls tall comics vertically (keys and mouse drag). Memory use is flat: the compressed ILBM stays in RAM and only the visible rows are decoded.

**Architecture:**
- Two new portable C99 modules, host-tested with gcc:
  - `ilbm_index` parses an in-memory ILBM, builds a per-row index, and decodes any row range into caller bitplanes;
  - `zoomscroll` holds the scroll arithmetic.
- In the Amiga layer:
  - `net` gains a fetch-into-buffer call;
  - `zoom.c` is rewritten around a 2-plane interlaced screen that scrolls with `ScrollRaster` and decodes only the newly exposed rows.

**Tech Stack:**
- C99, amiga-gcc with `-mcrt=nix13` (Kickstart 1.3 / V33 API only), host gcc for `make test`.
- MekkoGX build.
- The fujinet-nio firmware (POSIX TCP build) and its Python client, used to generate a test fixture.

**Spec:** `docs/superpowers/specs/2026-10-03-zoom-scroll-design.md`. Read it; it is the authority for behaviour.

## Global Constraints

- Kickstart 1.3 (V33) API only: no GadTools, no tag calls, no V36+ functions, and no reliance on return values that only exist from V36.
- No floating point. 68000. `-mcrt=nix13`.
- Portable modules in `src/*.c` must not include Amiga headers. They must build warning-free with host `gcc -std=c99 -Wall -Wextra -Werror` (via `make test`) and with amiga-gcc (`make amiga`).
- Zoom selector, exactly: `fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024`. It is the same for PAL and NTSC.
- Zoom screen: 640 × 512 (PAL) or 640 × 400 (NTSC), `HIRES|LACE`, **2 bitplanes**.
- The image buffer is allocated with `MEMF_ANY`, with a hard cap of **262144 bytes** (256 KB).
- Scrolling:
  - Up/Down: 16 rows;
  - page (Shift+Up/Down, Space, Backspace): screen height minus 32 rows;
  - T / B: top / bottom;
  - Esc exits;
  - left-button drag moves one row per pixel.
- Indicator: a 4-pixel bar at the right screen edge, only when the image is taller than the screen. Track pen 1, thumb pen 3.
- Error messages (exact text):
  - "Image too large for Zoom";
  - "Not enough memory for Zoom";
  - "Image transfer interrupted";
  - "FujiNet could not convert this image";
  - existing: "Not enough chip memory for Zoom", "FujiNet firmware lacks image conversion (update it)".
- The main window, Previous/Next, Fetch ID and Auto Refresh are unchanged.
- Commits: authored as the configured git user. **No `Co-Authored-By` trailer and no agent attribution.** Never push.
- Build environment: `source /home/bkrein/dev/fujinet-nio-workspace/scripts/env.sh` before `make amiga`.

## Review Focus

1. **Truncated buffer mid-row.** An interrupted transfer must index only rows whose every plane is complete. It must never decode bytes past `body_len`. Pinned by `test_ilbmx_truncated` (Task 1).
2. **Malformed run data.** A ByteRun1 run or literal that overflows a plane row must be rejected as `ILBMX_ERROR` at parse time, so decode can never write past `src_bpr`. Pinned by `test_ilbmx_overflow_rejected` (Task 1).
3. **Image shorter than the screen.** No scrolling, no bar, vertically centred; any scroll input is a no-op. Pinned by `test_zs_short_image` (Task 2).
4. **Scroll larger than the view** (for example T/B on a tall image). It must redraw the whole view instead of calling `ScrollRaster` with an out-of-range delta. Pinned by `test_zs_exposed_full` (Task 2).
5. **No content length from the firmware.** The buffer must grow by doubling up to the cap and report "Image too large for Zoom" beyond it. This is covered by code review plus the Amiberry check in Task 4. The growth policy is pinned by `test_bufgrow` (Task 3, via the portable `bufgrow_next()` helper).

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/ilbm_index.h`, `src/ilbm_index.c` | new | Parse an in-memory ILBM (header only, or header plus row index); decode a row range into bitplanes |
| `src/zoomscroll.h`, `src/zoomscroll.c` | new | Pure scroll arithmetic and buffer-growth policy |
| `src/selector.c` | modify | `selector_zoom()` returns the grey-4 width-fit selector |
| `src/netmap.h` | modify | Add `NET_ERR_ZOOMBIG` |
| `src/amiga/net.h`, `src/amiga/net.c` | modify | Add `net_fetch_image_buffer()`; add text for `NET_ERR_ZOOMBIG` |
| `src/amiga/zoom.c` | rewrite | 2-plane screen; fetch → index → draw; event loop; scroll; drag; indicator |
| `tests/test_main.c` | modify | New host tests |
| `tests/fixtures/2636_zoom_gray4.iff` | new | #2636 converted with the Zoom selector by the POSIX firmware |
| `README.md`, `amiga/ReadMe.txt`, `AGENTS.md` | modify | Zoom controls and behaviour |

---

### Task 1: `ilbm_index`: in-memory ILBM row index and range decode (plus the #2636 fixture)

**Files:**
- Create: `src/ilbm_index.h`, `src/ilbm_index.c`, `tests/fixtures/2636_zoom_gray4.iff`
- Modify: `tests/test_main.c` (add tests and register them in `main`)

**Interfaces:**
- Consumes: `ilbm_info_t` from `src/ilbm.h`; the streaming decoder `ilbm_init` / `ilbm_feed` / `ilbm_set_target` / `ilbm_rows_done` (tests only, as the reference).
- Produces:
  ```c
  enum { ILBMX_OK = 0, ILBMX_PARTIAL = 1, ILBMX_ERROR = -1 };
  typedef struct {
      ilbm_info_t info;
      const unsigned char *body;   /* first BODY data byte, or 0 if no BODY yet */
      unsigned long body_len;      /* BODY bytes actually present in the buffer */
      unsigned long *row_off;      /* caller array: offset into body of each indexed row */
      unsigned short rows;         /* complete rows indexed */
      unsigned short src_bpr;      /* ((w + 15) / 16) * 2 */
  } ilbm_doc_t;
  int  ilbm_doc_parse(ilbm_doc_t *d, const unsigned char *buf, unsigned long len,
                      unsigned long *row_off, unsigned short max_rows);
  void ilbm_doc_decode_rows(const ilbm_doc_t *d, unsigned short first, unsigned short count,
                            unsigned char **planes, unsigned char nplanes_dst,
                            unsigned short bpr, unsigned short dst_row0);
  ```

`ilbm_doc_parse` semantics:
- **Header-only call:** pass `row_off == 0`. It fills `d->info`, `d->body` and `d->body_len`, sets `d->rows = 0`, and returns `ILBMX_OK`, or `ILBMX_ERROR` for a non-ILBM or invalid header.
- **Index call:** pass `row_off` with room for `max_rows` entries. It indexes up to `min(info.h, max_rows)` rows.
  - Returns `ILBMX_OK` if all of those are complete.
  - Returns `ILBMX_PARTIAL` if the buffer ends earlier; `d->rows` counts only rows where every plane is complete.
  - Returns `ILBMX_ERROR` on malformed data, including any run or literal that would produce more than `src_bpr` bytes for a plane row.
- **Header validation** is the same as `ilbm.c`: `w > 0`, `h > 0`, `1 ≤ planes ≤ 8`, `compression ≤ 1`, BMHD length 20.
- **Chunks:** `CMAP` is copied the same way as `ilbm.c` (up to 32 entries, `ncolors` = entries present). Unknown chunks are skipped, honouring odd-length pad bytes. A missing BODY on a header-valid buffer gives `body = 0`, `body_len = 0`, and the index call returns `ILBMX_PARTIAL` with `rows = 0`.

`ilbm_doc_decode_rows` semantics:
- Decodes image rows `first .. first+count-1`. It stops early at `d->rows`.
- Image row `first+i` goes into destination row `dst_row0 + i`, at `planes[p] + (dst_row0 + i) * bpr`.
- Planes `p ≥ nplanes_dst` are decoded and discarded.
- Only bytes `col < bpr` are written.

- [ ] **Step 1: Generate the #2636 fixture from the real firmware**

Run (POSIX TCP firmware in the background, then the Python client):

```bash
source /home/bkrein/dev/fujinet-nio-workspace/scripts/env.sh
cd /home/bkrein/dev/fujinet-nio-workspace/repos/fujinet-nio
./build.sh -p fujibus-tcp-debug
./build/fujibus-tcp-debug/fujinet-nio > /tmp/claude-1000/-home-bkrein-dev-fujinet-xkcd/4ed29a17-189f-4319-b056-ccb1368b8ccf/scratchpad/fixture-nio.log 2>&1 &
NIO_PID=$!
sleep 3
./scripts/fujinet -p socket://127.0.0.1:65504 net get --content-type image \
  --selector "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024" \
  --out /home/bkrein/dev/fujinet-xkcd/tests/fixtures/2636_zoom_gray4.iff \
  https://imgs.xkcd.com/comics/what_if_2_countdown.png
kill $NIO_PID
python3 -c "import sys;d=open('/home/bkrein/dev/fujinet-xkcd/tests/fixtures/2636_zoom_gray4.iff','rb').read();print(d[:4],d[8:12],int.from_bytes(d[20:22],'big'),'x',int.from_bytes(d[22:24],'big'),'planes',d[28],'bytes',len(d))"
```

Expected: `b'FORM' b'ILBM' <w≤640> x <h≤1024> planes 2 bytes <N>`, with h close to 1024 and w close to 623. If `net get` needs a different flag order or option name, run `./scripts/fujinet net get --help` and adapt; the selector string must stay exactly as above. Do not kill any other `fujinet-nio` process; kill only `$NIO_PID`.

- [ ] **Step 2: Write the failing tests** (add to `tests/test_main.c`; add `#include "ilbm_index.h"`; call every new test from `main` with `RUN(...)`)

```c
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
            for (p = 0; p < np; ++p)
                CHECK(memcmp(got[p] + (size_t)first * bpr, ref[p] + (size_t)first * bpr, (size_t)cnt * bpr) == 0);
            if (fails) goto out;
        }
    }
out:
    for (p = 0; p < np; ++p) { free(ref[p]); free(got[p]); }
    free(idx);
}

static void test_ilbmx_tiny_ranges(void) { check_ranges_match(TINY, sizeof TINY, 1); }

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
```

Check the `TINY` byte offsets before relying on them. In `tests/test_main.c` the BODY data starts after `FORM`+len (8), `ILBM` (4), BMHD (8+20), CMAP (8+12) and the BODY header (8). That puts the first body byte at offset 8+4+28+20+8 = **68**, and it should be `0xFF`. If the test's own assertion `bad[68] == 0xFF` fails, recompute the offset and fix the test, not the code.

- [ ] **Step 3: Run the tests and confirm they fail**

Run: `make test`
Expected: compile FAIL with `ilbm_index.h: No such file or directory`.

- [ ] **Step 4: Implement `src/ilbm_index.h` and `src/ilbm_index.c`**

```c
/* src/ilbm_index.h — random access to an in-memory IFF ILBM: header, per-row index, row-range decode. */
#ifndef XKCD_ILBM_INDEX_H
#define XKCD_ILBM_INDEX_H
#include "ilbm.h"

enum { ILBMX_OK = 0, ILBMX_PARTIAL = 1, ILBMX_ERROR = -1 };

typedef struct {
    ilbm_info_t info;
    const unsigned char *body;   /* first BODY data byte, or 0 if no BODY yet */
    unsigned long body_len;      /* BODY bytes actually present in the buffer */
    unsigned long *row_off;      /* caller array: offset into body of each indexed row */
    unsigned short rows;         /* complete rows indexed */
    unsigned short src_bpr;      /* ((w + 15) / 16) * 2 */
} ilbm_doc_t;

/* row_off == 0: header only. Otherwise index up to min(h, max_rows) rows.
   Returns ILBMX_OK, ILBMX_PARTIAL (buffer ends early; d->rows complete rows), or ILBMX_ERROR. */
int  ilbm_doc_parse(ilbm_doc_t *d, const unsigned char *buf, unsigned long len,
                    unsigned long *row_off, unsigned short max_rows);

/* Decode image rows first..first+count-1 (stopping at d->rows) into destination rows dst_row0..;
   planes >= nplanes_dst are discarded, bytes at col >= bpr are not written. */
void ilbm_doc_decode_rows(const ilbm_doc_t *d, unsigned short first, unsigned short count,
                          unsigned char **planes, unsigned char nplanes_dst,
                          unsigned short bpr, unsigned short dst_row0);
#endif
```

```c
/* src/ilbm_index.c */
#include <string.h>
#include "ilbm_index.h"

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

static int parse_header(ilbm_doc_t *d, const unsigned char *buf, unsigned long len)
{
    unsigned long pos = 12;
    int have_bmhd = 0;

    memset(d, 0, sizeof *d);
    if (len < 12 || memcmp(buf, "FORM", 4) != 0 || memcmp(buf + 8, "ILBM", 4) != 0)
        return ILBMX_ERROR;
    while (pos + 8 <= len) {
        const unsigned char *ck = buf + pos;
        unsigned long clen = be32(ck + 4);
        unsigned long data = pos + 8;
        if (memcmp(ck, "BMHD", 4) == 0) {
            const unsigned char *b = buf + data;
            if (clen != 20 || data + 20 > len) return ILBMX_ERROR;
            d->info.w = (unsigned short)((b[0] << 8) | b[1]);
            d->info.h = (unsigned short)((b[2] << 8) | b[3]);
            d->info.planes = b[8];
            d->info.compression = b[10];
            if (!d->info.w || !d->info.h || !d->info.planes || d->info.planes > 8 || d->info.compression > 1)
                return ILBMX_ERROR;
            have_bmhd = 1;
        } else if (memcmp(ck, "CMAP", 4) == 0) {
            unsigned long n = clen, i;
            if (n > 96) n = 96;
            if (data + n > len) n = len > data ? len - data : 0;
            for (i = 0; i < n; ++i) d->info.cmap[i / 3][i % 3] = buf[data + i];
            d->info.ncolors = (unsigned char)(n / 3);
        } else if (memcmp(ck, "BODY", 4) == 0) {
            if (!have_bmhd) return ILBMX_ERROR;
            d->body = buf + data;
            d->body_len = data <= len ? len - data : 0;
            if (d->body_len > clen) d->body_len = clen;
            break;
        }
        pos = data + clen + (clen & 1);
    }
    if (!have_bmhd) return ILBMX_ERROR;
    d->src_bpr = (unsigned short)(((d->info.w + 15) / 16) * 2);
    return ILBMX_OK;
}

/* Skip one plane row starting at *pos. Returns 1 complete, 0 buffer ends first, -1 malformed. */
static int skip_plane_row(const ilbm_doc_t *d, unsigned long *pos)
{
    unsigned long p = *pos;
    unsigned short out = 0;

    if (d->info.compression == 0) {
        if (p + d->src_bpr > d->body_len) return 0;
        *pos = p + d->src_bpr;
        return 1;
    }
    while (out < d->src_bpr) {
        signed char c;
        if (p >= d->body_len) return 0;
        c = (signed char)d->body[p++];
        if (c >= 0) {
            unsigned short n = (unsigned short)(c + 1);
            if (out + n > d->src_bpr) return -1;
            if (p + n > d->body_len) return 0;
            p += n;
            out = (unsigned short)(out + n);
        } else if (c != -128) {
            unsigned short n = (unsigned short)(1 - c);
            if (out + n > d->src_bpr) return -1;
            if (p >= d->body_len) return 0;
            ++p;
            out = (unsigned short)(out + n);
        }
    }
    *pos = p;
    return 1;
}

int ilbm_doc_parse(ilbm_doc_t *d, const unsigned char *buf, unsigned long len,
                   unsigned long *row_off, unsigned short max_rows)
{
    unsigned long pos = 0;
    unsigned short want, r;
    unsigned char p;

    if (parse_header(d, buf, len) != ILBMX_OK) return ILBMX_ERROR;
    if (!row_off) return ILBMX_OK;
    d->row_off = row_off;
    want = d->info.h < max_rows ? d->info.h : max_rows;
    if (!d->body) return want ? ILBMX_PARTIAL : ILBMX_OK;
    for (r = 0; r < want; ++r) {
        unsigned long start = pos;
        for (p = 0; p < d->info.planes; ++p) {
            int k = skip_plane_row(d, &pos);
            if (k < 0) return ILBMX_ERROR;
            if (k == 0) return ILBMX_PARTIAL;
        }
        row_off[r] = start;
        d->rows = (unsigned short)(r + 1);
    }
    return ILBMX_OK;
}

static void decode_plane_row(const ilbm_doc_t *d, unsigned long *pos, unsigned char *dst, unsigned short bpr)
{
    unsigned long p = *pos;
    unsigned short out = 0;

    if (d->info.compression == 0) {
        for (; out < d->src_bpr; ++out, ++p)
            if (dst && out < bpr) dst[out] = d->body[p];
        *pos = p;
        return;
    }
    while (out < d->src_bpr) {
        signed char c = (signed char)d->body[p++];
        if (c >= 0) {
            unsigned short n = (unsigned short)(c + 1);
            for (; n; --n, ++p, ++out)
                if (dst && out < bpr) dst[out] = d->body[p];
        } else if (c != -128) {
            unsigned short n = (unsigned short)(1 - c);
            unsigned char v = d->body[p++];
            for (; n; --n, ++out)
                if (dst && out < bpr) dst[out] = v;
        }
    }
    *pos = p;
}

void ilbm_doc_decode_rows(const ilbm_doc_t *d, unsigned short first, unsigned short count,
                          unsigned char **planes, unsigned char nplanes_dst,
                          unsigned short bpr, unsigned short dst_row0)
{
    unsigned short i;
    unsigned char p;

    for (i = 0; i < count; ++i) {
        unsigned short r = (unsigned short)(first + i);
        unsigned long pos;
        if (r >= d->rows) return;
        pos = d->row_off[r];
        for (p = 0; p < d->info.planes; ++p) {
            unsigned char *dst = p < nplanes_dst
                ? planes[p] + (unsigned long)(dst_row0 + i) * bpr
                : 0;
            decode_plane_row(d, &pos, dst, bpr);
        }
    }
}
```

The decode never reads past `body_len`, because parse only indexes rows whose bytes were all verified present (the Review Focus 1 and 2 guarantee).

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `make test`
Expected: `ALL PASS`, including all `test_ilbmx_*`. Also run `source /home/bkrein/dev/fujinet-nio-workspace/scripts/env.sh && make amiga`: 0 warnings.

- [ ] **Step 6: Commit**

```bash
git add src/ilbm_index.h src/ilbm_index.c tests/test_main.c tests/fixtures/2636_zoom_gray4.iff
git commit -m "Add ilbm_index: in-memory ILBM row index and row-range decode (make test)"
```

---

### Task 2: `zoomscroll`: scroll arithmetic and buffer-growth policy

**Files:**
- Create: `src/zoomscroll.h`, `src/zoomscroll.c`
- Modify: `tests/test_main.c`

**Interfaces:**
- Produces:
  ```c
  #define ZS_LINE      16
  #define ZS_OVERLAP   32
  #define ZS_MIN_THUMB 8
  typedef struct { int img_h, view_h, top; } zs_t;
  void zs_init(zs_t *z, int img_h, int view_h);
  int  zs_scrollable(const zs_t *z);                 /* img_h > view_h */
  int  zs_max_top(const zs_t *z);                    /* max(0, img_h - view_h) */
  int  zs_page(const zs_t *z);                       /* max(ZS_LINE, view_h - ZS_OVERLAP) */
  int  zs_scroll(zs_t *z, int delta);                /* clamp top; return applied delta */
  void zs_exposed(int applied, int view_h, int *first, int *count);  /* view rows to redraw */
  void zs_thumb(const zs_t *z, int track_h, int *y, int *h);
  unsigned long bufgrow_next(unsigned long size, unsigned long need, unsigned long cap); /* 0 = over cap */
  ```

Semantics:
- `top` is the first image row shown at view row 0.
- A positive delta scrolls down: content moves up, and newly exposed rows are at the bottom.
- `zs_exposed`:
  - `applied == 0` → count 0;
  - `|applied| >= view_h` → first 0, count view_h (full redraw);
  - `applied > 0` → first `view_h - applied`, count `applied`;
  - `applied < 0` → first 0, count `-applied`.
- `zs_thumb`:
  - not scrollable → y 0, h track_h;
  - otherwise h = `track_h * view_h / img_h`, clamped to [ZS_MIN_THUMB, track_h], and y = `(track_h - h) * top / max_top`.
- `bufgrow_next(size, need, cap)`:
  - the smallest of `size*2, size*4, ...` that is ≥ need, then capped at cap;
  - returns 0 if `need > cap`.

- [ ] **Step 1: Write the failing tests** (add `#include "zoomscroll.h"`; register with `RUN`)

```c
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
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `make test`
Expected: compile FAIL with `zoomscroll.h: No such file or directory`.

- [ ] **Step 3: Implement**

```c
/* src/zoomscroll.h — scroll arithmetic for the Zoom view and the image-buffer growth policy. */
#ifndef XKCD_ZOOMSCROLL_H
#define XKCD_ZOOMSCROLL_H
#define ZS_LINE      16
#define ZS_OVERLAP   32
#define ZS_MIN_THUMB 8
typedef struct { int img_h, view_h, top; } zs_t;
void zs_init(zs_t *z, int img_h, int view_h);
int  zs_scrollable(const zs_t *z);
int  zs_max_top(const zs_t *z);
int  zs_page(const zs_t *z);
int  zs_scroll(zs_t *z, int delta);
void zs_exposed(int applied, int view_h, int *first, int *count);
void zs_thumb(const zs_t *z, int track_h, int *y, int *h);
unsigned long bufgrow_next(unsigned long size, unsigned long need, unsigned long cap);
#endif
```

```c
/* src/zoomscroll.c */
#include "zoomscroll.h"

void zs_init(zs_t *z, int img_h, int view_h)
{
    z->img_h = img_h;
    z->view_h = view_h;
    z->top = 0;
}

int zs_scrollable(const zs_t *z) { return z->img_h > z->view_h; }

int zs_max_top(const zs_t *z)
{
    int m = z->img_h - z->view_h;
    return m > 0 ? m : 0;
}

int zs_page(const zs_t *z)
{
    int p = z->view_h - ZS_OVERLAP;
    return p < ZS_LINE ? ZS_LINE : p;
}

int zs_scroll(zs_t *z, int delta)
{
    int t = z->top + delta, m = zs_max_top(z);
    if (t < 0) t = 0;
    if (t > m) t = m;
    delta = t - z->top;
    z->top = t;
    return delta;
}

void zs_exposed(int applied, int view_h, int *first, int *count)
{
    if (applied == 0) { *first = 0; *count = 0; return; }
    if (applied >= view_h || -applied >= view_h) { *first = 0; *count = view_h; return; }
    if (applied > 0) { *first = view_h - applied; *count = applied; }
    else             { *first = 0; *count = -applied; }
}

void zs_thumb(const zs_t *z, int track_h, int *y, int *h)
{
    long th;
    if (!zs_scrollable(z)) { *y = 0; *h = track_h; return; }
    th = (long)track_h * z->view_h / z->img_h;
    if (th < ZS_MIN_THUMB) th = ZS_MIN_THUMB;
    if (th > track_h) th = track_h;
    *h = (int)th;
    *y = (int)((long)(track_h - th) * z->top / zs_max_top(z));
}

unsigned long bufgrow_next(unsigned long size, unsigned long need, unsigned long cap)
{
    unsigned long n = size ? size : 1;
    if (need > cap) return 0;
    while (n < need) n *= 2;
    return n > cap ? cap : n;
}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `make test`
Expected: `ALL PASS`. Run `make amiga`: 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add src/zoomscroll.h src/zoomscroll.c tests/test_main.c
git commit -m "Add zoomscroll: Zoom scroll arithmetic and buffer growth policy (make test)"
```

---

### Task 3: Zoom selector, `NET_ERR_ZOOMBIG` and `net_fetch_image_buffer()`

**Files:**
- Modify: `src/selector.c`, `src/netmap.h`, `src/amiga/net.h`, `src/amiga/net.c`, `tests/test_main.c` (the `test_selectors` zoom expectations)

**Interfaces:**
- Consumes: `bufgrow_next` (Task 2); `ensure_up`, `read_chunk`, `finish`, `note_fn_error`, `IMAGE_WAITS` and `netmap_open_error` from `net.c` / `netmap`.
- Produces:
  ```c
  /* netmap.h: append to the enum after NET_ERR_NOIMAGE */
  NET_ERR_ZOOMBIG
  /* net.h */
  #define ZOOM_BUF_MAX   262144UL
  #define ZOOM_BUF_START 16384UL
  /* Reads the whole translated ILBM into one MEMF_ANY buffer. On NET_OK or NET_ERR_PARTIAL, *buf is
     non-zero and the caller frees it with FreeMem(*buf, *alloc_size). On every other result, *buf is 0. */
  unsigned char net_fetch_image_buffer(const char *img_url, const char *selector,
                                       unsigned char **buf, unsigned long *len, unsigned long *alloc_size);
  ```
- `net_error(NET_ERR_ZOOMBIG, n)` returns "Image too large for Zoom".

- [ ] **Step 1: Update the selector test (failing)**

In `tests/test_main.c` `test_selectors`, change both zoom expectations to:

```c
selector_zoom(s, sizeof s, 1); CHECK(strcmp(s, "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024") == 0);
selector_zoom(s, sizeof s, 0); CHECK(strcmp(s, "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024") == 0);
```

Run: `make test`. Expected: FAIL in `test_selectors`.

- [ ] **Step 2: Implement the selector**

```c
void selector_zoom(char *out, unsigned short max, int pal)
{
    (void)pal;   /* width-fit and scroll: the same box on PAL and NTSC */
    snprintf(out, max, "fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024");
}
```

Run: `make test`. Expected: `ALL PASS`.

- [ ] **Step 3: Add `NET_ERR_ZOOMBIG` and `net_fetch_image_buffer`**

`src/netmap.h`: append `NET_ERR_ZOOMBIG` to the enum, after `NET_ERR_NOIMAGE`.

`src/amiga/net.h`: add the defines and the prototype from the Interfaces block above.

`src/amiga/net.c`:
- add `#include "zoomscroll.h"` (for `bufgrow_next`);
- add `case NET_ERR_ZOOMBIG: return "Image too large for Zoom";` to `net_error`;
- add:

```c
unsigned char net_fetch_image_buffer(const char *img_url, const char *selector,
                                     unsigned char **out, unsigned long *out_len, unsigned long *out_alloc)
{
    static unsigned char chunk[512];
    fn_handle_t h;
    unsigned char *buf = 0, *nb;
    unsigned long size = 0, len = 0, want;
    unsigned short n, status = 0;
    uint32_t clen = 0;
    unsigned char fl = 0, ifl = 0, e;

    *out = 0; *out_len = 0; *out_alloc = 0;
    if ((e = ensure_up()) != FN_OK) return e;
    e = fn_open_translated(&h, FN_METHOD_GET, img_url, FN_OPEN_FOLLOW_REDIR, FN_TRANSLATE_IMAGE, 0, selector);
    if (e != FN_OK) return netmap_open_error(finish(e));
    for (;;) {
        e = read_chunk(h, len, chunk, sizeof chunk, &n, &fl, IMAGE_WAITS);
        if (e != FN_OK) break;
        if (!buf) {
            /* The first successful read means the translation is ready, so Info reports its size. */
            want = ZOOM_BUF_START;
            if (fn_info(h, &status, &clen, &ifl) == FN_OK && (ifl & FN_INFO_HAS_LENGTH) && clen > 0) {
                if (clen > ZOOM_BUF_MAX) { e = NET_ERR_ZOOMBIG; break; }
                want = clen;
            }
            if (want < n) want = n;
            if (!(buf = (unsigned char *)AllocMem(want, MEMF_ANY))) { e = NET_ERR_NOMEM; break; }
            size = want;
        }
        if (len + n > size) {
            unsigned long grow = bufgrow_next(size, len + n, ZOOM_BUF_MAX);
            if (!grow) { e = NET_ERR_ZOOMBIG; break; }
            if (!(nb = (unsigned char *)AllocMem(grow, MEMF_ANY))) { e = NET_ERR_NOMEM; break; }
            CopyMem(buf, nb, len);
            FreeMem(buf, size);
            buf = nb;
            size = grow;
        }
        CopyMem(chunk, buf + len, n);
        len += n;
        if ((fl & FN_READ_EOF) || n == 0) break;
    }
    fn_close(h);
    note_fn_error(e);
    if (e == FN_OK && buf) { *out = buf; *out_len = len; *out_alloc = size; return NET_OK; }
    /* A transport/read failure after some data keeps what arrived: zoom shows the complete rows. */
    if (buf && len > 0 && e < 0x80 && e != NETMAP_FN_UNSUPPORTED && e != NETMAP_FN_INVALID) {
        *out = buf; *out_len = len; *out_alloc = size;
        return NET_ERR_PARTIAL;
    }
    if (buf) FreeMem(buf, size);
    if (e == FN_OK) return NET_ERR_CONVERT;             /* EOF with no data */
    if (e == NETMAP_FN_UNSUPPORTED) return NET_ERR_TOOBIG;
    if (e == NETMAP_FN_INVALID) return NET_ERR_CONVERT;
    return e;
}
```

Check before relying on it:
- `net.c` already includes `<proto/exec.h>`, or the AllocMem/FreeMem/CopyMem prototypes and `MEMF_ANY` (`<exec/memory.h>`). Add any that are missing.
- `fn_info`'s length parameter type in fujinet-nio-lib `include/fujinet-nio.h` (it is `uint32_t *`).
- `FN_INFO_HAS_LENGTH` exists.

Match the existing types exactly so there are no warnings.

- [ ] **Step 4: Build**

Run: `make test` (ALL PASS), then `make amiga`. Expected: 0 warnings. `net_fetch_image_buffer` is unused until Task 4; if that yields an unused warning (it shouldn't for a non-static function), leave it.

- [ ] **Step 5: Commit**

```bash
git add src/selector.c src/netmap.h src/amiga/net.h src/amiga/net.c tests/test_main.c
git commit -m "Zoom: grey-4 width-fit selector and fetch-into-buffer for scrolling (make test, make amiga)"
```

---

### Task 4: Rewrite `zoom.c`: 2-plane scrolling Zoom; docs; Amiberry check

**Files:**
- Rewrite: `src/amiga/zoom.c` (`zoom.h` keeps `void zoom_show(const xkcd_comic_t *c);`)
- Modify: `README.md` (Controls section: Zoom keys and drag), `amiga/ReadMe.txt` (same), `AGENTS.md` (FujiNet contract: the Zoom selector; Amiga constraints: Zoom keeps the compressed image in memory and decodes visible rows)

**Interfaces:**
- Consumes:
  - `net_fetch_image_buffer`, `net_error`, `ZOOM_BUF_MAX` (Task 3);
  - `ilbm_doc_parse`, `ilbm_doc_decode_rows` (Task 1);
  - `zs_*` (Task 2);
  - `selector_zoom`;
  - `ui_is_pal`, `ui_status`, `ui_screen`, `ui_window`.

**Required behaviour** (exact constants):

```c
#define ZOOM_W      640
#define ZOOM_DEPTH  2
#define BAR_W       4
#define KEY_UP      0x4C
#define KEY_DOWN    0x4D
#define KEY_SPACE   0x40
#define KEY_BS      0x41
#define KEY_T       0x14
#define KEY_B       0x35
#define KEY_ESC     0x45
#define TEXT_PEN    3
#define ZOOM_CHIP_SLACK 16384UL
```

1. **Chip pre-check, as today, with `ZOOM_DEPTH` now 2.** `plane = ZOOM_W/8 * h`. If `AvailMem(MEMF_CHIP|MEMF_LARGEST) < plane || AvailMem(MEMF_CHIP) < ZOOM_DEPTH*plane + ZOOM_CHIP_SLACK`, call `ui_status("Not enough chip memory for Zoom")` and return.
2. **Open the screen and window.** Screen: 640 × h, depth 2, `HIRES|LACE`, `CUSTOMSCREEN|SCREENQUIET`, topaz 8, then `ShowTitle(scr, FALSE)`. Before the CMAP arrives, set pen 0 to white and pen 3 to black: `SetRGB4(vp,0,15,15,15)`, `SetRGB4(vp,3,0,0,0)`. Window:
   - IDCMP `RAWKEY|MOUSEBUTTONS|MOUSEMOVE`;
   - Flags `BACKDROP|BORDERLESS|ACTIVATE|RMBTRAP|REPORTMOUSE|SMART_REFRESH|NOCAREREFRESH`.
   - On failure, keep the existing close-screen, `ScreenToFront` and status path.
3. **Show "Loading #N..."** (the existing `zoom_text` helper, with `TEXT_PEN` 3 on pen 0).
4. **Fetch.** `e = net_fetch_image_buffer(c->img, sel, &buf, &len, &alloc)`.
   - If `buf == 0`: show `e == NET_ERR_NOMEM ? "Not enough memory for Zoom" : net_error(e, c->num)`, then go to the event loop with nothing to scroll.
5. **Index.**
   - Call `ilbm_doc_parse(&doc, buf, len, 0, 0)`. On `ILBMX_ERROR`, or `doc.info.planes > ZOOM_DEPTH`, or `doc.info.w > ZOOM_W`: show `net_error(NET_ERR_CONVERT, c->num)`.
   - Otherwise allocate `row_off` (`AllocMem(sizeof(unsigned long) * doc.info.h, MEMF_ANY)`). On failure show "Not enough memory for Zoom".
   - Then `r = ilbm_doc_parse(&doc, buf, len, row_off, doc.info.h)`. On `ILBMX_ERROR` show the convert error.
   - `ILBMX_PARTIAL`, or `e == NET_ERR_PARTIAL`, means "Image transfer interrupted", but still show and scroll `doc.rows` rows.
6. **Palette.** `SetRGB4` pens 0..3 from `doc.info.cmap` (bounded by `ncolors`), as the old header callback did.
7. **Geometry.**
   - `img_h = doc.rows`; `zs_init(&zs, img_h, h)`.
   - Horizontal: `x_byte = ((ZOOM_W - w) / 2 / 16) * 2`, clamped so `x_byte + doc.src_bpr <= BytesPerRow`.
   - If not scrollable: `y0 = (h - img_h) / 2`; otherwise `y0 = 0`.
   - Plane pointers for decode: `planes[p] = bm->Planes[p] + x_byte`, `bpr = bm->BytesPerRow`.
8. **First draw.** Clear the screen to pen 0, then `ilbm_doc_decode_rows(&doc, zs.top, visible, planes, ZOOM_DEPTH, bpr, y0)` with `visible = min(img_h, h)`. If scrollable, draw the bar. If the transfer was interrupted, show the message after drawing; it overlays the centre band.
9. **Bar** (only if scrollable).
   - Track: `RectFill(rp, ZOOM_W-BAR_W, 0, ZOOM_W-1, h-1)` in pen 1.
   - Thumb from `zs_thumb(&zs, h, &ty, &th)`: `RectFill(rp, ZOOM_W-BAR_W, ty, ZOOM_W-1, ty+th-1)` in pen 3.
10. **Event loop.** Drain all messages: copy `Class`, `Code`, `Qualifier` and `MouseY`, then `ReplyMsg` before acting. Sum a `delta`:
    - RAWKEY key-down only (`!(code & IECODE_UP_PREFIX)`). Shift is `Qualifier & (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)`.
      - `KEY_ESC` → done.
      - `KEY_UP`/`KEY_DOWN` → `∓ZS_LINE`, or `∓zs_page()` with Shift.
      - `KEY_SPACE` → `+zs_page()`; `KEY_BS` → `−zs_page()`.
      - `KEY_T` → `−zs.top`; `KEY_B` → `zs_max_top()−zs.top`.
    - `MOUSEBUTTONS`: `SELECTDOWN` → dragging = 1, `last_y = MouseY`; `SELECTUP` → dragging = 0.
    - `MOUSEMOVE` while dragging: `delta += last_y − MouseY`; `last_y = MouseY`.
11. **Apply the scroll once per drained batch.**
    - `applied = zs_scroll(&zs, delta)`; if 0, do nothing.
    - `zs_exposed(applied, h, &f, &cnt)`.
    - If `cnt < h`: `ScrollRaster(rp, 0, applied, 0, 0, ZOOM_W-BAR_W-1, h-1)`. `ScrollRaster` clears the vacated band to the BgPen; keep `SetBPen(rp, 0)`.
    - Else clear `0..ZOOM_W-BAR_W-1` × `0..h-1` to pen 0.
    - Then `ilbm_doc_decode_rows(&doc, zs.top + f, cnt, planes, ZOOM_DEPTH, bpr, f)`.
    - Redraw the bar. Then `Wait` for the next signal.
12. **Exit.** `CloseWindow`, `CloseScreen`, then `FreeMem(row_off, ...)` and `FreeMem(buf, alloc)` if allocated, then `ScreenToFront(ui_screen())` and `ActivateWindow(ui_window())`. No path may leak the buffer or the index; verify every early-exit path.

Decoding writes straight into the screen bitmap, which is as safe as today's Zoom: it is a backdrop window on its own screen, with no other windows or menus over it (`RMBTRAP`).

- [ ] **Step 1: Implement `zoom.c`** per the behaviour above.
  - Keep the existing helpers `zoom_text` and `zoom_clear`, adapted to `TEXT_PEN` 3.
  - Remove `zoom_header_cb`, and `ui_place_image` use, from `zoom.c`. Leave `ui_place_image` itself in `ui.c`, because the main view still uses it.
  - One statement per line where practical. Match the style of the surrounding Amiga code.
- [ ] **Step 2: Build**

Run: `make test` (ALL PASS) and `make amiga`. Expected: 0 warnings.

- [ ] **Step 3: Docs.**
  - `README.md` Controls: Zoom is width-fitted and grey; it lists the scroll keys, T/B and drag.
  - `amiga/ReadMe.txt`: the same in short form.
  - `AGENTS.md`: the Zoom selector string, and that Zoom keeps the compressed ILBM in `MEMF_ANY` memory (≤ 256 KB) and decodes only the visible rows.
- [ ] **Step 4: Amiberry manual check (KS 1.3, then 2.04).**
  - Use the workspace recipe: the POSIX firmware `./build/fujibus-tcp-debug/fujinet-nio` on port 65504, and `./scripts/build.sh amiga-workbench` with a profile that shares `fujinet-xkcd/r2r/amiga` as `XKCD:`, plus `scripts/amiberry-ipc` / `amiberry-type`.
  - For 2.04, use `build/amiga-envs/wb204` (a scratch copy of `base.hdf`) with `AMIBERRY_FAST_FILE_SYSTEM` pointing to its FastFileSystem, and `AMIBERRY_EXTRA_FLOPPY_0` for the ADF built with `tools/make-nio-adf.sh`.
  - Check each item and record PASS/FAIL with a screenshot path in the report:
    - [ ] Fetch ID 2636 → Zoom: the top of the comic is full-width, grey and readable, and the bar is shown.
    - [ ] Down/Up, Shift+Down, Space/Backspace, T, B all scroll correctly; the bar thumb tracks the position.
    - [ ] Left-button drag scrolls; release stops.
    - [ ] A short comic (e.g. #353) is centred with no bar; scroll keys do nothing.
    - [ ] A colour comic (e.g. #1732) shows in grey.
    - [ ] Esc returns to the main window with the same comic; main-window behaviour is unchanged.
    - [ ] Chip memory (`avail chip` in a Shell) is unchanged after 10 Zoom enter/exit cycles.

  If an emulator item cannot be run, mark it NOT RUN with the exact blocker. Kill every emulator and firmware process you start. Never use a `pkill -f` pattern that matches your own shell command line; kill by PID. Do not modify workspace configs or HDFs permanently; use scratch copies.
- [ ] **Step 5: Commit**

```bash
git add src/amiga/zoom.c README.md amiga/ReadMe.txt AGENTS.md
git commit -m "Zoom: width-fitted grey-4 view that scrolls tall comics (keys, drag, position bar)"
```
