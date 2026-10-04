/* src/amiga/timer.h - the auto-refresh countdown: one-second ticks toward a system-clock deadline */
#ifndef XKCD_TIMER_H
#define XKCD_TIMER_H
enum { TIMER_IDLE, TIMER_TICK, TIMER_DONE };
int  timer_open(void);               /* 1 ok; creates ports + timerequests on UNIT_VBLANK */
void timer_start(unsigned long seconds);
void timer_abort(void);
unsigned long timer_sigmask(void);
int  timer_poll(void);               /* consumes the reply; TIMER_TICK re-arms, TIMER_DONE stops */
unsigned short timer_secs_left(void);   /* rounded up; 0 when not running */
void timer_close(void);
#endif
