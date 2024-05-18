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

#include "spi.h"
#include "hardware.h"

void spi_init()
{

    // Enable the peripheral clock in the PMC
    PMC->PMC_PCER0 = 1 << ID_SPI0;

    // Enable SPI interface
    SPI0->SPI_CR = SPI_CR_SPIEN;

    // SPI Mode Register
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS;

    // SPI CS register
    SPI0->SPI_CSR[0] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // OSD
    SPI0->SPI_CSR[1] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(15); // USB
    SPI0->SPI_CSR[2] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // CONF_DATA0
    SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // SS2
}

void spi_wait4xfer_end()
{
    while (!(SPI0->SPI_SR & SPI_SR_TXEMPTY));
}

void EnableFpga()
{
    SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // SS2
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS(0x7); // NPCS3
    SPI0->SPI_CR = SPI_CR_SPIEN;
}

void EnableFpgaMinimig()
{
    SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(2) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // SS2
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS(0x7); // NPCS3
    SPI0->SPI_CR = SPI_CR_SPIEN;
}

void DisableFpga()
{
    spi_wait4xfer_end();
    SPI0->SPI_CR = SPI_CR_SPIDIS;
}

void EnableOsd()
{
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS(0x0E); // NPCS0
    SPI0->SPI_CR = SPI_CR_SPIEN;
}

void DisableOsd()
{
    spi_wait4xfer_end();
    SPI0->SPI_CR = SPI_CR_SPIDIS;
}

void EnableIO()
{
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS(0x0B); // NPCS2
    SPI0->SPI_CR = SPI_CR_SPIEN;
}

void DisableIO()
{
    spi_wait4xfer_end();
    SPI0->SPI_CR = SPI_CR_SPIDIS;
}

void EnableDMode() {}

void DisableDMode() {}

void EnableCard() {}

void DisableCard() {}

void spi_max_start()
{
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS | SPI_MR_PCS(0x0D); // NPCS1
    SPI0->SPI_CR = SPI_CR_SPIEN;
}

void spi_max_end()
{
    spi_wait4xfer_end();
    SPI0->SPI_CR = SPI_CR_SPIDIS;
}

void spi_block(unsigned short num)
{
    unsigned short i;
    unsigned long t;

    for (i = 0; i < num; i++) {
        while (!(SPI0->SPI_SR & SPI_SR_TDRE)); // wait until transmiter buffer is empty
        SPI0->SPI_TDR = 0xFF; // write dummy spi data
    }
    while (!(SPI0->SPI_SR & SPI_SR_TXEMPTY)); // wait for transfer end
    t = SPI0->SPI_RDR; // dummy read to empty receiver buffer for new data
}


void spi_transfer(const char *srcAddr, char *dstAddr, uint16_t len)
{
    static uint32_t dummy __attribute__ ((aligned)) = 0xdeadbeaf;

    // Transmitter setup
    XDMAC0->XDMAC_GD = XDMAC_GD_DI1;
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CC = XDMAC_CC_TYPE_PER_TRAN
                                                | XDMAC_CC_MBSIZE_SINGLE
                                                | XDMAC_CC_DSYNC_MEM2PER
                                                | XDMAC_CC_CSIZE_CHK_1
                                                | XDMAC_CC_DWIDTH_BYTE
                                                | XDMAC_CC_SIF_AHB_IF1
                                                | XDMAC_CC_DIF_AHB_IF1
                                                | (!srcAddr ? XDMAC_CC_SAM_FIXED_AM : XDMAC_CC_SAM_INCREMENTED_AM)
                                                | XDMAC_CC_DAM_FIXED_AM
                                                | XDMAC_CC_PERID(1); // SPI0 transmitter
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CDA = (uint32_t)&(SPI0->SPI_TDR);
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CSA = (!srcAddr) ? (uint32_t)&dummy : (uint32_t)srcAddr;
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CUBC = XDMAC_CUBC_UBLEN(len);
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CIS; //read interrupt reg to clear any flags prior to enabling channel
    XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CIE = XDMAC_CIE_BIE;

    // Receiver setup
    XDMAC0->XDMAC_GD = XDMAC_GD_DI2;
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CC = XDMAC_CC_TYPE_PER_TRAN
                                              | XDMAC_CC_MBSIZE_SINGLE
                                              | XDMAC_CC_DSYNC_PER2MEM
                                              | XDMAC_CC_CSIZE_CHK_1
                                              | XDMAC_CC_DWIDTH_BYTE
                                              | XDMAC_CC_SIF_AHB_IF1
                                              | XDMAC_CC_DIF_AHB_IF1
                                              | XDMAC_CC_SAM_FIXED_AM
                                              | (!dstAddr ? XDMAC_CC_DAM_FIXED_AM : XDMAC_CC_DAM_INCREMENTED_AM)
                                              | XDMAC_CC_PERID(2);  // SPI0 receiver
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CSA = (uint32_t)&(SPI0->SPI_RDR);
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CDA = (!dstAddr) ? (uint32_t)&dummy : (uint32_t)dstAddr;
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CUBC = XDMAC_CUBC_UBLEN(len);
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CIS; //read interrupt reg to clear any flags prior to enabling channel
    XDMAC0->XDMAC_CH[DMA_CH_SPI_REC].XDMAC_CIE = XDMAC_CIE_BIE;

    // Start the transmitter-receiver
    XDMAC0->XDMAC_GE = XDMAC_GE_EN1 | XDMAC_GE_EN2;

    // Wait for end of transfer
    while (!(XDMAC0->XDMAC_CH[DMA_CH_SPI_TRANS].XDMAC_CIS & XDMAC_CIS_BIS));
}

