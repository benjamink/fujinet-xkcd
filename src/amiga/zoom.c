/* src/amiga/zoom.c - full-screen interlaced view for Kickstart 1.3 (V33).

   Opens a hires interlaced 640 x 512 (PAL) / 640 x 400 (NTSC), 4-plane custom screen with no title
   bar and one borderless backdrop window, streams the comic at selector_zoom() size into it with
   pens 0-15 from the image's CMAP, then waits for Esc (key down) only. Closing restores the main
   screen and window. An auto-refresh tick that fires meanwhile is left pending for main's loop. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "selector.h"
#include "net.h"
#include "ui.h"
#include "zoom.h"

#define ZOOM_W      640
#define ZOOM_DEPTH  4
#define KEY_ESC     0x45
#define TEXT_PEN    15
#define MSG_COLS    (ZOOM_W / 8)
/* Chip RAM beyond the bitplanes that OpenScreen/OpenWindow need (copper lists for both interlace
   fields, layers, rastport): a generous estimate, only used to refuse early. */
#define ZOOM_CHIP_SLACK 16384UL

typedef struct {
    struct Screen *scr;
    struct Window *win;
    int h;
} zoom_ctx_t;

static struct TextAttr zoom_topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

/* Centred one-line message in pen 15 on pen 0; the full-width text band is cleared first so a
   shorter message never leaves part of an earlier one behind. */
static void zoom_text(struct RastPort *rp, int scr_h, const char *msg)
{
    int n = (int)strlen(msg), top = (scr_h - 8) / 2;
    if (n > MSG_COLS) n = MSG_COLS;
    SetAPen(rp, 0);
    RectFill(rp, 0, top - 2, ZOOM_W - 1, top + 9);
    SetAPen(rp, TEXT_PEN); SetBPen(rp, 0); SetDrMd(rp, JAM2);
    Move(rp, (ZOOM_W - n * 8) / 2, top + rp->TxBaseline);
    if (n) Text(rp, (STRPTR)msg, n);
}

static void zoom_clear(struct RastPort *rp, int scr_h)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, ZOOM_W - 1, scr_h - 1);
}

/* net_header_cb: whole screen is the box; pens 0..15 from the CMAP (bounded by ncolors). */
static int zoom_header_cb(void *ctx, const ilbm_info_t *info, unsigned char **planes, unsigned char *np,
                          unsigned short *bpr, unsigned short *rows)
{
    zoom_ctx_t *z = (zoom_ctx_t *)ctx;
    int i;

    /* Drop "Loading #N..." first: the decoder writes rows straight into the bitmap. */
    zoom_clear(z->win->RPort, z->h);
    if (!ui_place_image(&z->scr->BitMap, ZOOM_DEPTH, ZOOM_W, 0, z->h, info, planes, np, bpr, rows))
        return 0;
    for (i = 0; i < 16 && i < info->ncolors; i++)
        SetRGB4(&z->scr->ViewPort, i, info->cmap[i][0] >> 4, info->cmap[i][1] >> 4, info->cmap[i][2] >> 4);
    return 1;
}

void zoom_show(const xkcd_comic_t *c)
{
    struct NewScreen ns;
    struct NewWindow nw;
    struct Screen *scr;
    struct Window *win;
    struct IntuiMessage *m;
    zoom_ctx_t z;
    char sel[64], msg[32];
    unsigned char e;
    int h = ui_is_pal() ? 512 : 400, done = 0;
    ULONG plane = (ULONG)(ZOOM_W / 8) * (ULONG)h;

    /* V33 OpenScreen leaks a little memory each time it fails for lack of chip RAM (seen in Amiberry:
       128 bytes per failed attempt), so refuse up front when the planes clearly cannot fit. */
    if (AvailMem(MEMF_CHIP | MEMF_LARGEST) < plane ||
        AvailMem(MEMF_CHIP) < ZOOM_DEPTH * plane + ZOOM_CHIP_SLACK) {
        ui_status("Not enough chip memory for Zoom");
        return;
    }

    memset(&ns, 0, sizeof ns);
    ns.Width = ZOOM_W; ns.Height = (WORD)h; ns.Depth = ZOOM_DEPTH;
    ns.DetailPen = 0; ns.BlockPen = TEXT_PEN;
    ns.ViewModes = HIRES | LACE;
    ns.Type = CUSTOMSCREEN | SCREENQUIET;      /* no title bar rendering */
    ns.Font = &zoom_topaz8;
    if (!(scr = OpenScreen(&ns))) { ui_status("Not enough chip memory for Zoom"); return; }
    ShowTitle(scr, FALSE);                      /* title bar behind the backdrop window */
    SetRGB4(&scr->ViewPort, 0, 0xF, 0xF, 0xF);  /* until the CMAP arrives: black on white */
    SetRGB4(&scr->ViewPort, TEXT_PEN, 0x0, 0x0, 0x0);

    memset(&nw, 0, sizeof nw);
    nw.Width = ZOOM_W; nw.Height = (WORD)h;
    nw.DetailPen = 0; nw.BlockPen = TEXT_PEN;
    nw.IDCMPFlags = RAWKEY | MOUSEBUTTONS;
    nw.Flags = BACKDROP | BORDERLESS | ACTIVATE | RMBTRAP | SMART_REFRESH | NOCAREREFRESH;
    nw.Screen = scr;
    nw.Type = CUSTOMSCREEN;
    if (!(win = OpenWindow(&nw))) {
        CloseScreen(scr);
        ScreenToFront(ui_screen());
        ui_status("Not enough chip memory for Zoom");
        return;
    }

    zoom_clear(win->RPort, h);
    snprintf(msg, sizeof msg, "Loading #%ld...", c->num);
    zoom_text(win->RPort, h, msg);

    z.scr = scr; z.win = win; z.h = h;
    selector_zoom(sel, sizeof sel, ui_is_pal());
    e = net_fetch_image(c->img, sel, zoom_header_cb, &z);
    if (e != NET_OK) zoom_text(win->RPort, h, net_error(e, c->num));

    while (!done) {
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            ReplyMsg((struct Message *)m);
            if (cls == RAWKEY && code == KEY_ESC && !(code & IECODE_UP_PREFIX)) done = 1;
        }
        if (!done) Wait(1UL << win->UserPort->mp_SigBit);
    }

    CloseWindow(win);          /* every message taken from the port was replied above */
    CloseScreen(scr);
    ScreenToFront(ui_screen());
    ActivateWindow(ui_window());
}
