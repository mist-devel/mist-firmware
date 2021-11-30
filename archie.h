#ifndef ARCHIE_H
#define ARCHIE_H

#include "fat_compat.h"

void archie_init(void);
void archie_poll(void);
void archie_kbd(unsigned short code);
void archie_mouse(unsigned char b, char x, char y);
char *archie_get_rom_name(void);
char *archie_get_cmos_name(void);
char *archie_get_floppy_name(char b);
void archie_set_rom(const unsigned char *);
void archie_set_cmos(const unsigned char *);
void archie_set_floppy(char i, const unsigned char *);
void archie_save_config(void);
void archie_save_cmos(void);
void archie_setup_menu(void);

#endif // ARCHIE_H
