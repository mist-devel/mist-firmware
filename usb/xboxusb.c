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

/*
 Based on the work of

 Copyright (C) 2012 Kristian Lauszus, TKJ Electronics. All rights reserved.
*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "max3421e.h"
#include "usb.h"
#include "timer.h"
#include "joystick.h"
#include "joymapping.h"
#include "mist_cfg.h"
#include "state.h"
#include "user_io.h"
#include "debug.h"

#if 0
static uint8_t usb_xbox_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
	uint8_t rcode;

	union buf_u {
	    usb_configuration_descriptor_t conf_desc;
	    usb_interface_descriptor_t iface_desc;
	    usb_endpoint_descriptor_t ep_desc;
	    uint8_t raw[len];
	} buf, *p;

	// usb_interface_descriptor

	if(rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)) 
		return rcode;

	/* scan through all descriptors */
	p = &buf;
	while(len > 0) {
		switch(p->conf_desc.bDescriptorType) {
			case USB_DESCRIPTOR_CONFIGURATION:
				// we already had this, so we simply ignore it
				break;

			case USB_DESCRIPTOR_INTERFACE:
				usb_dump_interface_descriptor(&p->iface_desc);
				break;

			case USB_DESCRIPTOR_ENDPOINT:
				usb_dump_endpoint_descriptor(&p->ep_desc);
				break;

			default:
				hid_debugf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
		}

		// advance to next descriptor
		len -= p->conf_desc.bLength;
		p = (union buf_u*)(p->raw + p->conf_desc.bLength);
	}

	if(len != 0) {
		hid_debugf("Config underrun: %d", len);
		return USB_ERROR_CONFIGURAION_SIZE_MISMATCH;
	}

	return 0;
}
#endif

uint8_t usb_xbox_init(usb_device_t *dev) {
	uint8_t rcode;
	uint16_t pid;
	uint16_t vid;

	usb_configuration_descriptor_t conf_desc;

	dev->xbox_info.bPollEnable = false;
	usb_debugf("%s(%x)", __FUNCTION__, dev->bAddress);
	vid = dev->vid;
	pid = dev->pid;

	if(vid != XBOX_VID && vid != MADCATZ_VID && vid != JOYTECH_VID && vid != GAMESTOP_VID) {// Check VID
		usb_debugf("Not a XBOX VID (%04x)", vid);
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}
	if(pid == XBOX_WIRELESS_PID) {
		iprintf("You have plugged in a wireless Xbox 360 controller - it doesn't support USB communication\n");
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}
	if(pid == XBOX_WIRELESS_RECEIVER_PID || pid == XBOX_WIRELESS_RECEIVER_THIRD_PARTY_PID) {
		iprintf("Only Xbox 360 controllers via USB are supported\n");
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}
	if(pid != XBOX_WIRED_PID && pid != MADCATZ_WIRED_PID && pid != GAMESTOP_WIRED_PID && pid != AFTERGLOW_WIRED_PID && pid != JOYTECH_WIRED_PID) {// Check PID
		usb_debugf("Not a XBOX PID");
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}

	if(rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &conf_desc))
		return rcode;

	usb_dump_conf_descriptor(&conf_desc);

	// Skip parsing the interface and endpoint descriptors, and use known values */
	//usb_xbox_parse_conf(dev, 0, buf.conf_desc.wTotalLength);
	dev->xbox_info.interval = 4;
	dev->xbox_info.inEp.epAddr = 0x01;
	dev->xbox_info.inEp.maxPktSize = XBOX_EP_MAXPKTSIZE;
	dev->xbox_info.inEp.bmNakPower = USB_NAK_NOWAIT;
	dev->xbox_info.inEp.bmSndToggle = 0;
	dev->xbox_info.inEp.bmRcvToggle = 0;
	dev->xbox_info.qNextPollTime = 0;

	// Set Configuration Value
	if(rcode = usb_set_conf(dev, conf_desc.bConfigurationValue)) {
		iprintf("XBOX: error setting conf value (%d)\n", rcode);
		return rcode;
	}
	dev->xbox_info.jindex = joystick_add();
	dev->xbox_info.bPollEnable = true;
	return 0;
}

