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

#include <string.h>
#include <stdio.h>

#include "hardware.h"
#include "arch/barriers.h"
#include "usb/usb.h"
#include "usbdev.h"
#include "utils.h"
#include "debug.h"

#define WORD(a) (a)&0xff, ((a)>>8)&0xff

// Private members
unsigned int  currentRcvBank;

static const char langDescriptor[] = {
  /* Language descriptor */
  0x04,         // length of descriptor in bytes
  0x03,         // descriptor type
  WORD(0x0409)  // language index (0x0409 = US-English)
};

static const char devDescriptor[] = {
  /* Device descriptor */
  0x12,   // bLength
  0x01,   // bDescriptorType
  WORD(0x0200),   // bcdUSBL
  0xEF,   // bDeviceClass:    MISCELLANEOUS class code
  0x02,   // bDeviceSubclass: Common Class
  0x01,   // bDeviceProtocol: Interface Association Descriptor
  EP0_SIZE,   // bMaxPacketSize0
  WORD(0x1c40), // idVendorL
  WORD(0x0538), // idProductL
  WORD(0x0001), // bcdDeviceL
  0x01,   // iManufacturer
  0x02,   // iProduct
  0x00,   // SerialNumber
  0x01    // bNumConfigs
};

static const char cfgDescriptor[] = {
	/* ============== CONFIGURATION 1 =========== */
	/* Configuration 1 descriptor */
	0x09,   // CbLength
	0x02,   // CbDescriptorType
	WORD(0x6A),   // CwTotalLength 2 EP + Control
	0x03,   // CbNumInterfaces
	0x01,   // CbConfigurationValue
	0x00,   // CiConfiguration
	0x80,   // CbmAttributes 0xA0
	200,    // CMaxPower = 400mA

	/* IAD */
	0x08,   // bLength
	0x0B,   // bDescriptorType IAD
	0x00,   // bFirstInterface
	0x02,   // bInterfaceCount
	0x02,   // bDeviceClass:    CDC class code
	0x02,   // bDeviceSubclass: CDC class sub code
	0x00,   // bDeviceProtocol: CDC Device protocol
	0x00,   // iFunction

	/********************************************************/
	/* Communication Class Interface Descriptor Requirement */
	/********************************************************/
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

	/* Endpoint 3 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x83,   // bEndpointAddress, Endpoint 03 - IN
	0x03,   // bmAttributes      INT
	WORD(0x08),   // wMaxPacketSize
	INTERVAL,   // bInterval

	/* Data Class Interface Descriptor Requirement */
	0x09, // bLength
	0x04, // bDescriptorType
	0x01, // bInterfaceNumber
	0x00, // bAlternateSetting
	0x02, // bNumEndpoints
	0x0A, // bInterfaceClass
	0x00, // bInterfaceSubclass
	0x00, // bInterfaceProtocol
	0x06, // iInterface

	/* First alternate setting */
	/* Endpoint 1 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x81,   // bEndpointAddress, Endpoint 01 - IN
	0x02,   // bmAttributes      BULK
	WORD(BULK_SIZE),   // wMaxPacketSize
	0x00,   // bInterval

	/* Endpoint 2 descriptor */
	0x07,   // bLength
	0x05,   // bDescriptorType
	0x02,   // bEndpointAddress, Endpoint 02 - OUT
	0x02,   // bmAttributes      BULK
	WORD(BULK_SIZE),   // wMaxPacketSize
	0x00,    // bInterval

	/* IAD */
	0x08,   // bLength
	0x0B,   // bDescriptorType IAD
	0x02,   // bFirstInterface
	0x01,   // bInterfaceCount
	0x08,   // bDeviceClass:    Mass storage class
	0x06,   // bDeviceSubclass:
	0x50,   // bDeviceProtocol: Bulk-only transport
	0x00,   // iFunction

	/*******************************************************/
	/* Mass Storage Class Interface Descriptor Requirement */
	/*******************************************************/
	0x09, // bLength
	0x04, // bDescriptorType
	0x02, // bInterfaceNumber
	0x00, // bAlternateSetting
	0x02, // bNumEndpoints
	0x08, // bInterfaceClass
	0x06, // bInterfaceSubclass: SCSI transparent command set
	0x50, // bInterfaceProtocol: USB Mass Storage Class Bulk-Only
	0x00, // iInterface

	/* Endpoint 4 descriptor - Bulk-in */
	0x07, // bLength,
	0x05, // bDescriptorType
	0x84, // bEndpointAddress, Endpoint 04 - Bulk-in
	0x02, // bmAttributes BULK
	WORD(BULK_SIZE), // wMaxPacketSize
	0x00, // bInterval

	/* Endpoint 5 descriptor - Bulk-out */
	0x07, // bLength,
	0x05, // bDescriptorType
	0x05, // bEndpointAddress, Endpoint 05 - Bulk-out
	0x02, // bmAttributes BULK
	WORD(BULK_SIZE), // wMaxPacketSize
	0x00 // bInterval
};

