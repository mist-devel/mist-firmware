#include "timer.h"
#include "hardware.h"

// this is a 32 bit counter which overflows after 2^32 milliseconds
// -> after 46 days

void timer_init() {
  // reprogram the realtime timer to run at 1Khz
  InitRTTC();
}

msec_t timer_get_msec() {
  return GetRTTC();
}

bool timer_check(msec_t ref, msec_t delay) {
  msec_t now = GetRTTC();
  return ((now-ref) >= delay);
}

void timer_delay_msec(msec_t t) {
  msec_t now = GetRTTC();

  while(GetRTTC() - now < t);
}
