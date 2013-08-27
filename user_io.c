#include "AT91SAM7S256.h"
#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "osd.h"

#include "user_io.h"
#include "usb.h"

#include "keycodes.h"
#include "ikbd.h"

typedef enum { EMU_NONE, EMU_MOUSE, EMU_JOY0, EMU_JOY1 } emu_mode_t;
static emu_mode_t emu_mode = EMU_NONE;
static unsigned char emu_state = 0;
static long emu_timer;
#define EMU_MOUSE_FREQ 5

static unsigned char core_type = CORE_TYPE_UNKNOWN;
static unsigned char adc_state = 0;

AT91PS_ADC a_pADC = AT91C_BASE_ADC;
AT91PS_PMC a_pPMC = AT91C_BASE_PMC;

static char caps_lock_toggle = 0;

static void PollOneAdc() {
  static unsigned char adc_cnt = 0xff;

  // fetch result from previous run
  if(adc_cnt != 0xff) {
    unsigned int result;

    // wait for end of convertion
    while(!(AT91C_BASE_ADC->ADC_SR & (1 << (4+adc_cnt))));
    
    switch (adc_cnt) {
    case 0: result = AT91C_BASE_ADC->ADC_CDR4; break;
    case 1: result = AT91C_BASE_ADC->ADC_CDR5; break;
    case 2: result = AT91C_BASE_ADC->ADC_CDR6; break;
    case 3: result = AT91C_BASE_ADC->ADC_CDR7; break;
    }
    
    if(result < 128) adc_state |=  (1<<adc_cnt);
    if(result > 128) adc_state &= ~(1<<adc_cnt);
  }
  
  adc_cnt = (adc_cnt + 1)&3;
  
  // Enable desired chanel
  AT91C_BASE_ADC->ADC_CHER = 1 << (4+adc_cnt);
  
  // Start conversion
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_START;
}

static void InitADC(void) {
  // Enable clock for interface
  AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_ADC;

  // Reset
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_SWRST;
  AT91C_BASE_ADC->ADC_CR = 0x0;

  // Set maximum startup time and hold time
  AT91C_BASE_ADC->ADC_MR = 0x0F1F0F00 | AT91C_ADC_LOWRES_8_BIT;

  // make sure we get the first values immediately
  PollOneAdc();
  PollOneAdc();
  PollOneAdc();
  PollOneAdc();
}

// poll one adc channel every 25ms
static void PollAdc() {
  static long adc_timer = 0;

  if(CheckTimer(adc_timer)) {
    adc_timer = GetTimer(25);
    PollOneAdc();
  }
}

void user_io_init() {
  InitADC();

  ikbd_init();
}

unsigned char user_io_core_type() {
  return core_type;
}

void user_io_detect_core_type() {
  EnableIO();
  core_type = SPI(0xff);
  DisableIO();

  if((core_type != CORE_TYPE_DUMB) &&
     (core_type != CORE_TYPE_MINIMIG) &&
     (core_type != CORE_TYPE_PACE) &&
     (core_type != CORE_TYPE_MIST))
    core_type = CORE_TYPE_UNKNOWN;

  switch(core_type) {
  case CORE_TYPE_UNKNOWN:
    puts("Unable to identify core!");
    break;
    
  case CORE_TYPE_DUMB:
    puts("Identified core without user interface");
    break;
    
  case CORE_TYPE_MINIMIG:
    puts("Identified Minimig core");
    break;
    
  case CORE_TYPE_PACE:
    puts("Identified PACE core");
    break;
    
  case CORE_TYPE_MIST:
    puts("Identified MiST core");
    break;
  }
}

void user_io_joystick(unsigned char joystick, unsigned char map) {
  if(core_type == CORE_TYPE_MINIMIG || core_type == CORE_TYPE_PACE) {
    EnableIO();
    SPI(UIO_JOYSTICK0 + joystick);
    SPI(map);
    DisableIO();
  }

  if(core_type == CORE_TYPE_MIST)
    ikbd_joystick(joystick, map);
}