static const char idProduct[] = {0x10, 0x03, 'S',0,'i',0,'D',0,'i',0,'1',0,'2',0,'8',0};
static const char idManufacturer[] = {0x28, 0x03, 'M',0,'a',0,'n',0,'u',0,'F',0,'e',0,'r',0,'H',0,'i',0,'/',0,'S',0,'l',0,'i',0,'n',0,'g',0,'s',0,'h',0,'o',0,'t',0};

/* CDC Class Specific Request Code */
#define USB_SET_LINE_CODING           0x20
#define USB_GET_LINE_CODING           0x21
#define USB_SET_CONTROL_LINE_STATE    0x22

/* Mass Storage Class Specific Request Code */
#define STORAGE_RESET                 0xFF
#define STORAGE_GETMAXLUN             0xFE

typedef struct {
	unsigned int dwDTERRate;
	char bCharFormat;
	char bParityType;
	char bDataBits;
} CDC_LINE_CODING_t;

static CDC_LINE_CODING_t line = {
	115200, // baudrate
	0,      // 1 Stop Bit
	0,      // None Parity
	8};     // 8 Data bits

void usb_dump_regs() {
	usb_debugf("DEVCTRL:    %08x", USBHS->USBHS_DEVCTRL);
	usb_debugf("DEVISR:     %08x", USBHS->USBHS_DEVISR);
	usb_debugf("DEVIMR:     %08x", USBHS->USBHS_DEVIMR);
	usb_debugf("DEVEPT:     %08x", USBHS->USBHS_DEVEPT);
	usb_debugf("DEVEPTCFG0: %08x", USBHS->USBHS_DEVEPTCFG[0]);
	usb_debugf("DEVEPTISR0: %08x", USBHS->USBHS_DEVEPTISR[0]);
	usb_debugf("DEVEPTIMR0: %08x", USBHS->USBHS_DEVEPTIMR[0]);
	usb_debugf("DEVEPTCFG1: %08x", USBHS->USBHS_DEVEPTCFG[1]);
	usb_debugf("DEVEPTISR1: %08x", USBHS->USBHS_DEVEPTISR[1]);
	usb_debugf("DEVEPTIMR1: %08x", USBHS->USBHS_DEVEPTIMR[1]);
	usb_debugf("DEVEPTCFG2: %08x", USBHS->USBHS_DEVEPTCFG[2]);
	usb_debugf("DEVEPTISR2: %08x", USBHS->USBHS_DEVEPTISR[2]);
	usb_debugf("DEVEPTIMR2: %08x", USBHS->USBHS_DEVEPTIMR[2]);
	usb_debugf("DEVEPTCFG3: %08x", USBHS->USBHS_DEVEPTCFG[3]);
	usb_debugf("DEVEPTISR3: %08x", USBHS->USBHS_DEVEPTISR[3]);
	usb_debugf("DEVEPTIMR3: %08x", USBHS->USBHS_DEVEPTIMR[3]);
}

// Maximum transfer size on USB DMA
#define EPT_VIRTUAL_SIZE  0x8000

static void usb_dma_transfer(uint8_t ep, uint8_t* data, uint32_t size)
{
	UsbhsDevDma* devdma = &USBHS->USBHS_DEVDMA[ep-1];
	devdma->USBHS_DEVDMAADDRESS = (uint32_t) data;
	devdma->USBHS_DEVDMASTATUS = devdma->USBHS_DEVDMASTATUS; // clear pending bits
	devdma->USBHS_DEVDMACONTROL = 0;
	devdma->USBHS_DEVDMACONTROL = USBHS_DEVDMACONTROL_BURST_LCK | USBHS_DEVDMACONTROL_CHANN_ENB | USBHS_DEVDMACONTROL_BUFF_LENGTH(size);
	while (devdma->USBHS_DEVDMASTATUS & USBHS_DEVDMASTATUS_CHANN_ACT);
}

