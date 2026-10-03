# fujinet-xkcd (Amiga 500) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An Amiga 500 (Kickstart 1.3+) program, built with the MekkoGX framework, that shows random xkcd comics (image and caption) fetched through FujiNet NIO. FujiNet converts each PNG/JPEG/GIF into a native Amiga IFF ILBM on the fly.

**Architecture:** There are three layers, each in its own repository.
1. **fujinet-nio firmware** gets a new `Image` content translator. It decodes the downloaded image, scales it to a requested box (correcting for pixel aspect), reduces it to an OCS 12-bit palette, dithers it, packs it into bitplanes and returns a ByteRun1-compressed IFF ILBM through the existing translated-read path.
2. **fujinet-nio-lib** gets `fn_open_translated()`, which sends the open-extension block that non-BBC targets cannot send today.
3. **fujinet-xkcd**, a new repo created from the MekkoGX template, adds an `amiga` platform and an `amigagcc` toolchain to MekkoGX. Its client is a raw-Intuition (V33) app. It parses the xkcd JSON on the Amiga and stream-decodes the ILBM straight into screen bitplanes, so it never buffers a whole image.

**Tech Stack:**
- Firmware: C++17 with doctest; `stb_image` (public domain) handles PNG, JPEG and GIF decoding.
- Library: C99.
- App: amiga-gcc (`m68k-amigaos-gcc -mcrt=nix13`), Intuition/Graphics/Exec V33 and timer.device; host-side tests use gcc.
- Build: GNU make (MekkoGX); `xdftool` (amitools) builds the ADF.

**Spec:** this document. The **Requirements** section below is the spec, taken from the user's request on 2026-10-02 and the answers they gave in this session. There is no separate spec file.

## Requirements (spec)

R1. Use the xkcd JSON API (`https://xkcd.com/info.0.json` for the latest comic, `https://xkcd.com/<n>/info.0.json` for comic n). Show a **random** comic with its **image and caption**. The caption shows the title, plus the `alt` text, which is xkcd's real caption.
R2. **Previous** and **Next** buttons. Previous walks back through the last **25** comics viewed. Next moves forward through that history. Past the newest entry, Next fetches a new random comic.
R3. A **Zoom** button shows the image full-screen. **Esc** leaves full-screen.
R4. There is one menu, with the items **Fetch ID**, **Auto Refresh** and **Quit**.
R5. **Fetch ID** opens a window with a number field, **OK** and **Cancel**. OK fetches and shows that comic. A comic ID that does not exist shows an appropriate error.
R6. **Auto Refresh** opens a window with a seconds selector (**10–600**) and **Start**, **Stop** and **Cancel** buttons:
- Start begins fetching a random comic every N seconds.
- Stop ends the cycle.
- Cancel closes the window and changes nothing.
R7. FujiNet converts web images (PNG, plus the older JPEG and GIF comics) into a format the Amiga shows natively.
R8. The app uses fujinet-nio (fujinet-nio-lib → `fujinet-nio.device` broker → FujiNet NIO firmware).
R9. The repository follows MekkoGX conventions:
- Top-level `Makefile` holds `PRODUCT`, `PLATFORMS` and `SRC_DIRS = src src/%PLATFORM%`.
- Output goes to `r2r/<platform>/`.
- Reusable rules live only in `mekkogx/`.
- `.github/workflows/ci.yml` runs `make <platform>/r2r`.

## Global Constraints

- Kickstart/Workbench **1.3 (V33)** is the floor. Do not use GadTools, ASL, `CreateMsgPort` or any other V36+ call. Use `CreatePort`/`DeletePort`/`CreateExtIO` from amiga.lib. Link with `-mcrt=nix13`. (User decision, 2026-10-02.)
- CPU is a 68000: `-mcpu=68000 -msoft-float`, with no floating point anywhere in the Amiga code.
- Minimum machine is an A500 with **512 KB chip RAM**. Every chip allocation that fails must produce a message, never a crash.
- Main screen is **hires 640 × 256 (PAL) or 640 × 200 (NTSC), 4 bitplanes**:
  - Pens 0–3 are UI pens.
  - Comic images use pens 4–15 (`base=4,colors=12`).
  - Pick PAL or NTSC from `GfxBase->DisplayFlags & PAL`.
- Zoom screen is **hires interlaced 640 × 512 (PAL) or 640 × 400 (NTSC), 4 bitplanes**, with comic images using pens 0–15 (`base=0,colors=16`). (User decision, 2026-10-02.)
- Comic IDs are positive integers. The app gets the upper bound at run time from `info.0.json` and never hard-codes it.
- History depth is exactly **25**.
- The auto-refresh range is **10–600 seconds**, inclusive, with a default of 60.
- The FujiNet image selector grammar (Task 1) is the only contract between the firmware and the app. Both sides must agree on it character for character.
- Workspace rules from `fujinet-nio-workspace/AGENTS.md` apply to firmware and library work:
  - Never push.
  - Never add `Co-authored-by` trailers in the fujinet-nio-workspace submodules.
  - Run `make check` for every change under `repos/fujinet-nio-lib`.
  - Source `"$NIO_WORKSPACE/scripts/env.sh"` before building.
- Files under `mekkogx/` are "do not customize". New platform and toolchain files go there because they are reusable (not project-specific), and Task 5 prepares them as an upstream PR to FozzTexx/MekkoGX.

## Review Focus

1. **Comic with no static image.** Interactive comics such as #1608, #1663 and #2198 have `img` ending in `/comics/` or a non-image URL. Expected: the caption shows, and the image area says "No static image for this comic"; there is no hang and no crash. Pinned by the test in Task 6, `test_xkcd_parse_no_image`, and the handling in Task 9.
2. **Comic #404 and out-of-range IDs.** xkcd returns HTTP 404 for `/404/info.0.json` (it is a joke) and for IDs above the latest. Expected: "Comic #N does not exist", with the current comic left on screen, and random picks never choosing 404. Pinned by `test_random_pick_skips_404` in Task 6 and the status handling in Task 8.
3. **Very large source images** such as #657 and #1190, which are bigger than ~1000 × 1000. Expected: either a downscaled render, or a clear "Image too large for FujiNet to convert" message; the firmware must not exhaust memory. Pinned by `ImageTranslator rejects images above pixel cap` in Task 2.
4. **JSON escapes in title/alt** (`\"`, `\\`, `\n`, `\u2019`, `\u00e9`). Expected: readable Latin-1 text, with unmappable code points shown as `?` and no garbage bytes. Pinned by `test_json_unescape` in Task 6.
5. **Network failure mid-image, or no NIO driver loaded.** Expected:
   - If the transfer stops partway, the partly drawn image stays and an error shows in the status line.
   - If the driver is not loaded, the app shows "NIO driver not loaded" and the UI stays usable. Auto refresh keeps its schedule and retries at the next tick.

   Pinned by `test_ilbm_truncated_body` in Task 7 and the error paths in Task 8.

---

## File Structure

### fujinet-nio (firmware): `$NIO_WORKSPACE/repos/fujinet-nio`

| File | Responsibility |
|---|---|
| `include/fujinet/io/devices/network_translation.h` (modify) | add `ContentTranslationType::Image = 4` |
| `include/fujinet/io/devices/image_convert.h` (create) | pure conversion pipeline API: selector parse, scale, palette, dither, planar, ByteRun1, ILBM write |
| `src/lib/image_convert.cpp` (create) | implementation of the above; no I/O and no ESP-IDF code |
| `include/fujinet/io/devices/image_content_translator.h` (create) | `IContentTranslator` adapter |
| `src/lib/image_content_translator.cpp` (create) | decodes with stb_image, then calls the pipeline |
| `src/lib/stb_image_impl.cpp` (create) | the single `STB_IMAGE_IMPLEMENTATION` translation unit |
| `third_party/stb/stb_image.h` (create) | vendored stb_image v2.30 |
| `src/lib/network_device.cpp` (modify) | `make_translator()` returns `ImageContentTranslator` |
| `src/CMakeLists.txt`, `CMakeLists_posix.cmake` (modify) | add the sources and the include directory (via `scripts/update_cmake_sources.py`) |
| `tests/test_image_convert.cpp` (create) | doctest unit tests |
| `docs/network_device_protocol.md` (modify) | document translation type 4 and the selector |

### fujinet-nio-lib: `$NIO_WORKSPACE/repos/fujinet-nio-lib`

| File | Responsibility |
|---|---|
| `include/fujinet-nio.h` (modify) | `FN_TRANSLATE_*` constants and the `fn_open_translated()` prototype |
| `include/fn_internal.h` (modify) | `fn_build_open_packet_ext()` prototype |
| `src/common/fn_packet_build_open.c` (modify) | the ext-block builder; the old builder delegates to it |
| `src/common/fn_open.c` (modify) | `fn_open()` → `fn_open_translated(..., FN_TRANSLATE_NONE, 0, NULL)` |
| `src/platform/bbc/fn_open_translated_bbc.c` (create) | BBC stub that returns `FN_ERR_UNSUPPORTED` for anything other than NONE |
| `tests/open_ext_wire_test.c` and `tests/run_open_ext_wire_test.sh` (create) | wire test |
| `Makefile` (modify) | add `test-open-ext` to the `test` target |
| `docs/api.md` (modify) | document the new function |

### fujinet-xkcd (this repo): `/home/bkrein/dev/fujinet-xkcd`

| File | Responsibility |
|---|---|
| `Makefile` | MekkoGX top-level: PRODUCT, PLATFORMS, SRC_DIRS, NIO lib wiring, ADF hook |
| `mekkogx/**` | copied from upstream MekkoGX at commit `2c64656` |
| `mekkogx/toolchains/amigagcc.mk` (create) | the amiga-gcc toolchain for MekkoGX |
| `mekkogx/platforms/amiga.mk` (create) | the Amiga platform: executable plus OFS `.adf` disk via xdftool |
| `.github/workflows/ci.yml` | adds the `amiga` matrix entry and a host-test job |
| `src/xkcd.c` and `src/xkcd.h` | URL building, JSON → `xkcd_comic_t`, random pick, error text (portable) |
| `src/json.c` and `src/json.h` | a minimal flat-object string/number extractor with an unescaper (portable) |
| `src/history.c` and `src/history.h` | the 25-entry back/forward ring (portable) |
| `src/ilbm.c` and `src/ilbm.h` | the streaming IFF ILBM / ByteRun1 decoder into caller bitplanes (portable) |
| `src/wrap.c` and `src/wrap.h` | caption word-wrap into fixed columns (portable) |
| `src/selector.c` and `src/selector.h` | builds FujiNet image selector strings (portable) |
| `src/amiga/main.c` | startup, event loop, wiring |
| `src/amiga/net.c` and `src/amiga/net.h` | fujinet-nio fetch helpers: JSON into a buffer, image streamed through the ILBM decoder |
| `src/amiga/ui.c` and `src/amiga/ui.h` | main screen and window, buttons, menu, caption area, status line |
| `src/amiga/dialogs.c` and `src/amiga/dialogs.h` | the Fetch ID and Auto Refresh windows |
| `src/amiga/zoom.c` and `src/amiga/zoom.h` | the interlaced full-screen view and Esc handling |
| `src/amiga/timer.c` and `src/amiga/timer.h` | timer.device UNIT_VBLANK one-shot for auto refresh |
| `src/amiga/rng.c` and `src/amiga/rng.h` | xorshift32 seeded from DateStamp and VHPOSR |
| `tests/test_main.c` and `tests/Makefile.host` | host test runner for every portable module |
| `amiga/ReadMe.txt`, `amiga/icons/*.info` | disk contents |
| `README.md`, `.gitignore` | project docs |

`SRC_DIRS = src src/%PLATFORM%` makes the portable modules in `src/` part of every platform build, while `src/amiga/` builds only for `amiga`. Host tests compile `src/*.c` with gcc.

---

## Part A: FujiNet firmware image translator (repo `fujinet-nio`)

### Task 1: Pure image-conversion pipeline

**Files:**
- Create: `include/fujinet/io/devices/image_convert.h`, `src/lib/image_convert.cpp`, `tests/test_image_convert.cpp`
- Modify: `src/CMakeLists.txt`, `CMakeLists_posix.cmake` (run `python3 scripts/update_cmake_sources.py`)

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `fujinet::io::image`):
  ```cpp
  struct Options { int w=640, h=400, colors=16, base=0, parX=1, parY=1; bool dither=true; enum Mode{Auto,Gray,Color} mode=Auto; bool upscale=false; };
  bool parse_selector(const std::string& sel, Options& out);           // false on any bad key/value
  int  planes_for(const Options& o);                                    // ceil(log2(base+colors)), 1..5
  struct Rgb { std::uint8_t r,g,b; };
  struct Size { int w, h; };
  Size fit_size(int srcW, int srcH, const Options& o);                  // output pixel dims
  std::vector<std::uint8_t> scale_rgb(const std::uint8_t* rgb, int sw, int sh, Size out);   // box filter, 3 bytes/px
  std::vector<Rgb> make_palette(const std::vector<std::uint8_t>& rgb, const Options& o);    // o.colors entries, OCS 4-bit/channel values *17
  std::vector<std::uint8_t> map_pixels(const std::vector<std::uint8_t>& rgb, Size s, const std::vector<Rgb>& pal, const Options& o); // indices base..base+colors-1
  std::vector<std::uint8_t> byterun1(const std::uint8_t* row, std::size_t n);
  std::vector<std::uint8_t> write_ilbm(const std::vector<std::uint8_t>& idx, Size s, const std::vector<Rgb>& pal, const Options& o);
  ```

**Selector grammar (the firmware–app contract):** the selector is ASCII `key=value` pairs separated by `,`. Every key is optional and may appear at most once. Unknown keys are an error.

| key | values | default | meaning |
|---|---|---|---|
| `w` | 16..1024 | 640 | max output width in pixels |
| `h` | 16..1024 | 400 | max output height in pixels |
| `colors` | 2..32 | 16 | number of image colours |
| `base` | 0..30 | 0 | first pen index; `base+colors ≤ 32` |
| `par` | `X:Y`, 1..4 each | `1:1` | display pixel aspect (width:height); hires non-laced is `1:2` |
| `dither` | `fs` \| `none` | `fs` | Floyd–Steinberg error diffusion or nearest colour |
| `mode` | `auto` \| `gray` \| `color` | `auto` | `auto` = gray if every pixel has max(r,g,b)-min(r,g,b) ≤ 24 |
| `up` | `0` \| `1` | `0` | allow enlarging images smaller than the box |

**Output format:**
- A standard `FORM ILBM` containing BMHD, CMAP and BODY.
- BMHD values:
  - `w`/`h` are the output dimensions, `x=y=0`.
  - `nPlanes = planes_for(o)`, `masking=0`, `compression=1` (ByteRun1), `transparentColor=0`.
  - `xAspect=parX`, `yAspect=parY`, `pageWidth=w`, `pageHeight=h`.
