/* src/amiga/ui.c - main screen for Kickstart 1.3 (V33): custom hires screen, one borderless
   backdrop window, the xkcd menu and the Previous/Zoom/Next buttons.

   PAL layout (640 x 256); NTSC (640 x 200) takes 40 lines off the image box and one caption line:
     y   0- 10  screen title bar: "xkcd #353  Python  (2007-03-05)"
     y  12-161  image box, 624 x 150 (110 on NTSC)
     y 164-217  caption: alt text, 76 cols x 6 lines (5 on NTSC)
     y 224-238  [ < Previous ] [ Zoom ] [ Next > ]          "Auto: 60s"
     y 244-254  status line                                                         */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "selector.h"
#include "wrap.h"
#include "ui.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

#define SCR_W      640
#define DEPTH      4
#define IMG_TOP    12
#define LINE_H     9
#define CAP_COLS   76
#define CAP_X      16
#define CAP_MAX    6
#define BTN_H      15
#define STATUS_H   11
#define STATUS_COLS 78
#define AUTO_X     552
#define AUTO_COLS  10

#define KEY_Z      0x31
#define KEY_RIGHT  0x4E
#define KEY_LEFT   0x4F

enum { GID_PREV = 1, GID_ZOOM, GID_NEXT, NGAD = 3 };

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static struct Screen *scr;
static struct Window *win;
static struct TextFont *font;
static int pal, menu_on, prev_on;
static int img_h, cap_top, cap_lines, btn_top, status_top, scr_h;
static char scr_title[XKCD_TITLE_MAX + 40];

/* ---- Gadgets ----------------------------------------------------------- */

static struct Gadget gad[NGAD];
static struct IntuiText gad_text[NGAD];
static struct Border gad_light[NGAD], gad_dark[NGAD];
static WORD gad_light_xy[NGAD][6], gad_dark_xy[NGAD][6];
static const char *const gad_label[NGAD] = { "< Previous", "Zoom", "Next >" };
static const WORD gad_x[NGAD] = { 8, 112, 184 };
static const WORD gad_w[NGAD] = { 96, 64, 80 };

static void setup_gadgets(void)
{
    int i;
    for (i = 0; i < NGAD; i++) {
        WORD w = gad_w[i], h = BTN_H;
        WORD *l = gad_light_xy[i], *d = gad_dark_xy[i];
        l[0] = 0;     l[1] = h - 1; l[2] = 0;     l[3] = 0;     l[4] = w - 1; l[5] = 0;
        d[0] = 1;     d[1] = h - 1; d[2] = w - 1; d[3] = h - 1; d[4] = w - 1; d[5] = 1;
        memset(&gad_light[i], 0, sizeof gad_light[i]);
        gad_light[i].FrontPen = 2; gad_light[i].DrawMode = JAM1;
        gad_light[i].Count = 3; gad_light[i].XY = l; gad_light[i].NextBorder = &gad_dark[i];
        memset(&gad_dark[i], 0, sizeof gad_dark[i]);
        gad_dark[i].FrontPen = 1; gad_dark[i].DrawMode = JAM1;
        gad_dark[i].Count = 3; gad_dark[i].XY = d;

        memset(&gad_text[i], 0, sizeof gad_text[i]);
        gad_text[i].FrontPen = 1; gad_text[i].DrawMode = JAM1;
        gad_text[i].LeftEdge = (WORD)((w - 8 * (WORD)strlen(gad_label[i])) / 2);
        gad_text[i].TopEdge = (h - 8) / 2;
        gad_text[i].ITextFont = &topaz8;
        gad_text[i].IText = (UBYTE *)gad_label[i];

        memset(&gad[i], 0, sizeof gad[i]);
        gad[i].NextGadget = i + 1 < NGAD ? &gad[i + 1] : 0;
        gad[i].LeftEdge = gad_x[i]; gad[i].TopEdge = (WORD)btn_top;
        gad[i].Width = w; gad[i].Height = h;
        gad[i].Flags = GADGHCOMP;
        gad[i].Activation = RELVERIFY;
        gad[i].GadgetType = BOOLGADGET;
        gad[i].GadgetRender = (APTR)&gad_light[i];
        gad[i].GadgetText = &gad_text[i];
        gad[i].GadgetID = (UWORD)(GID_PREV + i);
    }
}