static void usb_write_fifo_buffer(uint8_t ep, uint8_t* data, uint32_t size)
{
	if (ep) { // EP0 doesn't have DMA
		usb_dma_transfer(ep, data, size);
	} else {
		volatile uint8_t *fifo = ((volatile uint8_t*)USBHS_RAM_ADDR) + EPT_VIRTUAL_SIZE * ep;
		dsb(); isb();
		for (; size; size--)
			*(fifo++) = *(data++);
		dsb(); isb();
	}
}

static void usb_read_fifo_buffer(uint8_t ep, uint8_t* data, uint32_t size)
{
	if (ep) { // EP0 doesn't have DMA
		usb_dma_transfer(ep, data, size);
	} else {
		volatile uint8_t *fifo = ((volatile uint8_t*)USBHS_RAM_ADDR) + EPT_VIRTUAL_SIZE * ep;
		dmb();
		for (; size; size--)
			*(data++) = *(fifo++);
	}
}

static uint32_t usb_get_fifo_byte_cnt(uint8_t ep)
{
	return ((USBHS->USBHS_DEVEPTISR[ep] & USBHS_DEVEPTISR_BYCT_Msk) >> USBHS_DEVEPTISR_BYCT_Pos);
}

typedef struct {
	uint8_t state;
	uint8_t *buf;
	uint16_t size;
} usb_dev_ep_t;

static usb_dev_ep_t usb_ep0;
static uint8_t maxlun = 0;
static bool enumerated = false;

