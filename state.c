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
#include "stdio.h"

#include "state.h"
#include "osd.h"
//#include "charrom.h"


/* latest joystick state */

void joy_reset ( mist_joystick_t joy ) {
	joy.vid = 0;
	joy.pid = 0;
	joy.num_buttons=1; // DB9 has 1 button
	joy.state=0;
	joy.state_extra=0;
	joy.usb_state=0;
	joy.usb_state_extra=0;
	joy.turbo=50;
	joy.turbo_counter=0;
	joy.turbo_mask=0x30;	 // A and B buttons		
	joy.turbo_state=0xFF;  // flip state (0 or 1)
}


/* latest joystick state */
static uint8_t osd_joy;
static uint8_t osd_joy_extra;
static uint8_t osd_joy2;
static uint8_t osd_joy_extra2;
void StateJoySet(uint8_t c, uint8_t joy_num) {
  //iprintf("OSD joy: %x\n", c);
	if(joy_num==0) 
		osd_joy = c;
	else
		osd_joy2 = c;
}
void StateJoySetExtra(uint8_t c, uint8_t joy_num) {
		if(joy_num==0) 
		osd_joy_extra = c;
	else
		osd_joy_extra = c;
}
uint8_t StateJoyGet(uint8_t joy_num) {
  return joy_num==0?osd_joy:osd_joy2;
}
uint8_t StateJoyGetExtra(uint8_t joy_num) {
  return joy_num==0?osd_joy_extra:osd_joy_extra2;
}
/* latest joystick state */

void OsdJoySet2(unsigned char c) {
  //iprintf("OSD joy 2: %x\n", c);
  osd_joy2 = c;
}
void OsdJoySetExtra2(unsigned char c) {
  osd_joy_extra2 = c;
}
unsigned char OsdJoyGet2() {
  return osd_joy2;
}
unsigned char OsdJoyGetExtra2() {
  return osd_joy_extra2;
}

static uint8_t raw_usb_joy;	      // four directions and 4 buttons
static uint8_t raw_usb_joy_extra; // eight extra buttons
static uint8_t raw_usb_joy_b;	      // four directions and 4 buttons
static uint8_t raw_usb_joy_extra_b; // eight extra buttons
void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num) {
	if(joy_num==0) {
		raw_usb_joy = usbjoy;
		raw_usb_joy_extra = usbextra;
	} else {
		raw_usb_joy_b = usbjoy;
		raw_usb_joy_extra_b = usbextra;
	}
}

uint8_t StateUsbJoyGet(uint8_t joy_num) {
	return (joy_num==0)?raw_usb_joy:raw_usb_joy_b;
}
uint8_t StateUsbJoyGetExtra(uint8_t joy_num) {
	return (joy_num==0)?raw_usb_joy_extra:raw_usb_joy_extra_b;
}

static uint16_t usb_vid;
static uint16_t usb_pid;
static uint8_t num_buttons;
static uint16_t usb_vid_b;
static uint16_t usb_pid_b;
static uint8_t num_buttons_b;
void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num, uint8_t joy_num) {
	if(joy_num==0) {
		usb_vid=vid;
		usb_pid=pid;
		num_buttons = num;
	} else {
		usb_vid_b=vid;
		usb_pid_b=pid;
		num_buttons_b = num;
	}
}
uint16_t StateUsbVidGet(uint8_t joy_num) {
	return joy_num==0?usb_vid:usb_vid_b;
}
uint16_t StateUsbPidGet(uint8_t joy_num) {
	return joy_num==0?usb_pid:usb_pid_b;
}
uint8_t StateUsbGetNumButtons(uint8_t joy_num) {
	return (joy_num==0)?num_buttons:num_buttons_b;
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
	unsigned i=0;
	key_modifier = modifier;
	for(i=0; i<6; i++) {
		if((keycodes[i]&0xFF) != 0xFF ) {
			key_pressed[i]=keycodes[i];
			if((keycodes_ps2[i]&0xFF) != 0xFF ) {
				//iprintf("PS2 keycode: %x\n", keycodes_ps2[i]);
				// translate EXT into 0E
				if(0x1000 & keycodes_ps2[i]) {
					//key_ps2[i] = keycodes_ps2[i]&0xFF | 0xE000;
				} else {
					//key_ps2[i] = keycodes_ps2[i]&0xFF;
				}
			} else {
				//key_ps2[i]=0;
			}
		}
		else {
			key_pressed[i]=0;
			//key_ps2[i]=0;
		}
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


/* core currently loaded */
static char lastcorename[261+10] = "CORE";
void StateCoreNameSet(const char* str) {
	siprintf(lastcorename, "%s", str);
}
char* StateCoreName() {
	return lastcorename;
}

// clear all states
void StateReset() {
	strcpy(lastcorename, "CORE");
	//State_key = 0;
	//joysticks=0;
	key_modifier = 0;
	for(int i=0; i<6; i++) {
		key_pressed[i]=0;
		//key_ps2[i]=0;
	}
  //joy_reset(mist_joy[0]);
  //joy_reset(mist_joy[1]);
  //joy_reset(mist_joy[2]);
}