void spi_read(char *addr, uint16_t len)
{
    spi_transfer(0, addr, len);
}

void spi_block_read(char *addr)
{
  spi_read(addr, 512);
}

void spi_write(const char *addr, uint16_t len)
{
    spi_transfer(addr, 0, len);
}

void spi_block_write(const char *addr)
{
  spi_write(addr, 512);
}

static unsigned char spi_speed;

void spi_slow()
{
  SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SLOW_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // init clock 100-400 kHz
  spi_speed = SPI_SLOW_CLK_VALUE;
}

void spi_fast()
{
  // set appropriate SPI speed for SD/SDHC card (max 25 Mhz)
  SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_SDC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // 24 MHz SPI clock
  spi_speed = SPI_SDC_CLK_VALUE;
}

void spi_fast_mmc()
{
  // set appropriate SPI speed for MMC card (max 20Mhz)
  SPI0->SPI_CSR[3] = SPI_CSR_CPOL | SPI_CSR_SCBR(SPI_MMC_CLK_VALUE) | SPI_CSR_DLYBCT(0) | SPI_CSR_CSAAT | SPI_CSR_DLYBS(10); // 16 MHz SPI clock
  spi_speed = SPI_MMC_CLK_VALUE;
}

unsigned char spi_get_speed()
{
  return spi_speed;
}

void spi_set_speed(unsigned char speed)
{
  switch (speed) {
    case SPI_SLOW_CLK_VALUE:
      spi_slow();
      break;

    case SPI_SDC_CLK_VALUE:
      spi_fast();
      break;

    default:
      spi_fast_mmc();
  }
}

/* generic helper */
unsigned char spi_in()
{
  return SPI(0);
}

void spi8(unsigned char parm)
{
  SPI(parm);
}

void spi16(unsigned short parm)
{
  SPI(parm >> 8);
  SPI(parm >> 0);
}

void spi16le(unsigned short parm)
{
  SPI(parm >> 0);
  SPI(parm >> 8);
}

void spi24(unsigned long parm)
{
  SPI(parm >> 16);
  SPI(parm >> 8);
  SPI(parm >> 0);
}

void spi32(unsigned long parm)
{
  SPI(parm >> 24);
  SPI(parm >> 16);
  SPI(parm >> 8);
  SPI(parm >> 0);
}

// little endian: lsb first
void spi32le(unsigned long parm)
{
  SPI(parm >> 0);
  SPI(parm >> 8);
  SPI(parm >> 16);
  SPI(parm >> 24);
}

void spi_n(unsigned char value, unsigned short cnt)
{
  while(cnt--) 
    SPI(value);
}

/* OSD related SPI functions */
void spi_osd_cmd_cont(unsigned char cmd)
{
  EnableOsd();
  SPI(cmd);
}

void spi_osd_cmd(unsigned char cmd)
{
  spi_osd_cmd_cont(cmd);
  DisableOsd();
}

void spi_osd_cmd8_cont(unsigned char cmd, unsigned char parm)
{
  EnableOsd();
  SPI(cmd);
  SPI(parm);
}

void spi_osd_cmd8(unsigned char cmd, unsigned char parm)
{
  spi_osd_cmd8_cont(cmd, parm);
  DisableOsd();
}

void spi_osd_cmd32_cont(unsigned char cmd, unsigned long parm)
{
  EnableOsd();
  SPI(cmd);
  spi32(parm);
}

void spi_osd_cmd32(unsigned char cmd, unsigned long parm)
{
  spi_osd_cmd32_cont(cmd, parm);
  DisableOsd();
}

void spi_osd_cmd32le_cont(unsigned char cmd, unsigned long parm)
{
  EnableOsd();
  SPI(cmd);
  spi32le(parm);
}

void spi_osd_cmd32le(unsigned char cmd, unsigned long parm)
{
  spi_osd_cmd32le_cont(cmd, parm);
  DisableOsd();
}

/* User_io related SPI functions */
void spi_uio_cmd_cont(unsigned char cmd)
{
  EnableIO();
  SPI(cmd);
}

void spi_uio_cmd(unsigned char cmd)
{
  spi_uio_cmd_cont(cmd);
  DisableIO();
}

void spi_uio_cmd8_cont(unsigned char cmd, unsigned char parm)
{
  EnableIO();
  SPI(cmd);
  SPI(parm);
}

void spi_uio_cmd8(unsigned char cmd, unsigned char parm)
{
  spi_uio_cmd8_cont(cmd, parm);
  DisableIO();
}

void spi_uio_cmd32(unsigned char cmd, unsigned long parm)
{
  EnableIO();
  SPI(cmd);
  SPI(parm);
  SPI(parm>>8);
  SPI(parm>>16);
  SPI(parm>>24);
  DisableIO();
}

void spi_uio_cmd64(unsigned char cmd, unsigned long long parm)
{
  EnableIO();
  SPI(cmd);
  SPI(parm);
  SPI(parm>>8);
  SPI(parm>>16);
  SPI(parm>>24);
  SPI(parm>>32);
  SPI(parm>>40);
  SPI(parm>>48);
  SPI(parm>>56);
  DisableIO();
}