- CMAP has `3 << nPlanes` bytes. Entries below `base` and at or above `base+colors` are written as 0.
- BODY rows are `((w+15)/16)*2` bytes per plane. Each row and each plane is ByteRun1-compressed separately, in plane order 0..n-1.
- Chunks with an odd length get a pad byte, and the FORM length is correct.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_image_convert.cpp
#include "doctest.h"
#include "fujinet/io/devices/image_convert.h"
using namespace fujinet::io::image;

TEST_CASE("ImageConvert: selector defaults and full parse") {
    Options o;
    CHECK(parse_selector("", o));
    CHECK(o.w == 640); CHECK(o.h == 400); CHECK(o.colors == 16); CHECK(o.base == 0);
    CHECK(parse_selector("w=624,h=190,colors=12,base=4,par=1:2,dither=none,mode=gray,up=1", o));
    CHECK(o.w == 624); CHECK(o.h == 190); CHECK(o.colors == 12); CHECK(o.base == 4);
    CHECK(o.parX == 1); CHECK(o.parY == 2); CHECK(!o.dither); CHECK(o.mode == Options::Gray); CHECK(o.upscale);
    CHECK(planes_for(o) == 4);
}

TEST_CASE("ImageConvert: selector rejects bad input") {
    Options o;
    CHECK_FALSE(parse_selector("w=0", o));
    CHECK_FALSE(parse_selector("colors=40", o));
    CHECK_FALSE(parse_selector("base=28,colors=8", o));   // 36 > 32
    CHECK_FALSE(parse_selector("bogus=1", o));
    CHECK_FALSE(parse_selector("w=10,w=20", o));
    CHECK_FALSE(parse_selector("par=0:1", o));
}

TEST_CASE("ImageConvert: fit_size keeps aspect with pixel-aspect correction") {
    // par=1:2: each output row is displayed twice as tall, so 300 square source rows need 150 output rows at scale 1.
    // Box 640x100: sx = 640*1024/740 = 885, sy = 100*2*1024/300 = 682 -> s = 682 (height-limited).
    Options o; parse_selector("w=640,h=100,par=1:2", o);
    Size s = fit_size(740, 300, o);
    CHECK(s.w == 492); CHECK(s.h == 99);     // 740*682/1024 = 492, 300*682/2048 = 99 (integer truncation)
    Options q; parse_selector("w=640,h=200,par=1:2", q);
    Size u = fit_size(740, 300, q);          // width-limited: s = 885
    CHECK(u.w == 639); CHECK(u.h == 129);
    Options p; parse_selector("w=640,h=400", p);
    Size t = fit_size(100, 50, p);           // no upscale by default
    CHECK(t.w == 100); CHECK(t.h == 50);
}

TEST_CASE("ImageConvert: byterun1 encodes runs and literals") {
    const std::uint8_t row[] = {0,0,0,0,1,2,3,3};
    auto e = byterun1(row, sizeof row);
    const std::vector<std::uint8_t> want = {0xFD,0x00, 0x01,1,2, 0xFF,3}; // run4 of 0, lit 2, run2 of 3
    CHECK(e == want);
}

TEST_CASE("ImageConvert: gray palette spans black to white") {
    std::vector<std::uint8_t> rgb = {0,0,0, 255,255,255, 128,128,128, 64,64,64};
    Options o; parse_selector("colors=4,mode=gray", o);
    auto pal = make_palette(rgb, o);
    REQUIRE(pal.size() == 4);
    CHECK(pal.front().r == 0); CHECK(pal.back().r == 255);
}

TEST_CASE("ImageConvert: ILBM header, CMAP base offset and indices") {
    std::vector<std::uint8_t> rgb(16*2*3, 255); rgb[0]=rgb[1]=rgb[2]=0;   // 16x2, first pixel black
    Options o; parse_selector("w=16,h=2,colors=2,base=4,mode=gray,dither=none", o);
    auto pal = make_palette(rgb, o);
    auto idx = map_pixels(rgb, {16,2}, pal, o);
    CHECK(idx[0] == 4); CHECK(idx[1] == 5);
    auto f = write_ilbm(idx, {16,2}, pal, o);
    CHECK(std::string(f.begin(), f.begin()+4) == "FORM");
    CHECK(std::string(f.begin()+8, f.begin()+12) == "ILBM");
    CHECK(std::string(f.begin()+12, f.begin()+16) == "BMHD");
    CHECK(f[28] == 3);                       // nPlanes for base 4 + 2 colours = 6 pens -> 3 planes
    CHECK(f[30] == 1);                       // compression = ByteRun1
    std::uint32_t formLen = (f[4]<<24)|(f[5]<<16)|(f[6]<<8)|f[7];
    CHECK(formLen + 8 == f.size());
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cd $NIO_WORKSPACE/repos/fujinet-nio && python3 scripts/update_cmake_sources.py && ./build.sh -cp fujibus-pty-debug`
Expected: compile FAIL with `image_convert.h: No such file or directory`.

- [ ] **Step 3: Implement `image_convert.h` and `image_convert.cpp`**

```cpp
// include/fujinet/io/devices/image_convert.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fujinet::io::image {

struct Options {
    int w = 640, h = 400, colors = 16, base = 0, parX = 1, parY = 1;
    bool dither = true;
    enum Mode { Auto, Gray, Color } mode = Auto;
    bool upscale = false;
};
struct Rgb { std::uint8_t r, g, b; };
struct Size { int w, h; };

bool parse_selector(const std::string& sel, Options& out);
int planes_for(const Options& o);
Size fit_size(int srcW, int srcH, const Options& o);
std::vector<std::uint8_t> scale_rgb(const std::uint8_t* rgb, int sw, int sh, Size out);
std::vector<Rgb> make_palette(const std::vector<std::uint8_t>& rgb, const Options& o);
std::vector<std::uint8_t> map_pixels(const std::vector<std::uint8_t>& rgb, Size s,
                                     const std::vector<Rgb>& pal, const Options& o);
std::vector<std::uint8_t> byterun1(const std::uint8_t* row, std::size_t n);
std::vector<std::uint8_t> write_ilbm(const std::vector<std::uint8_t>& idx, Size s,
                                     const std::vector<Rgb>& pal, const Options& o);
}
```

```cpp
// src/lib/image_convert.cpp
#include "fujinet/io/devices/image_convert.h"
#include <algorithm>
#include <cstdlib>

namespace fujinet::io::image {

static bool to_int(const std::string& s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 4) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    int v = std::atoi(s.c_str());
    if (v < lo || v > hi) return false;
    out = v; return true;
}

bool parse_selector(const std::string& sel, Options& out) {
    Options o; unsigned seen = 0;
    static const char* keys[] = {"w","h","colors","base","par","dither","mode","up"};
    std::size_t pos = 0;
    while (pos < sel.size()) {
        std::size_t end = sel.find(',', pos);
        if (end == std::string::npos) end = sel.size();
        std::string kv = sel.substr(pos, end - pos);
        pos = end + 1;
        std::size_t eq = kv.find('=');
        if (eq == std::string::npos) return false;
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        int ki = -1;
        for (int i = 0; i < 8; ++i) if (k == keys[i]) ki = i;
        if (ki < 0 || (seen & (1u << ki))) return false;
        seen |= 1u << ki;
        bool ok = true;
        switch (ki) {
            case 0: ok = to_int(v, 16, 1024, o.w); break;
            case 1: ok = to_int(v, 16, 1024, o.h); break;
            case 2: ok = to_int(v, 2, 32, o.colors); break;
            case 3: ok = to_int(v, 0, 30, o.base); break;
            case 4: { std::size_t c = v.find(':');
                      ok = c != std::string::npos && to_int(v.substr(0, c), 1, 4, o.parX)
                           && to_int(v.substr(c + 1), 1, 4, o.parY); } break;
            case 5: if (v == "fs") o.dither = true; else if (v == "none") o.dither = false; else ok = false; break;
            case 6: if (v == "auto") o.mode = Options::Auto; else if (v == "gray") o.mode = Options::Gray;
                    else if (v == "color") o.mode = Options::Color; else ok = false; break;
            case 7: if (v == "0") o.upscale = false; else if (v == "1") o.upscale = true; else ok = false; break;
        }
        if (!ok) return false;
    }
    if (o.base + o.colors > 32) return false;
    out = o; return true;
}

int planes_for(const Options& o) {
    int pens = o.base + o.colors, p = 1;
    while ((1 << p) < pens) ++p;
    return p;
}

Size fit_size(int srcW, int srcH, const Options& o) {
    // Display height of one output row is parY/parX of its width; source pixels are square.
    // Work in 1/1024 fixed point to stay integer-only.
    long sx = (long)o.w * 1024 / srcW;
    long sy = (long)o.h * o.parY * 1024 / ((long)srcH * o.parX);
    long s = std::min(sx, sy);
    if (!o.upscale) s = std::min(s, 1024L);
    int w = (int)std::max(1L, srcW * s / 1024);
    int h = (int)std::max(1L, (long)srcH * o.parX * s / (1024L * o.parY));
    return {std::min(w, o.w), std::min(h, o.h)};
}

std::vector<std::uint8_t> scale_rgb(const std::uint8_t* rgb, int sw, int sh, Size out) {
    std::vector<std::uint8_t> d((std::size_t)out.w * out.h * 3);
    for (int y = 0; y < out.h; ++y) {
        int y0 = (int)((long)y * sh / out.h), y1 = std::max(y0 + 1, (int)((long)(y + 1) * sh / out.h));
        for (int x = 0; x < out.w; ++x) {
            int x0 = (int)((long)x * sw / out.w), x1 = std::max(x0 + 1, (int)((long)(x + 1) * sw / out.w));
            long acc[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx, ++n)
                    for (int c = 0; c < 3; ++c) acc[c] += rgb[((std::size_t)yy * sw + xx) * 3 + c];
            for (int c = 0; c < 3; ++c) d[((std::size_t)y * out.w + x) * 3 + c] = (std::uint8_t)(acc[c] / n);
        }
    }
    return d;
}

static bool is_gray(const std::vector<std::uint8_t>& rgb) {
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
        int mx = std::max({rgb[i], rgb[i+1], rgb[i+2]}), mn = std::min({rgb[i], rgb[i+1], rgb[i+2]});
        if (mx - mn > 24) return false;
    }
    return true;
}

static std::uint8_t ocs(int v) { return (std::uint8_t)(((v + 8) / 17) * 17); }  // snap to 4-bit/channel

std::vector<Rgb> make_palette(const std::vector<std::uint8_t>& rgb, const Options& o) {
    std::vector<Rgb> pal;
    bool gray = o.mode == Options::Gray || (o.mode == Options::Auto && is_gray(rgb));
    if (gray) {
        for (int i = 0; i < o.colors; ++i) { auto v = ocs(i * 255 / (o.colors - 1)); pal.push_back({v, v, v}); }
        return pal;
    }
    // Median cut over the 4096-entry OCS histogram.
    struct Bin { int r, g, b; long n; };
    std::vector<long> hist(4096, 0);
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3)
        ++hist[((rgb[i] >> 4) << 8) | ((rgb[i+1] >> 4) << 4) | (rgb[i+2] >> 4)];
    std::vector<Bin> bins;
    for (int k = 0; k < 4096; ++k) if (hist[k]) bins.push_back({k >> 8, (k >> 4) & 15, k & 15, hist[k]});
    struct Box { std::size_t lo, hi; };
    std::vector<Box> boxes{{0, bins.size()}};
    auto range = [&](const Box& b, int ch) {
        int mn = 15, mx = 0;
        for (std::size_t i = b.lo; i < b.hi; ++i) { int v = ch == 0 ? bins[i].r : ch == 1 ? bins[i].g : bins[i].b; mn = std::min(mn, v); mx = std::max(mx, v); }
        return mx - mn;
    };
    while ((int)boxes.size() < o.colors) {
        int best = -1, bestCh = 0, bestR = 0;
        for (int i = 0; i < (int)boxes.size(); ++i) {
            if (boxes[i].hi - boxes[i].lo < 2) continue;
            for (int ch = 0; ch < 3; ++ch) { int r = range(boxes[i], ch); if (r > bestR) { bestR = r; best = i; bestCh = ch; } }
        }
        if (best < 0) break;
        Box b = boxes[best];
        std::sort(bins.begin() + b.lo, bins.begin() + b.hi, [bestCh](const Bin& a, const Bin& c) {
            return (bestCh == 0 ? a.r : bestCh == 1 ? a.g : a.b) < (bestCh == 0 ? c.r : bestCh == 1 ? c.g : c.b); });
        long total = 0; for (std::size_t i = b.lo; i < b.hi; ++i) total += bins[i].n;
        long acc = 0; std::size_t mid = b.lo;
        while (mid < b.hi - 1 && acc + bins[mid].n <= total / 2) acc += bins[mid++].n;
        if (mid == b.lo) ++mid;
        boxes[best] = {b.lo, mid}; boxes.push_back({mid, b.hi});
    }
    for (auto& b : boxes) {
        long r = 0, g = 0, bl = 0, n = 0;
        for (std::size_t i = b.lo; i < b.hi; ++i) { r += bins[i].r * bins[i].n; g += bins[i].g * bins[i].n; bl += bins[i].b * bins[i].n; n += bins[i].n; }
        if (n) pal.push_back({(std::uint8_t)(r / n * 17), (std::uint8_t)(g / n * 17), (std::uint8_t)(bl / n * 17)});
    }
    while ((int)pal.size() < o.colors) pal.push_back({0, 0, 0});
    return pal;
}