/* ---- Menu -------------------------------------------------------------- */

#define NITEM 3
static struct Menu menu;
static struct MenuItem items[NITEM];
static struct IntuiText item_text[NITEM];
static const char *const item_label[NITEM] = { "Fetch ID...", "Auto Refresh...", "Quit" };
static const char item_key[NITEM] = { 'F', 'A', 'Q' };

static void setup_menu(void)
{
    int i;
    WORD w = 2 + 15 * 8 + 16 + COMMWIDTH + 8;     /* text, gap, Amiga glyph, key letter */
    for (i = 0; i < NITEM; i++) {
        memset(&item_text[i], 0, sizeof item_text[i]);
        item_text[i].FrontPen = 1; item_text[i].BackPen = 2; item_text[i].DrawMode = JAM1;
        item_text[i].LeftEdge = 2; item_text[i].TopEdge = 1;
        item_text[i].ITextFont = &topaz8;
        item_text[i].IText = (UBYTE *)item_label[i];
        memset(&items[i], 0, sizeof items[i]);
        items[i].NextItem = i + 1 < NITEM ? &items[i + 1] : 0;
        items[i].TopEdge = (WORD)(i * 10);
        items[i].Width = w; items[i].Height = 10;
        items[i].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP | COMMSEQ;
        items[i].ItemFill = (APTR)&item_text[i];
        items[i].Command = item_key[i];
    }
    memset(&menu, 0, sizeof menu);
    menu.Width = 4 * 8 + 8; menu.Height = 10;      /* "xkcd" in topaz 8 plus margin */
    menu.Flags = MENUENABLED;
    menu.MenuName = (APTR)"xkcd";
    menu.FirstItem = &items[0];
}

/* ---- Drawing helpers --------------------------------------------------- */

static void clear(int x0, int y0, int x1, int y1)
{
    SetAPen(win->RPort, 0);
    RectFill(win->RPort, x0, y0, x1, y1);
}

/* Draws s at (x, top) in pen 1, clipped to cols characters. */
static void text_at(int x, int top, const char *s, int cols)
{
    struct RastPort *rp = win->RPort;
    int n = (int)strlen(s);
    if (n > cols) n = cols;
    SetAPen(rp, 1); SetBPen(rp, 0); SetDrMd(rp, JAM2);
    Move(rp, x, top + rp->TxBaseline);
    if (n) Text(rp, (STRPTR)s, n);
}

static void set_prev(int on)
{
    if (on == prev_on) return;
    prev_on = on;
    if (on) {
        /* V33 OnGadget leaves the ghosting dots; erase the button and redraw it. */
        OnGadget(&gad[0], win, 0);
        clear(gad[0].LeftEdge, gad[0].TopEdge, gad[0].LeftEdge + gad[0].Width - 1,
              gad[0].TopEdge + gad[0].Height - 1);
        RefreshGadgets(&gad[0], win, 0);
    } else {
        OffGadget(&gad[0], win, 0);
    }
}

/* ---- Public ------------------------------------------------------------ */

