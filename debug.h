// this file allows to enabled and disable rs232 debugging on a detailed basis
#ifndef DEBUG_H
#define DEBUG_H

// ------------ usb debugging -----------

#if 1
#define hidp_debugf(...) iprintf(__VA_ARGS__)
#else
#define hidp_debugf(...)
#endif

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
// 8bit debug output in blue
#define bit8_debugf(a, ...) iprintf("\033[1;34m8BIT: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define bit8_debugf(...)
#endif


#if 1
// usb asix debug output in blue
#define asix_debugf(a, ...) iprintf("\033[1;34mASIX: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define asix_debugf(...)
#endif

#endif // DEBUG_H
