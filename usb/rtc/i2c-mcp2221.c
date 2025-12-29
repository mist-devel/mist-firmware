//
// i2c-mcp2221.c
//
// I2C/RTC driver for Microchip MCP2221(A)
//

#include <string.h>

#include "usb.h"
#include "max3421e.h"
#include "rtc/i2c-mcp2221.h"
#include "rtc/pcf85263.h"
#include "rtc/ds3231.h"
#include "timer.h"
#include "debug.h"

#define REPORT_SIZE     64

#define MCP2221_VID     0x04d8
#define MCP2221_PID     0x00dd

// commands codes
enum {
    CMD_I2C_WR_DATA = 0x90,
    CMD_I2C_WR_NO_STOP = 0x94,
    CMD_I2C_WR_RPT_START = 0x92,
    CMD_I2C_RD_DATA = 0x91,
    CMD_I2C_RD_RPT_START = 0x93,
    CMD_I2C_GET_DATA = 0x40,
    CMD_I2C_PARAM_OR_STATUS = 0x10,
};

// states list of i2c engine
typedef enum uint8_t {
    I2C_IDLE = 0x00,
    I2C_ENG_BUSY = 0x01,
    I2C_START_TOUT = 0x12,
    I2C_STOP_TOUT = 0x62,
    I2C_WRADDRL_TOUT = 0x23,
    I2C_WRDATA_TOUT = 0x44,
    I2C_WRADDRL_NACK = 0x25,
    I2C_MASK_ADDR_NACK = 0x40,
    I2C_WRADDRL_SEND = 0x21,
    I2C_ADDR_NACK = 0x25,
    I2C_READ_PARTIAL = 0x54,
    I2C_READ_COMPL = 0x55,
} mcp_i2c_state_t;

// status/set parameters command
typedef struct {
    uint8_t  cmd_code;              // 0x10 = CMD_I2C_PARAM_OR_STATUS
    uint8_t  unused1;               // Any value
    uint8_t  cancel_i2c;            // 0x10 = Cancel the current I2C transfer
    uint8_t  set_i2c_speed;         // 0x20 = Set the I2C communication speed
    uint8_t  i2c_clock_divider;     // Value of the I2C system clock divider
    uint8_t  unused2[59];           // Any values
} __attribute__ ((packed)) mcp_set_cmd_t;

// status/set parameters response
typedef struct {
    uint8_t  cmd_echo;              // 0x10 = CMD_I2C_PARAM_OR_STATUS
    uint8_t  status;                // 0x00 = Command completed successfully
                                    // 0x01 = Command Error or Command unknown
                                    // 0x41 = Command was not executed
    uint8_t  cancel_i2c;            // 0x00 = No special operation
                                    // 0x10 = Transfer was marked for cancellation
                                    // 0x11 = Already in Idle mode
    uint8_t  set_i2c_speed;         // 0x00 = No special operation
                                    // 0x20 = New communication speed is being set
                                    // 0x21 = Speed change rejected
    uint8_t  i2c_req_divider;       // Value of the I2C system clock divider
    uint8_t  unused1[3];            // Don’t care
    uint8_t  i2c_engine_state;      // See in mcp_i2c_state_t
    uint16_t i2c_requested_len;     // Requested I2C transfer length
    uint16_t i2c_transfered_len;    // Number of already transferred bytes
    uint8_t  i2c_buf_counter;       // Internal I2C data buffer counter
    uint8_t  i2c_div;               // Current I2C speed divider
    uint8_t  i2c_timeout;           // Current I2C timeout value
    uint16_t i2c_address;           // I2C address being used
    uint8_t  unused2[2];            // Don’t care
    uint8_t  last_comm_status;      // 0x00 = Success
                                    // 0x01 = Addr NACK (slave address doesn't respond)
                                    // 0x02 = Data NACK (data transfer failed)
                                    // 0x03 = Timeout
    uint8_t  unknown;               // Don’t care
    uint8_t  scl_line_state;        // SCL line value, as read from the pin
    uint8_t  sda_line_state;        // SDA line value, as read from the pin
    uint8_t  intr_edge;             // Interrupt edge detector state, 0 or 1
    uint8_t  i2c_read_pending;      // 0, 1 or 2
    uint8_t  unused3[20];           // Don’t care
    uint8_t  hw_rev_major;          // ‘A’
    uint8_t  hw_rev_minor;          // ‘6’
    uint8_t  fw_rev_major;          // ‘1’
    uint8_t  fw_rev_minor;          // '2'
    uint16_t adc_ch0;               // ADC channel 0 input value
    uint16_t adc_ch1;               // ADC channel 1 input value
    uint16_t adc_ch2;               // ADC channel 2 input value
    uint8_t  unused4[8];            // Don’t care
} __attribute__ ((packed)) mcp_set_resp_t;