int ui_open(void)
{
    struct NewScreen ns;
    struct NewWindow nw;
    struct ViewPort *vp;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 33);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase) { ui_close(); return 0; }

    pal = (GfxBase->DisplayFlags & PAL) != 0;
    scr_h = pal ? 256 : 200;
    img_h = pal ? MAIN_IMG_H_PAL : MAIN_IMG_H_NTSC;
    cap_top = IMG_TOP + img_h + 2;
    cap_lines = pal ? CAP_MAX : CAP_MAX - 1;
    btn_top = pal ? 224 : 171;
    status_top = pal ? 244 : 188;

    memset(&ns, 0, sizeof ns);
    ns.Width = SCR_W; ns.Height = (WORD)scr_h; ns.Depth = DEPTH;
    ns.DetailPen = 1; ns.BlockPen = 2;
    ns.ViewModes = HIRES;
    ns.Type = CUSTOMSCREEN;
    ns.Font = &topaz8;
    ns.DefaultTitle = (UBYTE *)"xkcd";
    if (!(scr = OpenScreen(&ns))) { ui_close(); return 0; }
    vp = &scr->ViewPort;
    SetRGB4(vp, 0, 0xA, 0xA, 0xA);
    SetRGB4(vp, 1, 0x0, 0x0, 0x0);
    SetRGB4(vp, 2, 0xF, 0xF, 0xF);
    SetRGB4(vp, 3, 0x5, 0x8, 0xB);

    setup_gadgets();
    setup_menu();
    memset(&nw, 0, sizeof nw);
    nw.Width = SCR_W; nw.Height = (WORD)scr_h;
    nw.DetailPen = 1; nw.BlockPen = 2;
    nw.IDCMPFlags = MENUPICK | GADGETUP | RAWKEY;
    nw.Flags = BACKDROP | BORDERLESS | ACTIVATE | SMART_REFRESH | NOCAREREFRESH;
    nw.FirstGadget = &gad[0];
    nw.Screen = scr;
    nw.Type = CUSTOMSCREEN;
    if (!(win = OpenWindow(&nw))) { ui_close(); return 0; }
    SetMenuStrip(win, &menu);          /* VOID on V33: no return value to test */
    menu_on = 1;
    if ((font = OpenFont(&topaz8)) != 0) SetFont(win->RPort, font);
    SetWindowTitles(win, (UBYTE *)-1, (UBYTE *)"xkcd");
    prev_on = 1;
    ui_set_buttons(0, 0);
    return 1;
}

void ui_close(void)
{
    if (win) {
        if (menu_on) ClearMenuStrip(win);
        CloseWindow(win);
    }
    if (font) CloseFont(font);
    if (scr) CloseScreen(scr);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    win = 0; font = 0; scr = 0; menu_on = 0;
    GfxBase = 0; IntuitionBase = 0;
}

struct Screen *ui_screen(void) { return scr; }
struct Window *ui_window(void) { return win; }
int ui_is_pal(void) { return pal; }
unsigned long ui_sigmask(void) { return win ? 1UL << win->UserPort->mp_SigBit : 0; }

static int menu_action(UWORD code)
{
    while (code != MENUNULL) {
        struct MenuItem *it = ItemAddress(&menu, code);
        if (MENUNUM(code) == 0) {
            switch (ITEMNUM(code)) {
            case 0: return UI_FETCH_ID;
            case 1: return UI_AUTO;
            case 2: return UI_QUIT;
            }
        }
        if (!it) break;
        code = it->NextSelect;
    }
    return UI_NONE;
}

int ui_poll(void)
{
    struct IntuiMessage *m;
    int act = UI_NONE;

    if (!win) return UI_NONE;
    while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        int a = UI_NONE;
        ReplyMsg((struct Message *)m);
        if (cls == MENUPICK) a = menu_action(code);
        else if (cls == GADGETUP) {
            switch (g->GadgetID) {
            case GID_PREV: a = UI_PREV; break;
            case GID_ZOOM: a = UI_ZOOM; break;
            case GID_NEXT: a = UI_NEXT; break;
            }
        } else if (cls == RAWKEY && !(code & IECODE_UP_PREFIX)) {
            if (code == KEY_LEFT) a = UI_PREV;
            else if (code == KEY_RIGHT) a = UI_NEXT;
            else if (code == KEY_Z) a = UI_ZOOM;
        }
        if (act == UI_NONE) act = a;
    }
    return act;
}

