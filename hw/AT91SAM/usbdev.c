/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "AT91SAM7S256.h"
#include <string.h>

#include "debug.h"
#include "utils.h"
#include "at91sam_usb.h"
#include "usbdev.h"
#include "hardware.h"
#include "mist_cfg.h"

#define WORD(a) (a)&0xff, ((a)>>8)&0xff

static const char langDescriptor[] = {
  /* Language descriptor */
  0x04,         // length of descriptor in bytes
  0x03,         // descriptor type
  WORD(0x0409)  // language index (0x0409 = US-English)
};

static const char CDCDevDescriptor[] = {
  /* Device descriptor */
  0x12,   // bLength
  0x01,   // bDescriptorType
  WORD(0x0110),   // bcdUSBL
  0x02,   // bDeviceClass:    CDC class code
  0x00,   // bDeviceSubclass: CDC class sub code
  0x00,   // bDeviceProtocol: CDC Device protocol
  0x08,   // bMaxPacketSize0
  WORD(0x1c40), // idVendorL
  WORD(0x0537), // idProductL
  WORD(0x0001), // bcdDeviceL
  0x01,   // iManufacturer
  0x02,   // iProduct
  0x00,   // SerialNumber
  0x01    // bNumConfigs
};

static const char CDCCfgDescriptor[] = {
	/* ============== CONFIGURATION 1 =========== */
	/* Configuration 1 descriptor */
	0x09,   // CbLength
	0x02,   // CbDescriptorType
	WORD(0x43),   // CwTotalLength 2 EP + Control
	0x02,   // CbNumInterfaces
	0x01,   // CbConfigurationValue
	0x00,   // CiConfiguration
	0xC0,   // CbmAttributes 0xA0
	200,    // CMaxPower = 400mA

	/* Communication Class Interface Descriptor Requirement */
	0x09, // bLength
	0x04, // bDescriptorType
	0x00, // bInterfaceNumber
	0x00, // bAlternateSetting
	0x01, // bNumEndpoints
	0x02, // bInterfaceClass
	0x02, // bInterfaceSubclass
	0x00, // bInterfaceProtocol
	0x00, // iInterface

	/* Header Functional Descriptor */
	0x05, // bFunction Length
	0x24, // bDescriptor type: CS_INTERFACE
	0x00, // bDescriptor subtype: Header Func Desc
	WORD(0x0110), // bcdCDC:1.1

	/* ACM Functional Descriptor */
	0x04, // bFunctionLength
	0x24, // bDescriptor Type: CS_INTERFACE
	0x02, // bDescriptor Subtype: ACM Func Desc
	0x00, // bmCapabilities

	/* Union Functional Descriptor */
	0x05, // bFunctionLength
	0x24, // bDescriptorType: CS_INTERFACE
	0x06, // bDescriptor Subtype: Union Func Desc
	0x00, // bMasterInterface: Communication Class Interface
	0x01, // bSlaveInterface0: Data Class Interface

	/* Call Management Functional Descriptor */
	0x05, // bFunctionLength
	0x24, // bDescriptor Type: CS_INTERFACE
	0x01, // bDescriptor Subtype: Call Management Func Desc
	0x00, // bmCapabilities: D1 + D0
	0x01, // bDataInterface: Data Class Interface 1

	/* Endpoint 1 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x83,   // bEndpointAddress, Endpoint 03 - IN
	0x03,   // bmAttributes      INT
	WORD(0x08),   // wMaxPacketSize
	0xFF,   // bInterval

	/* Data Class Interface Descriptor Requirement */
	0x09, // bLength
	0x04, // bDescriptorType
	0x01, // bInterfaceNumber
	0x00, // bAlternateSetting
	0x02, // bNumEndpoints
	0x0A, // bInterfaceClass
	0x00, // bInterfaceSubclass
	0x00, // bInterfaceProtocol
	0x00, // iInterface

	/* First alternate setting */
	/* Endpoint 1 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x01,   // bEndpointAddress, Endpoint 01 - OUT
	0x02,   // bmAttributes      BULK
	WORD(AT91C_EP_OUT_SIZE),   // wMaxPacketSize
	0x00,   // bInterval

	/* Endpoint 2 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x82,   // bEndpointAddress, Endpoint 02 - IN
	0x02,   // bmAttributes      BULK
	WORD(AT91C_EP_IN_SIZE),   // wMaxPacketSize
	0x00    // bInterval
};

static const char storageDevDescriptor[] = {
  /* Device descriptor */
  0x12,   // bLength
  0x01,   // bDescriptorType
  WORD(0x0200),   // bcdUSBL
  0x00,   // bDeviceClass:    Specified by interface
  0x00,   // bDeviceSubclass: Specified by interface
  0x00,   // bDeviceProtocol: Specified by interface
  0x08,   // bMaxPacketSize0
  WORD(0x1c40), // idVendorL
  WORD(0x0537), // idProductL
  WORD(0x0001), // bcdDeviceL
  0x01,   // iManufacturer
  0x02,   // iProduct
  0x00,   // SerialNumber
  0x01    // bNumConfigs
};

