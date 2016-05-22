#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <stdbool.h>
#include <inttypes.h>

void virtual_joystick_remap_init(void);
void virtual_joystick_remap(char *);

uint16_t virtual_joystick_mapping (uint16_t vid, uint16_t pid, uint16_t joy_input);

#endif // JOYMAPPING_H
