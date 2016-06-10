/*****************************************************************************/
// Handles Virtual Joystick functions (USB->Joystick mapping and Keyboard bindings)
/*****************************************************************************/


#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <stdbool.h>
#include <inttypes.h>


#define JOYSTICK_ALIAS_NONE 					""
#define JOYSTICK_ALIAS_QANBA_Q4RAF 		"Qanba Q4RAF"
#define JOYSTICK_ALIAS_CHEAP_SNES 		"SNES Generic Pad"
#define JOYSTICK_ALIAS_IBUFALLO_SNES 	"iBuffalo SFC BSGP801"
#define JOYSTICK_ALIAS_IBUFALLO_NES 	"iBuffalo FC BGCFC801"
#define JOYSTICK_ALIAS_RETROLINK_GC 	"Retrolink N64/GC"
#define JOYSTICK_ALIAS_ROYDS_EX 			"ROYDS Stick.EX"
#define JOYSTICK_ALIAS_NEOGEO_DAPTOR 	"NEOGEO-daptor"
#define JOYSTICK_ALIAS_RETRO_FREAK 	  "Retro Freak gamepad"
#define JOYSTICK_ALIAS_8BITDO_SFC30 	"8BitDo SFC30"

/*****************************************************************************/

// INI parsing
void virtual_joystick_remap_init(void);
void virtual_joystick_remap(char *);

// runtime mapping
uint16_t virtual_joystick_mapping (uint16_t vid, uint16_t pid, uint16_t joy_input);

// name known joysticks
char* get_joystick_alias( uint16_t vid, uint16_t pid );

/*****************************************************************************/

// INI parsing
void joystick_key_map_init(void);
void joystick_key_map(char *);

// runtime mapping
void virtual_joystick_keyboard ( uint16_t vjoy );

/*****************************************************************************/

#endif // JOYMAPPING_H
