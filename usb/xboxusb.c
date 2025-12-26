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


static uint8_t usb_xbox_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
	uint8_t rcode;
	bool isGoodInterface = false;
	int initedEpCount = 0;
	usb_xbox_info_t *info = &dev->xbox_info;

	union buf_u {
	    usb_configuration_descriptor_t conf_desc;
	    usb_interface_descriptor_t iface_desc;
	    usb_endpoint_descriptor_t ep_desc;
	    uint8_t raw[len];
	} buf, *p;

	// usb_interface_descriptor
	if((rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)))
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
				if (p->iface_desc.bInterfaceClass == XBOX_INTERFACE_CLASS &&
						p->iface_desc.bInterfaceSubClass == XBOX_INTERFACE_SUBCLASS &&
						p->iface_desc.bInterfaceProtocol == XBOX_INTERFACE_PROTOCOL)
					isGoodInterface = true;
				else
					isGoodInterface = false;
				break;

			case USB_DESCRIPTOR_ENDPOINT:
				usb_dump_endpoint_descriptor(&p->ep_desc);
				if (isGoodInterface) {
					if ((p->ep_desc.bmAttributes & EP_TYPE_MSK) == EP_TYPE_INTR && (p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
						usb_debugf("in ep %d, interval = %dms", p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.bInterval);
						dev->xbox_info.interval = p->ep_desc.bInterval;
						dev->xbox_info.inEp.epAddr = p->ep_desc.bEndpointAddress & 0x0F;
						dev->xbox_info.inEp.epType = p->ep_desc.bmAttributes & EP_TYPE_MSK;
						dev->xbox_info.inEp.maxPktSize = p->ep_desc.wMaxPacketSize[0];
						dev->xbox_info.inEp.bmNakPower = USB_NAK_NOWAIT;
						initedEpCount++;
					}
					else if ((p->ep_desc.bmAttributes & EP_TYPE_MSK) == EP_TYPE_INTR && (p->ep_desc.bEndpointAddress & 0x80) == 0x00) {
						usb_debugf("out ep %d", p->ep_desc.bEndpointAddress & 0x0F);
						dev->xbox_info.outEp.epAddr = p->ep_desc.bEndpointAddress & 0x0F;
						dev->xbox_info.outEp.epType = p->ep_desc.bmAttributes & EP_TYPE_MSK;
						dev->xbox_info.outEp.maxPktSize = p->ep_desc.wMaxPacketSize[0];
						dev->xbox_info.outEp.bmNakPower = USB_NAK_NOWAIT;
						initedEpCount++;
					}
				}
				break;

			default:
				usb_debugf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
		}

		// advance to next descriptor
		if (!p->conf_desc.bLength || p->conf_desc.bLength > len) break;
		len -= p->conf_desc.bLength;
		p = (union buf_u*)(p->raw + p->conf_desc.bLength);
	}

	if (len != 0) {
		usb_debugf("Config underrun: %d", len);
		return USB_ERROR_CONFIGURATION_SIZE_MISMATCH;
	}
	if (initedEpCount != 2) {
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}
	return 0;
}

uint8_t usb_xbox_init(usb_device_t *dev, usb_device_descriptor_t *dev_desc) {
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

	memset(&dev->xbox_info, 0, sizeof(dev->xbox_info));

	if((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &conf_desc)))
		return rcode;
	usb_dump_conf_descriptor(&conf_desc);
	if((rcode = usb_xbox_parse_conf(dev, 0, conf_desc.wTotalLength))) {
		iprintf("XBOX: invalid configuration (%d)\n", rcode);
		return rcode;
	}

	// Set Configuration Value
	if((rcode = usb_set_conf(dev, conf_desc.bConfigurationValue))) {
		iprintf("XBOX: error setting conf value (%d)\n", rcode);
		return rcode;
	}

	// Some controllers (like 8bitdo usb wireless adapter 2) require this message to finish initialization
	uint8_t led_cmd[] = {0x01, 0x03, 0x02}; // led command: 1 flashes, then on
	if((rcode = usb_out_transfer(dev, &dev->xbox_info.outEp, sizeof(led_cmd), led_cmd))) {
		iprintf("XBOX: error sending led_cmd message (%d)\n", rcode);
	}

	usb_debugf("add xbox joystick #%d", joystick_count());
	dev->xbox_info.jindex = joystick_add();
	dev->xbox_info.bPollEnable = true;
	return 0;
}

uint8_t usb_xbox_release(usb_device_t *dev) {
	usb_debugf("releasing xbox joystick #%d, renumbering", dev->xbox_info.jindex);
	joystick_release(dev->xbox_info.jindex);
	return 0;
}

