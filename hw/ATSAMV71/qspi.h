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

#ifndef QSPI_H
#define QSPI_H

#include <inttypes.h>

#define QSPI_READ  0x40
#define QSPI_WRITE 0x41

void qspi_init();
void qspi_start_write();
void qspi_write(uint8_t data);
void qspi_write_block(const uint8_t *data, uint32_t len);
void qspi_end();

#endif // QSPI_H