// slave i2c command
typedef struct {
    uint8_t  cmd_code;              // I2C command code
    uint8_t  size_low;              // I2C transfer length, low byte
    uint8_t  size_high;             // I2C transfer length, high byte
    uint8_t  slave_addr;            // I2C slave address to communicate with
    uint8_t  data[60];              // Data buffer for write
} __attribute__ ((packed)) mcp_i2c_cmd_t;

// slave i2c response
typedef struct {
    uint8_t  cmd_echo;              // I2C command code echo
    uint8_t  is_failed;             // 0x00 = Completed successfully, 0x01 = Not completed
    uint8_t  internal_state;        // Internal I2C Engine state or Reserved
    uint8_t  data_size;             // Data size or Don’t care
    uint8_t  data[60];              // Data buffer for read or Don’t care
} __attribute__ ((packed)) mcp_i2c_resp_t;

static bool mcp_i2c_bulk_read(
    usb_device_t *, uint8_t, uint8_t, uint8_t *, uint8_t);

static bool mcp_i2c_bulk_write(
    usb_device_t *, uint8_t, uint8_t, uint8_t *, uint8_t);

static bool mcp_get_status(
    usb_device_t *, uint8_t *, bool);

static const i2c_bus_t mcp_i2c_bus = {
  .bulk_read = mcp_i2c_bulk_read,
  .bulk_write = mcp_i2c_bulk_write
};

// all of supported RTCs list
static const rtc_chip_t *rtc_chips[] = {
    &rtc_ds3231_chip,
    &rtc_pcf85263_chip,
    NULL
};

static void mcp_i2c_cancel(usb_device_t *dev, uint8_t *rpt)
{
    mcp_set_resp_t *resp = (mcp_set_resp_t *) rpt;

    // cancel current operation on bus
    if (!mcp_get_status(dev, rpt, true) || resp->i2c_engine_state != I2C_IDLE)
        return;

    usbrtc_debugf("%s: canceled", __FUNCTION__);
}

static bool mcp_i2c_wait_for(
    usb_device_t *dev, uint8_t *rpt, mcp_i2c_state_t state, int8_t timeout)
{
    // waiting until bus state changed
    mcp_set_resp_t *resp = (mcp_set_resp_t *) rpt;

    do {
        timer_delay_msec(1);
        usb_poll();
    } while (mcp_get_status(dev, rpt, false) && resp->i2c_engine_state != state && --timeout);

    if (!timeout || resp->status != 0 || resp->i2c_engine_state != state)
    {
        usbrtc_debugf("%s: bus timeout, error #%X:#%X",
            __FUNCTION__, resp->status, resp->i2c_engine_state);

        // trying to reset bus
        mcp_i2c_cancel(dev, rpt);
        return false;
    }

    usbrtc_debugf("%s: time left: %d", __FUNCTION__, timeout);
    return true;
}

static bool mcp_exec(usb_device_t *dev, uint8_t *rpt, uint16_t *size)
{
    // send command and get responce
    uint8_t rcode, cmd = rpt[0];
    rtc_info_t *info = &(dev->rtc_info);

    rcode = usb_out_transfer(dev, &info->ep_out, REPORT_SIZE, rpt);

    if (rcode)
    {
        usbrtc_debugf("%s: OUT: ep%d failed for #%X, error #%X",
            __FUNCTION__, info->ep_out.epAddr, cmd, rcode);
        return false;
    }

    *size = REPORT_SIZE;
    rpt[0] = rpt[1] = -1;

    rcode = usb_in_transfer(dev, &info->ep_in, size, rpt);

    // check for command echo and status code
    if (rcode || rpt[0] != cmd || rpt[1] != 0)
    {
        usbrtc_debugf("%s: IN: ep%d failed for #%X, error #%X:#%X:#%X",
            __FUNCTION__, info->ep_in.epAddr, cmd, rcode, rpt[1], rpt[8]);
        return false;
    }

    return true;
}

static bool mcp_set_i2c_clock(usb_device_t *dev, uint8_t *rpt, uint16_t clock)
{
    uint16_t size;

    // set new bus clock rate
    mcp_set_cmd_t *cmd = (mcp_set_cmd_t *) rpt;
    mcp_set_resp_t *resp = (mcp_set_resp_t *) rpt;
    rtc_info_t *info = &(dev->rtc_info);

    if (info->i2c_clock == clock)
        return true;

    if (clock > 400) clock = 400;
    if (clock < 50)  clock = 50;

    cmd->cmd_code = CMD_I2C_PARAM_OR_STATUS;
    cmd->i2c_clock_divider = (12000000 - clock*1000) - 3;
    cmd->set_i2c_speed = 0x20;
    cmd->cancel_i2c = 0x0;

    if (mcp_exec(dev, rpt, &size))
    {
        if (resp->set_i2c_speed == 0x20)
        {
            info->i2c_clock = clock;
            return true;
        } else {
            usbrtc_debugf("%s: mcp2221 error #%X:#%X",
                __FUNCTION__, resp->status, resp->set_i2c_speed);
        }
    }

    return false;
}

