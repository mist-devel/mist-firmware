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