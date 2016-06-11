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
#include "joymapping.h"
#include "../user_io.h"
#include "../mist_cfg.h"

// up to 8 buttons can be remapped
#define MAX_VIRTUAL_JOYSTICK_REMAP 8
#define MAX_JOYSTICK_KEYBOARD_MAP 16

/*****************************************************************************\
   Virtual joystick remap - custom parsing 
   The mapping translates directions plus generic HID buttons (1-12) into a sandard MiST "virtual joystick"
\****************************************************************************/

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

char* get_joystick_alias( uint16_t vid, uint16_t pid ) {
	
	if(vid==0x0F30 && pid==0x1012)
		return JOYSTICK_ALIAS_QANBA_Q4RAF;
	
	if(vid==0x081F && pid==0xE401)
		return JOYSTICK_ALIAS_CHEAP_SNES;
	
	if(vid==0x0583 && pid==0x2060)
		return JOYSTICK_ALIAS_IBUFALLO_SNES;
	
	if(vid==0x0411 && pid==0x00C6) 
		return JOYSTICK_ALIAS_IBUFALLO_SNES;
	
	if (vid==VID_RETROLINK && pid==0x0006)
		return JOYSTICK_ALIAS_RETROLINK_GC;
	
	if (vid==VID_RETROLINK && pid==0x0011)
		return JOYSTICK_ALIAS_RETROLINK_NES;
	
	if(vid==0x1F4F && pid==0x0003) 
		return JOYSTICK_ALIAS_ROYDS_EX;

	if(vid==VID_DAPTOR && pid==0xF947)
		return JOYSTICK_ALIAS_ATARI_DAPTOR2;
	
	if(vid==VID_DAPTOR && pid==0xF421)
		return JOYSTICK_ALIAS_NEOGEO_DAPTOR;
	
	if(vid==VID_DAPTOR && pid==0xF6EC)
		return JOYSTICK_ALIAS_NEOGEO_DAPTOR;
	
	if(vid==VID_DAPTOR && pid==0xF672)
		return JOYSTICK_ALIAS_VISION_DAPTOR;
		
	if(vid==0x1345 && pid==0x1030)
		return JOYSTICK_ALIAS_RETRO_FREAK;
	
	if(vid==0x1235 && pid==0xab21)
		return JOYSTICK_ALIAS_8BITDO_SFC30;
	
	if(vid==0x040b && pid==0x6533)
		return JOYSTICK_ALIAS_SPEEDLINK_COMP;
	
	return JOYSTICK_ALIAS_NONE;
		
}

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
	if(vid==VID_RETROLINK && pid==0x0006) {
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
	if(vid==VID_DAPTOR && pid==0xF421) {
	  mapping[btn_off+1] = JOY_B;  // red button "A" on pad (inverted order with NES/SNES
	  mapping[btn_off+2] = JOY_A;  // yellow button "B" on pad (inverted order with NES/SNES
	  mapping[btn_off+3] = JOY_Y | JOY_L;  // green button, "C" on pad (mapped to Y and L in SNES convention)
	  mapping[btn_off+4] = JOY_X | JOY_R;  // blue button "D"  
	  mapping[btn_off+5] = JOY_START;
	  mapping[btn_off+6] = JOY_SELECT;
	  use_default=0;
	}
	
	//mapping for 8bitdo SFC30
	if(vid==0x1235 && pid==0xab21) {
		mapping[btn_off+1] = JOY_A;
	  mapping[btn_off+2] = JOY_B;
	  //mapping[btn_off+3] // physical button #3 not used
		mapping[btn_off+4] = JOY_X;
		mapping[btn_off+5] = JOY_Y;
	  //mapping[btn_off+6] // physical button #6 not used
		mapping[btn_off+7] = JOY_L | JOY_L2; // also bind to buttons for flippers
	  mapping[btn_off+8] = JOY_R | JOY_R2; // also bind to buttons for flippers
	  //9 and 10 not used
		mapping[btn_off+11] = JOY_SELECT;
		mapping[btn_off+12] = JOY_START;
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

/*****************************************************************************\
   Virtual joystick to Keyboard mapping
   binds different button states of internal joypad to verious key combinations
\****************************************************************************/

/*****************************************************************************/

/*  Custom parsing for joystick->keyboard map
    We bind a bitmask of the virtual joypad with a keyboard USB code 
*/

static struct {
    uint16_t mask;
    uint8_t modifier;
    uint8_t keys[6];  // support up to 6 key codes
} joy_key_map[MAX_JOYSTICK_KEYBOARD_MAP];

void joy_key_map_init(void) {
  memset(joy_key_map, 0, sizeof(joy_key_map));
}


void joystick_key_map(char *s) {
  uint8_t i,j;
  uint8_t count;
  uint8_t assign=0;
  uint8_t len = strlen(s);
  uint8_t scancode=0;
  char *token;
  
  hid_debugf("%s(%s)", __FUNCTION__, s);
  
  if(len < 3) {
    hid_debugf("malformed entry");
    return;
  }
  
  // parse remap request
  for(i=0;i<MAX_JOYSTICK_KEYBOARD_MAP;i++) {
    // fill sequentially the available mapping slots, stopping at first empty one
    if(!joy_key_map[i].mask) {
      joy_key_map[i].modifier = 0;
      for(j=0;j<6;j++)
        joy_key_map[i].keys[j] = 0;
      count  = 0;
      token  = strtok (s, ",");
      while(s) {
        if (count==0) {
          joy_key_map[i].mask = strtol(s, NULL, 16);
        } else {
          scancode = strtol(s, NULL, 16);
          // set as modifier if scancode is on the relevant range (224 to 231)
          if(scancode>223) {
            //  bit  0     1      2    3    4     5      6    7
            //  key  LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
            //
            scancode -= 223;
            joy_key_map[i].modifier |= (0x01 << (scancode-1));
          } else {
            // max 6 keys
            if (assign < 7)
              joy_key_map[i].keys[assign++] = scancode;
          }
        }
        s = strtok (NULL, ",");
        count+=1;
      }
      return; // finished processing input string so exit
    }
  }
}

/*****************************************************************************/

void virtual_joystick_keyboard ( uint16_t vjoy ) {
	
  // ignore if globally switched off
  if(mist_cfg.joystick_disable_shortcuts)
	  return;
	
	// use button combinations as shortcut for certain keys
  uint8_t buf[6] = { 0,0,0,0,0,0 };
	
  // if OSD is open control it via USB joystick
  if(user_io_osd_is_visible() && !mist_cfg.joystick_ignore_osd) {
		int idx = 0;
		if(vjoy & JOY_A)     buf[idx++] = 0x28; // ENTER
		if(vjoy & JOY_B)     buf[idx++] = 0x29; // ESC                
		if(vjoy & JOY_START) buf[idx++] = 0x45; // F12
		if(vjoy & JOY_LEFT)  buf[idx++] = 0x50; // left arrow
		if(vjoy & JOY_RIGHT) buf[idx++] = 0x4F; // right arrow     
		
		// up and down uses SELECT or L for faster scrolling
		
		if(vjoy & JOY_UP) {
			if (vjoy & JOY_SELECT || vjoy & JOY_L) buf[idx] = 0x4B; // page up
			else buf[idx] = 0x52; // up arrow
			if (idx < 6) idx++; //avoid overflow if we assigned 6 already
		} 
		if(vjoy & JOY_DOWN) {
			if (vjoy & JOY_SELECT || vjoy & JOY_L) buf[idx] = 0x4E; // page down
			else buf[idx] = 0x51; // down arrow
			if (idx < 6) idx++; //avoid overflow if we assigned 6 already
		}       
		
  } else {
		
		// shortcuts mapped if start is pressed (take priority)
		if (vjoy & JOY_START) {
			//iprintf("joy2key START is pressed\n");
			int idx = 0;
			if(vjoy & JOY_A)       buf[idx++] = 0x28; // ENTER 
			if(vjoy & JOY_B)       buf[idx++] = 0x2C; // SPACE
			if(vjoy & JOY_L)       buf[idx++] = 0x29; // ESC
			if(vjoy & JOY_R)       buf[idx++] = 0x3A; // F1
			if(vjoy & JOY_SELECT)  buf[idx++] = 0x45;  //F12 // i.e. open OSD in most cores
		} else {
	
			// shortcuts with SELECT - mouse emulation
			if (vjoy & JOY_SELECT) {
	  		//iprintf("joy2key SELECT is pressed\n");
				unsigned char but = 0;
				char a0 = 0;
				char a1 = 0;
				if (vjoy & JOY_L)   but |= 1;
				if (vjoy & JOY_R)   but |= 2;
				if (vjoy & JOY_LEFT) a0 = -4;
				if (vjoy & JOY_RIGHT) a0 = 4;
				if (vjoy & JOY_UP) a1 = -2;
				if (vjoy & JOY_DOWN) a1 = 2;
				user_io_mouse(but, a0, a1);
			}
	
		}
	}
   
	// process mapped keyboard commands from mist.ini
	uint8_t i, j, k, count=0;
	uint8_t mapped_hit = 0;
	uint8_t modifier = 0;
	uint8_t has_mapping = 0;
	//uint8_t joy_buf[6] = { 0,0,0,0,0,0 };
	for(i=0;i<MAX_JOYSTICK_KEYBOARD_MAP;i++) {
	  if(vjoy & joy_key_map[i].mask) {
			has_mapping = 1;
			//iprintf("joy2key:%d\n", joy_key_map[i].mask);
			if (joy_key_map[i].modifier) {
				modifier |= joy_key_map[i].modifier;
				mapped_hit=1;
				//iprintf("joy2key hit (modifier):%d\n", joy_key_map[i].modifier);
			}
			// only override up to 6 keys, 
			// and preserve overrides from further up this function
			k = 0;
			for (j=0; j<6; j++) {
				if(buf[j]!=0) k=j+1; //next index to assign
			}
			for (j=0; j<6; j++) {
				if (k>=6) break; // max keys reached
				if (joy_key_map[i].keys[j]) {
					buf[k++] = joy_key_map[i].keys[j];
					mapped_hit=1;
					//iprintf("joy2key hit:%d\n", joy_key_map[i].keys[j]);
				}
			}
	  }
	}
	// generate key events but only if no other keys were pressed
	if (has_mapping && mapped_hit) {
		user_io_kbd(modifier, buf, UIO_PRIORITY_GAMEPAD); 
	} else {
		user_io_kbd(0x00, buf, UIO_PRIORITY_GAMEPAD); 
	}
}
