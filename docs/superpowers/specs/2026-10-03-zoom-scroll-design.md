# Zoom: width-fitted, scrolling tall comics

Date: 2026-10-03. Status: approved in conversation, pending written-spec review.

## Problem

Zoom fits the whole comic onto the screen, so tall comics become unreadable. For example, #2636 "What If? 2 Countdown" (740×1215 source) shrinks to 182×149 in the main box and is squeezed in Zoom too.

## Goal

Zoom shows comics fitted to the screen **width** and scrolls vertically, so tall comics are readable. Zoom also renders line art crisp and grey. The main window is unchanged.

## Decisions

- Scrolling happens in **Zoom only**. The main window keeps its fit-to-box preview.
- Zoom requests **grey, 4 colours, no dithering**: `fmt=ilbm,bits=4,mode=gray,colors=4,dither=none,w=640,h=1024`. Laced hires pixels are about square, so no `par` is sent. Colour comics show in grey in Zoom, and that is accepted.
- The firmware caps `h` at 1024. Taller comics are narrowed slightly to fit; raising the cap is a later upstream request if real comics need it.
- Storage: keep the **compressed ILBM in memory** and decode only the visible rows (option 1). Rejected:
  - a tall chip-RAM bitmap with blitting: 160 KB chip for 640×1024×2;
  - hardware scrolling of a tall screen bitmap: same chip cost.

## Data flow and memory

1. **Fetch.** `net` reads the whole translated ILBM into one buffer allocated with `MEMF_ANY` (any memory type).
   - The size comes from the firmware's reported content length.
   - If no length is reported, the buffer grows as data arrives.
   - The hard cap is **256 KB**.
2. **Index.** A new portable module, `src/ilbm_index.{c,h}`:
   - parses BMHD and CMAP;
   - records the byte offset of each row's compressed data in BODY (`uint32_t` per row, ≤ 4 KB for 1024 rows);
   - decodes any row range directly into caller bitplanes, reusing ByteRun1 semantics identical to `ilbm.c`.
3. **Screen.** Zoom opens 640 × 512 (PAL) or 640 × 400 (NTSC), `HIRES|LACE`, **2 bitplanes**: about 80 KB chip, down from 160 KB today.
4. **Drawing.**
   - The first screenful is decoded on entry.
   - A scroll step uses `ScrollRaster` on the screen's RastPort, then decodes only the newly exposed rows.
   - An image no taller than the screen is centred, as today: no scrolling and no bar.
5. **Lifetime.** The buffer, index and screen are freed on exit from Zoom.
6. **Memory per Zoom:** about 80 KB chip (screen), plus the compressed image (typically 10–40 KB for undithered grey line art; at most 256 KB), plus the index (≤ 4 KB).

## Zoom screen and controls

- On entry: show "Loading #N…". Set pens 0–3 from the image CMAP.
- Keys:
  - Up/Down: 16 rows;
  - Shift+Up/Down, Space, Backspace: one screen minus 32 rows of overlap;
  - **T** / **B**: top / bottom;
  - **Esc**: exit.
  - Key repeat is honoured.
- Mouse:
  - hold the left button and drag; the image follows the mouse one row per pixel moved;
  - uses `REPORTMOUSE` and `MOUSEMOVE` (V33);
  - `RMBTRAP` is kept.
- Position indicator:
  - a 4-pixel bar at the right screen edge, drawn only when the image is taller than the screen;
  - the track is pen 1 (dark grey) and the thumb is pen 3 (white);
  - thumb height and position are proportional to the visible part.
- Responsiveness: each loop pass drains all pending IDCMP messages and sums the scroll delta, then performs one clamped scroll and one exposed-rows decode.
- Unchanged: main window, Previous/Next, Fetch ID and Auto Refresh. Auto Refresh stays paused while Zoom is open, as today.

## Errors

Each case keeps the screen open with a centred message until Esc, unless stated otherwise.

| Situation | Behaviour |
|---|---|
| Not enough chip RAM for the screen | Existing "Not enough chip memory for Zoom" status; no screen opens |
| Image over 256 KB | "Image too large for Zoom" |
| Buffer allocation fails | "Not enough memory for Zoom" |
| Transfer interrupted | Complete rows are indexed and shown; scrolling covers those rows only; "Image transfer interrupted" |
| Invalid ILBM | "FujiNet could not convert this image" |
| Firmware without the Image type | Existing "FujiNet firmware lacks image conversion" |

## Components

| Unit | Responsibility | Depends on |
|---|---|---|
| `src/ilbm_index.{c,h}` (new, portable) | Parse a complete or truncated in-memory ILBM; build the row index; decode a row range into bitplanes | nothing Amiga-specific |
| `src/zoomscroll.{c,h}` (new, portable) | Scroll arithmetic: clamp the top row, page step, exposed row range for a delta, indicator thumb geometry | nothing |
| `src/selector.c` | `selector_zoom()` returns the grey-4 selector above (`w=640,h=1024`, PAL and NTSC alike) | — |
| `src/amiga/net.{c,h}` | New `net_fetch_image_buffer()`: open translated, read everything into an allocated buffer, apply the cap, return the buffer and length, and map errors like `net_fetch_image` | fujinet-nio-lib |
| `src/amiga/zoom.c` | 2-plane screen, the fetch→index→draw flow, the event loop, `ScrollRaster` with exposed-row decode, the indicator | the units above |

## Testing

- **Host (`make test`):**
  - Decode every row range of the TINY test image and of the real fixtures through `ilbm_index`. The output must match the streaming `ilbm.c` decoder byte for byte.
  - Truncated buffers index only the complete rows.
  - Malformed data is rejected.
  - `zoomscroll` clamping, page steps, exposed ranges and thumb geometry, including images shorter than the screen.
  - The selector string test is updated.
- **Fixture:** #2636 converted by the POSIX firmware with the Zoom selector, saved as `tests/fixtures/2636_zoom_gray4.iff`.
- **Amiberry, KS 1.3 and 2.04:**
  - #2636 scrolls top to bottom by keys and by drag;
  - a short comic is centred with no bar;
  - a colour comic shows in grey;
  - Esc returns cleanly;
  - chip memory is unchanged across 10 enter/exit cycles.
- **Real hardware:** a final check by the user.

## Out of scope

- Scrolling in the main window.
- Colour in Zoom.
- Horizontal scrolling.
- Raising the firmware's `h` cap.
- Making Zoom fit a chip-only 512 KB machine (it would also need the main screen closed while Zoom is open).
