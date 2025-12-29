#ifndef USB_RTC_MCP2221_H
#define USB_RTC_MCP2221_H

/*
 * I2C/RTC driver
 * for MCP2221(A) USB/I2S bridge chip
 */

#include "usb/rtc.h"

// usb device context
typedef struct {
    ep_t ep_in;
    ep_t ep_out;
    uint16_t i2c_clock;         // i2c bus clock rate
    uint8_t chip_type;          // rtc chip type in use
} rtc_info_t;

extern const usb_rtc_class_config_t usb_rtc_mcp2221_class;

// i2c bus interface
typedef struct {
    bool (*bulk_read)(usb_device_t *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
    bool (*bulk_write)(usb_device_t *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
} i2c_bus_t;

// clock chip interface
typedef struct {
    char name[12];              // rtc chip name
    uint16_t clock_rate;        // i2c operating freq, in kHz
    bool (*probe)(usb_device_t *, const i2c_bus_t *, int *);
    bool (*get_time)(usb_device_t *, const i2c_bus_t *, ctime_t);
    bool (*set_time)(usb_device_t *, const i2c_bus_t *, const ctime_t);
} rtc_chip_t;

#endif // USB_RTC_MCP2221_H
