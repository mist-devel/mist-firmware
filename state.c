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
static unsigned char osd_joy;
static unsigned char osd_joy_extra;
void OsdJoySet(unsigned char c) {
  //iprintf("OSD joy: %x\n", c);
  osd_joy = c;
}
void OsdJoySetExtra(unsigned char c) {
  osd_joy_extra = c;
}
unsigned char OsdJoyGet() {
  return osd_joy;
}
unsigned char OsdJoyGetExtra() {
  return osd_joy_extra;
}
/* latest joystick state */
static unsigned char osd_joy2;
static unsigned char osd_joy_extra2;
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
void OsdUsbJoySet(uint8_t usbjoy, uint8_t usbextra) {
	raw_usb_joy = usbjoy;
	raw_usb_joy_extra = usbextra;
}
uint8_t OsdUsbJoyGet() {
	return raw_usb_joy;
}
uint8_t OsdUsbJoyGetExtra() {
	return raw_usb_joy_extra;
}

static uint8_t raw_usb_joy_b;	      // four directions and 4 buttons
static uint8_t raw_usb_joy_extra_b; // eight extra buttons
void OsdUsbJoySetB(uint8_t usbjoy, uint8_t usbextra) {
	raw_usb_joy_b = usbjoy;
	raw_usb_joy_extra_b = usbextra;
}
uint8_t OsdUsbJoyGetB() {
	return raw_usb_joy_b;
}
uint8_t OsdUsbJoyGetExtraB() {
	return raw_usb_joy_extra_b;
}

/* connected HID information */
static unsigned int usb_vid;
static unsigned int usb_pid;
static unsigned int num_buttons;
unsigned int OsdUsbVidGet() {
	return usb_vid;
}
unsigned int OsdUsbPidGet() {
	return usb_pid;
}
unsigned int OsdUsbGetNumButtons() {
	return num_buttons;
}

/* connected HID information - joy 2*/
static unsigned int usb_vid_b;
static unsigned int usb_pid_b;
static unsigned int num_buttons_b;
void OsdUsbIdSetB(unsigned int vid, unsigned int pid, unsigned int num) {
	usb_vid_b=vid;
	usb_pid_b=pid;
	num_buttons_b = num;
}
unsigned int OsdUsbVidGetB() {
	return usb_vid_b;
}
unsigned int OsdUsbPidGetB() {
	return usb_pid_b;
}
unsigned int OsdUsbGetNumButtonsB() {
	return num_buttons_b;
}


/* keyboard data */
static uint8_t key_modifier = 0;
static unsigned char key_pressed[6] = { 0,0,0,0,0,0 };

void StateKeyboardSet( unsigned char modifier, char* keycodes, int* keycodes_ps2) {
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
void StateKeyboardModifiers(uint8_t m) {
	m = key_modifier;
	return;
}
void StateKeyboardPressed(char *keycodes) {
	unsigned i=0;
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