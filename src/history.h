/* src/history.h */
#ifndef XKCD_HISTORY_H
#define XKCD_HISTORY_H
#define HISTORY_MAX 25
typedef struct { long ids[HISTORY_MAX]; unsigned char count, pos; } history_t;
void history_init(history_t *h);
void history_push(history_t *h, long id);
int  history_back(history_t *h, long *id);
int  history_forward(history_t *h, long *id);
long history_current(const history_t *h);
#endif
