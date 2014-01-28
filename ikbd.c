/*

  http://removers.free.fr/wikipendium/wakka.php?wiki=IntelligentKeyboardBible
  https://www.kernel.org/doc/Documentation/input/atarikbd.txt

  ikbd ToDo:

  Feature                      Example using/needing it    impl. tested
  ---------------------------------------------------------------------
  mouse y at bottom            Bolo                         X     X
  mouse button key events      Goldrunner/A_008             X     X
  joystick interrogation mode  Xevious/A_004                X     X
  Absolute mouse mode          Addicataball/A_050           X     X
  disable mouse                ?                            X
  disable joystick             ?                            X
  Joysticks also generate      Goldrunner                   X     X
  mouse button events!
  Pause (cmd 0x13)             Wings of Death/A_427
  pause/resume                 Gembench
  mouse keycode mode           
 */

#include <stdio.h>
#include <string.h>

#include "user_io.h"
#include "hardware.h"
#include "ikbd.h"
#include "debug.h"

// atari ikbd stuff
#define IKBD_STATE_JOYSTICK_EVENT_REPORTING    0x01
#define IKBD_STATE_MOUSE_Y_BOTTOM              0x02
#define IKBD_STATE_MOUSE_BUTTON_AS_KEY         0x04   // mouse buttons act like keys
#define IKBD_STATE_MOUSE_DISABLED              0x08
#define IKBD_STATE_MOUSE_ABSOLUTE              0x10
#define IKBD_STATE_MOUSE_ABSOLUTE_IN_PROGRESS  0x20
#define IKBD_STATE_WAIT4RESET                  0x40

#define IKBD_DEFAULT IKBD_STATE_JOYSTICK_EVENT_REPORTING

#define QUEUE_LEN 16    // power of 2!
static unsigned short tx_queue[QUEUE_LEN];
static unsigned char wptr = 0, rptr = 0;
static unsigned long ikbd_timer = 0;

// structure to keep track of ikbd state
static struct {
  unsigned char cmd;
  unsigned char state;
  unsigned char expect;

  // joystick state
  unsigned char joystick[2];
  unsigned long joy_timer[2];
  unsigned char joy_pending[2];
  
  unsigned char date_buffer[6];

  // mouse state
  unsigned short mouse_abs_max_x, mouse_abs_max_y;
  unsigned char  mouse_abs_scale_x, mouse_abs_scale_y;
  unsigned char  mouse_abs_buttons;
  unsigned short mouse_pos_x, mouse_pos_y;

  unsigned int   tx_cnt;   // tx byte counter for debugging
} ikbd;

void ikbd_init() {
  // reset ikbd state
  memset(&ikbd, 0, sizeof(ikbd));
  ikbd.state = IKBD_DEFAULT | IKBD_STATE_WAIT4RESET;

  ikbd.mouse_abs_max_x = ikbd.mouse_abs_max_y = 65535;
  ikbd.mouse_abs_scale_x = ikbd.mouse_abs_scale_y = 1;

  ikbd.joy_timer[0] = ikbd.joy_timer[1] = 0;
  ikbd.joy_pending[0] = ikbd.joy_pending[1] = 0;

  // init ikbd date
  ikbd.date_buffer[0] = 113;
  ikbd.date_buffer[1] = 7;
  ikbd.date_buffer[2] = 20;
  ikbd.date_buffer[3] = 20;
  ikbd.date_buffer[4] = 58;
  ikbd.date_buffer[5] = 0;
}

void ikbd_reset(void) {
  ikbd.tx_cnt = 0;
  ikbd.state |= IKBD_STATE_WAIT4RESET;
}

static void enqueue(unsigned short b) {
  if(((wptr + 1)&(QUEUE_LEN-1)) == rptr)
    return;

  tx_queue[wptr] = b;
  wptr = (wptr+1)&(QUEUE_LEN-1);
}

// convert internal joystick format into atari ikbd format
static unsigned char joystick_map2ikbd(unsigned in) {
  unsigned char out = 0;

  if(in & JOY_UP)    out |= 0x01;
  if(in & JOY_DOWN)  out |= 0x02;
  if(in & JOY_LEFT)  out |= 0x04;
  if(in & JOY_RIGHT) out |= 0x08;
  if(in & JOY_BTN1)  out |= 0x80;

  return out;
}

unsigned char bcd2bin(unsigned char in) {
  return 10*(in >> 4) + (in & 0x0f);
}

