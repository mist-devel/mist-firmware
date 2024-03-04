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

#include "qspi.h"
#include "hardware.h"

static uint8_t* dst;

void qspi_init() {
  PMC->PMC_PCER1 = (1 << (ID_QSPI0 - 32));
  QSPI0->QSPI_CR = QSPI_CR_QSPIEN;
  QSPI0->QSPI_MR = QSPI_MR_SMM; // serial memory mode
  QSPI0->QSPI_SCR = QSPI_SCR_CPOL | QSPI_SCR_SCBR(6);
}

void qspi_start_write() {
  QSPI0->QSPI_SCR = QSPI_SCR_CPOL | QSPI_SCR_SCBR((MCLK/24000000) - 1);
  QSPI0->QSPI_IAR = 0;
  QSPI0->QSPI_ICR = QSPI_ICR_INST(QSPI_WRITE);
  QSPI0->QSPI_IFR = QSPI_IFR_WIDTH_QUAD_CMD | QSPI_IFR_INSTEN | QSPI_IFR_DATAEN | QSPI_IFR_TFRTYP_TRSFR_WRITE;
  QSPI0->QSPI_IER = QSPI_IER_TDRE;
  dst = (uint8_t*)QSPIMEM0_ADDR;
  uint32_t dummy = QSPI0->QSPI_IFR;
}

void qspi_write(uint8_t data) {
  *dst++ = data;
}

void qspi_write_block(const uint8_t *data, uint32_t len) {

  XDMAC0->XDMAC_GD = XDMAC_GD_DI1;
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CC = XDMAC_CC_TYPE_MEM_TRAN
                                               | XDMAC_CC_MBSIZE_SINGLE
                                               | XDMAC_CC_DSYNC_MEM2PER
                                               | XDMAC_CC_CSIZE_CHK_1
                                               | XDMAC_CC_DWIDTH_BYTE
                                               | XDMAC_CC_SIF_AHB_IF1
                                               | XDMAC_CC_DIF_AHB_IF1
                                               | XDMAC_CC_SAM_INCREMENTED_AM
                                               | XDMAC_CC_DAM_INCREMENTED_AM
                                               | XDMAC_CC_PERID(5); // QSPI transmitter
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CDA = (uint32_t)dst;
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CSA = (uint32_t)data;
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CUBC = XDMAC_CUBC_UBLEN(len);
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CIS; //read interrupt reg to clear any flags prior to enabling channel
  XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CIE = XDMAC_CIE_BIE;
  // Start the transmitter
  XDMAC0->XDMAC_GE = XDMAC_GE_EN3;

  // Wait for end of transfer
  while (!(XDMAC0->XDMAC_CH[DMA_CH_QSPI_TRANS].XDMAC_CIS & XDMAC_CIS_BIS));
  dst += len;
}

void qspi_end() {
  QSPI0->QSPI_CR = QSPI_CR_LASTXFER;
  while (!(QSPI0->QSPI_SR & QSPI_SR_INSTRE));
}