static bool mcp_get_status(
    usb_device_t *dev, uint8_t *rpt, bool with_cancel)
{
    uint16_t size;

    // get bridge status
    mcp_set_cmd_t *cmd = (mcp_set_cmd_t *) rpt;

    cmd->cmd_code = CMD_I2C_PARAM_OR_STATUS;
    cmd->cancel_i2c = with_cancel ? 0x10 : 0;
    cmd->set_i2c_speed = 0;

    return mcp_exec(dev, rpt, &size);
}

static uint8_t usb_hid_parse_conf(usb_device_t *dev, uint16_t len)
{
    uint8_t rcode;
    rtc_info_t *info = &(dev->rtc_info);
    bool isHID = false;

    union buf_u {
        usb_configuration_descriptor_t conf_desc;
        usb_interface_descriptor_t iface_desc;
        usb_endpoint_descriptor_t ep_desc;
        uint8_t raw[len];
    } buf, *p;

    // get full size descriptor
    if ((rcode = usb_get_conf_descr(dev, len, 0, &buf.conf_desc)))
        return rcode;

    p = &buf;

    // scan through all descriptors
    while (len > 0)
    {
        switch (p->conf_desc.bDescriptorType)
        {
            case USB_DESCRIPTOR_CONFIGURATION:
            case HID_DESCRIPTOR_HID:
            default:
                break;

            case USB_DESCRIPTOR_INTERFACE:
                isHID = (p->iface_desc.bInterfaceClass == USB_CLASS_HID);
                break;

            case USB_DESCRIPTOR_ENDPOINT:
                if (!isHID)
                    break;

                ep_t *ep = (p->ep_desc.bEndpointAddress & 0x80)
                    ? &info->ep_in : &info->ep_out;

                ep->epAddr = (p->ep_desc.bEndpointAddress & 0x0f);
                ep->epType = (p->ep_desc.bmAttributes & EP_TYPE_MSK);
                ep->maxPktSize = p->ep_desc.wMaxPacketSize[0];
                ep->bmNakPower = USB_NAK_NOWAIT;
                ep->epAttribs  = 0;
                break;
        }

        if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
            break;

        // advance to next descriptor
        len -= p->conf_desc.bLength;
        p = (union buf_u*)(p->raw + p->conf_desc.bLength);
    }

    return (info->ep_in.epType == EP_TYPE_INTR && info->ep_out.epType == EP_TYPE_INTR)
        ? 0 : USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
}

static uint8_t mcp_init(
    usb_device_t *dev, usb_device_descriptor_t *dev_desc)
{
    usbrtc_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

    if (dev_desc->bDeviceClass != USB_CLASS_MISC)
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    if ((dev_desc->idVendor != MCP2221_VID) || (dev_desc->idProduct != MCP2221_PID))
        return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

    union {
        mcp_set_cmd_t cmd;
        mcp_set_resp_t resp;
        usb_configuration_descriptor_t conf_desc;
        uint8_t raw[REPORT_SIZE];
    } buf;

    uint8_t rcode;

    // Use first config (actually there is only one)
    if ((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &buf.conf_desc))) {
        usbrtc_debugf("mcp2221: failed to get config0, error #%X", rcode);
        return rcode;
    }

    rtc_info_t *info = &(dev->rtc_info);
    ep_t *ep[] = { &info->ep_in, &info->ep_out, NULL };

    // Reset runtime info
    info->chip_type = info->i2c_clock = -1;

    for (uint8_t i = 0; ep[i]; i++)
    {
        ep[i]->epAddr = 1;
        ep[i]->epType = 0;
        ep[i]->maxPktSize = 8;
        ep[i]->epAttribs  = 0;
        ep[i]->bmNakPower = USB_NAK_NOWAIT;
    }

    // Parse HID descriptor
    if ((rcode = usb_hid_parse_conf(dev, buf.conf_desc.wTotalLength))) {
        usbrtc_debugf("mcp2221: failed to parse HID config, error #%X", rcode);
        return rcode;
    }

    // Set Configuration Value
    rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);
    if (rcode) {
        usbrtc_debugf("mcp2221: set config%d error %d",
            buf.conf_desc.bConfigurationValue, rcode);
    }

    // Wait mcp2221 chip i2c bus for idle
    if (!mcp_i2c_wait_for(dev, buf.raw, I2C_IDLE, 10)) {
        iprintf("mcp2221: i2c bus error #%X:#%X\n",
            buf.resp.status, buf.resp.i2c_engine_state);
        return USB_ERROR_NO_SUCH_DEVICE;
    }

    iprintf("mcp2221: chip found, rev: %c%c %c.%c\n",
        buf.resp.hw_rev_major, buf.resp.hw_rev_minor,
        buf.resp.fw_rev_major, buf.resp.fw_rev_minor);

    // Probe for clock chips
    for (int i = 0; rtc_chips[i]; i++)
    {
        int code = 0;
        const rtc_chip_t *chip = rtc_chips[i];

        if (!mcp_set_i2c_clock(dev, buf.raw, chip->clock_rate))
        {
            iprintf("mcp2221: cannot set bus clock rate to %u kHz\n",
                chip->clock_rate);
        }

        if (chip->probe(dev, &mcp_i2c_bus, &code))
        {
            iprintf("mcp2221: rtc %s found\n", chip->name);
            info->chip_type = i;
            return 0;
        }
        else
        {
            iprintf("mcp2221: rtc %s is not detected, code #%X\n",
                chip->name, code);
        }
    }

    return USB_ERROR_NO_SUCH_DEVICE;
}