unsigned char bin2bcd(unsigned char in) {
  return 16*(in/10) + (in % 10);
}

// process inout from atari core into ikbd
void ikbd_handle_input(unsigned char cmd) {
  // expecting a second byte for command
  if(ikbd.expect) {
    ikbd.expect--;

    switch(ikbd.cmd) {
    case 0x07: // set mouse button action
      ikbd_debugf("mouse button action = %x", cmd);
      
      // bit 2: Mouse buttons act like keys (LEFT=0x74 & RIGHT=0x75)
      if(cmd & 0x04) ikbd.state |=  IKBD_STATE_MOUSE_BUTTON_AS_KEY;
      else           ikbd.state &= ~IKBD_STATE_MOUSE_BUTTON_AS_KEY;
      
      break;

    case 0x09:
      if(ikbd.expect == 3) ikbd.mouse_abs_max_x = ((unsigned short)cmd) << 8;
      if(ikbd.expect == 2) ikbd.mouse_abs_max_x = (ikbd.mouse_abs_max_x & 0xff00) | cmd;
      if(ikbd.expect == 1) ikbd.mouse_abs_max_y = ((unsigned short)cmd) << 8;
      if(ikbd.expect == 0) ikbd.mouse_abs_max_y = (ikbd.mouse_abs_max_y & 0xff00) | cmd;
      
      if(!ikbd.expect) {
	ikbd_debugf("new abs max = %u/%u", ikbd.mouse_abs_max_x, ikbd.mouse_abs_max_y);
	ikbd.state &= ~IKBD_STATE_MOUSE_DISABLED;
	ikbd.state |=  IKBD_STATE_MOUSE_ABSOLUTE;
	ikbd.mouse_abs_buttons = 2 | 8;
      }
      break;
      
    case 0x0e:
      if(ikbd.expect == 3) ikbd.mouse_pos_x = ((unsigned short)cmd) << 8;
      if(ikbd.expect == 2) ikbd.mouse_pos_x = (ikbd.mouse_pos_x & 0xff00) | cmd;
      if(ikbd.expect == 1) ikbd.mouse_pos_y = ((unsigned short)cmd) << 8;
      if(ikbd.expect == 0) ikbd.mouse_pos_y = (ikbd.mouse_pos_y & 0xff00) | cmd;
      
      if(!ikbd.expect) 
	ikbd_debugf("new abs pos = %u/%u", ikbd.mouse_pos_x, ikbd.mouse_pos_y);
      break;
      
    case 0x0c:
      if(ikbd.expect == 1) ikbd.mouse_abs_scale_x = cmd;
      if(ikbd.expect == 0) ikbd.mouse_abs_scale_y = cmd;
      
      if(!ikbd.expect) 
	ikbd_debugf("absolute scale = %u/%u", ikbd.mouse_abs_scale_x, ikbd.mouse_abs_scale_y);
      break;
      
    case 0x1b:
      ikbd.date_buffer[5-ikbd.expect] = bcd2bin(cmd);
      if(!ikbd.expect) {
	ikbd_debugf("time/date = %u:%u:%u %u.%u.%u", 
		    ikbd.date_buffer[3], ikbd.date_buffer[4], ikbd.date_buffer[5],
		    ikbd.date_buffer[2], ikbd.date_buffer[1], 1900 + ikbd.date_buffer[0]);
      }
      break;

    case 0x80: // ibkd reset
      // reply "everything is ok"
      enqueue(0x8000 + 100);   // wait 100ms
      enqueue(0xf1);
      break;
      
    default:
      break;
    }

    return;
  }

  ikbd.cmd = cmd;

  // Caution: Reacting on incomplete commands may cause problems. E.g. Revenge of Doh on Automation 3
  // sends an incomplete "set absolute mouse mode" command before the game starts. The game itself
  // runs in relative mouse mode. Switching to absolute mouse mode causes the game not to work
  // anymore.
  switch(cmd) {
  case 0x07:
    ikbd_debugf("Set mouse button action");
    ikbd.expect = 1;
    break;

  case 0x08:
    ikbd_debugf("Set relative mouse positioning");
    ikbd.state &= ~IKBD_STATE_MOUSE_DISABLED;
    ikbd.state &= ~IKBD_STATE_MOUSE_ABSOLUTE;
    break;

  case 0x09:
    ikbd_debugf("Set absolute mouse positioning");
    ikbd.expect = 4;
    break;

  case 0x0b:
    ikbd_debugf("Set Mouse threshold");
    ikbd.expect = 2;
    break;

  case 0x0c:
    ikbd_debugf("Set Mouse scale");
    ikbd.expect = 2;
    break;

  case 0x0d:
    //    ikbd_debugf("Interrogate Mouse Position");
    if((ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE) && 
       !(ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE_IN_PROGRESS)) {

      ikbd.state |= IKBD_STATE_MOUSE_ABSOLUTE_IN_PROGRESS;

      enqueue(0x8000 + 36);
      enqueue(0xf7);
      enqueue(ikbd.mouse_abs_buttons);
      enqueue(ikbd.mouse_pos_x >> 8);
      enqueue(ikbd.mouse_pos_x & 0xff);
      enqueue(ikbd.mouse_pos_y >> 8);
      enqueue(ikbd.mouse_pos_y & 0xff);
      enqueue(0x4000 + 0xf7);

      ikbd.mouse_abs_buttons = 0;
    }
    break;

  case 0x0e:
    ikbd_debugf("Load Mouse Position");
    ikbd.expect = 5;
    break;

  case 0x0f:
    ikbd_debugf("Set Y at bottom");
    ikbd.state |= IKBD_STATE_MOUSE_Y_BOTTOM;
    break;

  case 0x10:
    ikbd_debugf("Set Y at top");
    ikbd.state &= ~IKBD_STATE_MOUSE_Y_BOTTOM;
    break;

  case 0x12:
    ikbd_debugf("Disable mouse");
    ikbd.state |= IKBD_STATE_MOUSE_DISABLED;
    break;

  case 0x14:
    ikbd_debugf("Set Joystick event reporting");
    ikbd.state |= IKBD_STATE_JOYSTICK_EVENT_REPORTING;
    break;

  case 0x15:
    ikbd_debugf("Set Joystick interrogation mode");
    ikbd.state &= ~IKBD_STATE_JOYSTICK_EVENT_REPORTING;
    break;

  case 0x16: // interrogate joystick
    // send reply
    enqueue(0x8000 + 70);   // wait 70ms
    enqueue(0xfd);
    enqueue(joystick_map2ikbd(ikbd.joystick[0]));
    enqueue(joystick_map2ikbd(ikbd.joystick[1]));
    break;

  case 0x1a:
    ikbd_debugf("Disable joysticks");
    ikbd.state &= ~IKBD_STATE_JOYSTICK_EVENT_REPORTING;
    break;

  case 0x1b:
    ikbd_debugf("Time of day clock set");
    ikbd.expect = 6;
    break;

  case 0x1c:
    ikbd_debugf("Interrogate time of day");
    ikbd_debugf("time/date = %u:%u:%u %u.%u.%u", 
		ikbd.date_buffer[3], ikbd.date_buffer[4], ikbd.date_buffer[5],
		ikbd.date_buffer[2], ikbd.date_buffer[1], 1900 + ikbd.date_buffer[0]);

    enqueue(0x8000 + 64);   // wait 64ms
    enqueue(0xfc);
    {
      int i;
      for(i=0;i<6;i++)
	enqueue(bin2bcd(ikbd.date_buffer[i]));
    }
    break;
    

  case 0x80:
    ikbd_debugf("Reset");
    ikbd.expect = 1;
    ikbd.state = IKBD_DEFAULT;
    break;

  default:
    ikbd_debugf("unknown command: %x", cmd);
    break;
  }
}

