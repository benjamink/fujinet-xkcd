/* src/countdown.h - auto-refresh countdown arithmetic on (seconds, microseconds) clock values */
#ifndef XKCD_COUNTDOWN_H
#define XKCD_COUNTDOWN_H
#define COUNTDOWN_US 1000000UL
/* Microseconds from now until the deadline; 0 once it has passed. Saturates at cap_us. */
unsigned long countdown_left_us(unsigned long dl_s, unsigned long dl_us,
                                unsigned long now_s, unsigned long now_us, unsigned long cap_us);
unsigned short countdown_shown(unsigned long left_us);   /* whole seconds, rounded up */
unsigned long countdown_wait_us(unsigned long left_us);  /* until the shown value next changes */
#endif