uint8_t usb_xbox_release(usb_device_t *dev) {
	joystick_release(dev->xbox_info.jindex);
	return 0;
}

#define swp2(a, msk) (((a & msk)<<1 | (a & msk)>>1) & msk)
static void usb_xbox_read_report(usb_device_t *dev, uint16_t len, uint8_t *buf) {
	if(!buf) return;
//	hexdump(buf, len, 0);
	if(buf[0] != 0x00 || buf[1] != 0x14) { // Check if it's the correct report - the controller also sends different status reports
		return;
	}

	uint16_t buttons = (swp2(buf[2], 0xc) >> 2) | (swp2(buf[2], 0x3) << 2) | swp2(buf[3], 0x30) | (swp2(buf[2], 0x30) << 2) | ((buf[3] & 0x03) << 10) | (swp2(buf[3], 0xc0) << 2);
	uint8_t idx = dev->xbox_info.jindex;
	StateUsbIdSet(dev->vid, dev->pid, 8, idx);

/*
	TODO: handle analogue parts
        hatValue[LeftHatX] = (int16_t)(((uint16_t)readBuf[7] << 8) | readBuf[6]);
        hatValue[LeftHatY] = (int16_t)(((uint16_t)readBuf[9] << 8) | readBuf[8]);
        hatValue[RightHatX] = (int16_t)(((uint16_t)readBuf[11] << 8) | readBuf[10]);
        hatValue[RightHatY] = (int16_t)(((uint16_t)readBuf[13] << 8) | readBuf[12]);
*/

	// map virtual joypad

	if(buttons != dev->xbox_info.oldButtons) {
		StateUsbJoySet(buttons, buttons>>8, idx);
		uint16_t vjoy = virtual_joystick_mapping(dev->vid, dev->pid, buttons);

		StateJoySet(vjoy, idx);
		StateJoySetExtra( vjoy>>8, idx);

		// swap joystick 0 and 1 since 1 is the one.
		// used primarily on most systems (most = Amiga and ST...need to get rid of this)
		if(idx == 0)      idx = 1;
		else if(idx == 1) idx = 0;
		// if real DB9 mouse is preffered, switch the id back to 1
		idx = (idx == 0) && mist_cfg.joystick0_prefer_db9 ? 1 : idx;

		user_io_digital_joystick(idx, vjoy & 0xFF);
		// new API with all extra buttons
		user_io_digital_joystick_ext(idx, vjoy);

		virtual_joystick_keyboard( vjoy );

		dev->xbox_info.oldButtons = buttons;
	}
}

uint8_t usb_xbox_poll(usb_device_t *dev) {

	if(!dev->xbox_info.bPollEnable)
		return 0;
	if (dev->xbox_info.qNextPollTime <= timer_get_msec()) {
		uint16_t read = dev->xbox_info.inEp.maxPktSize;
		uint8_t buf[dev->xbox_info.inEp.maxPktSize];
		// clear buffer
		memset(buf, 0, dev->xbox_info.inEp.maxPktSize);
		uint8_t rcode = usb_in_transfer(dev, &(dev->xbox_info.inEp), &read, buf);
		if (rcode) {
			if (rcode != hrNAK)
				usb_debugf("%s() error: %d", __FUNCTION__, rcode);
		} else {
			usb_xbox_read_report(dev, read, buf);
		}
		dev->xbox_info.qNextPollTime = timer_get_msec() + dev->xbox_info.interval;   // poll at requested rate
	}
	return 0;
}

const usb_device_class_config_t usb_xbox_class = {
  usb_xbox_init, usb_xbox_release, usb_xbox_poll };