void ikbd_poll(void) {
#ifdef IKBD_DEBUG
  static unsigned long xtimer = 0;
  static int last_cnt = 0;
  if(CheckTimer(xtimer)) {
    xtimer = GetTimer(2000);
    if(ikbd.tx_cnt != last_cnt) {
      ikbd_debugf("sent bytes: %d", ikbd.tx_cnt);
      last_cnt = ikbd.tx_cnt;
    }
  }
#endif

  // handle outstanding joystick events
  char i;
  for(i=0;i<2;i++) {
    // check if timeout is still running
    if((ikbd.joy_pending[i] & 0x40) && CheckTimer(ikbd.joy_timer[i]))
      ikbd.joy_pending[i] &= ~0x40;  // clear timeout flag
      
    if((ikbd.joy_pending[i] & 0xc0) == 0x80) {
      //      iprintf("delayed %d\n", i);
      ikbd_joystick(i, ikbd.joy_pending[i] & ~0xc0);
      ikbd.joy_pending[i] = 0;
    }
  }

  static unsigned long mtimer = 0;
  if(CheckTimer(mtimer)) {
    mtimer = GetTimer(10);
    
    // check for incoming ikbd data
    EnableIO();
    SPI(UIO_IKBD_IN);
    
    while(SPI(0))
      ikbd_handle_input(SPI(0));
    
    DisableIO();
  }

  // send data from queue if present
  if(rptr == wptr) return;

  // timer active?
  if(ikbd_timer) {
    if(!CheckTimer(ikbd_timer))
      return;

    //    ikbd_debugf("timer done");
    ikbd_timer = 0;
  }

  if(tx_queue[rptr] & 0xc000) {

    // request to start timer?
    if((tx_queue[rptr] & 0xc000) == 0x8000) 
      ikbd_timer = GetTimer(tx_queue[rptr] & 0x3fff);

    // cmd ack
    if((tx_queue[rptr] & 0xc000) == 0x4000)
      if((tx_queue[rptr] & 0xff) == 0xf7)
	ikbd.state &= ~IKBD_STATE_MOUSE_ABSOLUTE_IN_PROGRESS;

    rptr = (rptr+1)&(QUEUE_LEN-1);
    return;
  }

  // keep quiet as long has we haven't received a reset
  if(!(ikbd.state & IKBD_STATE_WAIT4RESET)) {
    //    ikbd_debugf("TX[%x]", tx_queue[rptr]);

    // transmit data from queue
    EnableIO();
    SPI(UIO_IKBD_OUT);
    SPI(tx_queue[rptr]);
    DisableIO();

    ikbd.tx_cnt++;
  }

  rptr = (rptr+1)&(QUEUE_LEN-1);  
}

