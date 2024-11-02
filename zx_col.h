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

/*
 * zx_col.h
 * Open/parse .col (ZX 80/81 colorization) files
 *
 */

#ifndef COL_FILE_H
#define COL_FILE_H

#include "FatFs/ff.h"

int zx_col_load(FIL *fil, unsigned char *buf);
int zx_chr_load(FIL *fil, unsigned char *buf);

#endif // COL_FILE_H