static uint8_t mcp_release(usb_device_t *dev)
{
    usbrtc_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

    return 0;
}

static bool mcp_i2c_bulk_read(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s(#%X, #%X, %u)",
        __FUNCTION__, addr, reg, length);

    union {
        mcp_i2c_cmd_t cmd;
        mcp_i2c_resp_t resp;
        uint8_t raw[REPORT_SIZE];
    } rpt;

    rtc_info_t *info = &(dev->rtc_info);
    uint16_t size, i;

    if (!buf || !length || length > sizeof(rpt.resp.data)-1)
        return false;

    if (!mcp_i2c_wait_for(dev, rpt.raw, I2C_IDLE, 10))
        return false;

    // set register pointer to 'reg'
    rpt.cmd.cmd_code = CMD_I2C_WR_NO_STOP;
    rpt.cmd.slave_addr = (addr << 1);
    rpt.cmd.size_high = 0;
    rpt.cmd.size_low = 1;
    rpt.cmd.data[0] = reg;

    if (!mcp_exec(dev, rpt.raw, &size))
        return false;

    // request to read 'length' byte(s)
    rpt.cmd.cmd_code = CMD_I2C_RD_RPT_START;
    rpt.cmd.slave_addr = (addr << 1) | 1;
    rpt.cmd.size_low = length;
    rpt.cmd.size_high = 0;

    if (!mcp_exec(dev, rpt.raw, &size))
        return false;

    if (!mcp_i2c_wait_for(dev, rpt.raw, I2C_READ_COMPL, 10))
        return false;

    // fetch buffered data
    rpt.cmd.cmd_code = CMD_I2C_GET_DATA;
    rpt.cmd.slave_addr = 0;
    rpt.cmd.size_high = 0;
    rpt.cmd.size_low = 0;

    if (mcp_exec(dev, rpt.raw, &size) && rpt.resp.data_size == length) {
        memcpy(buf, &rpt.resp.data, length);
        return true;
    }

    return false;
}

static bool mcp_i2c_bulk_write(
    usb_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t length)
{
    usbrtc_debugf("%s(#%X, #%X, %u)",
        __FUNCTION__, addr, reg, length);

    union {
        mcp_i2c_cmd_t cmd;
        uint8_t raw[REPORT_SIZE];
    } rpt;

    uint16_t size, i;

    if (!buf || !length || length > sizeof(rpt.cmd.data)-1)
        return false;

    if (!mcp_i2c_wait_for(dev, rpt.raw, I2C_IDLE, 10))
        return false;

    rpt.cmd.cmd_code = CMD_I2C_WR_DATA;
    rpt.cmd.size_low = (1 + length);
    rpt.cmd.size_high = 0;
    rpt.cmd.slave_addr = (addr << 1);

    rpt.cmd.data[0] = reg;
    memcpy(&rpt.cmd.data[1], buf, length);

    return mcp_exec(dev, rpt.raw, &size);
}

static bool mcp_get_time(struct usb_device_entry *dev, ctime_t date)
{
    rtc_info_t *info = &(dev->rtc_info);
    const rtc_chip_t *rtc = rtc_chips[info->chip_type];

    return rtc->get_time(dev, &mcp_i2c_bus, date);
}

static bool mcp_set_time(struct usb_device_entry *dev, const ctime_t date)
{
    const rtc_info_t *info = &(dev->rtc_info);
    const rtc_chip_t *rtc = rtc_chips[info->chip_type];

    return rtc->set_time(dev, &mcp_i2c_bus, date);
}

const usb_rtc_class_config_t usb_rtc_mcp2221_class = {
    .base = { USB_RTC, mcp_init, mcp_release, NULL },
    .get_time = mcp_get_time,
    .set_time = mcp_set_time,
};