static int nearest(const std::vector<Rgb>& pal, int r, int g, int b) {
    int best = 0; long bd = 1L << 30;
    for (int i = 0; i < (int)pal.size(); ++i) {
        long dr = r - pal[i].r, dg = g - pal[i].g, db = b - pal[i].b;
        long d = 3 * dr * dr + 6 * dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

std::vector<std::uint8_t> map_pixels(const std::vector<std::uint8_t>& rgb, Size s,
                                     const std::vector<Rgb>& pal, const Options& o) {
    std::vector<std::uint8_t> idx((std::size_t)s.w * s.h);
    std::vector<int> err((std::size_t)(s.w + 2) * 2 * 3, 0);   // two error rows, 1px border each side
    for (int y = 0; y < s.h; ++y) {
        int* cur = &err[(std::size_t)(y & 1) * (s.w + 2) * 3];
        int* nxt = &err[(std::size_t)((y + 1) & 1) * (s.w + 2) * 3];
        std::fill(nxt, nxt + (s.w + 2) * 3, 0);
        for (int x = 0; x < s.w; ++x) {
            const std::uint8_t* p = &rgb[((std::size_t)y * s.w + x) * 3];
            int c[3];
            for (int k = 0; k < 3; ++k) c[k] = std::clamp(p[k] + (o.dither ? cur[(x + 1) * 3 + k] / 16 : 0), 0, 255);
            int i = nearest(pal, c[0], c[1], c[2]);
            idx[(std::size_t)y * s.w + x] = (std::uint8_t)(o.base + i);
            if (!o.dither) continue;
            int e[3] = {c[0] - pal[i].r, c[1] - pal[i].g, c[2] - pal[i].b};
            for (int k = 0; k < 3; ++k) {
                cur[(x + 2) * 3 + k] += e[k] * 7;
                nxt[(x)     * 3 + k] += e[k] * 3;
                nxt[(x + 1) * 3 + k] += e[k] * 5;
                nxt[(x + 2) * 3 + k] += e[k] * 1;
            }
        }
    }
    return idx;
}

std::vector<std::uint8_t> byterun1(const std::uint8_t* row, std::size_t n) {
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < n) {
        std::size_t run = 1;
        while (i + run < n && run < 128 && row[i + run] == row[i]) ++run;
        if (run >= 2) { out.push_back((std::uint8_t)(257 - run)); out.push_back(row[i]); i += run; continue; }
        std::size_t lit = 1;
        while (i + lit < n && lit < 128 && !(i + lit + 1 < n && row[i + lit] == row[i + lit + 1])) ++lit;
        out.push_back((std::uint8_t)(lit - 1));
        out.insert(out.end(), row + i, row + i + lit);
        i += lit;
    }
    return out;
}

static void put32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((std::uint8_t)(x >> s)); }
static void put16(std::vector<std::uint8_t>& v, std::uint16_t x) { v.push_back((std::uint8_t)(x >> 8)); v.push_back((std::uint8_t)x); }
static void tag(std::vector<std::uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

std::vector<std::uint8_t> write_ilbm(const std::vector<std::uint8_t>& idx, Size s,
                                     const std::vector<Rgb>& pal, const Options& o) {
    const int planes = planes_for(o), bpr = ((s.w + 15) / 16) * 2;
    std::vector<std::uint8_t> f;
    tag(f, "FORM"); put32(f, 0); tag(f, "ILBM");
    tag(f, "BMHD"); put32(f, 20);
    put16(f, (std::uint16_t)s.w); put16(f, (std::uint16_t)s.h); put16(f, 0); put16(f, 0);
    f.push_back((std::uint8_t)planes); f.push_back(0); f.push_back(1); f.push_back(0);
    put16(f, 0); f.push_back((std::uint8_t)o.parX); f.push_back((std::uint8_t)o.parY);
    put16(f, (std::uint16_t)s.w); put16(f, (std::uint16_t)s.h);
    tag(f, "CMAP"); put32(f, 3u << planes);
    for (int i = 0; i < (1 << planes); ++i) {
        int k = i - o.base;
        if (k >= 0 && k < (int)pal.size()) { f.push_back(pal[k].r); f.push_back(pal[k].g); f.push_back(pal[k].b); }
        else { f.push_back(0); f.push_back(0); f.push_back(0); }
    }
    tag(f, "BODY"); std::size_t bodyLenAt = f.size(); put32(f, 0);
    std::vector<std::uint8_t> row(bpr);
    for (int y = 0; y < s.h; ++y)
        for (int p = 0; p < planes; ++p) {
            std::fill(row.begin(), row.end(), 0);
            for (int x = 0; x < s.w; ++x)
                if (idx[(std::size_t)y * s.w + x] & (1 << p)) row[x >> 3] |= (std::uint8_t)(0x80 >> (x & 7));
            auto e = byterun1(row.data(), row.size());
            f.insert(f.end(), e.begin(), e.end());
        }
    std::uint32_t bodyLen = (std::uint32_t)(f.size() - bodyLenAt - 4);
    for (int k = 0; k < 4; ++k) f[bodyLenAt + k] = (std::uint8_t)(bodyLen >> (24 - 8 * k));
    if (bodyLen & 1) f.push_back(0);
    std::uint32_t formLen = (std::uint32_t)(f.size() - 8);
    for (int k = 0; k < 4; ++k) f[4 + k] = (std::uint8_t)(formLen >> (24 - 8 * k));
    return f;
}
}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `./build.sh -cp fujibus-pty-debug && "$(find build -path '*fujibus-pty-debug*' -name fujinet-nio-tests -type f | head -1)" -tc='ImageConvert*'`
Expected: all `ImageConvert*` test cases PASS. The `fit_size` values in the test are derived by hand in the test's comments. If they disagree with the code, fix the code.

- [ ] **Step 5: Commit** (fujinet-nio submodule, no co-author trailer)

```bash
git add include/fujinet/io/devices/image_convert.h src/lib/image_convert.cpp tests/test_image_convert.cpp src/CMakeLists.txt CMakeLists_posix.cmake
git commit -m "image: add selector, scale, OCS palette, dither, ByteRun1 ILBM pipeline (tests: ImageConvert*)"
```

### Task 2: `ImageContentTranslator` and its registration

**Files:**
- Create: `third_party/stb/stb_image.h` (stb_image v2.30, downloaded from `https://raw.githubusercontent.com/nothings/stb/master/stb_image.h`), `src/lib/stb_image_impl.cpp`, `include/fujinet/io/devices/image_content_translator.h`, `src/lib/image_content_translator.cpp`
- Modify: `include/fujinet/io/devices/network_translation.h`, `src/lib/network_device.cpp:236-247`, `src/CMakeLists.txt`, `CMakeLists_posix.cmake` (add `third_party/stb` to the include directories), `tests/test_image_convert.cpp`, `docs/network_device_protocol.md`

**Interfaces:**
- Consumes: Task 1's `image::parse_selector`, `fit_size`, `scale_rgb`, `make_palette`, `map_pixels`, `write_ilbm`.
- Produces: `ContentTranslationType::Image = 4` on the wire, and `class ImageContentTranslator final : public IContentTranslator`. Any decode failure returns `StatusCode::InvalidRequest`. An image above the pixel cap returns `StatusCode::Unsupported`. Task 8 maps these to user messages.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_image_convert.cpp`)

```cpp
#include "fujinet/io/devices/image_content_translator.h"
#include "fujinet/io/devices/network_translation.h"

// 2x1 RGB PNG (black, white), generated with: python3 -c "import zlib,struct;..." — bytes inlined.
static const std::uint8_t kPng2x1[] = {
  0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
  0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x7B,0x40,0xE8,
  0xDD,0x00,0x00,0x00,0x0F,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x60,0x60,0x60,0xF8,
  0x0F,0x04,0x00,0x09,0xFB,0x03,0xFD,0x5D,0x8D,0x2B,0x52,0x00,0x00,0x00,0x00,0x49,
  0x45,0x4E,0x44,0xAE,0x42,0x60,0x82 };

static fujinet::io::TranslationConfig img_cfg(const char* sel) {
    fujinet::io::TranslationConfig c; c.type = fujinet::io::ContentTranslationType::Image; c.selector = sel; return c;
}

TEST_CASE("ImageTranslator converts PNG to ILBM") {
    fujinet::io::ImageContentTranslator t;
    REQUIRE(t.configure(img_cfg("colors=2,mode=gray,dither=none")) == fujinet::io::StatusCode::Ok);
    REQUIRE(t.append_body(kPng2x1, sizeof kPng2x1) == fujinet::io::StatusCode::Ok);
    REQUIRE(t.finalize() == fujinet::io::StatusCode::Ok);
    std::uint8_t buf[256]; std::uint16_t n = 0; bool eof = false;
    REQUIRE(t.read(0, buf, sizeof buf, n, eof) == fujinet::io::StatusCode::Ok);
    CHECK(eof); CHECK(n == t.translated_size());
    CHECK(std::string((char*)buf, 4) == "FORM");
    CHECK(buf[20] == 0); CHECK(buf[21] == 2);   // BMHD width = 2
}

TEST_CASE("ImageTranslator rejects bad selector and garbage body") {
    fujinet::io::ImageContentTranslator t;
    CHECK(t.configure(img_cfg("colors=99")) == fujinet::io::StatusCode::InvalidRequest);
    REQUIRE(t.configure(img_cfg("")) == fujinet::io::StatusCode::Ok);
    const std::uint8_t junk[] = {1,2,3,4};
    t.append_body(junk, sizeof junk);
    CHECK(t.finalize() == fujinet::io::StatusCode::InvalidRequest);
}

TEST_CASE("ImageTranslator rejects images above pixel cap") {
    fujinet::io::ImageContentTranslator t;
    t.set_max_pixels_for_test(1);                 // 2x1 PNG = 2 pixels > 1
    REQUIRE(t.configure(img_cfg("")) == fujinet::io::StatusCode::Ok);
    t.append_body(kPng2x1, sizeof kPng2x1);
    CHECK(t.finalize() == fujinet::io::StatusCode::Unsupported);
}
```

**Before you use `kPng2x1`, regenerate it with Python and check that the bytes match.** If they differ, use your own output:

```bash
python3 - <<'EOF'
import zlib,struct
def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
raw=b'\x00'+bytes([0,0,0,255,255,255])
png=b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',2,1,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b'')
print(','.join('0x%02X'%b for b in png))
EOF
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `./build.sh -cp fujibus-pty-debug`
Expected: compile FAIL with `image_content_translator.h: No such file or directory`.

- [ ] **Step 3: Implement**

```cpp
// include/fujinet/io/devices/network_translation.h  (edit the enum and the switch)
enum class ContentTranslationType : std::uint8_t { None = 0, Json = 1, Xml = 2, Rss = 3, Image = 4 };
// ...in is_known_translation_type add:
        case ContentTranslationType::Image:
```

```cpp
// include/fujinet/io/devices/image_content_translator.h
#pragma once
#include "fujinet/io/devices/content_translator.h"
#include "fujinet/io/devices/image_convert.h"
#include <vector>

namespace fujinet::io {

#ifndef FN_IMAGE_MAX_PIXELS
#  if defined(ESP_PLATFORM)
#    define FN_IMAGE_MAX_PIXELS (1200u * 1200u)   // RGB decode ~4.3 MB of PSRAM
#  else
#    define FN_IMAGE_MAX_PIXELS (4096u * 4096u)
#  endif
#endif

class ImageContentTranslator final : public IContentTranslator {
public:
    StatusCode configure(const TranslationConfig& config) override;
    void reset() override;
    StatusCode append_body(const std::uint8_t* data, std::size_t len) override;
    StatusCode finalize() override;
    std::uint64_t translated_size() const override { return _out.size(); }
    StatusCode read(std::uint32_t offset, std::uint8_t* out, std::size_t maxBytes,
                    std::uint16_t& actual, bool& eof) const override;
    void set_max_pixels_for_test(std::uint32_t n) { _maxPixels = n; }
private:
    image::Options _opt{};
    std::vector<std::uint8_t> _body, _out;
    std::uint32_t _maxPixels = FN_IMAGE_MAX_PIXELS;
};
}
```

```cpp
// src/lib/stb_image_impl.cpp
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
```

```cpp
// src/lib/image_content_translator.cpp
#include "fujinet/io/devices/image_content_translator.h"
#include "stb_image.h"
#include <algorithm>
#include <cstring>

namespace fujinet::io {

StatusCode ImageContentTranslator::configure(const TranslationConfig& config) {
    if (config.type != ContentTranslationType::Image) return StatusCode::InvalidRequest;
    image::Options o;
    if (!image::parse_selector(config.selector, o)) return StatusCode::InvalidRequest;
    _opt = o; reset();
    return StatusCode::Ok;
}

void ImageContentTranslator::reset() { _body.clear(); _out.clear(); }

StatusCode ImageContentTranslator::append_body(const std::uint8_t* d, std::size_t n) {
    _body.insert(_body.end(), d, d + n);
    return StatusCode::Ok;
}

StatusCode ImageContentTranslator::finalize() {
    int w = 0, h = 0, comp = 0;
    if (_body.empty() || !stbi_info_from_memory(_body.data(), (int)_body.size(), &w, &h, &comp))
        return StatusCode::InvalidRequest;
    if ((std::uint64_t)w * (std::uint64_t)h > _maxPixels) { _body.clear(); return StatusCode::Unsupported; }
    std::uint8_t* px = stbi_load_from_memory(_body.data(), (int)_body.size(), &w, &h, &comp, 3);
    std::vector<std::uint8_t>().swap(_body);       // free the PNG before allocating the scaled copy
    if (!px) return StatusCode::InvalidRequest;
    image::Size s = image::fit_size(w, h, _opt);
    std::vector<std::uint8_t> rgb = image::scale_rgb(px, w, h, s);
    stbi_image_free(px);
    auto pal = image::make_palette(rgb, _opt);
    auto idx = image::map_pixels(rgb, s, pal, _opt);
    _out = image::write_ilbm(idx, s, pal, _opt);
    return StatusCode::Ok;
}

StatusCode ImageContentTranslator::read(std::uint32_t off, std::uint8_t* out, std::size_t max,
                                        std::uint16_t& actual, bool& eof) const {
    if (off > _out.size()) return StatusCode::InvalidRequest;
    std::size_t n = std::min<std::size_t>({max, _out.size() - off, 0xFFFFu});
    if (n) std::memcpy(out, _out.data() + off, n);
    actual = (std::uint16_t)n;
    eof = off + n >= _out.size();
    return StatusCode::Ok;
}
}
```

```cpp
// src/lib/network_device.cpp make_translator(): add before Xml/Rss
        case ContentTranslationType::Image:
            return std::make_unique<ImageContentTranslator>();
// and add near the other includes:
#include "fujinet/io/devices/image_content_translator.h"
```

Update `docs/network_device_protocol.md` under "Defined types". Add `4 = Image`, paste the selector table and the output-format paragraph from Task 1, and change "Only JSON translation is implemented" to "JSON and Image translation are implemented."

- [ ] **Step 4: Run the tests and confirm they pass, then check the ESP32 build**

Run: `./build.sh -cp fujibus-pty-debug && "$(find build -path '*fujibus-pty-debug*' -name fujinet-nio-tests -type f | head -1)" -tc='Image*'`
Expected: PASS.
Run: `./build.sh -b`
Expected: the ESP32 build succeeds. If the link fails on stb symbols, check that `src/lib/stb_image_impl.cpp` and the `third_party/stb` include directory are in `src/CMakeLists.txt`.

- [ ] **Step 5: Run the whole firmware test suite to check for regressions**

Run: `"$(find build -path '*fujibus-pty-debug*' -name fujinet-nio-tests -type f | head -1)"`
Expected: 0 failures. The existing JSON translation tests must still pass.

- [ ] **Step 6: Commit**

```bash
git add third_party/stb include/fujinet/io/devices/*.h src/lib/*.cpp src/CMakeLists.txt CMakeLists_posix.cmake tests/test_image_convert.cpp docs/network_device_protocol.md
git commit -m "network: add Image content translator (PNG/JPEG/GIF -> ByteRun1 IFF ILBM) (tests: Image*, full suite)"
```

---

