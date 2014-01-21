// this file allows to enabled and disable rs232 debugging on a detailed basis
#ifndef DEBUG_H
#define DEBUG_H

// ------------ generic debugging -----------

#if 0
#define menu_debugf(...) iprintf(__VA_ARGS__)
#else
#define menu_debugf(...)
#endif


// ----------- minimig debugging -------------
#if 0
#define hdd_debugf(...) iprintf(__VA_ARGS__)
#else
#define hdd_debugf(...)
#endif

#if 0
#define fdd_debugf(...) iprintf(__VA_ARGS__)
#else
#define fdd_debugf(...)
#endif

// -------------- TOS debugging --------------

#if 0
#define tos_debugf(a, ...) iprintf("\033[1;32mTOS: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define tos_debugf(...)
#endif

#if 1
// ikbd debug output in red
#define IKBD_DEBUG
#define ikbd_debugf(a, ...) iprintf("\033[1;31mIKBD: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define ikbd_debugf(...)
#endif

#if 1
// cdc debug output in blue
#define CDC_DEBUG
#define cdc_debugf(a, ...) iprintf("\033[1;34mCDC: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define cdc_debugf(...)
#endif

#endif // DEBUG_H
