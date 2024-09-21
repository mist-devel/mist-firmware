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

#include <stdint.h>
#include <stdio.h>
#include "snes.h"
#include "fat_compat.h"
#include "debug.h"

enum HeaderField {
	CartName = 0x00,
	Mapper = 0x15,
	RomType = 0x16,
	RomSize = 0x17,
	RamSize = 0x18,
	CartRegion = 0x19,
	Company = 0x1a,
	Version = 0x1b,
	Complement = 0x1c,  //inverse checksum
	Checksum = 0x1e,
	ResetVector = 0x3c,
};

// From Main_MiSTer/support/snes/snes.cpp
static uint32_t score_header(FIL *file, uint32_t offset, uint32_t addr)
{
	int score = 0;
	UINT br;
	uint8_t *data = sector_buffer;

	snes_debugf("Header address: %08x offset: %d", addr, offset);

	if ((f_lseek(file, offset + addr) != FR_OK) ||
	    (f_tell(file) != (offset + addr)) ||
	    (f_read(file, data, 64, &br) != FR_OK)) {
		return 0;
	}

	uint16_t resetvector = data[ResetVector] | (data[ResetVector + 1] << 8);
	uint16_t checksum = data[Checksum] | (data[Checksum + 1] << 8);
	uint16_t complement = data[Complement] | (data[Complement + 1] << 8);

	snes_debugf("Reset vector: %04x, checksum: %04x, complement: %04x", resetvector, checksum, complement);

	//$00:[0000-7fff] contains uninitialized RAM and MMIO.
	//reset vector must point to ROM at $00:[8000-ffff] to be considered valid.
	if (resetvector < 0x8000) return 0;

	uint8_t resetop = 0;
	if (f_lseek(file, ((addr & ~0x7fff) | (resetvector & 0x7fff)) + offset ) != FR_OK) return 0;
	if (f_read(file, &resetop, 1, &br) != FR_OK) return 0;
	//uint8_t resetop = data[(addr & ~0x7fff) | (resetvector & 0x7fff)];  //first opcode executed upon reset
	uint8_t mapper = data[Mapper] & ~0x10;                      //mask off irrelevent FastROM-capable bit

	//some images duplicate the header in multiple locations, and others have completely
	//invalid header information that cannot be relied upon.
	//below code will analyze the first opcode executed at the specified reset vector to
	//determine the probability that this is the correct header.

	//most likely opcodes
	if (resetop == 0x78  //sei
		|| resetop == 0x18  //clc (clc; xce)
		|| resetop == 0x38  //sec (sec; xce)
		|| resetop == 0x9c  //stz $nnnn (stz $4200)
		|| resetop == 0x4c  //jmp $nnnn
		|| resetop == 0x5c  //jml $nnnnnn
		) score += 8;

	//plausible opcodes
	if (resetop == 0xc2  //rep #$nn
		|| resetop == 0xe2  //sep #$nn
		|| resetop == 0xad  //lda $nnnn
		|| resetop == 0xae  //ldx $nnnn
		|| resetop == 0xac  //ldy $nnnn
		|| resetop == 0xaf  //lda $nnnnnn
		|| resetop == 0xa9  //lda #$nn
		|| resetop == 0xa2  //ldx #$nn
		|| resetop == 0xa0  //ldy #$nn
		|| resetop == 0x20  //jsr $nnnn
		|| resetop == 0x22  //jsl $nnnnnn
		) score += 4;

	//implausible opcodes
	if (resetop == 0x40  //rti
		|| resetop == 0x60  //rts
		|| resetop == 0x6b  //rtl
		|| resetop == 0xcd  //cmp $nnnn
		|| resetop == 0xec  //cpx $nnnn
		|| resetop == 0xcc  //cpy $nnnn
		) score -= 4;

	//least likely opcodes
	if (resetop == 0x00  //brk #$nn
		|| resetop == 0x02  //cop #$nn
		|| resetop == 0xdb  //stp
		|| resetop == 0x42  //wdm
		|| resetop == 0xff  //sbc $nnnnnn,x
		) score -= 8;

	//at times, both the header and reset vector's first opcode will match ...
	//fallback and rely on info validity in these cases to determine more likely header.

	//a valid checksum is the biggest indicator of a valid header.
	if ((checksum + complement) == 0xffff && (checksum != 0) && (complement != 0)) score += 4;


	if (addr == 0x007fc0 && mapper == 0x20) score += 2;  //0x20 is usually LoROM
	if (addr == 0x00ffc0 && mapper == 0x21) score += 2;  //0x21 is usually HiROM
	if (addr == 0x007fc0 && mapper == 0x22) score += 2;  //0x22 is usually SDD1
	if (addr == 0x40ffc0 && mapper == 0x25) score += 2;  //0x25 is usually ExHiROM

	if (data[Company] == 0x33) score += 2;        //0x33 indicates extended header
	if (data[RomType] < 0x08) score++;
	if (data[RomSize] < 0x10) score++;
	if (data[RamSize] < 0x08) score++;
	if (data[CartRegion] < 14) score++;

	snes_debugf("Resetop: %02x mapper: %02x score: %d",
	    resetop, mapper, score);

	if (score < 0) score = 0;
	return score;
}

char snes_getromtype(FIL *file)
{
	uint32_t score_lo, score_hi, score_ex;
	UINT br;
	FSIZE_t size = f_size(file);
	FSIZE_t offset = size & 0x1ff;

	score_lo = score_header(file, offset, (FSIZE_t) 0x007fc0);
	score_hi = score_header(file, offset, (FSIZE_t) 0x00ffc0);
	score_ex = score_header(file, offset, (FSIZE_t) 0x40ffc0);
	f_rewind(file);

	if (score_ex) score_ex += 4;  //favor ExHiROM on images > 32mbits
	if (score_lo >= score_hi && score_lo >= score_ex) {
		iprintf("Detected LoROM\n");
		return 0; // probably LoROM
	}
	if (score_hi >= score_ex) {
		iprintf("Detected HiROM\n");
		return 1; // probably HiROM
	}
	if (score_ex) {
		iprintf("Detected ExHiROM\n");
		return 2; // probably ExHiROM
	}
	iprintf("No clue about ROM type\n");
	return 0; // no idea, fall back to LoROM
}