## Part B: fujinet-nio-lib open-extension API (repo `fujinet-nio-lib`)

### Task 3: `fn_open_translated()`

**Files:**
- Modify: `include/fujinet-nio.h` (next to `fn_open_long`), `include/fn_internal.h:111`, `src/common/fn_packet_build_open.c`, `src/common/fn_open.c`, `src/common/fn_ext.c`, `Makefile:144`, `docs/api.md`
- Create: `src/platform/bbc/fn_open_translated_bbc.c`, `tests/open_ext_wire_test.c`, `tests/run_open_ext_wire_test.sh`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```c
  #define FN_TRANSLATE_NONE  0
  #define FN_TRANSLATE_JSON  1
  #define FN_TRANSLATE_IMAGE 4
  uint8_t fn_open_translated(fn_handle_t *handle, uint8_t method, const char *url, uint8_t flags,
                             uint8_t translation_type, uint8_t translation_flags, const char *selector);
  uint16_t fn_build_open_packet_ext(uint8_t *buffer, uint8_t method, uint8_t flags, const char *url,
                                    uint8_t ttype, uint8_t tflags, const char *selector); /* 0 on overflow */
  ```
  The wire layout is the existing one plus `u32 openExtFlags (=1)`, `u8 type`, `u8 tflags`, `u16 selLen` and the selector bytes. All of this is appended only when `ttype != FN_TRANSLATE_NONE`, so packets from `fn_open()` stay byte-identical to today's.

- [ ] **Step 1: Write the failing wire test**

```c
/* tests/open_ext_wire_test.c */
#include <stdio.h>
#include <string.h>
#include "fujinet-nio.h"
#include "fn_internal.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    static uint8_t a[FN_MAX_PACKET_SIZE], b[FN_MAX_PACKET_SIZE];
    uint16_t la, lb, base;
    const char *url = "https://imgs.xkcd.com/comics/x.png";
    const char *sel = "w=624,h=190,colors=12,base=4,par=1:2";

    la = fn_build_open_packet(a, FN_METHOD_GET, 0x02, url);
    lb = fn_build_open_packet_ext(b, FN_METHOD_GET, 0x02, url, FN_TRANSLATE_NONE, 0, NULL);
    CHECK(la == lb && memcmp(a, b, la) == 0);          /* NONE == legacy bytes */

    lb = fn_build_open_packet_ext(b, FN_METHOD_GET, 0x02, url, FN_TRANSLATE_IMAGE, 0, sel);
    CHECK(lb == la + 4 + 1 + 1 + 2 + strlen(sel));
    base = la;                                          /* ext block starts where legacy packet ended */
    CHECK(b[base] == 1 && b[base+1] == 0 && b[base+2] == 0 && b[base+3] == 0);
    CHECK(b[base+4] == FN_TRANSLATE_IMAGE);
    CHECK(b[base+5] == 0);
    CHECK((b[base+6] | (b[base+7] << 8)) == strlen(sel));
    CHECK(memcmp(b + base + 8, sel, strlen(sel)) == 0);

    {   /* overflow returns 0 */
        static char big[FN_MAX_PACKET_SIZE];
        memset(big, 'a', sizeof big - 1);
        CHECK(fn_build_open_packet_ext(b, FN_METHOD_GET, 0, url, FN_TRANSLATE_IMAGE, 0, big) == 0);
    }
    printf(fails ? "open_ext_wire_test: %d failures\n" : "open_ext_wire_test: OK\n", fails);
    return fails != 0;
}
```

```sh
#!/bin/sh
# tests/run_open_ext_wire_test.sh
set -eu
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -Iinclude \
    tests/open_ext_wire_test.c \
    src/common/fn_packet_build_open.c \
    src/common/fn_packet_header.c \
    -o build/tests/open_ext_wire_test
build/tests/open_ext_wire_test
```

Before running, check which file defines `fn_build_header` and `fn_calc_packet_checksum`: `grep -ln "fn_build_header\|fn_calc_packet_checksum" src/common/*.c`. Replace `src/common/fn_packet_header.c` in the script with the files that grep lists.

- [ ] **Step 2: Run the test and confirm it fails**

Run: `cd $NIO_WORKSPACE/repos/fujinet-nio-lib && sh tests/run_open_ext_wire_test.sh`
Expected: compile FAIL with `implicit declaration of function 'fn_build_open_packet_ext'`.

- [ ] **Step 3: Implement**

In `src/common/fn_packet_build_open.c`, rename the body to `fn_build_open_packet_ext` and add these parameters:

```c
uint16_t fn_build_open_packet_ext(uint8_t *buffer, uint8_t method, uint8_t flags, const char *url,
                                  uint8_t ttype, uint8_t tflags, const char *selector)
{
    uint16_t offset, url_len, sel_len, payload_len, total_len;

    url_len = 0;
    while (url[url_len] != '\0') {
        ++url_len;
        if (url_len > FN_MAX_URL_LEN) return 0;
    }
    sel_len = 0;
    if (ttype != FN_TRANSLATE_NONE && selector != NULL) {
        while (selector[sel_len] != '\0') {
            ++sel_len;
            if (sel_len > FN_MAX_PACKET_SIZE) return 0;
        }
    }

    payload_len = 1 + 1 + 1 + 2 + url_len + 2 + 4 + 2;
    if (ttype != FN_TRANSLATE_NONE) payload_len += 4 + 1 + 1 + 2 + sel_len;
    total_len = FN_HEADER_SIZE + payload_len;
    if (total_len > FN_MAX_PACKET_SIZE) return 0;
    offset = fn_build_header(buffer, FN_DEVICE_NETWORK, FN_CMD_OPEN, total_len);

    buffer[offset++] = FN_PROTOCOL_VERSION;
    buffer[offset++] = method;
    buffer[offset++] = flags;
    buffer[offset++] = (uint8_t)(url_len & 0xFF);
    buffer[offset++] = (uint8_t)((url_len >> 8) & 0xFF);
    memcpy(buffer + offset, url, url_len);
    offset += url_len;
    memset(buffer + offset, 0, 8);          /* reqHeaderCount, bodyLenHint, respHeaderCount */
    offset += 8;

    if (ttype != FN_TRANSLATE_NONE) {
        buffer[offset++] = 0x01; buffer[offset++] = 0; buffer[offset++] = 0; buffer[offset++] = 0;
        buffer[offset++] = ttype;
        buffer[offset++] = tflags;
        buffer[offset++] = (uint8_t)(sel_len & 0xFF);
        buffer[offset++] = (uint8_t)(sel_len >> 8);
        if (sel_len) { memcpy(buffer + offset, selector, sel_len); offset += sel_len; }
    }

    buffer[FN_CHECKSUM_OFFSET] = fn_calc_packet_checksum(buffer, offset);
    return offset;
}

uint16_t fn_build_open_packet(uint8_t *buffer, uint8_t method, uint8_t flags, const char *url)
{
    return fn_build_open_packet_ext(buffer, method, flags, url, FN_TRANSLATE_NONE, 0, NULL);
}
```

In `src/common/fn_open.c`:
- Rename `fn_open` to `fn_open_translated(handle, method, url, flags, ttype, tflags, selector)`.
- Replace its `fn_build_open_packet(...)` call with `fn_build_open_packet_ext(_fn_req_buf, method, open_flags, url, ttype, tflags, selector)`.
- Add:

```c
uint8_t fn_open(fn_handle_t *handle, uint8_t method, const char *url, uint8_t flags)
{
    return fn_open_translated(handle, method, url, flags, FN_TRANSLATE_NONE, 0, NULL);
}
```

Check that BBC does not compile `src/common/fn_open.c`: `grep -n "fn_open.c" makefiles/*.mk Makefile`. If BBC does **not** compile it, create `src/platform/bbc/fn_open_translated_bbc.c`:

```c
#include "fujinet-nio.h"
uint8_t fn_open_translated(fn_handle_t *handle, uint8_t method, const char *url, uint8_t flags,
                           uint8_t ttype, uint8_t tflags, const char *selector)
{
    (void)tflags; (void)selector;
    if (ttype != FN_TRANSLATE_NONE) return FN_ERR_UNSUPPORTED;
    return fn_open(handle, method, url, flags);
}
```

If BBC does compile it, skip the stub and guard the ext branch with `#ifndef __BBC__` instead.

Add the prototypes and defines to `include/fujinet-nio.h` (with a doc comment in the style of `fn_open_long`) and to `include/fn_internal.h`. Add `sh tests/run_open_ext_wire_test.sh` as a `test-open-ext` target and list it in `test:` in `Makefile`. Document the function in `docs/api.md`.

- [ ] **Step 4: Run the test and the full library check**

Run: `sh tests/run_open_ext_wire_test.sh`
Expected: `open_ext_wire_test: OK`.
Run: `source "$NIO_WORKSPACE/scripts/env.sh" && make check`
Expected: every target builds and every test passes. This step is mandatory per the workspace AGENTS.md.

- [ ] **Step 5: Commit**

```bash
git add include/fujinet-nio.h include/fn_internal.h src/common/fn_packet_build_open.c src/common/fn_open.c src/platform/bbc tests/open_ext_wire_test.c tests/run_open_ext_wire_test.sh Makefile docs/api.md
git commit -m "Add fn_open_translated() sending open-extension translation block (make check)"
```

### Task 4: End-to-end check against the POSIX firmware

**Files:**
- Create: `fujinet-nio-lib/examples/network/img_translate.c`, a Linux example that writes the translated bytes to stdout

**Interfaces:**
- Consumes: `fn_open_translated`, `fn_read`, `fn_info`, `fn_close`, and the firmware Image translator.
- Produces: proof that a real xkcd PNG converts. Keep the output file as a fixture for Task 7.

- [ ] **Step 1: Write the example**

```c
/* examples/network/img_translate.c — usage: img_translate <url> <selector> > out.iff */
#include <stdio.h>
#include "fujinet-nio.h"

int main(int argc, char **argv)
{
    fn_handle_t h; uint8_t buf[512]; uint16_t n, status = 0; uint32_t off = 0, len = 0; uint8_t fl, err;
    if (argc < 3) { fprintf(stderr, "usage: %s url selector\n", argv[0]); return 2; }
    if ((err = fn_init()) != FN_OK) { fprintf(stderr, "init: %s\n", fn_error_string(err)); return 1; }
    err = fn_open_translated(&h, FN_METHOD_GET, argv[1], FN_OPEN_FOLLOW_REDIR, FN_TRANSLATE_IMAGE, 0, argv[2]);
    if (err != FN_OK) { fprintf(stderr, "open: %s\n", fn_error_string(err)); return 1; }
    do {
        err = fn_read(h, off, buf, sizeof buf, &n, &fl);
        if (err == FN_ERR_NOT_READY || err == FN_ERR_BUSY) continue;
        if (err != FN_OK) { fprintf(stderr, "read: %s\n", fn_error_string(err)); break; }
        fwrite(buf, 1, n, stdout); off += n;
    } while (!(fl & FN_READ_EOF) && n);
    fn_info(h, &status, &len, &fl);
    fprintf(stderr, "status=%u bytes=%lu\n", status, (unsigned long)off);
    fn_close(h); fn_shutdown();
    return err != FN_OK;
}
```

- [ ] **Step 2: Run it against the POSIX firmware**

Start the POSIX firmware with the PTY profile and point the Linux library build at it. The exact PTY and env settings are in `fujinet-nio-lib/docs/DEVELOPMENT.md` and `fujinet-nio/docs/posix_tcp_serial_channel.md`. Then run:

```bash
./img_translate https://imgs.xkcd.com/comics/barrel_cropped_\(1\).jpg "w=624,h=190,colors=12,base=4,par=1:2" > /tmp/x1.iff
./img_translate https://imgs.xkcd.com/comics/python.png "w=640,h=400,colors=16" > /tmp/x353.iff
python3 -c "import sys;d=open('/tmp/x353.iff','rb').read();print(d[:4],d[8:16],int.from_bytes(d[20:22],'big'),int.from_bytes(d[22:24],'big'),d[28],len(d))"
```

Expected: `b'FORM' b'ILBMBMHD' <w ≤ 640> <h ≤ 400> 4 <size>`. The size of line-art comics should be well under 40 KB. Record the actual sizes in the commit message; they validate the bandwidth assumption.
Optional: `convert /tmp/x353.iff /tmp/x353.png` (ImageMagick reads ILBM) to check the picture by eye.

- [ ] **Step 3: Copy the fixtures into the app repo and commit both repos**

```bash
mkdir -p /home/bkrein/dev/fujinet-xkcd/tests/fixtures && cp /tmp/x353.iff /home/bkrein/dev/fujinet-xkcd/tests/fixtures/python_640x400x16.iff
cd $NIO_WORKSPACE/repos/fujinet-nio-lib && git add examples/network/img_translate.c && git commit -m "examples: img_translate demo for Image translation (verified against POSIX firmware: <sizes>)"
```

---

## Part C: fujinet-xkcd repository (MekkoGX)

### Task 5: Repo scaffold from MekkoGX, plus the Amiga platform and toolchain

**Files:**
- Create (by copying from MekkoGX `2c64656`): `mekkogx/**`, `.gitignore`, `.github/workflows/ci.yml`
- Create: `Makefile`, `README.md`, `mekkogx/toolchains/amigagcc.mk`, `mekkogx/platforms/amiga.mk`, `src/version.c`, `src/amiga/main.c` (stub), `amiga/ReadMe.txt`

**Interfaces:**
- Consumes: the `fujinet-nio-lib` Amiga archive `$(FUJINET_NIO_LIB)/build/fujinet-nio-amiga.a` and its headers. The broker device header lives in `$(FUJINET_NIO_DRIVER)/amiga/include`.
- Produces:
  - `make amiga` → `r2r/amiga/xkcd` and `r2r/amiga/xkcd.adf`.
  - Variables available to later tasks: `NIO_LIB_A`, `FUJINET_NIO_LIB`.
  - The rule that every portable source in `src/*.c` must compile with both amiga-gcc and host gcc.

- [ ] **Step 1: Initialise the repo and copy MekkoGX**

```bash
cd /home/bkrein/dev/fujinet-xkcd
git init -b main
git clone -q https://github.com/FozzTexx/MekkoGX /tmp/claude-mekkogx && git -C /tmp/claude-mekkogx checkout -q 2c64656
cp -r /tmp/claude-mekkogx/mekkogx /tmp/claude-mekkogx/.gitignore /tmp/claude-mekkogx/.github .
printf '*.adf\n*.iff.out\n' >> .gitignore
```

- [ ] **Step 2: Write the toolchain file**