void user_io_poll() {
  if((core_type != CORE_TYPE_MINIMIG) &&
     (core_type != CORE_TYPE_PACE) &&
     (core_type != CORE_TYPE_MIST)) {
    return;  // no user io for the installed core
  }

  if(core_type == CORE_TYPE_MIST) {
    ikbd_poll();

#if 1
    // check for incoming serial data
    EnableIO();
    SPI(UIO_SERIAL_IN);
    
    while(SPI(0))
      putchar(SPI(0));
    
    DisableIO();
#endif
  }

  // poll db9 joysticks
  static int joy0_state = JOY0;
  if((*AT91C_PIOA_PDSR & JOY0) != joy0_state) {
    joy0_state = *AT91C_PIOA_PDSR & JOY0;
    
    unsigned char joy_map = 0;
    if(!(joy0_state & JOY0_UP))    joy_map |= JOY_UP;
    if(!(joy0_state & JOY0_DOWN))  joy_map |= JOY_DOWN;
    if(!(joy0_state & JOY0_LEFT))  joy_map |= JOY_LEFT;
    if(!(joy0_state & JOY0_RIGHT)) joy_map |= JOY_RIGHT;
    if(!(joy0_state & JOY0_BTN1))  joy_map |= JOY_BTN1;
    if(!(joy0_state & JOY0_BTN2))  joy_map |= JOY_BTN2;

    user_io_joystick(0, joy_map);
  }
  
  static int joy1_state = JOY1;
  if((*AT91C_PIOA_PDSR & JOY1) != joy1_state) {
    joy1_state = *AT91C_PIOA_PDSR & JOY1;
    
    unsigned char joy_map = 0;
    if(!(joy1_state & JOY1_UP))    joy_map |= JOY_UP;
    if(!(joy1_state & JOY1_DOWN))  joy_map |= JOY_DOWN;
    if(!(joy1_state & JOY1_LEFT))  joy_map |= JOY_LEFT;
    if(!(joy1_state & JOY1_RIGHT)) joy_map |= JOY_RIGHT;
    if(!(joy1_state & JOY1_BTN1))  joy_map |= JOY_BTN1;
    if(!(joy1_state & JOY1_BTN2))  joy_map |= JOY_BTN2;
    
    user_io_joystick(1, joy_map);
  }

  // frequently poll the adc the switches 
  // and buttons are connected to
  PollAdc();
  
  static unsigned char key_map = 0;
  unsigned char map = 0;
  if(adc_state & 1) map |= SWITCH2;
  if(adc_state & 2) map |= SWITCH1;

  if(adc_state & 4) map |= BUTTON1;
  if(adc_state & 8) map |= BUTTON2;
  
  if(map != key_map) {
    key_map = map;
    
    EnableIO();
    SPI(UIO_BUT_SW);
    SPI(map);
    DisableIO();
  }

  // mouse movement emulation is continous 
  if(emu_mode == EMU_MOUSE) {
    if(CheckTimer(emu_timer)) {
      emu_timer = GetTimer(EMU_MOUSE_FREQ);
      
      if(emu_state & JOY_MOVE) {
	unsigned char b = 0;
	char x = 0, y = 0;
	if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_LEFT)  x = -1; 
	if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_RIGHT) x = +1; 
	if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_UP)    y = -1; 
	if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_DOWN)  y = +1; 
	
	if(emu_state & JOY_BTN1) b |= 1;
	if(emu_state & JOY_BTN2) b |= 2;
	
	user_io_mouse(b, x, y);
      }
    }
  }

  if(core_type == CORE_TYPE_MIST) {
    // do some tos specific monitoring here
    tos_poll();
  }
}

int user_io_dip_switch1() {
  return(adc_state & 2);
}

int user_io_menu_button() {
  return(adc_state & 4);
}

static void send_keycode(unsigned short code) {
  if(core_type == CORE_TYPE_MINIMIG) {
    EnableIO();
    if(code & OSD) SPI(UIO_KBD_OSD);   // code for OSD
    else           SPI(UIO_KEYBOARD);
    SPI(code & 0xff);
    DisableIO();
  }

  if(core_type == CORE_TYPE_MIST)
    ikbd_keyboard(code);
}