void ui_show_comic(const xkcd_comic_t *c)
{
    static char lines[CAP_MAX + 1][81];
    int n, i;

    snprintf(scr_title, sizeof scr_title, "xkcd #%ld  %s  (%s)", c->num, c->title, c->date);
    SetWindowTitles(win, (UBYTE *)-1, (UBYTE *)scr_title);
    clear(0, IMG_TOP, SCR_W - 1, IMG_TOP + img_h - 1);
    clear(0, cap_top, SCR_W - 1, cap_top + cap_lines * LINE_H - 1);
    n = wrap_text(c->alt, CAP_COLS, lines, cap_lines + 1);
    if (n > cap_lines) {            /* more text than fits: mark the cut */
        char *last = lines[cap_lines - 1];
        size_t len = strlen(last);
        if (len > CAP_COLS - 3) len = CAP_COLS - 3;
        strcpy(last + len, "...");
        n = cap_lines;
    }
    for (i = 0; i < n; i++) text_at(CAP_X, cap_top + i * LINE_H, lines[i], CAP_COLS);
}

void ui_status(const char *msg)
{
    if (!win) return;
    clear(0, status_top, SCR_W - 1, status_top + STATUS_H - 1);
    text_at(8, status_top + 2, msg, STATUS_COLS);
}

void ui_set_buttons(int can_prev, int auto_on)
{
    char buf[AUTO_COLS + 1];
    set_prev(can_prev != 0);
    if (auto_on) snprintf(buf, sizeof buf, "Auto: %ds", auto_on);
    else buf[0] = 0;
    clear(AUTO_X, btn_top, AUTO_X + AUTO_COLS * 8 - 1, btn_top + BTN_H - 1);
    text_at(AUTO_X, btn_top + (BTN_H - 8) / 2, buf, AUTO_COLS);
}

int ui_place_image(const struct BitMap *bm, int depth, int box_w, int top, int box_h,
                   const ilbm_info_t *info, unsigned char **planes, unsigned char *np,
                   unsigned short *bpr, unsigned short *rows)
{
    int w = info->w, h = info->h, row_bytes = ((int)info->w + 15) / 16 * 2, x_byte, y0, i;

    /* Wider than the bitmap would wrap into the next row: refuse rather than corrupt. */
    if (!bm || w == 0 || w > bm->BytesPerRow * 8 || info->planes > depth || depth > bm->Depth) return 0;
    if (box_w > bm->BytesPerRow * 8) box_w = bm->BytesPerRow * 8;
    if (top < 0 || box_h <= 0 || top + box_h > bm->Rows) return 0;
    x_byte = w <= box_w ? (8 + (box_w - w) / 2) / 16 * 2 : 0;
    if (x_byte + row_bytes > bm->BytesPerRow) x_byte = bm->BytesPerRow - row_bytes;   /* never past the row */
    y0 = top + (h < box_h ? (box_h - h) / 2 : 0);
    for (i = 0; i < depth; i++)
        planes[i] = bm->Planes[i] + (long)y0 * bm->BytesPerRow + x_byte;
    *np = (unsigned char)depth;
    *bpr = bm->BytesPerRow;
    *rows = (unsigned short)(box_h - (y0 - top));
    return 1;
}

int ui_image_header_cb(void *ctx, const ilbm_info_t *info, unsigned char **planes, unsigned char *np,
                       unsigned short *bpr, unsigned short *rows)
{
    int i;

    (void)ctx;
    if (!scr || !ui_place_image(&scr->BitMap, DEPTH, MAIN_IMG_W, IMG_TOP, img_h, info, planes, np, bpr, rows))
        return 0;
    for (i = 4; i < 16 && i < info->ncolors; i++)
        SetRGB4(&scr->ViewPort, i, info->cmap[i][0] >> 4, info->cmap[i][1] >> 4, info->cmap[i][2] >> 4);
    return 1;
}

void ui_image_message(const char *msg)
{
    int n = (int)strlen(msg);
    if (n > SCR_W / 8) n = SCR_W / 8;
    clear(0, IMG_TOP, SCR_W - 1, IMG_TOP + img_h - 1);
    text_at((SCR_W - n * 8) / 2, IMG_TOP + (img_h - 8) / 2, msg, n);
}