```make
# mekkogx/toolchains/amigagcc.mk
CC_DEFAULT ?= m68k-amigaos-gcc
AS_DEFAULT ?= m68k-amigaos-as
LD_DEFAULT ?= $(CC_DEFAULT)
AR_DEFAULT ?= m68k-amigaos-ar

include $(MWD)/tc-common.mk

# -mcrt=nix13 keeps executables Kickstart 1.3 compatible and handles Workbench startup.
CFLAGS  += -mcpu=68000 -msoft-float -mcrt=nix13 -Os -Wall -Wextra -std=gnu99 -fomit-frame-pointer
LDFLAGS += -mcpu=68000 -msoft-float -mcrt=nix13 -s

DSTRING_OPEN = '"
DSTRING_CLOSE = "'
CFLAGS += -DGIT_VERSION=$(DSTRING_OPEN)$(GIT_VERSION)$(DSTRING_CLOSE)

define include-dir-flag
  -I$1
endef

define asm-include-dir-flag
  -I$1
endef

define library-dir-flag
  -L$1
endef

define library-flag
  -l$1
endef

define link-lib
  $(AR) rcs $1 $2
endef

define link-bin
  $(LD) $(LDFLAGS) -o $1 $2 $(LIBS)
endef

define compile
  $(CC) -MMD -MP -MF $(1:.o=.d) -c $(CFLAGS) -o $1 $2
endef

define assemble
  $(AS) $(ASFLAGS) -o $1 $2
endef
```

- [ ] **Step 3: Write the platform file** (an OFS ADF made with xdftool, following the msdos.mk structure)

```make
# mekkogx/platforms/amiga.mk
EXEC_SUFFIX =
DISK = $(R2R_PD)/$(PRODUCT_BASE).adf
LIBRARY = $(R2R_PD)/lib$(PRODUCT_BASE).$(PLATFORM).a
DISK_LABEL ?= $(PRODUCT_BASE)

MWD := $(realpath $(dir $(lastword $(MAKEFILE_LIST)))..)
include $(MWD)/common.mk
include $(MWD)/toolchains/amigagcc.mk

CFLAGS += -D__AMIGA__

XDFTOOL ?= $(shell command -v xdftool 2>/dev/null || \
             (command -v uvx >/dev/null 2>&1 && echo "uvx --from amitools xdftool"))

r2r:: $(BUILD_DISK) $(BUILD_EXEC) $(BUILD_LIB) $(R2R_EXTRA_DEPS)
	make -f $(PLATFORM_MK) $(PLATFORM)/r2r-post

# Non-bootable OFS disk readable by Kickstart 1.3. DISK_EXTRA_FILES entries are
# "src[:dest]"; dest defaults to the basename.
$(BUILD_DISK): $(DISK_EXECUTABLES) $(DISK_EXTRA_DEPS) $(DISK_EXTRA_FILES) | $(R2R_PD)
	@if [ -z "$(XDFTOOL)" ]; then echo "xdftool (amitools) or uvx is required"; exit 1; fi
	$(RM) $@
	$(XDFTOOL) $@ create + format $(DISK_LABEL) ofs \
	  $(foreach e,$(DISK_EXECUTABLES),+ write $(e) $(notdir $(e)))
	$(foreach f,$(DISK_EXTRA_FILES),$(call copy-to-disk,,$(firstword $(subst :, ,$(f))),$(or $(word 2,$(subst :, ,$(f))),$(notdir $(f))),$@);)
	@make -f $(PLATFORM_MK) $(PLATFORM)/disk-post

# $1 flags (unused) $2 source $3 destination name $4 disk image
define copy-to-disk
    $(XDFTOOL) $4 write $2 $3
endef
```

- [ ] **Step 4: Write the top-level Makefile**

```make
PRODUCT = xkcd
PLATFORMS += amiga

# Portable logic lives in src/, platform code in src/<platform>/.
SRC_DIRS = src src/%PLATFORM%
INCLUDE_DIRS = src

# fujinet-nio-lib is not fujinet-lib, so FUJINET_LIB stays undefined and the
# NIO library is wired in explicitly for amiga.
ifndef FUJINET_NIO_LIB
  ifdef NIO_WORKSPACE
    FUJINET_NIO_LIB := $(NIO_WORKSPACE)/repos/fujinet-nio-lib
  endif
endif
FUJINET_NIO_DRIVER ?= $(FUJINET_NIO_LIB)/../fujinet-nio-driver
NIO_LIB_A = $(FUJINET_NIO_LIB)/build/fujinet-nio-amiga.a

EXTRA_INCLUDE_AMIGA = $(FUJINET_NIO_LIB)/include $(FUJINET_NIO_DRIVER)/amiga/include
LIBS_EXTRA_AMIGA = $(NIO_LIB_A)
EXECUTABLE_EXTRA_DEPS_AMIGA = $(NIO_LIB_A)
DISK_EXTRA_FILES_AMIGA = amiga/ReadMe.txt:ReadMe

include mekkogx/toplevel-rules.mk

# Ask fujinet-nio-lib's own build whether its archive is current.
$(NIO_LIB_A):
	@test -f "$(FUJINET_NIO_LIB)/include/fujinet-nio.h" || { echo "Set FUJINET_NIO_LIB or NIO_WORKSPACE"; exit 1; }
	$(MAKE) -C $(FUJINET_NIO_LIB) amiga

# Host-side unit tests for the portable modules in src/.
.PHONY: test
test:
	$(MAKE) -f tests/Makefile.host
```

`$(NIO_LIB_A)` is used inside the platform sub-make, which includes this Makefile through `MEKKO_CONFIG`, so the rule is visible there.

- [ ] **Step 5: Write the stub `src/amiga/main.c` and `src/version.c`, then build**

```c
/* src/version.c */
const char xkcd_version[] = "$VER: xkcd 0.1 (" GIT_VERSION ")";
```

```c
/* src/amiga/main.c (stub; replaced in Task 9) */
#include <stdio.h>
#include "fujinet-nio.h"
int main(void) { printf("xkcd: fujinet-nio-lib %s\n", fn_version()); return 0; }
```

Run: `source $NIO_WORKSPACE/scripts/env.sh && make amiga && ls -l r2r/amiga`
Expected: `r2r/amiga/xkcd` (an Amiga hunk executable; `file` reports "AmigaOS loadseg()ble executable") and `r2r/amiga/xkcd.adf` (901120 bytes).
Run: `xdftool r2r/amiga/xkcd.adf list`
Expected: it lists `xkcd` and `ReadMe`.

- [ ] **Step 6: Add amiga to CI**

In `.github/workflows/ci.yml`:
- Replace the platform matrix with `- amiga`.
- The defoogi image may not ship amiga-gcc. Check with `docker run --rm fozztexx/defoogi:1.4.7 sh -c 'command -v m68k-amigaos-gcc'`. If it is missing, change the job to `runs-on: ubuntu-latest` with no container and add steps to:
  - install the amiga-gcc prebuilt from `https://franke.ms/download/amiga-gcc.tgz` into `/opt/amiga`;
  - check out `markjfisher/fujinet-nio-lib` and `markjfisher/fujinet-nio-driver` as siblings, then build with `FUJINET_NIO_LIB=$PWD/../fujinet-nio-lib`;
  - run `pip install amitools`.
- Add a `test` job with `runs-on: ubuntu-latest` and the step `make test`.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "Scaffold from MekkoGX 2c64656; add amiga platform/toolchain (make amiga builds xkcd + ADF)"
```

- [ ] **Step 8: Prepare the upstream MekkoGX change (do not push or open a PR without asking the user)**

Copy `mekkogx/platforms/amiga.mk` and `mekkogx/toolchains/amigagcc.mk` onto a branch of a local MekkoGX clone and commit them there. Report the branch to the user as ready for a PR to `FozzTexx/MekkoGX`.

### Task 6: Portable logic: JSON, xkcd model, history, random pick, caption wrap, selectors

**Files:**
- Create: `src/json.c`, `src/json.h`, `src/xkcd.c`, `src/xkcd.h`, `src/history.c`, `src/history.h`, `src/wrap.c`, `src/wrap.h`, `src/selector.c`, `src/selector.h`, `tests/test_main.c`, `tests/Makefile.host`

**Interfaces:**
- Consumes: nothing. Everything here is plain C99 with no Amiga headers.
- Produces:
  ```c
  /* json.h */
  int json_get_string(const char *buf, const char *end, const char *key, char *out, unsigned short max); /* 1 found */
  int json_get_long(const char *buf, const char *end, const char *key, long *out);
  /* xkcd.h */
  #define XKCD_TITLE_MAX 96
  #define XKCD_ALT_MAX   512
  #define XKCD_URL_MAX   160
  typedef struct { long num; char title[XKCD_TITLE_MAX]; char alt[XKCD_ALT_MAX]; char img[XKCD_URL_MAX];
                   char date[12]; unsigned char has_image; } xkcd_comic_t;
  void xkcd_info_url(long num, char *out, unsigned short max);   /* num<=0 => latest */
  int  xkcd_parse(const char *buf, unsigned short len, xkcd_comic_t *c); /* 1 ok */
  long xkcd_random_pick(unsigned long r, long latest, long avoid);       /* 1..latest, never 404, never avoid */
  /* history.h */
  #define HISTORY_MAX 25
  typedef struct { long ids[HISTORY_MAX]; unsigned char count, pos; } history_t; /* pos = index of current */
  void history_init(history_t *h);
  void history_push(history_t *h, long id);          /* drops forward entries; drops oldest at 25 */
  int  history_back(history_t *h, long *id);         /* 1 if moved */
  int  history_forward(history_t *h, long *id);      /* 1 if moved */
  long history_current(const history_t *h);          /* 0 if empty */
  /* wrap.h */
  int  wrap_text(const char *s, unsigned char cols, char lines[][81], int max_lines); /* returns line count */
  /* selector.h */
  void selector_main(char *out, unsigned short max, int pal);   /* main-window box */
  void selector_zoom(char *out, unsigned short max, int pal);   /* zoom-screen box */
  ```

**Layout constants used by `selector_*`.** These must match Task 8's `ui.c`.
- Main screen image box: 624 × 150 (PAL) or 624 × 110 (NTSC), with `par=1:2,colors=12,base=4`.
- Zoom box: 640 × 512 (PAL) or 640 × 400 (NTSC), with `par=1:1,colors=16,base=0`.

These give the following selectors:
- `"w=624,h=150,colors=12,base=4,par=1:2"` (PAL main)
- `"w=624,h=110,colors=12,base=4,par=1:2"` (NTSC main)
- `"w=640,h=512,colors=16,base=0"` (PAL zoom)
- `"w=640,h=400,colors=16,base=0"` (NTSC zoom)

- [ ] **Step 1: Write the failing tests**

```c
/* tests/test_main.c */
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "xkcd.h"
#include "history.h"
#include "wrap.h"
#include "selector.h"

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
    selector_main(s, sizeof s, 1); CHECK(strcmp(s, "w=624,h=150,colors=12,base=4,par=1:2") == 0);
    selector_main(s, sizeof s, 0); CHECK(strcmp(s, "w=624,h=110,colors=12,base=4,par=1:2") == 0);
    selector_zoom(s, sizeof s, 1); CHECK(strcmp(s, "w=640,h=512,colors=16,base=0") == 0);
    selector_zoom(s, sizeof s, 0); CHECK(strcmp(s, "w=640,h=400,colors=16,base=0") == 0);
}