static void usb_xbox_read_report(usb_device_t *dev, uint16_t len, uint8_t *buf) {
	if(!buf) return;
//	hexdump(buf, len, 0);
	if(buf[0] != 0x00 || buf[1] != 0x14) { // Check if it's the correct report - the controller also sends different status reports
		return;
	}

	// https://www.partsnotincluded.com/understanding-the-xbox-360-wired-controllers-usb-data/
	uint32_t buttons =
		((buf[2] & (1U<<0))? JOY_UP : 0) |
		((buf[2] & (1U<<1))? JOY_DOWN : 0) |
		((buf[2] & (1U<<2))? JOY_LEFT : 0) |
		((buf[2] & (1U<<3))? JOY_RIGHT : 0) |
		((buf[2] & (1U<<4))? JOY_START : 0) |
		((buf[2] & (1U<<5))? JOY_SELECT : 0) |
		((buf[2] & (1U<<6))? JOY_L3 : 0) |
		((buf[2] & (1U<<7))? JOY_R3 : 0) |
		((buf[3] & (1U<<0))? JOY_L : 0) |
		((buf[3] & (1U<<1))? JOY_R : 0) |
		((buf[3] & (1U<<4))? JOY_A : 0) |
		((buf[3] & (1U<<5))? JOY_B : 0) |
		((buf[3] & (1U<<6))? JOY_X : 0) |
		((buf[3] & (1U<<7))? JOY_Y : 0) ;

	// Handle triggers
	if(buf[4] > JOYSTICK_AXIS_MID) buttons |= JOY_L2;
	if(buf[5] > JOYSTICK_AXIS_MID) buttons |= JOY_R2;

	// Handle left and right stick (discard low order byte)
	uint8_t jmap = 0;
	if(((buf[7]+128) & 0xFF) < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
	if(((buf[7]+128) & 0xFF) > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
	if(((buf[9]+128) & 0xFF) < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_DOWN;
	if(((buf[9]+128) & 0xFF) > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_UP;
	buttons |= jmap;

	jmap = 0;
	if(((buf[11]+128) & 0xFF) < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
	if(((buf[11]+128) & 0xFF) > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
	if(((buf[13]+128) & 0xFF) < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_DOWN;
	if(((buf[13]+128) & 0xFF) > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_UP;
	buttons |= (jmap << 16);

	uint32_t vjoy = virtual_joystick_mapping(dev->vid, dev->pid, buttons);
	// add right stick (no remap)
	vjoy |= (jmap << 16);

	uint8_t idx = dev->xbox_info.jindex;
	StateUsbIdSet(dev->vid, dev->pid, 12, idx);
	StateUsbJoySet(buttons, buttons>>8, idx);
	StateJoySet(vjoy, idx);
	StateJoySetExtra(vjoy>>8, idx);
	StateJoySetAnalogue(buf[7], buf[9], buf[11], buf[13], idx);
	StateJoySetRight(jmap, idx);
	StateJoySetMenu((buf[3] & (1U<<2)), idx);

	// swap joystick 0 and 1 since 1 is the one.
	// used primarily on most systems (most = Amiga and ST...need to get rid of this)
	if(!mist_cfg.joystick_disable_swap || user_io_core_type() != CORE_TYPE_8BIT) {
		if(idx == 0)      idx = 1;
		else if(idx == 1) idx = 0;
	}
	// if real DB9 mouse is preffered, switch the id back to 1
	idx = (idx == 0) && mist_cfg.joystick0_prefer_db9 ? 1 : idx;

	if(buttons != dev->xbox_info.oldButtons) {
		user_io_digital_joystick(idx, vjoy & 0xFF);
		// new API with all extra buttons
		user_io_digital_joystick_ext(idx, vjoy);
		virtual_joystick_keyboard( vjoy );
		dev->xbox_info.oldButtons = buttons;
	}
	user_io_analog_joystick(idx, buf[7], ~buf[9], buf[11], ~buf[13]);
}

uint8_t usb_xbox_poll(usb_device_t *dev) {

	if(!dev->xbox_info.bPollEnable)
		return 0;
	if (timer_check(dev->xbox_info.qLastPollTime, dev->xbox_info.interval)) {
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
		dev->xbox_info.qLastPollTime = timer_get_msec();   // poll at requested rate
	}
	return 0;
}

const usb_device_class_config_t usb_xbox_class = {
  usb_xbox_init, usb_xbox_release, usb_xbox_poll };
