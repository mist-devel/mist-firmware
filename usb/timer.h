// interface between USB timer and minimig timer

#ifndef TIMER_H
#define TIMER_H

#include <inttypes.h>
#include <stdbool.h>
typedef uint32_t msec_t;

void timer_init();
msec_t timer_get_msec();
void timer_delay_msec(msec_t t);
bool timer_check(msec_t ref, msec_t delay);

#endif // TIMER_H