int main(void) {
    RUN(test_json_basic); RUN(test_json_unescape); RUN(test_json_truncates);
    RUN(test_xkcd_parse); RUN(test_xkcd_parse_no_image); RUN(test_xkcd_urls);
    RUN(test_random_pick_skips_404); RUN(test_history); RUN(test_wrap); RUN(test_selectors);
    /* ILBM tests are added in Task 7 */
    printf(fails ? "%d FAILURES\n" : "ALL PASS\n", fails);
    return fails != 0;
}
```

```make
# tests/Makefile.host
HOSTCC ?= gcc
SRCS := $(filter-out src/version.c,$(wildcard src/*.c))
build/host/test_main: tests/test_main.c $(SRCS) $(wildcard src/*.h)
	@mkdir -p build/host
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -O1 -Isrc -o $@ tests/test_main.c $(SRCS)
	$@
.PHONY: build/host/test_main
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `make test`
Expected: compile FAIL with `json.h: No such file or directory`.

- [ ] **Step 3: Implement the modules**

```c
/* src/json.h */
#ifndef XKCD_JSON_H
#define XKCD_JSON_H
int json_get_string(const char *buf, const char *end, const char *key, char *out, unsigned short max);
int json_get_long(const char *buf, const char *end, const char *key, long *out);
#endif
```

```c
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
```

```c
/* src/xkcd.h */
#ifndef XKCD_XKCD_H
#define XKCD_XKCD_H
#define XKCD_TITLE_MAX 96
#define XKCD_ALT_MAX   512
#define XKCD_URL_MAX   160
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
```

```c
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
    c->has_image = (unsigned char)(ends_with_ci(c->img, ".png") || ends_with_ci(c->img, ".jpg") ||
                                   ends_with_ci(c->img, ".jpeg") || ends_with_ci(c->img, ".gif"));
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
```

```c
/* src/history.h */
#ifndef XKCD_HISTORY_H
#define XKCD_HISTORY_H
#define HISTORY_MAX 25
typedef struct { long ids[HISTORY_MAX]; unsigned char count, pos; } history_t;
void history_init(history_t *h);
void history_push(history_t *h, long id);
int  history_back(history_t *h, long *id);
int  history_forward(history_t *h, long *id);
long history_current(const history_t *h);
#endif
```

```c
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
```

```c
/* src/wrap.h */
#ifndef XKCD_WRAP_H
#define XKCD_WRAP_H
int wrap_text(const char *s, unsigned char cols, char lines[][81], int max_lines);
#endif
```

```c
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
```

```c
/* src/selector.h */
#ifndef XKCD_SELECTOR_H
#define XKCD_SELECTOR_H
#define MAIN_IMG_W      624
#define MAIN_IMG_H_PAL  150
#define MAIN_IMG_H_NTSC 110
void selector_main(char *out, unsigned short max, int pal);
void selector_zoom(char *out, unsigned short max, int pal);
#endif
```

```c
/* src/selector.c */
#include <stdio.h>
#include "selector.h"

void selector_main(char *out, unsigned short max, int pal)
{
    snprintf(out, max, "w=%d,h=%d,colors=12,base=4,par=1:2", MAIN_IMG_W, pal ? MAIN_IMG_H_PAL : MAIN_IMG_H_NTSC);
}

void selector_zoom(char *out, unsigned short max, int pal)
{
    snprintf(out, max, "w=640,h=%d,colors=16,base=0", pal ? 512 : 400);
}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `make test`
Expected: `ALL PASS`. If `test_wrap`'s second-line or 4-line expectations fail, step through the wrap by hand at 20 columns and fix the **implementation**. The intended behaviour is greedy word wrap: trailing spaces trimmed, and a word longer than `cols` hard-split.

- [ ] **Step 5: Check that the portable code also cross-compiles**

Run: `make amiga`
Expected: the build succeeds and the new `src/*.c` objects appear under `build/xkcd/amiga/src/`.

- [ ] **Step 6: Commit**

```bash
git add src tests && git commit -m "Add portable JSON, xkcd model, 25-entry history, wrap, selector modules (make test)"
```

### Task 7: Streaming ILBM decoder

**Files:**
- Create: `src/ilbm.c`, `src/ilbm.h`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: the Task 4 fixture `tests/fixtures/python_640x400x16.iff`.
- Produces:
  ```c
  typedef struct { unsigned short w, h; unsigned char planes, compression; unsigned char cmap[32][3]; unsigned char ncolors; } ilbm_info_t;
  enum { ILBM_NEED_MORE = 0, ILBM_HEADER = 1, ILBM_DONE = 2, ILBM_ERROR = -1 };
  typedef struct ilbm_s ilbm_t;   /* opaque-ish; struct defined in header so callers can stack-allocate */
  void ilbm_init(ilbm_t *d);
  /* Feed bytes; *used receives bytes consumed. Returns ILBM_HEADER once (after BMHD+CMAP, at BODY start;
     caller must then call ilbm_set_target before feeding more), ILBM_DONE after last row. */
  int  ilbm_feed(ilbm_t *d, const unsigned char *p, unsigned short n, unsigned short *used);
  const ilbm_info_t *ilbm_info(const ilbm_t *d);
  /* planes[i] points at the first byte of the destination row 0 for plane i, already offset to the image's
     top-left; bpr = destination bytes per row; max_rows clips. Planes beyond nplanes_dst are decoded and discarded. */
  void ilbm_set_target(ilbm_t *d, unsigned char **planes, unsigned char nplanes_dst, unsigned short bpr, unsigned short max_rows);
  unsigned short ilbm_rows_done(const ilbm_t *d);
  ```
  Destination byte alignment: the app always places images on a 16-pixel boundary, so `ilbm_set_target` takes byte pointers and never needs bit shifting.

- [ ] **Step 1: Write the failing tests** (add to `tests/test_main.c` and register them in `main`)

```c
#include "ilbm.h"
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
    unsigned char *buf, **planes; long n; int i, r; ilbm_t d;
    if (!f) { printf("  (fixture missing, skipped)\n"); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n); CHECK(fread(buf, 1, (size_t)n, f) == (size_t)n); fclose(f);
    planes = malloc(4 * sizeof *planes);
    for (i = 0; i < 4; ++i) planes[i] = calloc(80, 512);
    {   unsigned long off = 0; unsigned short used;
        ilbm_init(&d);
        while (off < (unsigned long)n) {
            r = ilbm_feed(&d, buf + off, (unsigned short)((n - off) > 512 ? 512 : (n - off)), &used);
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
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `make test`
Expected: compile FAIL with `ilbm.h: No such file or directory`.

- [ ] **Step 3: Implement**

```c
/* src/ilbm.h */
#ifndef XKCD_ILBM_H
#define XKCD_ILBM_H
typedef struct { unsigned short w, h; unsigned char planes, compression; unsigned char cmap[32][3]; unsigned char ncolors; } ilbm_info_t;
enum { ILBM_NEED_MORE = 0, ILBM_HEADER = 1, ILBM_DONE = 2, ILBM_ERROR = -1 };
typedef struct ilbm_s {
    ilbm_info_t info;
    unsigned char state, hdr[8], hdrn;
    unsigned long chunk_left;             /* bytes left in current chunk (excluding pad) */
    unsigned char pad, have_bmhd, bmhd[20], bmhdn;
    unsigned short cmapn;
    unsigned char *planes[8], nplanes_dst;
    unsigned short bpr_dst, max_rows, row, plane, col, src_bpr;
    unsigned char op_left, op_run;        /* ByteRun1: bytes still to emit; 1 = waiting for/using run value */
} ilbm_t;
void ilbm_init(ilbm_t *d);
int  ilbm_feed(ilbm_t *d, const unsigned char *p, unsigned short n, unsigned short *used);
const ilbm_info_t *ilbm_info(const ilbm_t *d);
void ilbm_set_target(ilbm_t *d, unsigned char **planes, unsigned char nplanes_dst, unsigned short bpr, unsigned short max_rows);
unsigned short ilbm_rows_done(const ilbm_t *d);
#endif
```

```c
/* src/ilbm.c — byte-at-a-time state machine so any chunking works */
#include <string.h>
#include "ilbm.h"

enum { S_FORM, S_FORMTYPE, S_CHUNK_HDR, S_BMHD, S_CMAP, S_SKIP, S_PAD, S_BODY_WAIT, S_BODY, S_DONE, S_ERR };

static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

void ilbm_init(ilbm_t *d) { memset(d, 0, sizeof *d); d->state = S_FORM; }
const ilbm_info_t *ilbm_info(const ilbm_t *d) { return &d->info; }
unsigned short ilbm_rows_done(const ilbm_t *d) { return d->row; }

void ilbm_set_target(ilbm_t *d, unsigned char **planes, unsigned char n, unsigned short bpr, unsigned short max_rows)
{
    unsigned char i;
    for (i = 0; i < n && i < 8; ++i) d->planes[i] = planes[i];
    d->nplanes_dst = n; d->bpr_dst = bpr; d->max_rows = max_rows;
    d->src_bpr = (unsigned short)(((d->info.w + 15) / 16) * 2);
    d->state = S_BODY;
}

static void put(ilbm_t *d, unsigned char b)
{
    if (d->plane < d->nplanes_dst && d->row < d->max_rows && d->col < d->bpr_dst)
        d->planes[d->plane][(unsigned long)d->row * d->bpr_dst + d->col] = b;
    if (++d->col == d->src_bpr) {
        d->col = 0;
        if (++d->plane == d->info.planes) { d->plane = 0; ++d->row; }
    }
}

static void end_chunk(ilbm_t *d) { d->state = d->pad ? S_PAD : S_CHUNK_HDR; d->hdrn = 0; }

static void body_byte(ilbm_t *d, unsigned char b)
{
    if (d->info.compression == 0) { put(d, b); return; }
    if (d->op_left == 0) {                          /* control byte */
        signed char c = (signed char)b;
        if (c >= 0)        { d->op_run = 0; d->op_left = (unsigned char)(c + 1); }
        else if (c != -128) { d->op_run = 1; d->op_left = (unsigned char)(1 - c); }
        return;
    }
    if (d->op_run) { while (d->op_left) { put(d, b); --d->op_left; } }
    else           { put(d, b); --d->op_left; }
}

int ilbm_feed(ilbm_t *d, const unsigned char *p, unsigned short n, unsigned short *used)
{
    unsigned short i = 0;
    while (i < n && d->state != S_ERR && d->state != S_DONE) {
        unsigned char b = p[i];
        switch (d->state) {
        case S_FORM:                                     /* "FORM" + u32 length */
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn == 4 && memcmp(d->hdr, "FORM", 4) != 0) d->state = S_ERR;
            else if (d->hdrn == 8) { d->hdrn = 0; d->state = S_FORMTYPE; }
            break;
        case S_FORMTYPE:                                 /* "ILBM" */
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn == 4) { d->hdrn = 0; d->state = memcmp(d->hdr, "ILBM", 4) ? S_ERR : S_CHUNK_HDR; }
            break;
        case S_CHUNK_HDR:
            d->hdr[d->hdrn++] = b; ++i;
            if (d->hdrn < 8) break;
            d->hdrn = 0;
            d->chunk_left = be32(d->hdr + 4);
            d->pad = (unsigned char)(d->chunk_left & 1);
            if (!memcmp(d->hdr, "BMHD", 4))      { d->state = d->chunk_left == 20 ? S_BMHD : S_ERR; d->bmhdn = 0; }
            else if (!memcmp(d->hdr, "CMAP", 4)) { d->state = S_CMAP; d->cmapn = 0; if (!d->chunk_left) end_chunk(d); }
            else if (!memcmp(d->hdr, "BODY", 4)) { d->state = d->have_bmhd ? S_BODY_WAIT : S_ERR; }
            else if (d->chunk_left == 0)         end_chunk(d);
            else                                 d->state = S_SKIP;
            break;
        case S_BMHD:
            d->bmhd[d->bmhdn++] = b; ++i;
            if (d->bmhdn == 20) {
                d->info.w = (unsigned short)((d->bmhd[0] << 8) | d->bmhd[1]);
                d->info.h = (unsigned short)((d->bmhd[2] << 8) | d->bmhd[3]);
                d->info.planes = d->bmhd[8];
                d->info.compression = d->bmhd[10];
                if (!d->info.w || !d->info.h || !d->info.planes || d->info.planes > 8 || d->info.compression > 1)
                    d->state = S_ERR;
                else { d->have_bmhd = 1; end_chunk(d); }
            }
            break;
        case S_CMAP:
            if (d->cmapn < 96) d->info.cmap[d->cmapn / 3][d->cmapn % 3] = b;
            ++d->cmapn; ++i;
            if (--d->chunk_left == 0) { d->info.ncolors = (unsigned char)((d->cmapn > 96 ? 96 : d->cmapn) / 3); end_chunk(d); }
            break;
        case S_SKIP:
            ++i;
            if (--d->chunk_left == 0) end_chunk(d);
            break;
        case S_PAD:
            ++i; d->state = S_CHUNK_HDR; d->hdrn = 0;
            break;
        case S_BODY_WAIT:                                /* caller must ilbm_set_target() */
            *used = i; return ILBM_HEADER;
        case S_BODY:
            if (d->chunk_left == 0 || d->row >= d->info.h) { d->state = S_DONE; break; }
            ++i; --d->chunk_left;
            body_byte(d, b);
            if (d->row >= d->info.h) d->state = S_DONE;
            break;
        }
    }
    if (d->state == S_BODY_WAIT) { *used = i; return ILBM_HEADER; }
    if (d->state == S_BODY && (d->chunk_left == 0 || d->row >= d->info.h)) d->state = S_DONE;
    if (d->state == S_DONE) { *used = n; return ILBM_DONE; }   /* trailing bytes (pad/extra chunks) are ignored */
    *used = i;
    return d->state == S_ERR ? ILBM_ERROR : ILBM_NEED_MORE;
}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `make test`
Expected: `ALL PASS`, including `test_ilbm_tiny_any_chunking` for every chunk size from 1 to 78 (the whole file) and `test_ilbm_fixture` (which uses the Task 4 file).

- [ ] **Step 5: Commit**

```bash
git add src/ilbm.c src/ilbm.h tests/test_main.c tests/fixtures && git commit -m "Add streaming ByteRun1 ILBM decoder into caller bitplanes (make test, any chunk size)"
```

### Task 8: Amiga networking, RNG and timer modules

**Files:**
- Create: `src/amiga/net.c`, `src/amiga/net.h`, `src/amiga/rng.c`, `src/amiga/rng.h`, `src/amiga/timer.c`, `src/amiga/timer.h`

**Interfaces:**
- Consumes:
  - `fn_init`, `fn_open`, `fn_open_translated`, `fn_read`, `fn_info`, `fn_close` and `fn_shutdown` (Task 3);
  - `xkcd_*` (Task 6);
  - `ilbm_*` (Task 7).
- Produces:
  ```c
  /* net.h */
  enum { NET_OK = 0, NET_ERR_NODEVICE = 0x80, NET_ERR_NOTFOUND, NET_ERR_PARSE, NET_ERR_HTTP,
         NET_ERR_TOOBIG, NET_ERR_CONVERT, NET_ERR_PARTIAL, NET_ERR_NOMEM };
  unsigned char net_fetch_comic(long num, xkcd_comic_t *out);          /* num<=0 => latest */
  /* Streams the translated ILBM. on_header is called once with the info; it must return the target
     planes (or 0 to abort) by filling the given arrays. Returns NET_OK, or NET_ERR_PARTIAL if rows were drawn
     before a failure. */
  typedef int (*net_header_cb)(void *ctx, const ilbm_info_t *info, unsigned char **planes,
                               unsigned char *nplanes, unsigned short *bpr, unsigned short *max_rows);
  unsigned char net_fetch_image(const char *img_url, const char *selector, net_header_cb cb, void *ctx);
  const char *net_error(unsigned char err, long num);   /* user-facing text; num used for "Comic #N does not exist" */
  void net_shutdown(void);
  /* rng.h */
  void rng_seed(void);                 /* DateStamp ticks ^ VHPOSR */
  unsigned long rng_next(void);        /* xorshift32 */
  /* timer.h */
  int  timer_open(void);               /* 1 ok; creates port + timerequest on UNIT_VBLANK */
  void timer_start(unsigned long seconds);
  void timer_abort(void);
  unsigned long timer_sigmask(void);
  int  timer_fired(void);              /* consumes the reply; 1 if a started request completed */
  void timer_close(void);
  ```

Behaviour that `net_fetch_comic` must have:
- Open the URL with `FN_OPEN_FOLLOW_REDIR`.
- Poll `fn_read` with `Delay(1)` on `FN_ERR_NOT_READY` or `FN_ERR_BUSY`, giving up after 15 seconds (750 polls). Images get 60 seconds, because the translator returns NotReady until the whole download and conversion has finished.
- Read into a static 4 KB buffer. Longer bodies are truncated; the `alt`/`title`/`img` fields come early in xkcd's JSON, and the transcript is not needed.
- Call `fn_info` and check the status: 404 → `NET_ERR_NOTFOUND`; any other status ≥ 400 → `NET_ERR_HTTP`.
- If `xkcd_parse` fails → `NET_ERR_PARSE`.
- On `FN_ERR_NOT_FOUND` from `fn_init` → `NET_ERR_NODEVICE`.
- On `FN_ERR_TRANSPORT` or `FN_ERR_IO` → call `net_shutdown()`, so the next call re-runs `fn_init`. This pattern comes from ISS Tracker's `fetch.c`.

Behaviour that `net_fetch_image` must have:
- Open with `fn_open_translated(..., FN_TRANSLATE_IMAGE, 0, selector)`.
- Read 512-byte chunks and feed them to `ilbm_feed`.
- On `ILBM_HEADER`, call `cb`. If `cb` returns 0 → `NET_ERR_NOMEM`.
- If `fn_read` returns `FN_ERR_UNSUPPORTED` → `NET_ERR_TOOBIG` (the pixel cap). If it returns `FN_ERR_INVALID` → `NET_ERR_CONVERT`.
- On EOF before `ILBM_DONE` → `NET_ERR_PARTIAL`.

`net_error` text:

| code | text |
|---|---|
| NODEVICE | "NIO driver not loaded (run fujinet-nio first)" |
| NOTFOUND | "Comic #%ld does not exist" |
| PARSE | "Unexpected reply from xkcd.com" |
| HTTP | "xkcd.com returned an error" |
| TOOBIG | "Image too large for FujiNet to convert" |
| CONVERT | "FujiNet could not convert this image" |
| PARTIAL | "Image transfer interrupted" |
| NOMEM | "Not enough chip memory" |

