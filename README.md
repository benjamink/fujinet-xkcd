# fujinet-xkcd

An [xkcd](https://xkcd.com) comic viewer for the Amiga 500, built on
[FujiNet](https://fujinet.online) and the fujinet-nio stack.

It shows a random comic with its title and its real caption (the `alt`
text). You can step back through the last 25 comics, view a comic
full-screen, fetch a comic by number, and have a new random comic appear on
a timer. Comic data comes from the xkcd JSON API (`https://xkcd.com/info.0.json`
and `https://xkcd.com/<n>/info.0.json`).

The Amiga cannot decode PNG, JPEG or GIF in reasonable time or memory, so
the FujiNet does it. The app opens the image URL with the firmware's
**Image translator**, which fetches the image, scales it to fit, reduces
its colours and returns a ByteRun1 IFF ILBM. The app streams the ILBM
straight into the screen's bitplanes.

## Requirements

- An Amiga with Kickstart/Workbench **1.3** (V33) or later and **512 KB**
  chip RAM. The main viewer runs on a stock 512K A500. The app uses only V33
  OS calls, the nix13 C runtime and no floating point.
- **Zoom** (full-screen, 640x512 interlaced) needs about 180 KB of free chip
  RAM. On a chip-only 512K machine booted from an HDF it fails gracefully with
  "Not enough chip memory for Zoom". In practice Zoom wants a 512K
  trapdoor/slow-RAM expansion (or a minimal startup-sequence).
- A FujiNet running a **FujiNet NIO firmware with the Image translator**
  (content translation type `4`). Firmware without it cannot convert the
  comics.
- The **`fujinet-nio.device`** broker, resident. On Workbench 1.3 install
  the FujiNet NIO drivers, then run
  `C:fujinet-load-resident DEVS:fujinet-nio.device fujinet-nio.device`
  (for example from `S:Startup-Sequence`). Without it the app starts but
  shows "NIO driver not loaded".

## Building

Requires [amiga-gcc](https://github.com/bebbo/amiga-gcc) on `PATH`, a
checkout of [fujinet-nio-lib](https://github.com/markjfisher/fujinet-nio-lib)
with `fujinet-nio-driver` next to it, and `xdftool` from
[amitools](https://github.com/cnvogelg/amitools) for the disk image.

In the fujinet-nio workspace:

```sh
source $NIO_WORKSPACE/scripts/env.sh
make amiga        # -> r2r/amiga/xkcd and r2r/amiga/xkcd.adf
make test         # host-side unit tests of the portable code in src/
```

Outside the workspace, set `FUJINET_NIO_LIB` (and `FUJINET_NIO_DRIVER` if
the driver repository is not next to it) before `make amiga`. The library
archive is built on demand by fujinet-nio-lib's own Makefile.

The project follows [MekkoGX](https://github.com/FozzTexx/MekkoGX)
conventions: the top-level `Makefile` sets `PRODUCT`, `PLATFORMS` and
`SRC_DIRS`, reusable rules live in `mekkogx/`, and output goes to
`r2r/<platform>/`. Portable logic (JSON parsing, history, ILBM decoding,
text wrapping, selectors) lives in `src/`; Amiga code lives in `src/amiga/`.

The disk image contains `xkcd`, `ReadMe` and their icons. The icons in
`amiga/icons/` are generated from the text pixel art in `amiga/gfx/` by
`tools/mkinfo.py` and committed, so the build does not need Python. Rerun
`tools/mkinfo.py` from the repository root after editing the art.

## Controls

| Control | Key | Action |
|---|---|---|
| **< Previous** | Left | Go back through the last 25 comics viewed |
| **Zoom** | Z | Show the comic full-screen (interlaced); **Esc** returns |
| **Next >** | Right | Go forward through history; past the newest entry, fetch a new random comic |

The **Project** menu (right mouse button):

| Item | Shortcut | Action |
|---|---|---|
| Fetch ID… | Amiga-F | Show a comic by number. OK or **Return** fetches it; Cancel or **Esc** closes the window. A number that is not a comic shows "Comic #N does not exist". |
| Auto Refresh… | Amiga-A | Fetch a random comic every 10–600 seconds (default 60). **Start** begins, **Stop** ends, **Cancel** changes nothing. The interval counts from when a comic finishes loading. |
| Quit | Amiga-Q | Quit |

Interactive comics with no static image (for example #1608) show their
caption with "No static image for this comic". Images too large for the
FujiNet to convert show "Image too large for FujiNet to convert". Firmware
that predates image conversion rejects the request, and the status line shows
"FujiNet firmware lacks image conversion (update it)"; update the FujiNet
firmware.

## The FujiNet image selector

The app and the firmware share one contract: the selector string passed
with content translation type `4` (`Image`) when the image URL is opened.
It is a comma-separated list of `key=value` pairs (`w`, `h`, `colors`,
`base`, `par`, `dither`, `mode`, `up`). The firmware replies with a
`FORM ILBM` (BMHD, CMAP, ByteRun1 BODY). The full grammar, defaults, limits
and error codes are documented in
`fujinet-nio/docs/network_device_protocol.md` in the
[fujinet-nio](https://github.com/markjfisher/fujinet-nio) firmware
repository, under "Content Translation". Until the Image translator is
merged upstream, it is on the `feature/xkcd-image-translator` branch.

This app sends (see `src/selector.c`):

- Main view, pens 4–15 on the 640×256 (PAL) or 640×200 (NTSC) hires screen:
  `w=624,h=150,colors=12,base=4,par=1:2` (PAL) or `h=110` (NTSC).
- Zoom, pens 0–15 on the 640×512 (PAL) or 640×400 (NTSC) interlaced screen:
  `w=640,h=512,colors=16,base=0` (PAL) or `h=400` (NTSC).

## Credits

Comics by Randall Munroe, [xkcd.com](https://xkcd.com), licensed under
Creative Commons Attribution-NonCommercial 2.5.
