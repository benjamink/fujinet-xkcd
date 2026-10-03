/* src/amiga/zoom.c - full-screen interlaced view for Kickstart 1.3 (V33).

   Opens a hires interlaced 640 x 512 (PAL) / 640 x 400 (NTSC), 2-bitplane custom screen with no
   title bar and one borderless backdrop window. The comic arrives as a grey-4 ILBM at selector_zoom()
   size and is kept compressed in memory; only the visible rows are decoded into the screen bitmap.
   Tall comics scroll (keys, left-button drag) with a 4-pixel position bar at the right edge. Esc
   closes the view and restores the main screen and window. An auto-refresh tick that fires meanwhile
   is left pending for main's loop. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "selector.h"
#include "ilbm_index.h"
#include "zoomscroll.h"
#include "net.h"
#include "ui.h"
#include "zoom.h"

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
#define TEXT_PEN    0                   /* black in the grey ramp */
#define BG_PEN      3                   /* paper: the lightest grey (white) */
#define MSG_COLS    (ZOOM_W / 8)
/* Chip RAM beyond the bitplanes that OpenScreen/OpenWindow need (copper lists for both interlace
   fields, layers, rastport): a generous estimate, only used to refuse early. */
#define ZOOM_CHIP_SLACK 16384UL

static struct TextAttr zoom_topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

/* Centred one-line message in TEXT_PEN on BG_PEN; the full-width text band is cleared first so a
   shorter message never leaves part of an earlier one behind. */
static void zoom_text(struct RastPort *rp, int scr_h, const char *msg)
{
    int n = (int)strlen(msg), top = (scr_h - 8) / 2;
    if (n > MSG_COLS) n = MSG_COLS;
    SetAPen(rp, BG_PEN);
    RectFill(rp, 0, top - 2, ZOOM_W - 1, top + 9);
    SetAPen(rp, TEXT_PEN); SetBPen(rp, BG_PEN); SetDrMd(rp, JAM2);
    Move(rp, (ZOOM_W - n * 8) / 2, top + rp->TxBaseline);
    if (n) Text(rp, (STRPTR)msg, n);
}

static void zoom_clear(struct RastPort *rp, int scr_h)
{
    SetAPen(rp, BG_PEN);
    RectFill(rp, 0, 0, ZOOM_W - 1, scr_h - 1);
}

/* Position bar: track in pen 1, thumb in pen 3. */
static void zoom_bar(struct RastPort *rp, const zs_t *zs, int h)
{
    int ty, th;

    zs_thumb(zs, h, &ty, &th);
    SetAPen(rp, 1);
    RectFill(rp, ZOOM_W - BAR_W, 0, ZOOM_W - 1, h - 1);
    SetAPen(rp, BG_PEN);
    RectFill(rp, ZOOM_W - BAR_W, ty, ZOOM_W - 1, ty + th - 1);
}

