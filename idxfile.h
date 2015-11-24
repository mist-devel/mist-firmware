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

#include "fat.h"

typedef struct
{
	fileTYPE file;
        unsigned long  index[1024];
        unsigned long  index_size;
} IDXFile;

static inline unsigned char IDXRead(IDXFile *file, unsigned char *pBuffer) {
  return FileRead(&(file->file), pBuffer);
}

static inline unsigned char IDXWrite(IDXFile *file, unsigned char *pBuffer) {
  return FileWrite(&(file->file), pBuffer);
}
  
unsigned char IDXOpen(IDXFile *file, const char *name);
unsigned char IDXSeek(IDXFile *file, unsigned long lba);
void IDXIndex(IDXFile *pIDXF);

#endif