void ikbd_joystick(unsigned char joystick, unsigned char map) {
  // todo: suppress events for joystick 0 as long as mouse
  // is enabled?
  
  if(ikbd.state & IKBD_STATE_JOYSTICK_EVENT_REPORTING) {

    // report rate is limited
    // check if it's already time for a new joystick report
    if(ikbd.joy_pending[joystick] & 0x40) {
      //    if(!CheckTimer(ikbd.joy_timer[joystick])) {
      //      iprintf("too fast on joy %d\n", joystick);

      // bit 7 marks this entry as valid
      ikbd.joy_pending[joystick] = 0xc0 | map;
      return;
    }

    // next report for this joystick earliest in 50 ms
    ikbd.joy_timer[joystick] = GetTimer(50);
    ikbd.joy_pending[joystick] = 0x40;   // 0x40 = "timeout is active" flag

    // only report joystick data for joystick 0 if the mouse is disabled
    if((ikbd.state & IKBD_STATE_MOUSE_DISABLED) || (joystick == 1)) {    
      //      iprintf("tx for %d - %x\n", joystick, joystick_map2ikbd(map));
      
      enqueue(0xfe + joystick);
      enqueue(joystick_map2ikbd(map));
    }
    
    if(!(ikbd.state & IKBD_STATE_MOUSE_DISABLED)) {
      // the fire button also generates a mouse event if 
      // mouse reporting is enabled
      if((map & JOY_BTN1) != (ikbd.joystick[joystick] & JOY_BTN1)) {
	// generate mouse event (ikbd_joystick_buttons is evaluated inside 
	// user_io_mouse)
	ikbd.joystick[joystick] = map;
	
	//	enqueue(0x8000 + 5); // some small pause in between
	ikbd_mouse(0, 0, 0);
      }
    }
  }
  
  // save state of joystick for interrogation mode
  ikbd.joystick[joystick] = map;
}

void ikbd_keyboard(unsigned char code) {
#ifdef IKBD_DEBUG
  ikbd_debugf("send keycode %x%s", code&0x7f, (code&0x80)?" BREAK":"");
#endif
  enqueue(code);
}