static void usb_irq_handler(void) {
	uint32_t isr = USBHS->USBHS_DEVISR;
	isr &= USBHS->USBHS_DEVIMR;
	usb_debugf("usb_irq_handler isr=%08x", isr);
	//usb_dump_regs();

	if (isr & USBHS_DEVISR_EORST) {
		usb_debugf("EORST");
		usb_ep0.state = 0;
		enumerated = false;
		USBHS->USBHS_DEVICR = USBHS_DEVICR_EORSTC; // ack
		USBHS->USBHS_DEVIER = USBHS_DEVIER_SUSPES; // enable suspend int

		USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_RXSTPES;

		// setup endpoint 1 for IN requests
		USBHS->USBHS_DEVEPTCFG[1] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
		                            BULK_SIZE_CONF | USBHS_DEVEPTCFG_EPDIR_IN | USBHS_DEVEPTCFG_EPTYPE_BLK | USBHS_DEVEPTCFG_AUTOSW;

		// setup endpoint 2 for OUT requests
		USBHS->USBHS_DEVEPTCFG[2] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
		                            BULK_SIZE_CONF | USBHS_DEVEPTCFG_EPDIR_OUT | USBHS_DEVEPTCFG_EPTYPE_BLK | USBHS_DEVEPTCFG_AUTOSW;

		// setup endpoint 3 for INTRPT requests
		USBHS->USBHS_DEVEPTCFG[3] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
		                            EP0_SIZE_CONF | USBHS_DEVEPTCFG_EPDIR_IN | USBHS_DEVEPTCFG_EPTYPE_INTRPT | USBHS_DEVEPTCFG_AUTOSW;
		USBHS->USBHS_DEVEPTIER[3] = USBHS_DEVEPTIER_TXINES;

		// setup endpoint 4 for IN requests
		USBHS->USBHS_DEVEPTCFG[4] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
		                            BULK_SIZE_CONF | USBHS_DEVEPTCFG_EPDIR_IN | USBHS_DEVEPTCFG_EPTYPE_BLK | USBHS_DEVEPTCFG_AUTOSW;

		// setup endpoint 5 for OUT requests
		USBHS->USBHS_DEVEPTCFG[5] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
		                            BULK_SIZE_CONF | USBHS_DEVEPTCFG_EPDIR_OUT | USBHS_DEVEPTCFG_EPTYPE_BLK | USBHS_DEVEPTCFG_AUTOSW;

		USBHS->USBHS_DEVEPT = USBHS_DEVEPT_EPEN0 | USBHS_DEVEPT_EPEN1 | USBHS_DEVEPT_EPEN2 | USBHS_DEVEPT_EPEN3 | USBHS_DEVEPT_EPEN4 | USBHS_DEVEPT_EPEN5;
		USBHS->USBHS_DEVIER = USBHS_DEVIER_SUSPES | USBHS_DEVIER_WAKEUPES; // enable suspend int
	} else if (isr & USBHS_DEVISR_WAKEUP) {
		usb_debugf("WAKEUP");
		USBHS->USBHS_CTRL &= ~USBHS_CTRL_FRZCLK;
		USBHS->USBHS_DEVICR = USBHS_DEVICR_WAKEUPC; // ack
		USBHS->USBHS_DEVIDR = USBHS_DEVIDR_WAKEUPEC; // disable wakeup int
		USBHS->USBHS_DEVIER = USBHS_DEVIER_SUSPES; // enable suspend int
	} else if (isr & USBHS_DEVISR_SUSP) {
		usb_debugf("SUSPEND");
		USBHS->USBHS_DEVICR = USBHS_DEVICR_SUSPC; // ack
		USBHS->USBHS_DEVIDR = USBHS_DEVIDR_SUSPEC; // disable suspend int
		USBHS->USBHS_DEVIER = USBHS_DEVIER_WAKEUPES; // enable wakeup int
		USBHS->USBHS_CTRL |= USBHS_CTRL_FRZCLK;
	} else if (isr & 0x1000) {
		// ctrl endpoint
		uint32_t ctrl_isr = USBHS->USBHS_DEVEPTISR[0];
		uint32_t fifo_cnt;
		uint8_t buf[64];

		if (ctrl_isr & USBHS_DEVEPTISR_RXSTPI) {
			usb_debugf("RXSTPI");
			fifo_cnt = MIN(usb_get_fifo_byte_cnt(0), sizeof(buf));
			usb_read_fifo_buffer(0, buf, fifo_cnt);
			//hexdump(buf, fifo_cnt, 0);
			if (fifo_cnt >= 8) {
				uint16_t maxlen = buf[6] + (buf[7]<<8);
				switch (buf[1]) { // bRequest
					case USB_REQUEST_GET_DESCRIPTOR:
						switch(buf[3]) {
							case USB_DESCRIPTOR_DEVICE:
								usb_debugf("send dev descriptor");
								usb_ep0.buf = (char*)devDescriptor;
								usb_ep0.size = MIN(sizeof(devDescriptor), maxlen);
								usb_ep0.state = 1;
								USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
								break;
							case USB_DESCRIPTOR_CONFIGURATION:
								usb_debugf("send conf descriptor");
								usb_ep0.buf = (char*)cfgDescriptor;
								usb_ep0.size = MIN(sizeof(cfgDescriptor), maxlen);
								usb_ep0.state = 1;
								USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
								break;
							case USB_DESCRIPTOR_STRING:
								switch (buf[2]) {
									case 0:
										usb_debugf("send lang descriptor");
										usb_ep0.buf = (char*)langDescriptor;
										usb_ep0.size = MIN(sizeof(langDescriptor), maxlen);
										usb_ep0.state = 1;
										USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
										break;
									case 1:
										usb_debugf("send manufacturer descriptor");
										usb_ep0.buf = (char*)idManufacturer;
										usb_ep0.size = MIN(sizeof(idManufacturer), maxlen);
										usb_ep0.state = 1;
										USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
										break;
									case 2:
										usb_debugf("send product descriptor");
										usb_ep0.buf = (char*)idProduct;
										usb_ep0.size = MIN(sizeof(idProduct), maxlen);
										usb_ep0.state = 1;
										USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
										break;
								}
								break;
						}
						break;
					case USB_REQUEST_SET_ADDRESS:
						usb_debugf("set address to %d", buf[2]);
						USBHS->USBHS_DEVCTRL |= USBHS_DEVCTRL_UADD(buf[2]);
						usb_ep0.buf = 0;
						usb_ep0.size = 0;
						usb_ep0.state = 2;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
						break;
					case USB_REQUEST_SET_CONFIGURATION:
						usb_debugf("set config to %d", buf[2]);
						usb_ep0.buf = 0;
						usb_ep0.size = 0;
						usb_ep0.state = 1;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
						break;
					case USB_SET_LINE_CODING:
						usb_debugf("set line coding");
						usb_ep0.buf = (char*)&line;
						usb_ep0.size = MIN(sizeof(CDC_LINE_CODING_t), maxlen);
						usb_ep0.state = 4;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_RXOUTES;
						break;
					case USB_GET_LINE_CODING:
						usb_debugf("get line coding");
						usb_ep0.buf = (char*)devDescriptor;
						usb_ep0.size = MIN(sizeof(CDC_LINE_CODING_t), maxlen);
						usb_ep0.state = 1;
						break;
					case USB_SET_CONTROL_LINE_STATE:
						usb_debugf("set ctrl line state");
						usb_ep0.buf = 0;
						usb_ep0.size = 0;
						usb_ep0.state = 1;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
						break;
					// handle Mass Storage class requests
					case STORAGE_RESET:
						usb_debugf("storage reset");
						usb_ep0.buf = 0;
						usb_ep0.size = 0;
						usb_ep0.state = 1;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
						break;
					case STORAGE_GETMAXLUN:
						usb_debugf("getmaxlun");
						usb_ep0.buf = &maxlun;
						usb_ep0.size = sizeof(maxlun);
						usb_ep0.state = 1;
						USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_TXINES;
						break;
					default:
						usb_debugf("unhandled: %02x", buf[1]);
						//TODO: stall
				}
			}
			USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_RXSTPIC;
		}

		if ((ctrl_isr & USBHS_DEVEPTISR_RXOUTI) && (usb_ep0.state & 0x04)) {
			uint32_t readbytes = MIN(usb_ep0.size, usb_get_fifo_byte_cnt(0));
			usb_debugf("RXOUTI readbytes=%d",readbytes);
			if (readbytes) {
				usb_read_fifo_buffer(0, usb_ep0.buf, readbytes);
				//hexdump(usb_ep[0].buf, readbytes, 0);
				usb_ep0.buf += readbytes;
				usb_ep0.size -= readbytes;
			} else {
				USBHS->USBHS_DEVEPTIDR[0] = USBHS_DEVEPTIDR_RXOUTEC;
				usb_ep0.state = 0;
			}
			USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_RXOUTIC; // ack
		}

		if ((ctrl_isr & USBHS_DEVEPTISR_TXINI) && (usb_ep0.state & 0x03)) {
			uint8_t writebytes = MIN(usb_ep0.size, EP0_SIZE);
			usb_debugf("TXINI writebytes=%d", writebytes);

			if (writebytes) {
				usb_write_fifo_buffer(0, usb_ep0.buf, writebytes);
				usb_ep0.buf += writebytes;
				usb_ep0.size -= writebytes;
			}
			USBHS->USBHS_DEVEPTICR[0] = USBHS_DEVEPTICR_TXINIC; // ack

			if (usb_ep0.state == 2) {
				WaitTimer(1);
				USBHS->USBHS_DEVCTRL |= USBHS_DEVCTRL_ADDEN;
				enumerated = true;
			}
			if (!writebytes) {
				// end of transfer, disable interrupt
				USBHS->USBHS_DEVEPTIDR[0] = USBHS_DEVEPTIDR_TXINEC;
				usb_ep0.state = 0;
			}
		}
	} else if (isr & 0x8000) {
		// intrpt endpoint
		uint32_t intrpt_isr = USBHS->USBHS_DEVEPTISR[3];
		usb_debugf("INTRPT isr=%08x", intrpt_isr);
		USBHS->USBHS_DEVEPTICR[3] = USBHS_DEVEPTICR_TXINIC; // ack
	} else if (isr & 0xff) {
		USBHS->USBHS_DEVICR = 0xff; // ack
	}
}

