#ifndef IDXFILE_H
#define IDXFILE_H

/*	Indexed file for fast random access on big files for e.g. hdd images
	Copyright (c) 2015 by Till Harbaum

	Contributed to the Minimig project, which is free software;
	you can redistribute it and/or modify it under the terms of
	the GNU General Public License as published by the Free Software Foundation;
	either version 3 of the License, or (at your option) any later version.

	Minimig is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fat_compat.h"

#define SZ_TBL 1024
#define SD_IMAGES 4

typedef struct
{
	char valid;
	FIL file;
	DWORD clmt[SZ_TBL];
} IDXFile;

// sd_image slots:
// Minimig:  0-3 - IDE
// Atari ST: 0-1 - FDD
//           2-3 - ACSI
// Archie:   0-1 - IDE
//           2-3 - FDD
// 8 bit:    0-3 - Block access

extern IDXFile sd_image[SD_IMAGES];

static inline unsigned char IDXRead(IDXFile *file, unsigned char *pBuffer, uint8_t blksz) {
  UINT br;
  return f_read(&(file->file), pBuffer, 512<<blksz, &br);
}

static inline unsigned char IDXWrite(IDXFile *file, unsigned char *pBuffer, uint8_t blksz) {
  UINT bw;
  return f_write(&(file->file), pBuffer, 512<<blksz, &bw);
}

unsigned char IDXOpen(IDXFile *file, const char *name, char mode);
void IDXClose(IDXFile *file);
unsigned char IDXSeek(IDXFile *file, unsigned long lba);
void IDXIndex(IDXFile *pIDXF);

#endif