static const char storageCfgDescriptor[] = {
	/* ============== CONFIGURATION 1 =========== */
	/* Configuration 1 descriptor */
	0x09,   // CbLength
	0x02,   // CbDescriptorType
	WORD(0x20),   // CwTotalLength 2 EP + Control
	0x01,   // CbNumInterfaces
	0x01,   // CbConfigurationValue
	0x00,   // CiConfiguration
	0xC0,   // CbmAttributes 0xA0
	200,    // CMaxPower = 400mA

	/*******************************************************/
	/* Mass Storage Class Interface Descriptor Requirement */
	/*******************************************************/
	0x09, // bLength
	0x04, // bDescriptorType
	0x00, // bInterfaceNumber
	0x00, // bAlternateSetting
	0x02, // bNumEndpoints
	0x08, // bInterfaceClass
	0x06, // bInterfaceSubclass: SCSI transparent command set
	0x50, // bInterfaceProtocol: USB Mass Storage Class Bulk-Only
	0x00, // iInterface

	/* Endpoint 1 descriptor - Bulk-out */
	0x07, // bLength,
	0x05, // bDescriptorType
	0x01, // bEndpointAddress, Endpoint 01 - Bulk-out
	0x02, // bmAttributes BULK
	WORD(AT91C_EP_OUT_SIZE), // wMaxPacketSize
	0x00, // bInterval

	/* Endpoint 2 descriptor - Bulk-in */
	0x07, // bLength,
	0x05, // bDescriptorType
	0x82, // bEndpointAddress, Endpoint 02 - Bulk-in
	0x02, // bmAttributes BULK
	WORD(AT91C_EP_IN_SIZE), // wMaxPacketSize
	0x00 // bInterval
};


/* USB standard request code */
#define STD_GET_STATUS_ZERO           0x0080
#define STD_GET_STATUS_INTERFACE      0x0081
#define STD_GET_STATUS_ENDPOINT       0x0082

#define STD_CLEAR_FEATURE_ZERO        0x0100
#define STD_CLEAR_FEATURE_INTERFACE   0x0101
#define STD_CLEAR_FEATURE_ENDPOINT    0x0102

#define STD_SET_FEATURE_ZERO          0x0300
#define STD_SET_FEATURE_INTERFACE     0x0301
#define STD_SET_FEATURE_ENDPOINT      0x0302

#define STD_SET_ADDRESS               0x0500
#define STD_GET_DESCRIPTOR            0x0680
#define STD_SET_DESCRIPTOR            0x0700
#define STD_GET_CONFIGURATION         0x0880
#define STD_SET_CONFIGURATION         0x0900
#define STD_GET_INTERFACE             0x0A81
#define STD_SET_INTERFACE             0x0B01
#define STD_SYNCH_FRAME               0x0C82

/* CDC Class Specific Request Code */
#define GET_LINE_CODING               0x21A1
#define SET_LINE_CODING               0x2021
#define SET_CONTROL_LINE_STATE        0x2221

/* Mass Storage Class Specific Request Code */
#define STORAGE_RESET                 0xFF21
#define STORAGE_GETMAXLUN             0xFEA1

typedef struct {
	unsigned int dwDTERRate;
	char bCharFormat;
	char bParityType;
	char bDataBits;
} AT91S_CDC_LINE_CODING, *AT91PS_CDC_LINE_CODING;

AT91S_CDC_LINE_CODING line = {
	115200, // baudrate
	0,      // 1 Stop Bit
	0,      // None Parity
	8};     // 8 Data bits

