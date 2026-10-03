/* src/amiga/main.c - xkcd viewer: app state, history and the event loop */
#include <stdio.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include "xkcd.h"
#include "history.h"
#include "selector.h"
#include "net.h"
#include "rng.h"
#include "timer.h"
#include "ui.h"
#include "dialogs.h"
#include "zoom.h"

static history_t h;
static xkcd_comic_t current, fetched;
static long latest_num;
static int auto_on;
static unsigned short auto_secs = AUTO_DEFAULT;
static char sel_main[64];

static void update_buttons(void)
{
    ui_set_buttons(h.count && h.pos > 0, auto_on ? auto_secs : 0);
}

static int fetch_latest(void)
{
    unsigned char e;
    ui_status("Contacting xkcd.com...");
    e = net_fetch_comic(0, &fetched);
    if (e != NET_OK) { ui_status(net_error(e, 0)); latest_num = 0; return 0; }
    latest_num = fetched.num;
    return 1;
}

static int show_id(long id, int push)
{
    char msg[32];
    unsigned char e;

    snprintf(msg, sizeof msg, "Fetching #%ld...", id);
    ui_status(msg);
    e = net_fetch_comic(id, &fetched);
    if (e != NET_OK) { ui_status(net_error(e, id)); return 0; }
    current = fetched;
    if (push) history_push(&h, current.num);
    update_buttons();
    ui_show_comic(&current);
    if (!current.has_image) {
        ui_image_message("No static image for this comic");
        ui_status("");
        return 1;
    }
    ui_status("Converting image...");
    e = net_fetch_image(current.img, sel_main, ui_image_header_cb, 0);
    ui_status(e == NET_OK ? "" : net_error(e, current.num));
    return 1;
}

static void show_random(void)
{
    if (!latest_num && !fetch_latest()) return;
    (void)show_id(xkcd_random_pick(rng_next(), latest_num, history_current(&h)), 1);
}

int main(void)
{
    long id;
    int quit = 0;

    rng_seed();
    if (!timer_open()) { printf("xkcd: cannot open timer.device\n"); return 20; }
    if (!ui_open()) { printf("xkcd: cannot open screen (not enough chip memory?)\n"); timer_close(); return 20; }
    selector_main(sel_main, sizeof sel_main, ui_is_pal());
    history_init(&h);

    fetch_latest();
    show_random();

    while (!quit) {
        switch (ui_poll()) {
        case UI_PREV:
            /* a failed fetch leaves the screen unchanged, so step history back to match it */
            if (history_back(&h, &id) && !show_id(id, 0)) history_forward(&h, &id);
            break;
        case UI_NEXT:
            if (!history_forward(&h, &id)) show_random();
            else if (!show_id(id, 0)) history_back(&h, &id);
            break;
        case UI_ZOOM:
            if (current.has_image) zoom_show(&current); else ui_status("No image to zoom");
            break;
        case UI_FETCH_ID:
            if (dlg_fetch_id(&id, latest_num)) (void)show_id(id, 1);
            break;
        case UI_AUTO:
            switch (dlg_auto_refresh(&auto_secs, auto_on)) {
            case DLG_START: auto_on = 1; timer_start(auto_secs); break;
            case DLG_STOP:  auto_on = 0; timer_abort(); break;
            default: break;
            }
            update_buttons();
            break;
        case UI_QUIT:
            quit = 1;
            break;
        default: {
            ULONG sig = Wait(ui_sigmask() | timer_sigmask() | SIGBREAKF_CTRL_C);
            if (sig & SIGBREAKF_CTRL_C) quit = 1;
            /* timer_fired() is the authority: the port signal alone can be stale after an abort. */
            else if (timer_fired() && auto_on) { show_random(); timer_start(auto_secs); }
            break;
        }
        }
    }
    timer_close();
    net_shutdown();
    ui_close();
    return 0;
}
