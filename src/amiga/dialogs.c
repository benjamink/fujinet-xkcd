/* src/amiga/dialogs.c - the Fetch ID and Auto Refresh windows (Kickstart 1.3, V33 only).

   Each dialog opens a small window on the main screen and runs its own modal loop on that
   window's port. Every message is replied; the window is closed on every path. The main
   window's IDCMP is not drained meanwhile (its messages wait in its own port), and the
   auto-refresh timer is not serviced: main() catches a fired tick afterwards through
   timer_poll(), because the timer's signal bit stays set until main() Waits on it. The
   countdown runs on the system clock, so it loses no time while a dialog is open.       */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <exec/interrupts.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "ui.h"
#include "dialogs.h"

#define KEY_ESC     0x45
#define KEY_RETURN  0x44
#define KEY_ENTER   0x43            /* numeric keypad Enter */
#define BTN_H       14
#define HINT_PEN    3

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

/* ---- Shared helpers ---------------------------------------------------- */

typedef struct {
    struct Gadget g;
    struct IntuiText t;
    struct Border light, dark;
    WORD lxy[6], dxy[6];
} button_t;

/* A boolean push button with the same two-tone frame as the main window's buttons. */
static void button_init(button_t *b, WORD x, WORD y, WORD w, UWORD id, const char *label,
                        struct Gadget *next)
{
    WORD h = BTN_H;
    memset(b, 0, sizeof *b);
    b->lxy[0] = 0; b->lxy[1] = h - 1; b->lxy[2] = 0;     b->lxy[3] = 0;     b->lxy[4] = w - 1; b->lxy[5] = 0;
    b->dxy[0] = 1; b->dxy[1] = h - 1; b->dxy[2] = w - 1; b->dxy[3] = h - 1; b->dxy[4] = w - 1; b->dxy[5] = 1;
    b->light.FrontPen = 2; b->light.DrawMode = JAM1; b->light.Count = 3; b->light.XY = b->lxy;
    b->light.NextBorder = &b->dark;
    b->dark.FrontPen = 1; b->dark.DrawMode = JAM1; b->dark.Count = 3; b->dark.XY = b->dxy;
    b->t.FrontPen = 1; b->t.DrawMode = JAM1;
    b->t.LeftEdge = (WORD)((w - 8 * (WORD)strlen(label)) / 2);
    b->t.TopEdge = (h - 8) / 2;
    b->t.ITextFont = &topaz8;
    b->t.IText = (UBYTE *)label;
    b->g.NextGadget = next;
    b->g.LeftEdge = x; b->g.TopEdge = y; b->g.Width = w; b->g.Height = h;
    b->g.Flags = GADGHCOMP;
    b->g.Activation = RELVERIFY;
    b->g.GadgetType = BOOLGADGET;
    b->g.GadgetRender = (APTR)&b->light;
    b->g.GadgetText = &b->t;
    b->g.GadgetID = id;
}

/* Height of the title bar a dragable, titled window gets on the main screen (V33 rule). */
static WORD title_h(struct Screen *s)
{
    return (WORD)(s->WBorTop + (s->Font ? s->Font->ta_YSize : 8) + 1);
}

/* Opens a centred dialog window on the main screen; NULL (and a status message) on failure. */
static struct Window *dialog_open(WORD w, WORD h, const char *title, ULONG idcmp, struct Gadget *first)
{
    struct Screen *s = ui_screen();
    struct NewWindow nw;
    struct Window *dw;

    if (!s) return 0;
    memset(&nw, 0, sizeof nw);
    nw.Width = w; nw.Height = h;
    nw.LeftEdge = (WORD)((s->Width - w) / 2);
    nw.TopEdge = (WORD)((s->Height - h) / 2);
    nw.DetailPen = (UBYTE)-1; nw.BlockPen = (UBYTE)-1;
    nw.IDCMPFlags = idcmp;
    nw.Flags = ACTIVATE | WINDOWDRAG | WINDOWDEPTH | WINDOWCLOSE | SMART_REFRESH | NOCAREREFRESH;
    nw.FirstGadget = first;
    nw.Title = (UBYTE *)title;
    nw.Screen = s;
    nw.Type = CUSTOMSCREEN;
    if (!(dw = OpenWindow(&nw))) { ui_status("Not enough memory for window"); return 0; }
    if (ui_window()) SetWindowTitles(dw, (UBYTE *)-1, ui_window()->ScreenTitle);   /* keep "xkcd #N ..." */
    return dw;
}

