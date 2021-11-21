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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "usb.h"
#include "debug.h"
#include "../utils.h"
#include "../mist_cfg.h"
#include "../state.h"

static unsigned char joysticks = 0;      // number of detected usb joysticks

uint8_t joystick_count() {
	return joysticks;
}

uint8_t joystick_add() {
	StateNumJoysticksSet(joysticks+1);
	return joysticks++;
}

uint8_t joystick_index(uint8_t jindex) {
	// If DB9 joystick are preferred: USB joysticks are shifted to 2,3...
	if(mist_cfg.joystick_db9_fixed_index) {
		jindex += 2;
	}

	return jindex;
}

uint8_t joystick_release(uint8_t c_jindex) {
	// walk through all devices and search for sticks with a higher id

	// search for all joystick interfaces on all hid devices
	usb_device_t *dev = usb_get_devices();
	uint8_t j;
	for(j=0;j<USB_NUMDEVICES;j++) {
		if(dev[j].bAddress && (dev[j].class == &usb_hid_class)) {
			// search for joystick interfaces
			uint8_t k;
			for(k=0;k<MAX_IFACES;k++) {
				if(dev[j].hid_info.iface[k].device_type == HID_DEVICE_JOYSTICK) {
					uint8_t jindex = joystick_index(dev[j].hid_info.iface[k].jindex);
					if(jindex > c_jindex) {
						iprintf("decreasing joystick index of dev #%d from %d to %d\n", j,
							jindex, jindex-1);
						dev[j].hid_info.iface[k].jindex--;
						StateUsbIdSet( dev[j].vid, dev[j].pid, dev[j].hid_info.iface[k].conf.joystick_mouse.button_count, dev[j].hid_info.iface[k].jindex);
					}
				}
			}
		}
		if(dev[j].bAddress && (dev[j].class == &usb_xbox_class)) {
			uint8_t jindex = joystick_index(dev[j].xbox_info.jindex);
			if(jindex > c_jindex) {
				iprintf("Decreasing xbox index of dev #%d from %d to %d\n", j, jindex, jindex-1);
				dev[j].xbox_info.jindex--;
				StateUsbIdSet( dev[j].vid, dev[j].pid, 8/*button_count*/, dev[j].xbox_info.jindex);
			}
		}

	}
	// one less joystick in the system ...
	joysticks--;
	StateNumJoysticksSet(joysticks);
	if (joysticks < 6)
		StateUsbIdSet(0, 0, 0, joysticks);
}
