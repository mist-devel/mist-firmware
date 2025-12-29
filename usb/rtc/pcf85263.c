//
// pcf85263.c
//
// Driver for NXP PCF85263/PCF85363 rtc
//

#include "usb.h"
#include "rtc/pcf85263.h"

#define PCF85263_ADDR   0x51

// date/time registers
#define REG_100THS      0x00
#define REG_SECS        0x01
    #define SECS_BIT_OS         0x80
#define REG_MINUTES     0x02
    #define MINUTES_BIT_EMON    0x80
#define REG_HOURS       0x03
#define REG_DAYS        0x04
#define REG_WEEKDAY     0x05
#define REG_MONTHS      0x06
#define REG_YEARS       0x07

// control registers
#define CTRL_OSCILLATOR 0x25
    #define OSC_BIT_12HR        0x20
#define CTRL_BATTERY    0x26
    #define BATT_BIT_BSM_VBAT   0x02
#define CTRL_PIN_IO     0x27
    #define IO_BIT_CLKPM_OFF    0x80
    #define IO_BIT_INTAPM_HIZ   0x11
#define CTRL_FUNCTION   0x28
    #define FUNC_BIT_COF_OFF    0x07
#define CTRL_INTA_EN    0x29
#define CTRL_INTB_EN    0x2a
#define CTRL_FLAGS      0x2b
#define CTRL_STOP_EN    0x2e
    #define STOP_BIT_EN_STOP    0x01
#define CTRL_RESET      0x2f
    #define RESET_CPR           0xa4

static bool pcf85263_probe(
    usb_device_t *dev, const i2c_bus_t *i2c, int *)
{
    uint8_t setup[] = {
        BATT_BIT_BSM_VBAT,
        (IO_BIT_CLKPM_OFF | IO_BIT_INTAPM_HIZ),
        FUNC_BIT_COF_OFF, 0, 0, 0
    };

    // disable CLK output pin for battery saving
    // reset interrupts settings to defaults, clear all flags
    return i2c->bulk_write(dev, PCF85263_ADDR, CTRL_BATTERY, setup, sizeof(setup));
}

static bool pcf85263_get_time(
    usb_device_t *dev, const i2c_bus_t *i2c, ctime_t date)
{
    uint8_t regs[REG_YEARS + 1];

    if (!i2c->bulk_read(dev, PCF85263_ADDR, REG_100THS, regs, sizeof(regs)))
        return false;

    date[T_YEAR]  = bcd2bin(regs[REG_YEARS]) + 100;
    date[T_MONTH] = bcd2bin(regs[REG_MONTHS]);
    date[T_DAY]   = bcd2bin(regs[REG_DAYS]);
    date[T_HOUR]  = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[T_MIN]   = bcd2bin(regs[REG_MINUTES] & 0x7F);
    date[T_SEC]   = bcd2bin(regs[REG_SECS] & 0x7F);
    date[T_WDAY]  = regs[REG_WEEKDAY] + 1;

    return true;
}

static bool pcf85263_set_time(
    usb_device_t *dev, const i2c_bus_t *i2c, const ctime_t date)
{
    uint8_t regs[REG_YEARS + 1], zero = 0,
        stop_reset[] = { STOP_BIT_EN_STOP, RESET_CPR };

    regs[REG_100THS]  = 0;
    regs[REG_YEARS]   = bin2bcd(date[T_YEAR] % 100);
    regs[REG_MONTHS]  = bin2bcd(date[T_MONTH]);
    regs[REG_DAYS]    = bin2bcd(date[T_DAY]);
    regs[REG_HOURS]   = bin2bcd(date[T_HOUR]);
    regs[REG_MINUTES] = bin2bcd(date[T_MIN]);
    regs[REG_SECS]    = bin2bcd(date[T_SEC]);
    regs[REG_WEEKDAY] = date[T_WDAY] - 1;

    // stop and clear prescaler
    // set new time
    // start
    return i2c->bulk_write(dev, PCF85263_ADDR, CTRL_STOP_EN, stop_reset, sizeof(stop_reset))
        && i2c->bulk_write(dev, PCF85263_ADDR, REG_100THS,   regs, sizeof(regs))
        && i2c->bulk_write(dev, PCF85263_ADDR, CTRL_STOP_EN, &zero, 1);
}

const rtc_chip_t rtc_pcf85263_chip = {
    .name = "PCF85263",
    .clock_rate = 400,
    .probe = pcf85263_probe,
    .get_time = pcf85263_get_time,
    .set_time = pcf85263_set_time,
};
