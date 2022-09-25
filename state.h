#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include <inttypes.h>

void StateReset();

//// type definitions ////

typedef struct {
	uint16_t vid;							// USB vendor ID
	uint16_t pid;							// USB product ID
	uint8_t  num_buttons; 		// number of physical buttons reported by HID parsing
	uint8_t  state;   				// virtual joystick: current state of 4 direction + 4 first buttons
	uint8_t  state_extra;  		 // current state of 8 more buttons
	uint8_t  right;			 // right stick state
	uint8_t  usb_state;				// raw USB state of direction and buttons
	uint8_t  usb_state_extra; // raw USB state of 8 more buttons
	uint8_t  analogue[4];
	
} mist_joystick_t;



/*****
 * Various functions to retrieve hardware state from the State
 */

void StateReset();

 // USB raw data for joystick
void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num);
void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num_buttons, uint8_t joy_num);
uint8_t StateUsbJoyGet(uint8_t joy_num);
uint8_t StateUsbJoyGetExtra(uint8_t joy_num);
uint8_t StateUsbGetNumButtons(uint8_t joy_num);
uint16_t StateUsbVidGet(uint8_t joy_num);
uint16_t StateUsbPidGet(uint8_t joy_num);


// State of first (virtual) internal joystick i.e. after mapping
void StateJoySet(uint8_t c, uint8_t joy_num);
void StateJoySetExtra(uint8_t c, uint8_t joy_num);
void StateJoySetRight(uint8_t c, uint8_t joy_num);
uint8_t StateJoyGet(uint8_t joy_num);
uint8_t StateJoyGetExtra(uint8_t joy_num);
uint8_t StateJoyGetRight(uint8_t joy_num);

void StateJoySetAnalogue(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t joy_num);
uint8_t StateJoyGetAnalogue(uint8_t idx, uint8_t joy_num);
// Keep track of connected sticks
uint8_t StateNumJoysticks();
void StateNumJoysticksSet(uint8_t num);

// keyboard status
void StateKeyboardSet( uint8_t modifier, uint8_t* pressed, uint16_t* pressed_ps2); //get usb and ps2 codes
uint8_t StateKeyboardModifiers();
void StateKeyboardPressed(uint8_t *pressed);
void StateKeyboardPressedPS2(uint16_t *keycodes);

#endif

