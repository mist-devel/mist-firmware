/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

This code keeps status of MiST state

*/

#include <string.h>
#include <stdio.h>

#include "state.h"
#include "osd.h"


// for I/O
static mist_joystick_t mist_joystick_temp;

#define joy_init { \
	.vid = 0, \
	.pid = 0, \
	.num_buttons=1, \
	.state=0 , \
	.state_extra=0, \
	.right=0, \
	.usb_state=0, \
	.usb_state_extra=0, \
	.analogue={0,0,0,0}, \
	.menu_button=0, \
	}

/* latest joystick state */
static mist_joystick_t mist_joysticks[7] = { // 7th one is dummy, used to store defaults
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	joy_init
};


// De-init all joysticks, useful when changing core
void StateReset() {
	uint8_t idx;

	for(idx=0; idx<6; idx++) {
		StateJoySet(0, idx);
		StateJoySetExtra(0, idx);
		StateJoySetRight(0, idx);
		StateJoySetAnalogue(0, 0, 0, 0, idx);
		StateJoySetMenu(0, idx);
		StateUsbIdSet(0, 0, 0, idx);
		StateUsbJoySet(0, 0, idx);
	}
}

/* latest joystick state */

void StateJoySet(uint8_t c, uint8_t joy_num) {
  //iprintf("OSD joy: %x\n", c);
	if (joy_num > 5) return;
	mist_joysticks[joy_num].state = c;
}
void StateJoySetExtra(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].state_extra = c;
}
void StateJoySetRight(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].right = c;
}
void StateJoySetAnalogue(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].analogue[0] = lx;
	mist_joysticks[joy_num].analogue[1] = ly;
	mist_joysticks[joy_num].analogue[2] = rx;
	mist_joysticks[joy_num].analogue[3] = ry;
}
static uint8_t mist_joystick_menu;
void StateJoySetMenu(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].menu_button = c;
}

uint8_t StateJoyGet(uint8_t joy_num) {
  return (joy_num < 6) ? mist_joysticks[joy_num].state : 0;
}
uint8_t StateJoyGetExtra(uint8_t joy_num) {
  return (joy_num < 6) ? mist_joysticks[joy_num].state_extra : 0;
}
uint8_t StateJoyGetRight(uint8_t joy_num) {
  return (joy_num < 6) ? mist_joysticks[joy_num].right : 0;
}
uint8_t StateJoyGetAnalogue(uint8_t idx, uint8_t joy_num) {
	return (joy_num < 6 && idx < 4) ? mist_joysticks[joy_num].analogue[idx] : 0;
}
uint8_t StateJoyGetMenu(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].menu_button : 0;
}
uint8_t StateJoyGetMenuAny() {
	uint8_t joy_num;
	for(joy_num=0; joy_num<6; joy_num++) {
		if (mist_joysticks[joy_num].menu_button)
			return mist_joysticks[joy_num].menu_button;
	}
	return 0;
}



void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].usb_state = usbjoy;
	mist_joysticks[joy_num].usb_state_extra = usbextra;
}

uint8_t StateUsbJoyGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].usb_state : 0;
}
uint8_t StateUsbJoyGetExtra(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].usb_state_extra : 0;
}


void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].vid = vid;
	mist_joysticks[joy_num].pid = pid;
	mist_joysticks[joy_num].num_buttons = num;
}
uint16_t StateUsbVidGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].vid : 0;
}
uint16_t StateUsbPidGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].pid : 0;
}
uint8_t StateUsbGetNumButtons(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].num_buttons : 0;
}

// Keep track of connected sticks
uint8_t joysticks=0;
uint8_t StateNumJoysticks() {
	return joysticks;
}

void StateNumJoysticksSet(uint8_t num) {
	joysticks = num;
}

/* keyboard data */
static uint8_t key_modifier = 0;
static uint8_t key_pressed[6] = { 0,0,0,0,0,0 };
static uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };

void StateKeyboardPressedPS2(uint16_t *keycodes) {
	unsigned i=0;
	for(i=0; i<6; i++) {
		keycodes[i]=keys_ps2[i];
	}
}
void StateKeyboardSet( uint8_t modifier, uint8_t* keycodes, uint16_t* keycodes_ps2) {
	unsigned i=0,j=0;
	key_modifier = modifier;
	for(i=0; i<6; i++) {
		//iprintf("Key N=%d, USB=%x, PS2=%x\n", i, keycodes[i], keycodes_ps2[i]);
		if(((keycodes[i]&0xFF) != 0xFF) && (keycodes[i]&0xFF)) {
			key_pressed[j]=keycodes[i];
			if((keycodes_ps2[i]&0xFF) != 0xFF ) {
				// translate EXT into 0E
				if(0x1000 & keycodes_ps2[i]) {
					keys_ps2[j++] = (keycodes_ps2[i]&0xFF) | 0xE000;
				} else {
					keys_ps2[j++] = keycodes_ps2[i]&0xFF;
				}
			} else {
				keys_ps2[j++] = 0;
			}
		}
	}

	while(j<6) {
		key_pressed[j]=0;
		keys_ps2[j++]=0;
	}
}

uint8_t StateKeyboardModifiers() {
	return key_modifier;
}
void StateKeyboardPressed(uint8_t *keycodes) {
	uint8_t i=0;
	for(i=0; i<6; i++) 
		keycodes[i]=key_pressed[i];
}