static void tx(char c) {
  while(!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
  AT91C_BASE_US0->US_THR = c;
}

static void tx_str(char *str) {
  while(*str) tx(*str++);
}

static void tx_hex(char *str, unsigned int num) {
  const char c[] = "0123456789abcdef";
  int s = 28;

  tx_str(str);

  while(s >= 0) {
    tx(c[(num >> s)&0x0f]);
    s -= 4;
  }

  tx('\n');
  tx('\r');
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Enumerate
//* \brief This function is a callback invoked when a SETUP packet is received
//*----------------------------------------------------------------------------

static void usbdev_enumerate(const char *devDescriptor, uint16_t devDescriptorLen,
                             const char *cfgDescriptor, uint16_t cfgDescriptorLen) {

  uint8_t bmRequestType, bRequest, bConf;
  uint16_t wValue, wIndex, wLength, wStatus;

  //  tx_hex("1: ", AT91C_BASE_UDP->UDP_CSR[0]);

  // setup packet available?
  if ( !(AT91C_BASE_UDP->UDP_CSR[0] & AT91C_UDP_RXSETUP) ) {

    // discard any pending payload
    AT91C_BASE_UDP->UDP_CSR[0] &= ~(AT91C_UDP_RX_DATA_BK0 | AT91C_UDP_RX_DATA_BK1);

    //    tx('x');
    return;
  }

  bmRequestType = AT91C_BASE_UDP->UDP_FDR[0];
  bRequest      = AT91C_BASE_UDP->UDP_FDR[0];
  wValue        = (AT91C_BASE_UDP->UDP_FDR[0] & 0xFF) | (AT91C_BASE_UDP->UDP_FDR[0] << 8);
  wIndex        = (AT91C_BASE_UDP->UDP_FDR[0] & 0xFF) | (AT91C_BASE_UDP->UDP_FDR[0] << 8);
  wLength       = (AT91C_BASE_UDP->UDP_FDR[0] & 0xFF) | (AT91C_BASE_UDP->UDP_FDR[0] << 8);

  // check direction bit (bit 7 set -> device to host)
  if (bmRequestType & 0x80) {
    AT91C_BASE_UDP->UDP_CSR[0] |= AT91C_UDP_DIR;
    while ( !(AT91C_BASE_UDP->UDP_CSR[0] & AT91C_UDP_DIR) );
  }

  AT91C_BASE_UDP->UDP_CSR[0] &= ~AT91C_UDP_RXSETUP;
  while ( (AT91C_BASE_UDP->UDP_CSR[0]  & AT91C_UDP_RXSETUP)  );

  // Handle supported standard device request Cf Table 9-3 in USB specification Rev 1.1
  switch ((bRequest << 8) | bmRequestType) {
  case STD_GET_DESCRIPTOR:
    if (wValue == 0x100)       // Return Device Descriptor
      AT91F_USB_SendData(devDescriptor, MIN(devDescriptorLen, wLength));
    else if (wValue == 0x200)  // Return Configuration Descriptor
      AT91F_USB_SendData(cfgDescriptor, MIN(cfgDescriptorLen, wLength));
    else if (wValue == 0x300)  // Return Language Descriptor
      AT91F_USB_SendData(langDescriptor, MIN(sizeof(langDescriptor), wLength));
    else if (wValue == 0x301)  // Return Manufacturer String Descriptor
      AT91F_USB_SendStr("Till Harbaum", wLength);
    else if (wValue == 0x302)  // Return Product String Descriptor
      AT91F_USB_SendStr("MIST Board", wLength);
    else
      AT91F_USB_SendStall();
    break;

  case STD_SET_ADDRESS:
    //    tx_hex("address set ", wValue);
    AT91F_USB_SendZlp();
    AT91C_BASE_UDP->UDP_FADDR = (AT91C_UDP_FEN | wValue);
    AT91C_BASE_UDP->UDP_GLBSTATE  = (wValue) ? AT91C_UDP_FADDEN : 0;
    break;

  case STD_SET_CONFIGURATION:
    //    tx_hex("config selected ", wValue);
    AT91F_USB_SendZlp();
    AT91C_BASE_UDP->UDP_GLBSTATE  = (wValue) ? AT91C_UDP_CONFG : AT91C_UDP_FADDEN;
    AT91C_BASE_UDP->UDP_CSR[1] = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_OUT) : 0;
    AT91C_BASE_UDP->UDP_CSR[2] = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_IN)  : 0;
    AT91C_BASE_UDP->UDP_CSR[3] = (wValue) ? (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_ISO_IN)   : 0;
    break;

  case STD_GET_CONFIGURATION:
    bConf = AT91C_BASE_UDP->UDP_GLBSTATE & AT91C_UDP_CONFG?1:0;
    AT91F_USB_SendData(&bConf, 1);
    break;

  case STD_GET_STATUS_ZERO:
  case STD_GET_STATUS_INTERFACE:
    AT91F_USB_SendWord(0);
    break;

  case STD_GET_STATUS_ENDPOINT:
    wIndex &= 0x0F;
    if ((AT91C_BASE_UDP->UDP_GLBSTATE & AT91C_UDP_CONFG) && (wIndex <= 3))
      AT91F_USB_SendWord((AT91C_BASE_UDP->UDP_CSR[wIndex] & AT91C_UDP_EPEDS) ? 0 : 1);
    else if ((AT91C_BASE_UDP->UDP_GLBSTATE & AT91C_UDP_FADDEN) && (wIndex == 0)) 
      AT91F_USB_SendWord((AT91C_BASE_UDP->UDP_CSR[wIndex] & AT91C_UDP_EPEDS) ? 0 : 1);
    else
      AT91F_USB_SendStall();
    break;

  case STD_SET_FEATURE_ENDPOINT:
    wIndex &= 0x0F;
    if ((wValue == 0) && wIndex && (wIndex <= 3)) {
      AT91C_BASE_UDP->UDP_CSR[wIndex] = 0;
      AT91F_USB_SendZlp();
    } else
      AT91F_USB_SendStall();
    break;

  case STD_SET_FEATURE_ZERO:
  case STD_CLEAR_FEATURE_ZERO:
    AT91F_USB_SendStall();
    break;

  case STD_SET_FEATURE_INTERFACE:
  case STD_CLEAR_FEATURE_INTERFACE:
    AT91F_USB_SendZlp();
    break;

  case STD_CLEAR_FEATURE_ENDPOINT:
    wIndex &= 0x0F;
    if ((wValue == 0) && wIndex && (wIndex <= 3)) {
      if (wIndex == 1)
        AT91C_BASE_UDP->UDP_CSR[1] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_OUT);
      else if (wIndex == 2)
        AT91C_BASE_UDP->UDP_CSR[2] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_BULK_IN);
      else if (wIndex == 3)
        AT91C_BASE_UDP->UDP_CSR[3] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_ISO_IN);
      AT91F_USB_SendZlp();
    } else
      AT91F_USB_SendStall();
    break;

  // handle CDC class requests
  case GET_LINE_CODING:
    AT91F_USB_SendData((char *) &line, MIN(sizeof(line), wLength));
    break;

  case SET_CONTROL_LINE_STATE:
    AT91F_USB_SendZlp();
    break;

  // handle Mass Storage class requests
  case STORAGE_RESET:
    AT91F_USB_SendZlp();
    break;

  case STORAGE_GETMAXLUN:
    AT91F_USB_SendWord(0);
    break;

  default:
    AT91F_USB_SendStall();
    break;
  }
}