/* Closes the dialog after replying anything still queued, and gives the keyboard back. */
static void dialog_close(struct Window *dw)
{
    struct IntuiMessage *m;
    while ((m = (struct IntuiMessage *)GetMsg(dw->UserPort)) != 0) ReplyMsg((struct Message *)m);
    CloseWindow(dw);
    if (ui_window()) ActivateWindow(ui_window());
}

/* ---- Esc while a string gadget is active --------------------------------
   On V33 an active string gadget swallows every key, Esc included, and Intuition sends no
   RAWKEY. So while the Fetch ID window is up, a small input.device handler (priority 51, ahead
   of Intuition's 50) takes an Esc key-down meant for that window, sets esc_hit and signals the
   window's port. If input.device cannot be opened, Esc still works whenever the field is not
   active; Cancel and the close gadget always work.                                      */

static struct Window *volatile esc_win;
static struct Task *esc_task;
static volatile int esc_hit;
static struct MsgPort *in_port;
static struct IOStdReq *in_io;
static struct Interrupt in_int;
static int in_open, in_added;

static struct InputEvent *esc_handler(register struct InputEvent *ev __asm("a0"),
                                      register APTR data __asm("a1"))
{
    struct InputEvent *e;
    struct Window *w = esc_win;
    (void)data;
    if (!w || IntuitionBase->ActiveWindow != w) return ev;
    for (e = ev; e; e = e->ie_NextEvent)
        if (e->ie_Class == IECLASS_RAWKEY && e->ie_Code == KEY_ESC) {
            e->ie_Class = IECLASS_NULL;         /* Intuition never sees it */
            esc_hit = 1;
            Signal(esc_task, 1UL << w->UserPort->mp_SigBit);
        }
    return ev;
}

static void esc_watch_stop(void)
{
    if (in_added) {
        in_io->io_Command = IND_REMHANDLER;
        in_io->io_Data = (APTR)&in_int;
        DoIO((struct IORequest *)in_io);
    }
    if (in_open) CloseDevice((struct IORequest *)in_io);
    if (in_io) DeleteExtIO((struct IORequest *)in_io);
    if (in_port) DeletePort(in_port);
    in_added = in_open = 0; in_io = 0; in_port = 0;
    esc_win = 0;
}

static void esc_watch_start(struct Window *dw)
{
    esc_hit = 0;
    esc_task = FindTask(0);
    esc_win = dw;
    if (!(in_port = CreatePort(0, 0)) ||
        !(in_io = (struct IOStdReq *)CreateExtIO(in_port, sizeof *in_io)) ||
        OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)in_io, 0) != 0) {
        esc_watch_stop();
        return;
    }
    in_open = 1;
    memset(&in_int, 0, sizeof in_int);
    in_int.is_Node.ln_Type = NT_INTERRUPT;
    in_int.is_Node.ln_Pri = 51;
    in_int.is_Node.ln_Name = (char *)"xkcd Fetch ID Esc";
    in_int.is_Code = (void (*)())(void *)esc_handler;     /* via void *: register-arg ABI */
    in_io->io_Command = IND_ADDHANDLER;
    in_io->io_Data = (APTR)&in_int;
    if (DoIO((struct IORequest *)in_io) == 0) in_added = 1;
}

/* Clears a one-line text band (pen 0) and writes s in the given pen. */
static void line_at(struct Window *dw, int x, int top, int x1, const char *s, int pen)
{
    struct RastPort *rp = dw->RPort;
    int n = (int)strlen(s), cols = (x1 - x + 1) / 8;
    if (n > cols) n = cols;
    SetAPen(rp, 0);
    RectFill(rp, x, top, x1, top + 8);
    SetAPen(rp, pen); SetBPen(rp, 0); SetDrMd(rp, JAM2);
    Move(rp, x, top + rp->TxBaseline);
    if (n) Text(rp, (STRPTR)s, n);
}

/* ---- Fetch ID ---------------------------------------------------------- */

enum { FID_NUM = 1, FID_OK, FID_CANCEL };
#define FID_W 260
#define FID_H 70

/* LONGINT gadget text -> value. The buffer is read directly rather than StringInfo.LongInt so
   the value is current even when OK is clicked while the gadget is still active. */
static long parse_long(const UBYTE *b)
{
    long v = 0;
    int neg = 0;
    while (*b == ' ') b++;
    if (*b == '-') { neg = 1; b++; }
    while (*b >= '0' && *b <= '9') {
        if (v > 99999999L) break;
        v = v * 10 + (*b++ - '0');
    }
    return neg ? -v : v;
}

