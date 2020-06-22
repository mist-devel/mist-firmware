#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "charrom.h"

unsigned char charfont[128][8];

void font_load() {
	memcpy(&charfont, &charrom, 128*8);

	fileTYPE file;
	if(FileOpen(&file,"SYSTEM  FNT")) {
		iprintf("Loading SYSTEM.FNT\n");
		unsigned char addr;
		if(file.size == 1024) {
			// full
			addr=0;
		} else if(file.size == 768) {
			//32-127
			addr=32;
		} else {
			iprintf("Invalid SYSTEM.FNT size!\n");
			return;
		}

		// load and convert SYSTEM.FNT
		for(int s = 0; s < 2; s++) {
			FileRead(&file, sector_buffer);
			for(int i = 0; i < 512; i++) {
				char row=i & 0x07;
				if (row == 0) for(int j = 0; j < 8; j++) charfont[addr][j] = 0;
				for(int j = 0; j < 8; j++) {
					charfont[addr][7-j] |= (((sector_buffer[i] >> j) & 0x01) << row);
				}

				if(row == 7) addr++;
				if(addr == 128) return;
			}
			FileNextSector(&file);
		}
	}
}