`FN_ERR_TRANSPORT` → "No reply from FujiNet", `FN_ERR_TIMEOUT` → "FujiNet timed out", and anything else → `fn_error_string()`.

- [ ] **Step 1: Write the modules**

```c
/* src/amiga/rng.c */
#include <proto/dos.h>
#include <hardware/custom.h>
#include "rng.h"
extern struct Custom custom;
static unsigned long s = 2463534242UL;
void rng_seed(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    s ^= ((unsigned long)ds.ds_Tick << 16) ^ (unsigned long)ds.ds_Minute ^ (unsigned long)custom.vhposr;
    if (!s) s = 2463534242UL;
}
unsigned long rng_next(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
```

```c
/* src/amiga/timer.c — V33-safe: CreatePort/CreateExtIO from amiga.lib */
#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include "timer.h"

static struct MsgPort *port;
static struct timerequest *tr;
static int open_ok, pending;

int timer_open(void)
{
    if (!(port = CreatePort(0, 0))) return 0;
    if (!(tr = (struct timerequest *)CreateExtIO(port, sizeof *tr))) { timer_close(); return 0; }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0) != 0) { timer_close(); return 0; }
    open_ok = 1;
    return 1;
}
void timer_start(unsigned long seconds)
{
    timer_abort();
    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_secs = seconds; tr->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)tr);
    pending = 1;
}
void timer_abort(void)
{
    if (!pending) return;
    if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
    WaitIO((struct IORequest *)tr);
    pending = 0;
}
unsigned long timer_sigmask(void) { return port ? 1UL << port->mp_SigBit : 0; }
int timer_fired(void)
{
    if (!pending || !CheckIO((struct IORequest *)tr)) return 0;
    WaitIO((struct IORequest *)tr);
    pending = 0;
    return 1;
}
void timer_close(void)
{
    timer_abort();
    if (open_ok) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteExtIO((struct IORequest *)tr);
    if (port) DeletePort(port);
    tr = 0; port = 0; open_ok = 0;
}
```

`WaitIO` is only ever called on a request that `SendIO` actually sent. That respects the workspace rule "do not `WaitIO` an OpenDevice-only IORequest".

```c
/* src/amiga/net.c */
#include <stdio.h>
#include <proto/dos.h>
#include "fujinet-nio.h"
#include "net.h"

#define JSON_WAITS  750         /* 15 s at Delay(1) = 1/50 s */
#define IMAGE_WAITS 3000        /* 60 s: FujiNet buffers the whole image, then decodes/converts before the first read */
static unsigned char up;

static unsigned char ensure_up(void)
{
    unsigned char e;
    if (up) return FN_OK;
    e = fn_init();
    if (e == FN_ERR_NOT_FOUND) return NET_ERR_NODEVICE;
    if (e != FN_OK) return e;
    up = 1; return FN_OK;
}

static unsigned char finish(unsigned char e)
{
    if (e == FN_ERR_TRANSPORT || e == FN_ERR_IO) net_shutdown();
    return e;
}

/* Reads one chunk with NOT_READY/BUSY polling. */
static unsigned char read_chunk(fn_handle_t h, unsigned long off, unsigned char *b, unsigned short max,
                                unsigned short *n, unsigned char *fl, int max_waits)
{
    int waits = 0; unsigned char e;
    for (;;) {
        *n = 0; *fl = 0;
        e = fn_read(h, off, b, max, n, fl);
        if (e != FN_ERR_NOT_READY && e != FN_ERR_BUSY) return e;
        if (++waits > max_waits) return FN_ERR_TIMEOUT;
        Delay(1);
    }
}

unsigned char net_fetch_comic(long num, xkcd_comic_t *out)
{
    static char buf[4096];
    char url[64]; fn_handle_t h; unsigned short n, total = 0, status = 0; unsigned long len; unsigned char fl, e;
    if ((e = ensure_up()) != FN_OK) return e;
    xkcd_info_url(num, url, sizeof url);
    if ((e = fn_open(&h, FN_METHOD_GET, url, FN_OPEN_FOLLOW_REDIR)) != FN_OK) return finish(e);
    while (total < sizeof buf - 1) {
        e = read_chunk(h, total, (unsigned char *)buf + total, (unsigned short)(sizeof buf - 1 - total), &n, &fl, JSON_WAITS);
        if (e != FN_OK) break;
        total += n;
        if ((fl & FN_READ_EOF) || n == 0) break;
    }
    if (e == FN_OK && fn_info(h, &status, &len, &fl) == FN_OK && (fl & FN_INFO_HAS_STATUS)) {
        if (status == 404) e = NET_ERR_NOTFOUND;
        else if (status >= 400) e = NET_ERR_HTTP;
    }
    fn_close(h);
    if (e != FN_OK) return finish(e);
    buf[total] = 0;
    return xkcd_parse(buf, total, out) ? NET_OK : NET_ERR_PARSE;
}

unsigned char net_fetch_image(const char *img_url, const char *selector, net_header_cb cb, void *ctx)
{
    static unsigned char buf[512];
    static ilbm_t dec;
    fn_handle_t h; unsigned long off = 0; unsigned short n, used, k; unsigned char fl, e; int r = ILBM_NEED_MORE;
    if ((e = ensure_up()) != FN_OK) return e;
    e = fn_open_translated(&h, FN_METHOD_GET, img_url, FN_OPEN_FOLLOW_REDIR, FN_TRANSLATE_IMAGE, 0, selector);
    if (e != FN_OK) return finish(e);
    ilbm_init(&dec);
    while (r != ILBM_DONE) {
        e = read_chunk(h, off, buf, sizeof buf, &n, &fl, IMAGE_WAITS);
        if (e != FN_OK) break;
        off += n;
        for (k = 0; k < n && r != ILBM_DONE; k += used) {
            r = ilbm_feed(&dec, buf + k, (unsigned short)(n - k), &used);
            if (r == ILBM_ERROR) { e = NET_ERR_CONVERT; break; }
            if (r == ILBM_HEADER) {
                unsigned char *planes[8]; unsigned char np; unsigned short bpr, rows;
                if (!cb(ctx, ilbm_info(&dec), planes, &np, &bpr, &rows)) { e = NET_ERR_NOMEM; break; }
                ilbm_set_target(&dec, planes, np, bpr, rows);
            }
        }
        if (e != FN_OK || (fl & FN_READ_EOF) || n == 0) break;
    }
    fn_close(h);
    if (e == FN_ERR_UNSUPPORTED) e = NET_ERR_TOOBIG;
    else if (e == FN_ERR_INVALID) e = NET_ERR_CONVERT;
    if (e == FN_OK && r != ILBM_DONE) e = ilbm_rows_done(&dec) ? NET_ERR_PARTIAL : NET_ERR_CONVERT;
    if (e != FN_OK && e < 0x80 && ilbm_rows_done(&dec)) { finish(e); return NET_ERR_PARTIAL; }
    return finish(e);
}

const char *net_error(unsigned char e, long num)
{
    static char msg[48];
    switch (e) {
    case NET_ERR_NODEVICE: return "NIO driver not loaded (run fujinet-nio first)";
    case NET_ERR_NOTFOUND: snprintf(msg, sizeof msg, "Comic #%ld does not exist", num); return msg;
    case NET_ERR_PARSE:    return "Unexpected reply from xkcd.com";
    case NET_ERR_HTTP:     return "xkcd.com returned an error";
    case NET_ERR_TOOBIG:   return "Image too large for FujiNet to convert";
    case NET_ERR_CONVERT:  return "FujiNet could not convert this image";
    case NET_ERR_PARTIAL:  return "Image transfer interrupted";
    case NET_ERR_NOMEM:    return "Not enough chip memory";
    case FN_ERR_TRANSPORT: return "No reply from FujiNet";
    case FN_ERR_TIMEOUT:   return "FujiNet timed out";
    default:               return fn_error_string(e);
    }
}

void net_shutdown(void) { if (up) { fn_shutdown(); up = 0; } }
```

The `net.h`, `rng.h` and `timer.h` headers contain exactly the declarations from the Interfaces block, with `#include "xkcd.h"` and `#include "ilbm.h"` in `net.h`.

**Check before relying on it:** confirm how the firmware maps `StatusCode::Unsupported` and `StatusCode::InvalidRequest` to `fn_read` return codes. Run `grep -n "Unsupported\|InvalidRequest" $NIO_WORKSPACE/repos/fujinet-nio-lib/src/common/*.c` and the `fn_parse_*` status mapping. Adjust the two mappings in `net_fetch_image` to match.

- [ ] **Step 2: Build**

Run: `make amiga`
Expected: the build succeeds with no warnings from `src/amiga/net.c`, `rng.c` or `timer.c`.

- [ ] **Step 3: Commit**

```bash
git add src/amiga && git commit -m "amiga: net (JSON + streamed ILBM), xorshift RNG, VBLANK timer (make amiga)"
```

### Task 9: Main screen UI, menu, buttons and the main loop

**Files:**
- Create: `src/amiga/ui.c`, `src/amiga/ui.h`
- Modify: `src/amiga/main.c` (replace the stub)

**Interfaces:**
- Consumes:
  - Tasks 6–8: `history_*`, `xkcd_*`, `wrap_text`, `selector_main`, `net_*`, `rng_*` and `timer_*`.
  - Task 10's `dlg_fetch_id`, `dlg_auto_refresh` and Task 11's `zoom_show`, through prototypes declared now in `dialogs.h` and `zoom.h`. Task 9 adds temporary stubs that return "cancel".
- Produces:
  ```c
  /* ui.h */
  enum { UI_NONE, UI_PREV, UI_NEXT, UI_ZOOM, UI_FETCH_ID, UI_AUTO, UI_QUIT };
  int  ui_open(void);                 /* 1 ok; opens screen+window */
  void ui_close(void);
  struct Screen *ui_screen(void);
  struct Window *ui_window(void);
  int  ui_is_pal(void);
  unsigned long ui_sigmask(void);
  int  ui_poll(void);                 /* drains IDCMP, returns first UI_* action */
  void ui_show_comic(const xkcd_comic_t *c);     /* title bar, caption, clears image area */
  void ui_status(const char *msg);               /* bottom status line */
  void ui_set_buttons(int can_prev, int auto_on);
  int  ui_image_header_cb(void *ctx, const ilbm_info_t *i, unsigned char **planes, unsigned char *np,
                          unsigned short *bpr, unsigned short *rows);   /* centres image in box, sets pens 4-15 */
  void ui_image_message(const char *msg);        /* centred text in image box ("No static image...") */
  ```

**Screen layout** (PAL 640 × 256 shown; for NTSC, take 40 lines off the image box):

```
y 0-10    screen title bar: "xkcd #353  Python  (2007-03-05)"      [menu on RMB: Project ▸ Fetch ID…, Auto Refresh…, Quit]
y 12-161  image box 624×150 at x=8 (x and box width 16-px aligned)
y 164-219 caption: alt text, word-wrapped at 76 cols × up to 6 lines (topaz 8)
y 224-238 [ < Previous ]  [ Zoom ]  [ Next > ]          "Auto: 60s" indicator when running
y 244-254 status line
```

- The screen is a custom screen with `ViewModes = HIRES`, 4 planes and `Type = CUSTOMSCREEN`. It has one borderless backdrop window covering it, with `IDCMP = MENUPICK | GADGETUP | RAWKEY`.
- Pens: 0 = `0xAAA` (light gray), 1 = `0x000` (black), 2 = `0xFFF` (white), 3 = `0x58B` (WB 1.3 blue). The image header callback sets pens 4–15 from the CMAP with `SetRGB4(vp, i, r>>4, g>>4, b>>4)`.
- Buttons are boolean gadgets with `GADGHCOMP`, drawn with `Border` structures. When there is no history, Previous is disabled with `OffGadget`. Keyboard shortcuts: Left = previous, Right = next, Z = zoom.
- The menu is one `struct Menu` called "Project", with three `MenuItem`s (`ITEMTEXT | ITEMENABLED | HIGHCOMP | COMMSEQ`) and the shortcuts Amiga-F, Amiga-A and Amiga-Q. On V33, `CheckIt` is not needed.
- Image placement: `x_byte = (8 + (624 - w)/2) / 16 * 2` (word-aligned) and `y0 = 12 + (150 - h)/2`. `planes[i] = bm->Planes[i] + y0*bm->BytesPerRow + x_byte` for i = 0..3, `bpr = bm->BytesPerRow`, `rows = 150 - (y0 - 12)`.

**Main loop behaviour (`main.c`):**
1. Start-up:
   - `rng_seed()`, then `timer_open()`, then `ui_open()`. If any of them fails, print the failure to stdout and exit 20.
   - `net_fetch_comic(0, &latest)` caches `latest.num` for `latest_num`, with the status "Contacting xkcd.com…". On failure, show the error and set `latest_num = 0`.
   - Then `show_random()`.
2. `show_id(id, push)`:
   - Set the status to "Fetching #id…" and call `net_fetch_comic(id)`.
   - On error, show the status from `net_error(e, id)` and leave the screen unchanged.
   - Otherwise, if `push` is true, call `history_push(h, c.num)`. Then call `ui_show_comic(&c)`.
   - If `!c.has_image`, show `ui_image_message("No static image for this comic")`. Otherwise set the status to "Converting image…" and call `net_fetch_image(c.img, sel_main, ui_image_header_cb, 0)`. Show any error in the status line; NET_ERR_PARTIAL leaves the partial image visible.
   - On success, clear the status line.
   - Keep `current` (an `xkcd_comic_t`) for Zoom.
3. `show_random()`:
   - If `latest_num == 0`, retry fetching the latest first.
   - `id = xkcd_random_pick(rng_next(), latest_num, history_current(&h))`, then `show_id(id, 1)`.
4. UI_PREV: `if (history_back(&h,&id)) show_id(id,0)`.
5. UI_NEXT: `if (history_forward(&h,&id)) show_id(id,0); else show_random();`
6. UI_ZOOM: `if (current.has_image) zoom_show(&current); else ui_status("No image to zoom");`
7. UI_FETCH_ID:
   - `long id; if (dlg_fetch_id(&id, latest_num)) show_id(id, 1);`
   - The dialog already rejects non-numbers and values ≤ 0. Values above `latest_num` are allowed through, so the server's 404 produces the standard error message.
8. UI_AUTO:
   - `int r = dlg_auto_refresh(&auto_secs, auto_on);`
   - `DLG_START` → `auto_on = 1; timer_start(auto_secs);`
   - `DLG_STOP` → `auto_on = 0; timer_abort();`
   - `DLG_CANCEL` → nothing.
   - Then `ui_set_buttons(...)`, which updates the "Auto: Ns" text.
9. Wait on `ui_sigmask() | timer_sigmask() | SIGBREAKF_CTRL_C`.
   - On a timer signal: `if (timer_fired() && auto_on) { show_random(); timer_start(auto_secs); }`. The timer is restarted after the fetch completes, so slow fetches never stack up.