void user_io_mouse(unsigned char b, char x, char y) {

  // send mouse data as minimig expects it
  if(core_type == CORE_TYPE_MINIMIG) {
    EnableIO();
    SPI(UIO_MOUSE);
    SPI(x);
    SPI(y);
    SPI(b);
    DisableIO();
  }

  // send mouse data as mist expects it
  if(core_type == CORE_TYPE_MIST)
    ikbd_mouse(b, x, y);
}

// check if this is a key that's supposed to be suppressed
// when emulation is active
static unsigned char is_emu_key(unsigned char c) {
  static const unsigned char m[] = { JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP };

  if(emu_mode == EMU_NONE)
    return 0;

  // direction keys R/L/D/U
  if(c >= 0x4f && c <= 0x52)
    return m[c-0x4f];

  return 0;
}  

#define EMU_BTN1  0  // left control
#define EMU_BTN2  4  // right control

unsigned short keycode(unsigned char in) {
  if(core_type == CORE_TYPE_MINIMIG) 
    return usb2ami[in];

  if(core_type == CORE_TYPE_MIST) 
    return usb2atari[in];

  return MISS;
}

void check_reset(unsigned char modifiers)
{
	if(core_type==CORE_TYPE_MINIMIG){
		if(modifiers==0x45) // ctrl - alt - alt
			OsdReset(RESET_NORMAL);
	}
}

