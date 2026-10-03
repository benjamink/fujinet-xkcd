# Acceptance run, October 2026

End-to-end acceptance of the Amiga 500 xkcd viewer against the spec
requirements R1–R9 and the plan's Review Focus items.

## Setup

- **App:** branch `feature/amiga-client`. `make clean && make test && make amiga`
  all succeeded with 0 warnings and `ALL PASS`. `r2r/amiga/xkcd` is 37 548 bytes.
- **Emulator:** Amiberry, A500 (`a500-000`), Kickstart/Workbench 1.3,
  cycle-exact 68000. It booted from a scratch copy of the WB 1.3 hard-disk
  image; no workspace image or config was changed.
  - **Main run:** 512 KB chip RAM, no fast RAM. The `a500-000` machine
    preset also adds **512 KB slow ("trapdoor") RAM**, which `avail` lists
    as "fast". Earlier task reports described this setup as "512K chip, no
    fast RAM"; it is really chip plus slow RAM.
  - **Chip-only re-check:** after the main run, slow RAM was set to 0 through
    IPC (a runtime setting, not saved) and the machine was hard-reset. See
    the "512 KB chip only" rows.
- **Firmware:** POSIX `fujibus-tcp-debug` build of fujinet-nio
  `feature/xkcd-image-translator` (8d2c152), which includes the Image content
  translator (type 4). It was started by the workspace's Amiberry launcher.
- **Broker:** `fujinet-nio.device`, loaded with `fujinet-load-resident`.
- **Driving:** keys and menu shortcuts were sent through the Amiberry IPC
  socket, and mouse clicks with relative mouse moves. Comic IDs, image sizes
  and timings come from the firmware's fujibus log: the `fn_open` URL
  payloads, the `FORM`/`BMHD` header of the first read reply, and the
  handle-close time.
- All timings are at normal A500 speed unless they are marked as warp.

## Results