10. UI_QUIT or Ctrl-C: `timer_close()`, `net_shutdown()`, `ui_close()`, then exit 0.

- [ ] **Step 1: Write `ui.c`, `ui.h`, `main.c`, plus temporary `dialogs.c`/`dialogs.h` and `zoom.c`/`zoom.h` stubs**

The stubs' declarations are final:

```c
/* src/amiga/dialogs.h */
#ifndef XKCD_DIALOGS_H
#define XKCD_DIALOGS_H
enum { DLG_CANCEL = 0, DLG_START = 1, DLG_STOP = 2 };
#define AUTO_MIN 10
#define AUTO_MAX 600
#define AUTO_DEFAULT 60
int dlg_fetch_id(long *id, long latest);                  /* 1 = OK with *id set, 0 = cancel */
int dlg_auto_refresh(unsigned short *secs, int running);  /* DLG_*; *secs updated only on START */
#endif
```

```c
/* src/amiga/zoom.h */
#ifndef XKCD_ZOOM_H
#define XKCD_ZOOM_H
#include "xkcd.h"
void zoom_show(const xkcd_comic_t *c);   /* blocks until Esc (or error), restores main screen */
#endif
```

`ui.c` must use only V33 calls: `OpenScreen` (with a `NewScreen` struct), `OpenWindow` (`NewWindow`), `SetMenuStrip`, `ClearMenuStrip`, `AddGadget`, `RefreshGadgets`, `OnGadget`, `OffGadget`, `SetRGB4`, `SetAPen`, `RectFill`, `Move`, `Text`, `GetMsg`/`ReplyMsg`, `SetWindowTitles`, and `OpenFont` for topaz 8. Write it in the style of `nio-config/src/platform/amiga/amiga_gui.c`, which is in this workspace: static `Gadget`, `IntuiText` and `Border` arrays, initialised in a setup function. Detect PAL with `((struct GfxBase *)GfxBase)->DisplayFlags & PAL`.

- [ ] **Step 2: Build**

Run: `make amiga`
Expected: the build succeeds and `r2r/amiga/xkcd.adf` is rebuilt.

- [ ] **Step 3: Manual test in Amiberry, WB 1.3 profile** (follow `$NIO_WORKSPACE/docs/amiga/amiberry-testing.md`, with the POSIX firmware built in Task 2 serving FujiNet)

Insert `r2r/amiga/xkcd.adf`, load the NIO driver as documented, and run `df1:xkcd` from the CLI. Check each of the following:
- [ ] A random comic appears within ~15 seconds, with its title in the title bar and wrapped alt text below.
- [ ] Next shows a different comic. After 3 Nexts, Previous ×3 walks back in exact reverse order, and Next then walks forward through the same IDs before fetching new random ones.
- [ ] After 27 Nexts, Previous stops after 24 steps (25 entries).
- [ ] Right-mouse menu → Quit exits cleanly, back to the CLI with no Guru.
- [ ] Start the app without the NIO driver: the status line says "NIO driver not loaded (run fujinet-nio first)" and Quit still works.

Record the results (with a screenshot from `./scripts/amiberry-ipc` if available) in the commit message.

- [ ] **Step 4: Commit**

```bash
git add src/amiga && git commit -m "amiga: main screen, Project menu, Previous/Next/Zoom buttons, 25-entry history loop (Amiberry WB1.3 manual check)"
```

### Task 10: Fetch ID and Auto Refresh windows

**Files:**
- Modify: `src/amiga/dialogs.c` (replace the stubs)

**Interfaces:**
- Consumes: `ui_screen()` and `ui_window()` (Task 9), plus `AUTO_MIN`, `AUTO_MAX` and `AUTO_DEFAULT`.
- Produces: the final `dlg_fetch_id` and `dlg_auto_refresh` behaviour declared in Task 9.

**Fetch ID window** (on the main screen, 260 × 70, centred, `ACTIVATE | WINDOWDRAG | WINDOWDEPTH`, title "Fetch xkcd by ID", `IDCMP = GADGETUP | RAWKEY | CLOSEWINDOW`):
- An integer string gadget: `STRGADGET | LONGINT`, a 7-character buffer, activated on open with `ActivateGadget`.
- OK and Cancel buttons. Return confirms (the same as OK) and Esc cancels.
- On OK, read `((struct StringInfo *)g->SpecialInfo)->LongInt`.
- If the value is ≤ 0, keep the window open and show "Enter a number from 1 to <latest>" in the window, in red pen 3. If `latest` is 0, show "Enter a positive number" instead.
- Values above `latest` are accepted. The server's 404 then produces "Comic #N does not exist" in the main status line, as R5 requires.

**Auto Refresh window** (300 × 90, title "Auto Refresh"):
- A horizontal proportional gadget (`PROPGADGET`, `AUTOKNOB | FREEHORIZ`):
  - `HorizBody = MAXBODY / 60`.
  - The value maps linearly onto 10..600 in steps of 10: `secs = 10 + ((HorizPot * 59UL + MAXPOT/2) / MAXPOT) * 10`.
  - The window listens for `GADGETDOWN`, `MOUSEMOVE` and `GADGETUP` while dragging. A live "Every NNN seconds" label redraws while dragging.
- `[-]` and `[+]` buttons nudge the value by 10. Clamp to `AUTO_MIN..AUTO_MAX`.
- Start, Stop and Cancel buttons:
  - Start: `*secs = value; return DLG_START`.
  - Stop: `return DLG_STOP`. It is disabled with `OffGadget` when `!running`.
  - Cancel and Esc: `return DLG_CANCEL`. `*secs` stays unchanged.
- The initial value is `*secs` (`AUTO_DEFAULT` on first use). The slider position is `HorizPot = ((secs-10)/10) * MAXPOT / 59`.
- A label shows "Status: running every N s" or "Status: stopped".

Put the slider mapping in a portable helper so it can be tested on the host. Create `src/autorange.c` and `src/autorange.h`:

```c
/* src/autorange.h */
#ifndef XKCD_AUTORANGE_H
#define XKCD_AUTORANGE_H
#define AUTO_MIN 10
#define AUTO_MAX 600
#define AUTO_DEFAULT 60
unsigned short auto_from_pot(unsigned short pot);      /* 0..0xFFFF -> 10..600 step 10 */
unsigned short auto_to_pot(unsigned short secs);
unsigned short auto_clamp(long secs);
#endif
```

```c
/* src/autorange.c */
#include "autorange.h"
unsigned short auto_clamp(long s) { if (s < AUTO_MIN) return AUTO_MIN; if (s > AUTO_MAX) return AUTO_MAX; return (unsigned short)(((s + 5) / 10) * 10); }
unsigned short auto_from_pot(unsigned short pot) { return (unsigned short)(AUTO_MIN + ((pot * 59UL + 0x7FFF) / 0xFFFF) * 10); }
unsigned short auto_to_pot(unsigned short secs) { secs = auto_clamp(secs); return (unsigned short)(((secs - AUTO_MIN) / 10) * 0xFFFFUL / 59); }
```

Then remove the `AUTO_*` defines from `dialogs.h` and `#include "autorange.h"` there instead.

- [ ] **Step 1: Write the failing host test** (add to `tests/test_main.c` and register it in `main`)

```c
#include "autorange.h"
static void test_autorange(void)
{
    unsigned short s;
    CHECK(auto_from_pot(0) == 10 && auto_from_pot(0xFFFF) == 600);
    for (s = 10; s <= 600; s += 10) CHECK(auto_from_pot(auto_to_pot(s)) == s);
    CHECK(auto_clamp(0) == 10 && auto_clamp(9999) == 600 && auto_clamp(64) == 60);
}
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `make test`
Expected: compile FAIL with `autorange.h: No such file or directory`.

- [ ] **Step 3: Implement `autorange.c` (above) and `dialogs.c`**

Each dialog runs its own modal loop: `Wait(1L << win->UserPort->mp_SigBit)`. While a dialog is up, the main window's IDCMP is not drained, which is acceptable. **The auto-refresh timer is not serviced while a dialog is open.** If it fired, the main loop catches it after the dialog closes, through `timer_fired()`.

- [ ] **Step 4: Run the tests, build, and check manually in Amiberry**

Run: `make test && make amiga`
Expected: `ALL PASS` and a successful build. Then, in Amiberry:
- [ ] Fetch ID → `353` → OK shows "Python".
- [ ] Fetch ID → `404` → OK shows "Comic #404 does not exist", and the previous comic stays on screen.
- [ ] Fetch ID → `999999` → "Comic #999999 does not exist".
- [ ] Fetch ID → empty or `0` → the inline hint appears and the window stays open. Cancel closes it with no change.
- [ ] Auto Refresh → drag to 10 → Start: a new comic appears every ~10 s, and the indicator shows "Auto: 10s".
- [ ] Reopen it → Cancel: still running at 10 s. Reopen → Stop: stops. While stopped, the Stop button is disabled.
- [ ] Previous during auto refresh still works, and the next tick adds a new random comic to the history.

- [ ] **Step 5: Commit**

```bash
git add src tests && git commit -m "amiga: Fetch ID (integer, OK/Cancel) and Auto Refresh (10-600 s slider, Start/Stop/Cancel) windows (make test; Amiberry manual)"
```

### Task 11: Zoom (full-screen interlaced)

**Files:**
- Modify: `src/amiga/zoom.c` (replace the stub)

**Interfaces:**
- Consumes: `net_fetch_image`, `selector_zoom`, `ui_is_pal()` and `ui_status()`.
- Produces: the final `zoom_show(const xkcd_comic_t *c)`.

**Behaviour:**
1. Open a custom screen: 640 × 512 (PAL) or 640 × 400 (NTSC), `ViewModes = HIRES | LACE`, depth 4, `SHOWTITLE` off. If `OpenScreen` fails, call `ui_status("Not enough chip memory for Zoom")` and return.
2. Open a borderless `BACKDROP | BORDERLESS | ACTIVATE | RMBTRAP` window covering the screen, with `IDCMP = RAWKEY | MOUSEBUTTONS`.
3. Fill with pen 0, then call `net_fetch_image(c->img, sel_zoom, zoom_header_cb, &ctx)`. The callback:
   - sets pens 0–15 from the CMAP;
   - centres the image with the same word-aligned formula, using the full screen as the box.

   While it fetches, show "Loading #N…" centred, in pen 15 drawn over pen 0. Clear the text when the header arrives.
4. On error, draw the `net_error()` text centred and keep the screen up, so the user still presses Esc.
5. Wait for RAWKEY code `0x45` (Esc, key down). Ignore every other key. A left mouse click does not exit; only Esc does, per R3.
6. Close the window, close the screen, call `ScreenToFront(ui_screen())` and `ActivateWindow(ui_window())`.

The Escape keycode on V33 RAWKEY is `0x45`. Mask out key-up events with `!(code & IECODE_UP_PREFIX)`.

- [ ] **Step 1: Implement `zoom.c`**
- [ ] **Step 2: Build**

Run: `make amiga`
Expected: a successful build.

- [ ] **Step 3: Check manually in Amiberry (512 KB chip, A500 profile)**
- [ ] Zoom on #353 shows a large, readable interlaced image, and Esc returns to the main screen with the same comic and caption.
- [ ] Zoom on a colour comic (for example #1732, "Earth Temperature Timeline", or any recent colour strip) shows sensible colours.
- [ ] Zoom when no comic image is available shows the status "No image to zoom", and no screen opens.
- [ ] With 512 KB chip RAM, Zoom either works or shows "Not enough chip memory for Zoom". There must be no crash.
- [ ] Pressing Zoom then Esc 10 times in a row shows no memory leak: the `avail chip` reading in a CLI before and after differs by less than 1 KB.

- [ ] **Step 4: Commit**

```bash
git add src/amiga/zoom.c && git commit -m "amiga: full-screen interlaced Zoom with Esc to exit (Amiberry A500 512K manual check)"
```

### Task 12: Disk polish, README and the end-to-end acceptance run

**Files:**
- Create: `amiga/ReadMe.txt`, and `amiga/icons/xkcd.info`, `amiga/icons/Disk.info` and `amiga/icons/ReadMe.info` (copy and adapt from `fujinet-iss-tracker/amiga/icons` with its `tools/mkinfo.py`)
- Modify: `Makefile` (`DISK_EXTRA_FILES_AMIGA += amiga/icons/xkcd.info:xkcd.info amiga/icons/Disk.info:Disk.info amiga/ReadMe.txt:ReadMe amiga/icons/ReadMe.info:ReadMe.info`) and `README.md`

- [ ] **Step 1: Write the README.** It covers:
  - what the app does;
  - requirements: KS 1.3+, 512 KB, a FujiNet NIO firmware version with the Image translator, and the `fujinet-nio.device` broker;
  - building: `source $NIO_WORKSPACE/scripts/env.sh && make amiga`, plus `make test`;
  - the controls: the buttons, Left/Right/Z, the menu shortcuts Amiga-F/A/Q, and Esc in Zoom;
  - the FujiNet image selector contract, with a link to `fujinet-nio/docs/network_device_protocol.md`.
- [ ] **Step 2: Run the full acceptance pass.** Run `make clean && make test && make amiga`, then work through every R1–R9 item and every Review Focus item in Amiberry:
  - R1: random comic with image and caption.
  - R2: Previous/Next and the 25-entry depth.
  - R3: Zoom and Esc.
  - R4: the menu has exactly three items.
  - R5: Fetch ID OK/Cancel, and the error for 404 and out-of-range IDs.
  - R6: Auto Refresh 10/600 bounds, Start/Stop/Cancel.
  - R7: a PNG (#353), a JPEG (#1, "Barrel"), and a GIF if one can be found (`grep -l '.gif'` across a few `info.0.json`; otherwise note that none could be found).
  - R8: runs through fujinet-nio-lib and the broker.
  - R9: `make amiga/r2r` output in `r2r/amiga/`.
  - Review Focus: #1608 shows the no-image message; a large comic such as #657 or #1190 either renders or shows the "too large" message; a caption containing `’` renders as `'`; the "NIO driver not loaded" path.

  Record pass or fail for each item in `docs/acceptance-2026-10.md`.
- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "Disk icons, ReadMe, README, acceptance record (make test; make amiga; Amiberry R1-R9)"
```

---

## Execution order and dependencies

```
Task 1 → Task 2 (firmware)  ─┐
Task 3 (lib) ────────────────┼→ Task 4 (E2E + fixture) → Task 7 (needs fixture)
Task 5 (scaffold) → Task 6 ──┘                          → Task 8 → Task 9 → Task 10 → Task 11 → Task 12
```

- Tasks 1–2, Task 3 and Tasks 5–6 can proceed in parallel.
- Task 7's fixture test skips cleanly if the fixture is missing, so Task 7 can start before Task 4. In that case, re-run `make test` once the fixture exists.
