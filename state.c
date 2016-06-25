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

static mist_joystick_t mist_joy[3] = { // 3rd one is dummy, used to store defaults
	{
		.vid = 0,
		.pid = 0,
		.num_buttons=1, // DB9 has 1 button
		.state=0,
		.state_extra=0,
		.usb_state=0,
		.usb_state_extra=0,
		.turbo=50,
		.turbo_counter=0,
		.turbo_mask=0x30,	 // A and B buttons		
		.turbo_state=0xFF  // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
		.num_buttons=1, // DB9 has 1 button
		.state=0,
		.state_extra=0,
		.usb_state=0,
		.usb_state_extra=0,
		.turbo=0,
		.turbo_counter=0,
		.turbo_mask=0x30, // A and B buttons		
		.turbo_state=0xFF // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
		.num_buttons=1, // DB9 has 1 button
		.state=0,
		.state_extra=0,
		.usb_state=0,
		.usb_state_extra=0,
		.turbo=0,
		.turbo_counter=0,
		.turbo_mask=0x30, // A and B buttons		
		.turbo_state=0xFF // flip state (0 or 1)
	}
};

void StateNumJoysticksSet(unsigned char num) {
	mist_joystick_t joy;
	//joysticks = num;
	if(1) { //joysticks<3) {
		//clear USB joysticks
		if(1) //joysticks<2)
			joy = mist_joy[0];
		else
			joy = mist_joy[1];
		joy.vid=0;
		joy.vid=0;
		joy.num_buttons=1;
		joy.state=0;
		joy.state_extra=0;
		joy.usb_state=0;
		joy.usb_state_extra=0;
	}
}

// state of MIST virtual joystick

mist_joystick_t StateJoyGet(uint8_t joy_num) {
		if(joy_num>1) return mist_joy[2]; 
		return mist_joy[joy_num];
}

void StateJoySet(unsigned char c, uint8_t joy_num) {
  if(joy_num>1) return;
	mist_joy[joy_num].state = c;
	if(c==0) StateTurboReset(joy_num); //clear turbo if no button pressed
}
void StateJoySetExtra(unsigned char c, uint8_t joy_num) {
  if(joy_num>1) return;
	mist_joy[joy_num].state_extra = c;
}

// raw state of USB controller

void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num) {
	if(joy_num>1) return;
	mist_joy[joy_num].usb_state = usbjoy;
	mist_joy[joy_num].usb_state_extra = usbextra;
}

/* connected HID information */
void StateUsbIdSet(unsigned int vid, unsigned int pid, unsigned int btn_count, uint8_t joy_num) {
	if(joy_num>1) return;
	mist_joy[joy_num].vid = vid;
	mist_joy[joy_num].pid = pid;
	mist_joy[joy_num].num_buttons = btn_count;
}

/* handle button's turbo timers */
void StateTurboUpdate(uint8_t joy_num) {
	if(joy_num>1) return;
	mist_joy[joy_num].turbo_counter += 1;
	if(mist_joy[joy_num].turbo_counter > mist_joy[joy_num].turbo) {
		mist_joy[joy_num].turbo_counter = 0;
		mist_joy[joy_num].turbo_state ^= mist_joy[joy_num].turbo_mask;
	}
}
/* reset all turbo timers and state */
void StateTurboReset(uint8_t joy_num) {
	if(joy_num>1) return;
	mist_joy[joy_num].turbo_counter = 0;
	mist_joy[joy_num].turbo_state = 0xFF;
}
/* set a specific turbo mask and timeout */
void StateTurboSet ( uint16_t turbo, uint16_t mask, uint8_t joy_num ) {
	if(joy_num>1) return;
	StateTurboReset(joy_num);
	mist_joy[joy_num].turbo = turbo;
	mist_joy[joy_num].turbo_mask = mask;
}
/* return Joy state including turbo settings */
uint8_t StateJoyState ( uint8_t joy_num ) {
	if(joy_num>1) return 0;
	uint8_t result = mist_joy[joy_num].state;
	result &=  mist_joy[joy_num].turbo_state;
	return result;
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
  joy_reset(mist_joy[0]);
  joy_reset(mist_joy[1]);
  joy_reset(mist_joy[2]);
}