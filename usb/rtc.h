#ifndef USB_RTC_H
#define USB_RTC_H

#include "utils.h"

struct ep_t;
typedef struct usb_device_entry usb_device_t;

// internal time format: year,month,day,hour,min,sec,wday
typedef uint8_t ctime_t[7];

enum {
    T_YEAR = 0,
    T_MONTH,
    T_DAY,
    T_HOUR,
    T_MIN,
    T_SEC,
    T_WDAY      // 1..7
};

// usb rtc device driver interface
typedef struct {
    usb_device_class_config_t base;
    bool (*get_time)(usb_device_t *, ctime_t);
    bool (*set_time)(usb_device_t *, const ctime_t);
} usb_rtc_class_config_t;

bool usb_rtc_get_time(ctime_t);
bool usb_rtc_set_time(const ctime_t);

#endif // USB_RTC_H