void ikbd_mouse(unsigned char b, char x, char y) {
  if(ikbd.state & IKBD_STATE_MOUSE_DISABLED)
    return;

  // joystick and mouse buttons are wired together in
  // atari st
  b |= (ikbd.joystick[0] & JOY_BTN1)?1:0;
  b |= (ikbd.joystick[1] & JOY_BTN1)?2:0;

  // save state for absolute mouse reports
  if(b & 2) ikbd.mouse_abs_buttons |= 1;
  else      ikbd.mouse_abs_buttons |= 2;
  if(b & 1) ikbd.mouse_abs_buttons |= 4;
  else      ikbd.mouse_abs_buttons |= 8;
  
  static unsigned char b_old = 0;
  // monitor state of two mouse buttons
  if(b != b_old) {
    // check if mouse buttons are supposed to be treated like keys
    if(ikbd.state & IKBD_STATE_MOUSE_BUTTON_AS_KEY) {
      // Mouse buttons act like keys (LEFT=0x74 & RIGHT=0x75)
      
      // handle left mouse button
      if((b ^ b_old) & 1) ikbd_keyboard(0x74 | ((b&1)?0x00:0x80));
      // handle right mouse button
      if((b ^ b_old) & 2) ikbd_keyboard(0x75 | ((b&2)?0x00:0x80));
    }
    b_old = b;
  }

#if 0
  if(ikbd.state & IKBD_STATE_MOUSE_BUTTON_AS_KEY) {
    b = 0;
    // if mouse position is 0/0 quit here
    if(!x && !y) return;
  }
#endif

  if(ikbd.state & IKBD_STATE_MOUSE_Y_BOTTOM)
    y = -y;

  if(ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE) {
    x /= ikbd.mouse_abs_scale_x;
    y /= ikbd.mouse_abs_scale_y;

    //    ikbd_debugf("abs inc %d %d -> ", x, y);

    if(x < 0) {
      x = -x;

      if(ikbd.mouse_pos_x > x) ikbd.mouse_pos_x -= x;
      else     	               ikbd.mouse_pos_x = 0;
    } else if(x > 0) {
      if(ikbd.mouse_pos_x < ikbd.mouse_abs_max_x - x)
	ikbd.mouse_pos_x += x;
      else
	ikbd.mouse_pos_x = ikbd.mouse_abs_max_x;
    }

    if(y < 0) {
      y = -y;
      if(ikbd.mouse_pos_y >  y) ikbd.mouse_pos_y -= y;
      else	                ikbd.mouse_pos_y = 0;
    } else if(y > 0) {
      if(ikbd.mouse_pos_y < ikbd.mouse_abs_max_y - y)
	ikbd.mouse_pos_y += y;
      else
	ikbd.mouse_pos_y = ikbd.mouse_abs_max_y;
    }

    //    ikbd_debugf("%d %d\n", ikbd.mouse_pos_x, ikbd.mouse_pos_y);
  } else {
    // atari has mouse button bits swapped
    enqueue(0xf8|((b&1)?2:0)|((b&2)?1:0));
    enqueue(x & 0xff);
    enqueue(y & 0xff);
  }
}

// advance the ikbd time by one second
void ikbd_update_time(void) {
  static const char mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  short year = 1900 + ikbd.date_buffer[0];
  char is_leap = (!(year % 4) && (year % 100)) || !(year % 400);

  //  ikbd_debugf("time update %u:%02u:%02u %u.%u.%u", 
  //	      ikbd.date_buffer[3], ikbd.date_buffer[4], ikbd.date_buffer[5],
  //	      ikbd.date_buffer[2], ikbd.date_buffer[1], year);

  // advance seconds
  ikbd.date_buffer[5]++;
  if(ikbd.date_buffer[5] == 60) {
    ikbd.date_buffer[5] = 0;

    // advance minutes
    ikbd.date_buffer[4]++;
    if(ikbd.date_buffer[4] == 60) {
      ikbd.date_buffer[4] = 0;

      // advance hours
      ikbd.date_buffer[3]++;
      if(ikbd.date_buffer[3] == 24) {
	ikbd.date_buffer[3] = 0;

	// advance days
	ikbd.date_buffer[2]++;
	if((ikbd.date_buffer[2] == mdays[ikbd.date_buffer[1]-1]+1) ||
	   (is_leap && (ikbd.date_buffer[1] == 2) && (ikbd.date_buffer[2] == 29))) {
	  ikbd.date_buffer[2] = 1;

	  // advance month
	  ikbd.date_buffer[1]++;
	  if(ikbd.date_buffer[1] == 13) {
	    ikbd.date_buffer[1] = 0;
	    
	    // advance year
	    ikbd.date_buffer[0]++;
	  }
	}
      }
    }
  }
}
