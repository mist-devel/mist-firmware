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
	uint8_t  usb_state;				// raw USB state of direction and buttons
	uint8_t  usb_state_extra; // raw USB state of 8 more buttons
	uint16_t turbo;				  	// 0 if disabled, otherwise max number to flip state
	uint16_t turbo_counter; 	// increased when using turbo, flips state when passing turbo
	uint8_t turbo_mask;				// buttons subject to turbo
	uint8_t turbo_state;			// current mask to apply
	
} mist_joystick_t;



/*****
 * Various functions to retrieve hardware state from the State
 */

 // USB raw data for joystick
void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num);
void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num_buttons, uint8_t joy_num);
uint8_t StateUsbJoyGet(uint8_t joy_num);
uint8_t StateUsbJoyGetExtra(uint8_t joy_num);
uint8_t StateUsbGetNumButtons(uint8_t joy_num);
uint16_t StateUsbVidGet(uint8_t joy_num);
uint16_t StateUsbPidGet(uint8_t joy_num);


// State of first (virtual) internal joystisk i.e. after mapping
void StateJoySet(uint8_t c, uint8_t joy_num);
void StateJoySetExtra(uint8_t c, uint8_t joy_num);
uint8_t StateJoyGet(uint8_t joy_num);
uint8_t StateJoyGetExtra(uint8_t joy_num);


 /*
mist_joystick_t StateJoyGet(uint8_t joy_num); // all data
uint8_t StateJoyState ( uint8_t joy_num );		// directions and 4 buttons, reflecting turbo settings

// Keep track of connected sticks
unsigned char StateNumJoysticks();
void StateNumJoysticksSet(unsigned char num);

// turbo function
/*
void StateTurboUpdate(uint8_t joy_num);
void StateTurboReset(uint8_t joy_num);
void StateTurboSet ( uint16_t turbo, uint16_t mask, uint8_t joy_num );
*/



// keyboard status
void StateKeyboardSet( unsigned char modifier, char* pressed, int* pressed_ps2); //get usb and ps2 codes
void StateKeyboardModifiers(uint8_t m);
void StateKeyboardPressed(char *pressed);
void StateKeyboardPressedPS2(unsigned int *keycodes);

// get/set core currently loaded
void StateCoreNameSet(const char* str);
char* StateCoreName();

#endif

