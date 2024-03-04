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

#ifndef SPI_H
#define SPI_H

#include <inttypes.h>

#include "hardware.h"
#include "attrs.h"

/* main init functions */
void spi_init(void);
void spi_slow();
void spi_fast();
void spi_fast_mmc();
void spi_wait4xfer_end();
unsigned char spi_get_speed();
void spi_set_speed(unsigned char speed);

/* chip select functions */
void EnableFpga(void);
void DisableFpga(void);
void EnableOsd(void);
void DisableOsd(void);
void EnableDMode();
void DisableDMode();
void EnableCard();
void DisableCard();

/* generic helper */
unsigned char spi_in();
void spi8(unsigned char parm);
void spi16(unsigned short parm);
void spi16le(unsigned short parm);
void spi24(unsigned long parm);
void spi32(unsigned long parm);
void spi32le(unsigned long parm);
void spi_n(unsigned char value, unsigned short cnt);

/* block transfer functions */
void spi_block_read(char *addr);
void spi_read(char *addr, uint16_t len);
void spi_block_write(const char *addr);
void spi_write(const char *addr, uint16_t len);
void spi_block(unsigned short num);

/* OSD related SPI functions */
void spi_osd_cmd_cont(unsigned char cmd);
void spi_osd_cmd(unsigned char cmd);
void spi_osd_cmd8_cont(unsigned char cmd, unsigned char parm);
void spi_osd_cmd8(unsigned char cmd, unsigned char parm);
void spi_osd_cmd32_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le(unsigned char cmd, unsigned long parm);

/* User_io related SPI functions */
void spi_uio_cmd_cont(unsigned char cmd);
void spi_uio_cmd(unsigned char cmd);
void spi_uio_cmd8(unsigned char cmd, unsigned char parm);
void spi_uio_cmd8_cont(unsigned char cmd, unsigned char parm);
void spi_uio_cmd32(unsigned char cmd, unsigned long parm);
void spi_uio_cmd64(unsigned char cmd, unsigned long long parm);
  
static inline unsigned char SPI(unsigned char outByte) {
  unsigned char b;
  while (!(SPI0->SPI_SR & SPI_SR_TDRE));
  SPI0->SPI_TDR = outByte;
  while (!(SPI0->SPI_SR & SPI_SR_RDRF));
  b = (unsigned char)SPI0->SPI_RDR;
  return(b);
}

void spi_max_start();
void spi_max_end();

#define SPI_SDC_CLK_VALUE MCLK/24000000  // 24 Mhz
#define SPI_MMC_CLK_VALUE MCLK/16000000  // 16 Mhz
#define SPI_SLOW_CLK_VALUE MCLK/600000   // 600kHz

#define SPI_MINIMIGV1_HACK

#endif // SPI_H