| Item | Result | Evidence / notes |
|---|---|---|
| **Build gate:** `make clean && make test && make amiga` | PASS | rc 0, 0 warnings, `ALL PASS`. The ADF lists `xkcd`, `xkcd.info`, `Disk.info`, `ReadMe` and `ReadMe.info`. |
| **R1** Random comic with image and caption | PASS | At start-up the app read `info.0.json` (latest = 3306) and then picked random #2293 "RIP John Conway". The title bar showed `xkcd #2293  RIP John Conway  (2020-04-13)`, the image (a GIF) was drawn, and the caption "1937-2020" appeared below it. Every later random pick (about 40 in total) showed its image and wrapped alt text. |
| R1: time to first image | PASS (info) | From typing `xkcd` (00:39:29.0) to the close of the first image handle (00:39:42.9) took **about 14 s**. That image was a small GIF (227×149, 5 934-byte ILBM). Typical main-view images (11–32 KB ILBM) finished 17–44 s after their `fn_open`. Examples: #353 took 19.5 s (13 834 B), #1 took 29.9 s (19 844 B), #657 took 34.7 s (24 312 B) and #378 took 44.1 s (32 242 B). |
| **R2** Previous / Next, 25-entry depth | PASS | Run under warp: 26 Nexts from #2293 viewed 27 comics, then Previous was pressed 25 times. A script checked the log: the 24 Previous fetches were exactly the reverse of entries 3–26 (573, 12, 3138 … 2982, 3166), and the 25th Previous fetched nothing. #3166 was shown with Previous ghosted. #2293 and #2330, the two oldest, had dropped out of history. Next from #3166 then fetched #2982 (the next history entry, not a random pick). |
| **R3** Zoom and Esc | PASS | On #961, `Z` opened the interlaced full screen and showed the GIF at 370×223 (28 146-byte ILBM, 16 pens, 39 s). `q`, space and Return did not leave it. Esc returned to the main screen with #961 and its caption intact. |
| **R4** One menu with exactly three items | PASS | Holding the right mouse button showed one menu, "Project", with exactly "Fetch ID...  [A]F", "Auto Refresh...  [A]A" and "Quit  [A]Q". Releasing the button off the items did nothing. |
| **R5** Fetch ID: OK | PASS | Amiga-F → `353` Return → #353 "Python" (PNG). Typing `2` and clicking **OK** → #2 (JPEG). |
| R5: Cancel / Esc | PASS | Typing `55` then Esc closed the window with no fetch. Typing `77` then clicking **Cancel** closed it with no fetch. The `fn_open` count stayed at 125 throughout. |
| R5: empty field | PASS | OK with an empty field showed the hint "Enter a number from 1 to 3306" and kept the window open. The upper bound comes from the live `info.0.json`. |
| R5 / **RF2**: #404 | PASS | Fetch ID `404` → status line "Comic #404 does not exist". The previous comic (#2982) stayed on screen. No random pick in this run was 404. |
| R5 / RF2: out-of-range ID | PASS | Fetch ID `999999` → "Comic #999999 does not exist", and the current comic stayed on screen. |
| **R6** Auto Refresh at the 10 s bound | PASS | [-] clamps at "Every 10 seconds". After Start, the indicator showed "Auto: 10s". The first fetch came 9.9 s after Start. Each later fetch began 10.1–10.2 s after the previous image finished (for example, image end 01:02:57.35 → next `info.0.json` 01:03:07.48). The interval counts from when the comic finishes loading, as the README documents. |
| R6 at the 600 s bound | PASS | Dragging the slider fully right showed "Every 600 seconds", and [+] at the top stayed at 600. After Start (01:09:42), the indicator showed "Auto: 600s". There were no fetches until the first tick at **01:19:45**, 603 s later; the extra seconds are the gap between the Start click and the timestamp being taken. |
| R6: Stop | PASS | Reopening the dialog (while running) showed "Status: running every 10 s". **Stop** cleared the "Auto:" indicator, and there were no further fetches. With the timer stopped, Stop is ghosted. |
| R6: Cancel | PASS | During the 600 s run, the dialog was reopened ("Status: running every 600 s"). The slider was nudged to 580, then **Cancel** was pressed. The indicator still read "Auto: 600s", and the tick still came at 600 s, so the interval was neither changed nor restarted. |
| **R7** PNG | PASS | #353 `python.png` → 264×149, 13 834-byte ILBM, drawn correctly. Most random picks were PNG. |
| R7 JPEG | PASS | #1 "Barrel - Part 1" `barrel_cropped_(1).jpg` → 556×149, 19 844 B. #2 `tree_cropped_(1).jpg` also rendered. |
| R7 GIF | PASS | #961 "Eternal Flame" `eternal_flame.gif` → 370×111 (main view) and 370×223 (Zoom). #2293 `rip_john_conway.gif` was a random pick and also rendered. An animated GIF shows its first frame. No GIF turned up in about 500 `info.0.json` files (#1–120 and #2900–3306) scanned with `grep '.gif'`. #961 and #1116 are known animated comics, and both are GIFs. |
| **R8** Uses fujinet-nio-lib → `fujinet-nio.device` → NIO firmware | PASS | The app needs the resident device: without it, it reports "NIO driver not loaded". With the device loaded, the firmware log shows each image opened with `openExtFlags=1`, translation type `04` and selector `w=624,h=150,colors=12,base=4,par=1:2` (PAL main view). It replies with `FORM…ILBM BMHD…`. |
| **R9** MekkoGX conventions | PASS | The top-level `Makefile` holds `PRODUCT = xkcd`, `PLATFORMS += amiga` and `SRC_DIRS = src src/%PLATFORM%`. `make amiga/r2r` writes `r2r/amiga/xkcd` and `r2r/amiga/xkcd.adf`. `.github/workflows/ci.yml` runs `make ${{ matrix.platform }}/r2r` (and `make test`). |
| **RF1** Comic with no static image (#1608) | PASS | #1608 "Hoverboard": the title bar and caption ("Return to the play area") appeared, and the image area said "No static image for this comic". There was no image fetch, no hang and no crash. |
| **RF3** Very large source images | PASS (render branch) | The `img` URLs of #657 and #1190 point to small images (740×467 and an ordinary `time.png`). Both rendered downscaled (474×149, 419×149). The largest `img` found was #1732 `earth_temperature_timeline.png` at **740×14 957**. It converted in a few seconds to a 14×146 sliver (1 856-byte ILBM) and rendered without errors. The firmware stayed up. |
| RF3: "Image too large for FujiNet to convert" message | NOT RUN | The POSIX firmware caps images by total pixel count (4096×4096 = 16.7 MP). #1732 is 11 MP, so it is accepted, and no xkcd `img` exceeds the POSIX cap. On ESP32 the cap is 1200×1200, and #1732 would take this path. The message mapping is covered by `test_netmap`, and the cap by the firmware's `ImageTranslator rejects images above pixel cap` test. |
| **RF4** JSON escapes: `’` renders as `'` | PASS | #2954 "Bracket Symbols" has an alt text that begins `’"‘”’"`. It rendered as `'"'"'" means "I edited this text on both my phone and my laptop before sending it"`, with ASCII quotes and no garbage bytes. |
| **RF5** "NIO driver not loaded" | PASS | Before the driver was loaded, `xkcd` opened its screen and showed "NIO driver not loaded (run fujinet-nio first)". Next kept the UI responsive. The Auto Refresh dialog opened, [-] reached 10 s, and Start showed "Auto: 10s" with the message still in place 24 s later. Amiga-Q quit cleanly to the CLI with no Guru. |
| RF5: network failure mid-image | PASS | While #657 was loading, the firmware process was paused (SIGSTOP) about 10 s into the transfer. The partly drawn image (top third) stayed on screen, and the status line changed from "Converting image..." to **"Image transfer interrupted"**. After the firmware resumed, the first Next reported "No reply from FujiNet". The second Next worked normally (#2618). |
| RF5: auto refresh keeps its schedule after a failure | PASS | With auto refresh at 10 s, the firmware was paused partway through an image (#1765). The partial image stayed, and the next tick still fired ("Fetching #2333..." shown while the firmware was still paused). After resuming, that request failed, and the following tick (#2744) loaded normally 10 s later. |
| **Disk icons / ReadMe on Workbench 1.3** | PASS | `r2r/amiga/xkcd.adf` was inserted in DF0 of the running WB 1.3. The disk appeared as "xkcd" with the floppy icon from `Disk.info`. Opening it showed the "ReadMe" (page) and "xkcd" (stick figure + "xkcd") icons. Double-clicking ReadMe opened it in `SYS:Utilities/More`, and the text was readable. Double-clicking the xkcd tool icon started the app from Workbench (#1744 loaded). Amiga-Q returned to Workbench cleanly. |
| **512 KB chip only:** main view | PASS | With no slow RAM, after booting from the HDF and loading the driver, `avail` showed 227 888 bytes chip free (largest block 185 952). The app opened, showed random #2762 with image and caption, and Next loaded #2304. A second run started with #2901. Free chip (MemHeader) while running was 72 448. |
| **512 KB chip only:** Zoom (R3) | FAIL (graceful) | `Z` showed "Not enough chip memory for Zoom" in the status line. There was no crash, and the main view stayed usable. Zoom needs four 640×512 planes (164 KB) plus 16 KB of slack, but only about 72 KB of chip RAM was free. This boot setup (HDF, two Amiberry directory shares and the resident NIO driver) uses about 290 KB of chip before the app starts. A floppy-booted A500 would leave more, but that was not tested. Zoom works whenever there is about 180 KB of chip RAM free besides the app, as on the main run (with slow RAM). Whether the spec's minimum machine has to support Zoom is a decision for the controller. |
| **512 KB chip only:** no leak across runs | PASS | After the first run and Quit, free chip went from 227 888 to 219 840. That is a one-time 8 KB (device/handler first-use allocations). A second run and Quit left exactly **219 840**, so nothing leaked per run. |
| Quit | PASS | Amiga-Q from the main window returned to the CLI with no Guru, both with and without the driver. |

## Observations (not failures)

- **Recovery after a firmware pause.** After the firmware was paused and
  resumed, the first request after recovery failed once ("No reply from
  FujiNet" or a failed `info.0.json` read). Requests worked again from the
  next try. This is transport resynchronisation after stale replies, and the
  app recovers without a restart.
- **Lost keystroke in Fetch ID.** One digit typed about 1 s after Amiga-F
  (while the window was still opening) was lost, so OK showed the
  empty-field hint. Typing again worked. With a 1.5 s pause after Amiga-F,
  every typed ID arrived.
- **Main-view widths.** Full-width images come back 623 px wide, not 624,
  because the converter rounds while preserving aspect.
- **Very tall comics.** #1732 is readable only as a sliver in the main view.
  Zoom gives it more height, but it remains a tall strip. This is expected
  for a 1:20 aspect image.

## Not run

- **NTSC layout:** the profile is PAL.
- **Zoom on a floppy-booted, chip-only A500:** see the "512 KB chip only"
  rows above.
- **"Image too large for FujiNet to convert" on screen:** see RF3 above.