void zoom_show(const xkcd_comic_t *c)
{
    struct NewScreen ns;
    struct NewWindow nw;
    struct Screen *scr;
    struct Window *win;
    struct RastPort *rp;
    struct BitMap *bm;
    struct IntuiMessage *m;
    ilbm_doc_t doc;
    zs_t zs;
    unsigned char *buf = 0;
    unsigned long len = 0, alloc = 0;
    unsigned long *row_off = 0;
    unsigned char *planes[ZOOM_DEPTH];
    char sel[64], msg[32];
    unsigned char e;
    const char *err = 0;
    int h = ui_is_pal() ? 512 : 400;
    int done = 0, have = 0, dragging = 0, last_y = 0;
    int i, r, x_byte, y0 = 0, visible, bpr;
    ULONG plane = (ULONG)(ZOOM_W / 8) * (ULONG)h;

    /* V33 OpenScreen leaks a little memory each time it fails for lack of chip RAM (seen in Amiberry:
       128 bytes per failed attempt), so refuse up front when the planes clearly cannot fit. */
    if (AvailMem(MEMF_CHIP | MEMF_LARGEST) < plane ||
        AvailMem(MEMF_CHIP) < ZOOM_DEPTH * plane + ZOOM_CHIP_SLACK) {
        ui_status("Not enough chip memory for Zoom");
        return;
    }

    memset(&ns, 0, sizeof ns);
    ns.Width = ZOOM_W;
    ns.Height = (WORD)h;
    ns.Depth = ZOOM_DEPTH;
    ns.DetailPen = TEXT_PEN;
    ns.BlockPen = BG_PEN;
    ns.ViewModes = HIRES | LACE;
    ns.Type = CUSTOMSCREEN | SCREENQUIET;      /* no title bar rendering */
    ns.Font = &zoom_topaz8;
    if (!(scr = OpenScreen(&ns))) {
        ui_status("Not enough chip memory for Zoom");
        return;
    }
    ShowTitle(scr, FALSE);                      /* title bar behind the backdrop window */
    SetRGB4(&scr->ViewPort, BG_PEN, 15, 15, 15);  /* until the CMAP arrives: black on white */
    SetRGB4(&scr->ViewPort, TEXT_PEN, 0, 0, 0);

    memset(&nw, 0, sizeof nw);
    nw.Width = ZOOM_W;
    nw.Height = (WORD)h;
    nw.DetailPen = TEXT_PEN;
    nw.BlockPen = BG_PEN;
    nw.IDCMPFlags = RAWKEY | MOUSEBUTTONS | MOUSEMOVE;
    nw.Flags = BACKDROP | BORDERLESS | ACTIVATE | RMBTRAP | REPORTMOUSE | SMART_REFRESH | NOCAREREFRESH;
    nw.Screen = scr;
    nw.Type = CUSTOMSCREEN;
    if (!(win = OpenWindow(&nw))) {
        CloseScreen(scr);
        ScreenToFront(ui_screen());
        ui_status("Not enough chip memory for Zoom");
        return;
    }
    rp = win->RPort;
    bm = &scr->BitMap;
    bpr = bm->BytesPerRow;
    zs_init(&zs, 0, h);                 /* always defined, even when no image arrives */

    zoom_clear(rp, h);
    snprintf(msg, sizeof msg, "Loading #%ld...", c->num);
    zoom_text(rp, h, msg);

    selector_zoom(sel, sizeof sel, ui_is_pal());
    e = net_fetch_image_buffer(c->img, sel, &buf, &len, &alloc);
    if (!buf) {
        err = (e == NET_ERR_NOMEM) ? "Not enough memory for Zoom" : net_error(e, c->num);
    } else {
        r = ilbm_doc_parse(&doc, buf, len, 0, 0);
        if (r == ILBMX_ERROR && e == NET_ERR_PARTIAL) {
            err = "Image transfer interrupted";
        } else if (r == ILBMX_ERROR || doc.info.planes > ZOOM_DEPTH || doc.info.w > ZOOM_W) {
            err = net_error(NET_ERR_CONVERT, c->num);
        } else if (!(row_off = (unsigned long *)AllocMem(sizeof(unsigned long) * doc.info.h, MEMF_ANY))) {
            err = "Not enough memory for Zoom";
        } else {
            r = ilbm_doc_parse(&doc, buf, len, row_off, doc.info.h);
            if (r == ILBMX_ERROR) {
                err = (e == NET_ERR_PARTIAL) ? "Image transfer interrupted" : net_error(NET_ERR_CONVERT, c->num);
            } else {
                if (r == ILBMX_PARTIAL || e == NET_ERR_PARTIAL) err = "Image transfer interrupted";
                have = 1;
            }
        }
    }

    if (have) {
        int img_h = doc.rows;
        int w = doc.info.w;

        for (i = 0; i < 4 && i < doc.info.ncolors; i++)
            SetRGB4(&scr->ViewPort, i, doc.info.cmap[i][0] >> 4, doc.info.cmap[i][1] >> 4,
                    doc.info.cmap[i][2] >> 4);
        zs_init(&zs, img_h, h);
        x_byte = ((ZOOM_W - w) / 2 / 16) * 2;
        if (x_byte + (int)doc.src_bpr > bpr) x_byte = bpr - (int)doc.src_bpr;
        if (x_byte < 0) x_byte = 0;
        if (!zs_scrollable(&zs)) y0 = (h - img_h) / 2;
        for (i = 0; i < ZOOM_DEPTH; i++) planes[i] = bm->Planes[i] + x_byte;

        zoom_clear(rp, h);
        visible = img_h < h ? img_h : h;
        WaitBlit();                     /* blitter clear must finish before the CPU decodes */
        ilbm_doc_decode_rows(&doc, (unsigned short)zs.top, (unsigned short)visible, planes, ZOOM_DEPTH,
                             (unsigned short)bpr, (unsigned short)y0);
        if (zs_scrollable(&zs)) zoom_bar(rp, &zs, h);
    }
    if (err) zoom_text(rp, h, err);

    SetBPen(rp, BG_PEN);                /* ScrollRaster clears the vacated band to BG_PEN */
    while (!done) {
        int delta = 0;

        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD qual = m->Qualifier;
            int my = m->MouseY;
            int shift;

            ReplyMsg((struct Message *)m);
            shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
            if (cls == RAWKEY && !(code & IECODE_UP_PREFIX)) {
                switch (code) {
                case KEY_ESC:
                    done = 1;
                    break;
                case KEY_UP:
                    delta -= shift ? zs_page(&zs) : ZS_LINE;
                    break;
                case KEY_DOWN:
                    delta += shift ? zs_page(&zs) : ZS_LINE;
                    break;
                case KEY_SPACE:
                    delta += zs_page(&zs);
                    break;
                case KEY_BS:
                    delta -= zs_page(&zs);
                    break;
                case KEY_T:
                    delta -= zs.top;
                    break;
                case KEY_B:
                    delta += zs_max_top(&zs) - zs.top;
                    break;
                }
            } else if (cls == MOUSEBUTTONS) {
                if (code == SELECTDOWN) {
                    dragging = 1;
                    last_y = my;
                } else if (code == SELECTUP) {
                    dragging = 0;
                }
            } else if (cls == MOUSEMOVE && dragging) {
                delta += last_y - my;
                last_y = my;
            }
        }
        if (have && !done && delta) {
            int applied = zs_scroll(&zs, delta);

            if (applied) {
                int f, cnt;

                zs_exposed(applied, h, &f, &cnt);
                if (cnt < h) {
                    ScrollRaster(rp, 0, applied, 0, 0, ZOOM_W - BAR_W - 1, h - 1);
                } else {
                    SetAPen(rp, BG_PEN);
                    RectFill(rp, 0, 0, ZOOM_W - BAR_W - 1, h - 1);
                }
                WaitBlit();             /* ScrollRaster/RectFill blits must finish first */
                ilbm_doc_decode_rows(&doc, (unsigned short)(zs.top + f), (unsigned short)cnt, planes,
                                     ZOOM_DEPTH, (unsigned short)bpr, (unsigned short)f);
                zoom_bar(rp, &zs, h);
            }
        }
        if (!done) Wait(1UL << win->UserPort->mp_SigBit);
    }

    CloseWindow(win);          /* every message taken from the port was replied above */
    CloseScreen(scr);
    if (row_off) FreeMem(row_off, sizeof(unsigned long) * doc.info.h);
    if (buf) FreeMem(buf, alloc);
    ScreenToFront(ui_screen());
    ActivateWindow(ui_window());
}
