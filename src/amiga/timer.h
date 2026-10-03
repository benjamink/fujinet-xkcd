/* src/amiga/timer.h */
#ifndef XKCD_TIMER_H
#define XKCD_TIMER_H
int  timer_open(void);               /* 1 ok; creates port + timerequest on UNIT_VBLANK */
void timer_start(unsigned long seconds);
void timer_abort(void);
unsigned long timer_sigmask(void);
int  timer_fired(void);              /* consumes the reply; 1 if a started request completed */
void timer_close(void);
#endif
