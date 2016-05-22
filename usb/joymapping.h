/*****************************************************************************/
// Handles Virtual Joystick functions (USB->Joystick mapping and Keyboard bindings)
/*****************************************************************************/


#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <stdbool.h>
#include <inttypes.h>

/*****************************************************************************/

// INI parsing
void virtual_joystick_remap_init(void);
void virtual_joystick_remap(char *);

// runtime mapping
uint16_t virtual_joystick_mapping (uint16_t vid, uint16_t pid, uint16_t joy_input);

/*****************************************************************************/

// INI parsing
void joystick_key_map_init(void);
void joystick_key_map(char *);

// runtime mapping
void virtual_joystick_keyboard ( uint16_t vjoy );

/*****************************************************************************/

#endif // JOYMAPPING_H
