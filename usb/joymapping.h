/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*****************************************************************************/
// Handles Virtual Joystick functions (USB->Joystick mapping and Keyboard bindings)
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <stdbool.h>
#include <inttypes.h>

// VID of vendors who are consistent
#define VID_DAPTOR 		0x04D8
#define VID_RETROLINK 0x0079

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint16_t mapping[16];
    int      tag;
} joymapping_t;

/*****************************************************************************/

// INI parsing
void virtual_joystick_remap_init(char);
char virtual_joystick_remap(char *, char, int);

// add new mapping
void virtual_joystick_remap_update(joymapping_t*);
void virtual_joystick_tag_update(uint16_t vid, uint16_t pid, int newtag);

// runtime mapping
uint16_t virtual_joystick_mapping (uint16_t vid, uint16_t pid, uint16_t joy_input);

// name known joysticks
const char* get_joystick_name( uint16_t vid, uint16_t pid );

/*****************************************************************************/

// INI parsing
void joystick_key_map_init(void);
char joystick_key_map(char *, char, int);

// runtime mapping
bool virtual_joystick_keyboard ( uint16_t vjoy );

/*****************************************************************************/

#endif // JOYMAPPING_H