void usb_dev_open(void) {

	puts(__FUNCTION__);

	usb_ep0.state = 0;
	enumerated = false;

	PMC->PMC_PCER1 = 1 << (ID_USBHS - 32);

	// configure 48MHz USB PLL
	PMC->PMC_USB = PMC_USB_USBDIV(PLLCLK / 48000000 - 1);
	PMC->PMC_SCER = PMC_SCER_USBCLK;

#if USB_HS
	// configure 480MHz UTMI PLL
	UTMI->UTMI_CKTRIM = 0x1; // 16 MHZ UPLL source clock
	PMC->CKGR_UCKR = CKGR_UCKR_UPLLEN | CKGR_UCKR_UPLLCOUNT(5);
	while (!(PMC->PMC_SR & PMC_SR_LOCKU));
#endif

	USBHS->USBHS_CTRL = USBHS_CTRL_UIMOD_DEVICE | USBHS_CTRL_USBE | USBHS_CTRL_VBUSHWC;

#if USB_HS
	while (!(USBHS->USBHS_SR & USBHS_SR_CLKUSABLE));
#endif

	// setup endpoint 0 for ctrl requests
	USBHS->USBHS_DEVEPTCFG[0] = USBHS_DEVEPTCFG_ALLOC | USBHS_DEVEPTCFG_EPBK_1_BANK |
	                            EP0_SIZE_CONF | USBHS_DEVEPTCFG_AUTOSW;

	if (!(USBHS->USBHS_DEVEPTISR[0] & USBHS_DEVEPTISR_CFGOK)) {
		usb_debugf("Error configuring endpoint 0");
		return;
	}
	USBHS->USBHS_DEVEPT = USBHS_DEVEPT_EPEN0;

	//usb_dump_regs();

	// enable device interrupts
	USBHS->USBHS_DEVIER = USBHS_DEVIER_PEP_0 | USBHS_DEVIER_PEP_1 | USBHS_DEVIER_PEP_2 | USBHS_DEVIER_PEP_3 | USBHS_DEVIER_PEP_4 | USBHS_DEVIER_PEP_5 |
	                      USBHS_DEVIER_EORSTES;
	USBHS->USBHS_DEVEPTIER[0] = USBHS_DEVEPTIER_RXSTPES;
	// disable all host interrupts
	USBHS->USBHS_HSTIER = 0;
	USBHS->USBHS_HSTICR = 0xFF;

	NVIC_SetVector(ID_USBHS, (uint32_t) &usb_irq_handler);
	NVIC_EnableIRQ(ID_USBHS);
	NVIC_SetPriority(ID_USBHS, 0xFFFFFFFF);

	// activate USB
#if USB_HS
	USBHS->USBHS_DEVCTRL = 0;
#else
	USBHS->USBHS_DEVCTRL = USBHS_DEVCTRL_SPDCONF_LOW_POWER;
#endif

}

