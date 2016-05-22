/*  
This file defines how to handle mapping in the MiST controllers in various ways:

	1) USB input to internal "virtual joystick" (standardizes inputs)
	2) Virtual joystick to keyboard
	
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "timer.h"
#include "debug.h"
#include "../user_io.h"
#include "../mist_cfg.h"

// up to 8 buttons can be remapped
#define MAX_VIRTUAL_JOYSTICK_REMAP 8
#define MAX_JOYSTICK_KEYBOARD_MAP 16

/*****************************************************************************/
/*  Virtual joystick remap - custom parsing 
    The mapping translates directions plus generic HID buttons (1-12) into a standard MiST "virtual joystick"
*/

static struct {
    uint16_t vid;
    uint16_t pid;
    uint16_t mapping[16];
} joystick_mappers[MAX_VIRTUAL_JOYSTICK_REMAP];

static uint16_t default_joystick_mapping [16] = {
	JOY_RIGHT,
	JOY_LEFT,
	JOY_DOWN,
	JOY_UP,
	JOY_A,
	JOY_B,
	JOY_SELECT,
	JOY_START,
	JOY_X,
	JOY_Y,
	JOY_L,
	JOY_R,
	JOY_L2,
	JOY_R2,
	JOY_L3,
	JOY_R3 
};

void virtual_joystick_remap_init(void) {
  memset(joystick_mappers, 0, sizeof(joystick_mappers));
}

/* Parses an input comma-separated string into a mapping strucutre
   The string is expected to have the following format:  [VID],[PID],[comma separated list of buttons]
   and requires at least 13  characters in length 
*/
void virtual_joystick_remap(char *s) {
  
  uint8_t i;
  uint8_t count;
  uint8_t off = 0; 
  uint8_t len = strlen(s);
  uint16_t value = 0;
  char *token;
  char *sub_token;
  
  hid_debugf("%s(%s)", __FUNCTION__, s);
  
  if(len < 13) {
    hid_debugf("malformed entry");
    return;
  }
  
  // parse remap request
  for(i=0;i<MAX_VIRTUAL_JOYSTICK_REMAP;i++) {
    // fill sequentially the available mapping slots, stopping at first empty one
    if(!joystick_mappers[i].vid) {
	  // init mapping data
      for (count=0; count<16; count++)
        joystick_mappers[i].mapping[count]=0;
      // parse this first to stop if error - it will occupy a slot and proceed
	  value = strtol(s, NULL, 16); 
	  if (value==0) continue; // ignore zero entries
      joystick_mappers[i].vid = value;
      // default assignment for directions
      joystick_mappers[i].mapping[0] = JOY_RIGHT;
      joystick_mappers[i].mapping[1] = JOY_LEFT;
      joystick_mappers[i].mapping[2] = JOY_DOWN;
      joystick_mappers[i].mapping[3] = JOY_UP;
      count  = 0;
      token  = strtok (s, ",");
      while(token!=NULL) {
        //if (count==0) joystick_mappers[i].vid = strtol(token, NULL, 16); -- VID mapping already done
		value = strtol(token, NULL, 16);
        if (count==1) {
          joystick_mappers[i].pid = value;
        } 
        else {
            //parse sub-tokens sequentially and assign 16-bit value to them
            joystick_mappers[i].mapping[off+count-2] = value;
            hid_debugf("parsed: %x/%x %d -> %d", 
                      joystick_mappers[i].vid, joystick_mappers[i].pid,
                      count-1, joystick_mappers[i].mapping[off+count-2]);
        }
        token = strtok (NULL, ",");
        count+=1;
      }
      return; // finished processing input string so exit
    }
  }
}

/*****************************************************************************/

