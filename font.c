#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat_compat.h"
#include "charrom.h"

unsigned char charfont[128][8];

char char_row(char c, char row) {
	char r=0;
	for(int i=0;i<8;i++) {
		r |= (((charfont[c & 0x7f][7-i]>>row) & 0x01)<<i);
	}
	return r;
}

void font_load() {
	memcpy(&charfont, &charrom, 128*8);

	FIL file;
	if(f_open(&file, "/SYSTEM.FNT", FA_READ) == FR_OK) {
		iprintf("Loading SYSTEM.FNT\n");
		unsigned char addr;
		if(f_size(&file) == 1024) {
			// full
			addr=0;
		} else if(f_size(&file) == 768) {
			//32-127
			addr=32;
		} else {
			iprintf("Invalid SYSTEM.FNT size!\n");
			f_close(&file);
			return;
		}

		// load and convert SYSTEM.FNT
		for(int s = 0; s < 2; s++) {
			FileReadBlock(&file, sector_buffer);
			for(int i = 0; i < 512; i++) {
				char row=i & 0x07;
				if (row == 0) for(int j = 0; j < 8; j++) charfont[addr][j] = 0;
				for(int j = 0; j < 8; j++) {
					charfont[addr][7-j] |= (((sector_buffer[i] >> j) & 0x01) << row);
				}

				if(row == 7) addr++;
				if(addr == 128) return;
			}
		}
		f_close(&file);
	}
}