//////////////

static void AT91F_CDC_Enumerate(void) {
  usbdev_enumerate(CDCDevDescriptor, sizeof(CDCDevDescriptor), CDCCfgDescriptor, sizeof(CDCCfgDescriptor));
}

static void AT91F_Mass_Storage_Enumerate(void) {
  usbdev_enumerate(storageDevDescriptor, sizeof(storageDevDescriptor), storageCfgDescriptor, sizeof(storageCfgDescriptor));
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Open
//* \brief
//*----------------------------------------------------------------------------
static void usb_cdc_open() {
  AT91F_USB_Open(&AT91F_CDC_Enumerate);
}

//*----------------------------------------------------------------------------
//* \fn    usb_cdc_read
//* \brief Read available data from Endpoint OUT
//*----------------------------------------------------------------------------
uint16_t usb_cdc_read(char *pData, uint16_t length) {
  if (!usb_cdc_is_configured()) return 0;
  return AT91F_USB_Read(pData, length);
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Write
//* \brief Send through endpoint 2
//*----------------------------------------------------------------------------
uint16_t usb_cdc_write(const char *pData, uint16_t length) {
  if (!usb_cdc_is_configured()) return length;
  return AT91F_USB_Write(pData, length);
}

uint8_t usb_cdc_is_configured() {
  return (!mist_cfg.usb_storage);
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Open
//* \brief
//*----------------------------------------------------------------------------
static void usb_storage_open() {
  AT91F_USB_Open(&AT91F_Mass_Storage_Enumerate);
}
//*----------------------------------------------------------------------------
//* \fn    usb_cdc_read
//* \brief Read available data from Endpoint OUT
//*----------------------------------------------------------------------------
uint16_t usb_storage_read(char *pData, uint16_t length) {
  if (!usb_storage_is_configured()) return 0;
  return AT91F_USB_Read(pData, length);
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_CDC_Write
//* \brief Send through endpoint 2
//*----------------------------------------------------------------------------
uint16_t usb_storage_write(const char *pData, uint16_t length) {
  if (!usb_storage_is_configured()) return length;
  return AT91F_USB_Write(pData, length);
}

uint8_t usb_storage_is_configured() {
  return (mist_cfg.usb_storage);
}

void usb_dev_open(void) {
  if (mist_cfg.usb_storage)
    usb_storage_open();
  else
    usb_cdc_open();
}

void usb_dev_reconnect() {
    *AT91C_PIOA_SODR = USB_PUP;
    WaitTimer(20);
    usb_dev_open();
}