int dlg_fetch_id(long *id, long latest)
{
    static UBYTE buf[7], undo[7];
    static struct StringInfo si;
    static struct Gadget num;
    static struct Border box;
    static WORD box_xy[10];
    static button_t ok, cancel;
    static char hint[40];
    struct Window *dw;
    WORD t = ui_screen() ? title_h(ui_screen()) : 11;
    WORD row = t + 6, hint_top = t + 22, btn_top = FID_H - BTN_H - 6;
    int result = -1;

    memset(buf, 0, sizeof buf); memset(undo, 0, sizeof undo);
    memset(&si, 0, sizeof si);
    si.Buffer = buf; si.UndoBuffer = undo; si.MaxChars = sizeof buf;

    memset(&box, 0, sizeof box);
    box_xy[0] = -3; box_xy[1] = -2;  box_xy[2] = 66; box_xy[3] = -2;
    box_xy[4] = 66; box_xy[5] = 9;   box_xy[6] = -3; box_xy[7] = 9;
    box_xy[8] = -3; box_xy[9] = -2;
    box.FrontPen = 1; box.DrawMode = JAM1; box.Count = 5; box.XY = box_xy;

    button_init(&cancel, FID_W - 20 - 80, btn_top, 80, FID_CANCEL, "Cancel", 0);
    button_init(&ok, 20, btn_top, 80, FID_OK, "OK", &cancel.g);
    memset(&num, 0, sizeof num);
    num.NextGadget = &ok.g;
    num.LeftEdge = 140; num.TopEdge = row; num.Width = 64; num.Height = 8;
    num.Flags = GADGHCOMP;
    num.Activation = RELVERIFY | LONGINT;
    num.GadgetType = STRGADGET;
    num.GadgetRender = (APTR)&box;
    num.SpecialInfo = (APTR)&si;
    num.GadgetID = FID_NUM;

    if (latest > 0) snprintf(hint, sizeof hint, "Enter a number from 1 to %ld", latest);
    else snprintf(hint, sizeof hint, "Enter a positive number");

    if (!(dw = dialog_open(FID_W, FID_H, "Fetch xkcd by ID", GADGETUP | RAWKEY | CLOSEWINDOW, &num)))
        return 0;
    line_at(dw, 20, row, 20 + 14 * 8 - 1, "Comic number:", 1);
    esc_watch_start(dw);
    ActivateGadget(&num, dw, 0);        /* return value not relied on (V33) */

    while (result < 0) {
        struct IntuiMessage *m;
        Wait(1UL << dw->UserPort->mp_SigBit);
        while ((m = (struct IntuiMessage *)GetMsg(dw->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD gid = cls == GADGETUP ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            int accept = 0;
            ReplyMsg((struct Message *)m);
            if (result >= 0) continue;          /* already decided: just drain */
            if (cls == CLOSEWINDOW || gid == FID_CANCEL) result = 0;
            else if (gid == FID_OK || gid == FID_NUM) accept = 1;   /* OK, or Return in the field */
            else if (cls == RAWKEY && !(code & IECODE_UP_PREFIX)) {
                if (code == KEY_ESC) result = 0;
                else if (code == KEY_RETURN || code == KEY_ENTER) accept = 1;
            }
            if (accept) {
                long v = parse_long(buf);
                if (v > 0) { *id = v; result = 1; }
                else {
                    line_at(dw, 12, hint_top, FID_W - 12, hint, HINT_PEN);
                    ActivateGadget(&num, dw, 0);
                }
            }
        }
        if (result < 0 && esc_hit) result = 0;      /* Esc taken by the input handler */
    }
    esc_watch_stop();
    dialog_close(dw);
    return result;
}

/* ---- Auto Refresh ------------------------------------------------------ */

enum { AR_SLIDER = 1, AR_MINUS, AR_PLUS, AR_START, AR_STOP, AR_CANCEL };
#define AR_W 300
#define AR_H 90
#define AR_BODY (MAXBODY / 60)

static struct Gadget ar_prop;
static struct PropInfo ar_pi;

static void ar_draw_value(struct Window *dw, WORD top, unsigned short v)
{
    char s[24];
    snprintf(s, sizeof s, "Every %u seconds", (unsigned)v);
    line_at(dw, 12, top, AR_W - 12, s, 1);
}

/* Puts the knob exactly on v's step. */
static void ar_set_knob(struct Window *dw, unsigned short v)
{
    NewModifyProp(&ar_prop, dw, 0, AUTOKNOB | FREEHORIZ, auto_to_pot(v), 0, AR_BODY, MAXBODY, 1);
}

int dlg_auto_refresh(unsigned short *secs, int running)
{
    static struct Image knob;
    static button_t minus, plus, start, stop, cancel;
    static char status[40];
    struct Window *dw;
    WORD t = ui_screen() ? title_h(ui_screen()) : 11;
    WORD val_top = t + 4, slider_top = t + 16, status_top = t + 34, btn_top = AR_H - BTN_H - 6;
    unsigned short v = auto_clamp(*secs), shown;
    int result = -1;

    button_init(&cancel, 208, btn_top, 80, AR_CANCEL, "Cancel", 0);
    button_init(&stop, 110, btn_top, 80, AR_STOP, "Stop", &cancel.g);
    button_init(&start, 12, btn_top, 80, AR_START, "Start", &stop.g);
    if (!running) stop.g.Flags |= GADGDISABLED;        /* drawn ghosted from the start */
    button_init(&plus, AR_W - 12 - 24, slider_top, 24, AR_PLUS, "+", &start.g);
    button_init(&minus, 12, slider_top, 24, AR_MINUS, "-", &plus.g);

    memset(&knob, 0, sizeof knob);
    memset(&ar_pi, 0, sizeof ar_pi);
    ar_pi.Flags = AUTOKNOB | FREEHORIZ;
    ar_pi.HorizPot = auto_to_pot(v);
    ar_pi.HorizBody = AR_BODY;
    ar_pi.VertBody = MAXBODY;
    memset(&ar_prop, 0, sizeof ar_prop);
    ar_prop.NextGadget = &minus.g;
    ar_prop.LeftEdge = 12 + 24 + 6; ar_prop.TopEdge = slider_top;
    ar_prop.Width = AR_W - 2 * (12 + 24 + 6); ar_prop.Height = BTN_H;
    ar_prop.Flags = GADGHNONE;                 /* COMP would invert into the comic pens */
    ar_prop.Activation = GADGIMMEDIATE | RELVERIFY | FOLLOWMOUSE;
    ar_prop.GadgetType = PROPGADGET;
    ar_prop.GadgetRender = (APTR)&knob;       /* AUTOKNOB: Intuition fills this Image in */
    ar_prop.SpecialInfo = (APTR)&ar_pi;
    ar_prop.GadgetID = AR_SLIDER;

    if (running) snprintf(status, sizeof status, "Status: running every %u s", (unsigned)*secs);
    else snprintf(status, sizeof status, "Status: stopped");

    if (!(dw = dialog_open(AR_W, AR_H, "Auto Refresh",
                           GADGETUP | GADGETDOWN | MOUSEMOVE | RAWKEY | CLOSEWINDOW, &ar_prop)))
        return DLG_CANCEL;
    ar_draw_value(dw, val_top, v);
    line_at(dw, 12, status_top, AR_W - 12, status, 1);
    shown = v;

    while (result < 0) {
        struct IntuiMessage *m;
        Wait(1UL << dw->UserPort->mp_SigBit);
        while ((m = (struct IntuiMessage *)GetMsg(dw->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD gid = (cls == GADGETUP || cls == GADGETDOWN) ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            ReplyMsg((struct Message *)m);
            if (result >= 0) continue;          /* already decided: just drain */
            if (cls == CLOSEWINDOW) result = DLG_CANCEL;
            else if (cls == RAWKEY) {
                if (code == KEY_ESC) result = DLG_CANCEL;
            } else if (cls == MOUSEMOVE || (cls == GADGETDOWN && gid == AR_SLIDER)) {
                v = auto_from_pot(ar_pi.HorizPot);       /* live while dragging */
            } else if (cls == GADGETUP) {
                switch (gid) {
                case AR_SLIDER: v = auto_from_pot(ar_pi.HorizPot); ar_set_knob(dw, v); break;
                case AR_MINUS:  v = auto_clamp((long)v - 10); ar_set_knob(dw, v); break;
                case AR_PLUS:   v = auto_clamp((long)v + 10); ar_set_knob(dw, v); break;
                case AR_START:  *secs = v; result = DLG_START; break;
                case AR_STOP:   if (running) result = DLG_STOP; break;
                case AR_CANCEL: result = DLG_CANCEL; break;
                }
            }
        }
        /* One redraw per batch: MOUSEMOVE arrives in bursts while dragging. */
        if (result < 0 && v != shown) { ar_draw_value(dw, val_top, v); shown = v; }
    }
    dialog_close(dw);
    return result;
}