//*----------------------------------------------------------------------------
//* \fn    usb_is_configured
//* \brief Test if the device is configured
//*----------------------------------------------------------------------------
static uint8_t usb_is_configured(void) {
	return (!!(USBHS->USBHS_CTRL & USBHS_CTRL_USBE) && enumerated);
}

//*----------------------------------------------------------------------------
//* \fn    usb_read
//* \brief Read available data from Endpoint OUT
//*----------------------------------------------------------------------------
uint16_t usb_read(uint8_t ep, char *pData, uint16_t length) {
	uint16_t nbBytesRcv = 0;

	if (usb_is_configured()) {
		if (USBHS->USBHS_DEVEPTISR[ep] & USBHS_DEVEPTISR_RXOUTI) {
			USBHS->USBHS_DEVEPTICR[ep] = USBHS_DEVEPTICR_RXOUTIC; // ack int
			nbBytesRcv = usb_get_fifo_byte_cnt(ep);
			//usb_debugf("received %d bytes on ep %d", nbBytesRcv, ep);
			uint16_t read = nbBytesRcv;
			while (read) {
				uint16_t chunk = MIN(read, length);
				usb_read_fifo_buffer(ep, pData, chunk);
				read -= chunk;
				pData += chunk;
			}
			USBHS->USBHS_DEVEPTIDR[ep] = USBHS_DEVEPTIDR_FIFOCONC; // ack fifo
		}
	}
	return nbBytesRcv;
}

//*----------------------------------------------------------------------------
//* \fn    usb_write
//* \brief Send through Endpoint IN
//*----------------------------------------------------------------------------
static uint16_t usb_write(uint8_t ep, const char *pData, uint16_t length) {
	if(usb_is_configured()) {
		while (length) {
			uint16_t write = MIN(length, BULK_IN_SIZE);
			while (!(USBHS->USBHS_DEVEPTISR[ep] & USBHS_DEVEPTISR_TXINI));
			USBHS->USBHS_DEVEPTICR[ep] = USBHS_DEVEPTICR_TXINIC; // ack int
			usb_write_fifo_buffer(ep, (char*)pData, write);
			USBHS->USBHS_DEVEPTIDR[ep] = USBHS_DEVEPTIDR_FIFOCONC; // ack fifo
			length -= write;
			pData += write;
		}
	}
	return length;
}

/////////////////

uint8_t usb_cdc_is_configured(void) {
	return (usb_is_configured());
}

uint16_t usb_cdc_read(char *pData, uint16_t length) {
	return usb_read(2, pData, length);
}

uint16_t usb_cdc_write(const char *pData, uint16_t length) {
	return usb_write(1, pData, length);
}

uint8_t usb_storage_is_configured(void) {
	return (usb_is_configured());
}

uint16_t usb_storage_read(char *pData, uint16_t length) {
	return usb_read(5, pData, length);
}

uint16_t usb_storage_write(const char *pData, uint16_t length) {
	return usb_write(4, pData, length);
}

void usb_dev_reconnect(void) {}