/* Translates USB input into internal virtual joystick,
   with some default handling for common/known gampads */

   uint16_t virtual_joystick_mapping (uint16_t vid, uint16_t pid, uint16_t joy_input) {
	
	uint8_t i;
	
	// defines translations between physical buttons and virtual joysticks
    uint16_t mapping[16];
	// keep directions by default
	for(i=0; i<4; i++) 
	   mapping[i]=default_joystick_mapping[i]; 
	// blank the rest
	for(i=4; i<16; i++) mapping[i]=0;

	uint8_t use_default=1;
	uint8_t btn_off = 3; // start at three since array is 0 based, so 4 = button 1
	
	// mapping for Qanba Q4RAF
	if( vid==0x0F30 && pid==0x1012) {
	  mapping[btn_off+1]  = JOY_A;
	  mapping[btn_off+2]  = JOY_B;
	  mapping[btn_off+4]  = JOY_A;
	  mapping[btn_off+3]  = JOY_B;     
	  mapping[btn_off+5]  = JOY_X; //for jump
	  mapping[btn_off+6]  = JOY_SELECT;
	  mapping[btn_off+8]  = JOY_SELECT;
	  mapping[btn_off+10] = JOY_START;       
	  use_default=0;
	}
	
	// mapping for no-brand cheap snes clone pad
	if(vid==0x081F && pid==0xE401) {
	  mapping[btn_off+2]  = JOY_A;
	  mapping[btn_off+3]  = JOY_B;
	  mapping[btn_off+1]  = JOY_B; // allow two ways to hold the controller  
	  mapping[btn_off+4]  = JOY_UP;
	  mapping[btn_off+5]  = JOY_L | JOY_L2; // also bind to buttons for flippers
	  mapping[btn_off+6]  = JOY_R | JOY_R2; 
	  mapping[btn_off+9]  = JOY_SELECT;
	  mapping[btn_off+10] = JOY_START;       
	  use_default=0;
	}
	
	// mapping for iBuffalo SNES pad - BSGP801
	if(vid==0x0583 && pid==0x2060) {
	  mapping[btn_off+1] = JOY_A;
	  mapping[btn_off+2] = JOY_B;
	  mapping[btn_off+3] = JOY_B;  // allow two ways to hold the controller
	  mapping[btn_off+4] = JOY_UP; 
	  mapping[btn_off+5] = JOY_L | JOY_L2; // also bind to buttons for flippers
	  mapping[btn_off+6] = JOY_R | JOY_R2;                 
	  mapping[btn_off+7] = JOY_SELECT;
	  mapping[btn_off+8] = JOY_START;
	  use_default=0;
	}
	
	//mapping for Buffalo NES pad - BGCFC801
	if(vid==0x0411 && pid==0x00C6) {
	  mapping[btn_off+1] = JOY_A;
	  mapping[btn_off+2] = JOY_B;
	  mapping[btn_off+3] = JOY_B;  // allow two ways to hold the controller
	  mapping[btn_off+4] = JOY_UP; 
	  mapping[btn_off+5] = JOY_L | JOY_L2; // also bind to buttons for flippers
	  mapping[btn_off+6] = JOY_R | JOY_R2;                     
	  mapping[btn_off+7] = JOY_SELECT;
	  mapping[btn_off+8] = JOY_START;
	  use_default=0;
	} 

	//mapping for RetroLink N64 and Gamecube pad (same vid/pid)
	if(vid==0x0079 && pid==0x0006) {
	  mapping[btn_off+7] = JOY_A;  // A on N64 pad
	  mapping[btn_off+9] = JOY_B;  // B on N64 pad
	  mapping[btn_off+3] = JOY_A;  // A on GC pad
	  mapping[btn_off+4] = JOY_B;  // B on GC pad
	  mapping[btn_off+5] = JOY_L | JOY_SELECT;
	  mapping[btn_off+8] = JOY_L | JOY_SELECT; // Z button on N64 pad
	  mapping[btn_off+6] = JOY_R | JOY_SELECT;
	  mapping[btn_off+10] = JOY_START;
	  use_default=0;
	}
	
	//mapping for ROYDS Stick.EX
	if(vid==0x1F4F && pid==0x0003) {
	  mapping[btn_off+3] = JOY_A;  // Circle (usually select in PSx)
	  mapping[btn_off+1] = JOY_B;  // Cross  (usually cancel in PSx)
	  mapping[btn_off+2] = JOY_X;  // Triangle
	  mapping[btn_off+4] = JOY_Y;  // Square
	  mapping[btn_off+5] = JOY_L;
	  mapping[btn_off+6] = JOY_R; 
	  mapping[btn_off+7] = JOY_L2; 
	  mapping[btn_off+8] = JOY_R2;
	  mapping[btn_off+9] = JOY_SELECT;
	  mapping[btn_off+10] = JOY_START;
	  use_default=0;
	}
	
	//mapping for NEOGEO-daptor
	if(vid==0x04D8 && pid==0xF421) {
	  mapping[btn_off+1] = JOY_B;  // red button "A" on pad (inverted order with NES/SNES
	  mapping[btn_off+2] = JOY_A;  // yellow button "B" on pad (inverted order with NES/SNES
	  mapping[btn_off+3] = JOY_Y | JOY_L;  // green button, "C" on pad (mapped to Y and L in SNES convention)
	  mapping[btn_off+4] = JOY_X | JOY_R;  // blue button "D"  
	  mapping[btn_off+5] = JOY_START;
	  mapping[btn_off+6] = JOY_SELECT;
	  use_default=0;
	}
	
	// apply remap information from mist.ini if present
	uint8_t j;
	for(j=0;j<MAX_VIRTUAL_JOYSTICK_REMAP;j++) {
	  if(joystick_mappers[j].vid==vid && joystick_mappers[j].pid==pid) {
		for(i=0; i<16; i++) 
		  mapping[i]=joystick_mappers[j].mapping[i];
		use_default=0;
	  }
	}
	
	// apply default mapping to rest of buttons if requested
	if (use_default) {
	  for(i=4; i<16; i++) 
		if (mapping[i]==0) mapping[i]=default_joystick_mapping[i];
	}
	
	uint16_t vjoy = 0;
	for(i=0; i<16; i++) 
	  if (joy_input & (0x01<<i))  vjoy |= mapping[i];
  
  return vjoy;
}

