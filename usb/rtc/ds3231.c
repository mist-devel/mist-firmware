//
// ds3231.c
//
// Driver for Maxim DS3231/DS3232 rtc
//

#include "usb.h"
#include "rtc/ds3231.h"

#define DS3231_ADDR     0x68

// date/time registers
#define REG_SECS        0x00
#define REG_MINUTES     0x01
#define REG_HOURS       0x02
    #define HOURS_BIT_AMPM      0x20
    #define HOURS_BIT_12HR      0x40
#define REG_WEEKDAY     0x03
#define REG_DAYS        0x04
#define REG_MONTHS      0x05
    #define MONTHS_BIT_CENTURY  0x80
#define REG_YEARS       0x06

// control registers
#define REG_CR          0x0E
#define REG_SR          0x0F

static bool ds3231_probe(
    usb_device_t *dev, const i2c_bus_t *i2c, int *code)
{
    uint8_t cr = 0, zero = 0;

    // disable CLK output pin for battery saving
    if (!i2c->bulk_write(dev, DS3231_ADDR, REG_SR, &zero, 1))
        return false;

    // checking of Control Register for specific bits values
    if (i2c->bulk_read(dev, DS3231_ADDR, REG_CR, &cr, 1) && (cr & 0x1c) == 0x1c)
        return true;

    *code = cr;
    return false;
}

static bool ds3231_get_time(
    usb_device_t *dev, const i2c_bus_t *i2c, ctime_t date)
{
    uint8_t regs[7];

    if (!i2c->bulk_read(dev, DS3231_ADDR, REG_SECS, regs, 7))
        return false;

    date[T_YEAR] = bcd2bin(regs[REG_YEARS]);

    if (regs[REG_MONTHS] & MONTHS_BIT_CENTURY)
        date[T_YEAR] += 100;

    date[T_MONTH] = bcd2bin(regs[REG_MONTHS] & 0x1f);
    date[T_DAY] = bcd2bin(regs[REG_DAYS]);

    if (regs[REG_HOURS] & HOURS_BIT_12HR)
    {
        date[T_HOUR] = bcd2bin(regs[REG_HOURS] & 0x1f);

        if (regs[REG_HOURS] & HOURS_BIT_AMPM)
            date[T_HOUR] += 12;
    }
    else
    {
        date[T_HOUR] = bcd2bin(regs[REG_HOURS]);
    }

    date[T_MIN]  = bcd2bin(regs[REG_MINUTES]);
    date[T_SEC]  = bcd2bin(regs[REG_SECS]);
    date[T_WDAY] = regs[REG_WEEKDAY];

    return true;
}

static bool ds3231_set_time(
    usb_device_t *dev, const i2c_bus_t *i2c, const ctime_t date)
{
    uint8_t regs[7];

    regs[REG_MONTHS] = bin2bcd(date[T_MONTH]);

    if (date[T_YEAR] >= 100)
        regs[REG_MONTHS] |= MONTHS_BIT_CENTURY;

    regs[REG_YEARS]   = bin2bcd(date[T_YEAR] % 100);
    regs[REG_DAYS]    = bin2bcd(date[T_DAY]);
    regs[REG_HOURS]   = bin2bcd(date[T_HOUR]);
    regs[REG_MINUTES] = bin2bcd(date[T_MIN]);
    regs[REG_SECS]    = bin2bcd(date[T_SEC]);
    regs[REG_WEEKDAY] = date[T_WDAY];

    return i2c->bulk_write(dev, DS3231_ADDR, REG_SECS, regs, 7);
}

const rtc_chip_t rtc_ds3231_chip = {
    .name = "DS3231",
    .clock_rate = 400,
    .probe = ds3231_probe,
    .get_time = ds3231_get_time,
    .set_time = ds3231_set_time,
};