unsigned char modifier_keycode(unsigned char index) {
  /* usb modifer bits: 
        0     1     2    3    4     5     6    7
      LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  if(core_type == CORE_TYPE_MINIMIG) {
    static const unsigned char amiga_modifier[] = 
      { 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };
    return amiga_modifier[index];
  }

 if(core_type == CORE_TYPE_MIST) {
    static const unsigned char atari_modifier[] = 
      { 0x1d, 0x2a, 0x38, MISS, 0x1d, 0x36, 0x38, MISS };
    return atari_modifier[index];
  } 

  return MISS;
}

// unsigned char osdcode(unsigned char c) {
//   int i = 0;
//   while(usb2osd[i][0] && usb2osd[i][0] != c)
//     i++;
  
//   if(!usb2osd[i][0])
//     iprintf("ERROR: Unsupported OSD code %x!\n", c);
  
//   return usb2osd[i][1];
// }

// set by OSD code to suppress forwarding of those keys to the core which
// may be in use by an active OSD
static char osd_eats_keys = false;

void user_io_osd_key_enable(char on) {
  osd_eats_keys = on;
}

static char key_used_by_osd(unsigned short s) {
  if((s & OSD_LOC) && !(s & 0xff))  return true;   // this key is only used in OSD and has no keycode
  if(!osd_eats_keys)                return false;  // OSD currently doesn't use keyboard
  return((s & OSD_LOC) != 0);
}

void user_io_kbd(unsigned char m, unsigned char *k) {
  if((core_type == CORE_TYPE_MINIMIG) ||
     (core_type == CORE_TYPE_MIST)) {

    static unsigned char modifier = 0, pressed[6] = { 0,0,0,0,0,0 };
    int i, j;
    
    // modifier keys are used as buttons in emu mode
    if(emu_mode != EMU_NONE) {
      char last_btn = emu_state & (JOY_BTN1 | JOY_BTN2);
      if(m & (1<<EMU_BTN1)) emu_state |=  JOY_BTN1;
      else                  emu_state &= ~JOY_BTN1;
      if(m & (1<<EMU_BTN2)) emu_state |=  JOY_BTN2;
      else                  emu_state &= ~JOY_BTN2;
      
      // check if state of mouse buttons has changed
      if(last_btn != (emu_state & (JOY_BTN1 | JOY_BTN2))) {
	if(emu_mode == EMU_MOUSE) {
	  unsigned char b;
	  if(emu_state & JOY_BTN1) b |= 1;
	  if(emu_state & JOY_BTN2) b |= 2;
	  user_io_mouse(b, 0, 0);
	}
	
	if(emu_mode == EMU_JOY0) 
	  user_io_joystick(0, emu_state);
	
	if(emu_mode == EMU_JOY1) 
	  user_io_joystick(1, emu_state);
      }
    }
    
    // handle modifier keys
    if(m != modifier) {
      for(i=0;i<8;i++) {
	if((m & (1<<i)) && !(modifier & (1<<i)))	// Do we have a downstroke on a modifier key?
	{
	  check_reset(m);
	  // shift keys are used for mouse joystick emulation in emu mode
	  if(((i != EMU_BTN1) && (i != EMU_BTN2)) || (emu_mode == EMU_NONE))
	    if(modifier_keycode(i) != MISS)
	      send_keycode(modifier_keycode(i));
	}
	if(!(m & (1<<i)) && (modifier & (1<<i)))
	  if(((i != EMU_BTN1) && (i != EMU_BTN2)) || (emu_mode == EMU_NONE))
	    if(modifier_keycode(i) != MISS)
	      send_keycode(0x80 | modifier_keycode(i));
      }
      
      modifier = m;
    }
    
    // check if there are keys in the pressed list which aren't 
    // reported anymore
    for(i=0;i<6;i++) {
      unsigned short code = keycode(pressed[i]);
      
      if(pressed[i] && code != MISS) {
	for(j=0;j<6 && pressed[i] != k[j];j++);
	
	// don't send break for caps lock
	if(j == 6) {
	  // special OSD key handled internally 
	  //if(code & OSD_LOC) 
	    //OsdKeySet(0x80 | osdcode(pressed[i]));
    OsdKeySet(0x80 | usb2ami[pressed[i]]);

	  if(!key_used_by_osd(code)) {
	    if(is_emu_key(pressed[i])) {
	      emu_state &= ~is_emu_key(pressed[i]);
	    
	      if(emu_mode == EMU_JOY0) 
		user_io_joystick(0, emu_state);
	      
	      if(emu_mode == EMU_JOY1) 
		user_io_joystick(1, emu_state);
	    } else if(!(code & CAPS_LOCK_TOGGLE) &&
		      !(code & NUM_LOCK_TOGGLE))
	      send_keycode(0x80 | code);	
	  }
	}
      }  
    }
    
    for(i=0;i<6;i++) {
      unsigned short code = keycode(k[i]);

      if(k[i] && (k[i] <= KEYCODE_MAX) && code != MISS) {
	// check if this key is already in the list of pressed keys
	for(j=0;j<6 && k[i] != pressed[j];j++);

	if(j == 6) {
	  // special OSD key handled internally 
	  //if(code & OSD_LOC) 
	    //OsdKeySet(osdcode(k[i]));
    OsdKeySet(usb2ami[k[i]]); 

	  // no further processing of any key that is currently 
	  // redirected to the OSD
	  if(!key_used_by_osd(code)) {
	    if (is_emu_key(k[i])) {
	      emu_state |= is_emu_key(k[i]);
	    
	      if(emu_mode == EMU_JOY0) 
		user_io_joystick(0, emu_state);
	      
	      if(emu_mode == EMU_JOY1) 
		user_io_joystick(1, emu_state);
	    } else if(!(code & CAPS_LOCK_TOGGLE)&&
		      !(code & NUM_LOCK_TOGGLE)) 
	      send_keycode(code);
	    else {
	      if(code & CAPS_LOCK_TOGGLE) {
		// send alternating make and break codes for caps lock
		send_keycode((code & 0xff) | (caps_lock_toggle?0x80:0));
		caps_lock_toggle = !caps_lock_toggle;
		
		hid_set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
	      }
	      if(code & NUM_LOCK_TOGGLE) {
		// num lock has four states indicated by leds:
		// all off: normal
		// num lock on, scroll lock on: mouse emu
		// num lock on, scroll lock off: joy0 emu
		// num lock off, scroll lock on: joy1 emu
		
		if(emu_mode == EMU_MOUSE)
		  emu_timer = GetTimer(EMU_MOUSE_FREQ);
		
		emu_mode = (emu_mode+1)&3;
		if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY0) 
		  hid_set_kbd_led(HID_LED_NUM_LOCK, true);
		else
		  hid_set_kbd_led(HID_LED_NUM_LOCK, false);
		
		if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY1) 
		  hid_set_kbd_led(HID_LED_SCROLL_LOCK, true);
		else
		  hid_set_kbd_led(HID_LED_SCROLL_LOCK, false);
	      }
	    }
	  }
	}
      }
    }
    
  for(i=0;i<6;i++) 
    pressed[i] = k[i];
  }
}
