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

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mmc.h"

// SD CMD6 argument structure
// CMD6 arg[ 3: 0] function group 1, access mode
#define SD_CMD6_GRP1_HIGH_SPEED     (0x1lu << 0)
#define SD_CMD6_GRP1_DEFAULT        (0x0lu << 0)
// CMD6 arg[ 7: 4] function group 2, command system
#define SD_CMD6_GRP2_NO_INFLUENCE   (0xFlu << 4)
#define SD_CMD6_GRP2_DEFAULT        (0x0lu << 4)
// CMD6 arg[11: 8] function group 3, 0xF or 0x0
#define SD_CMD6_GRP3_NO_INFLUENCE   (0xFlu << 8)
#define SD_CMD6_GRP3_DEFAULT        (0x0lu << 8)
// CMD6 arg[15:12] function group 4, 0xF or 0x0
#define SD_CMD6_GRP4_NO_INFLUENCE   (0xFlu << 12)
#define SD_CMD6_GRP4_DEFAULT        (0x0lu << 12)
// CMD6 arg[19:16] function group 5, 0xF or 0x0
#define SD_CMD6_GRP5_NO_INFLUENCE   (0xFlu << 16)
#define SD_CMD6_GRP5_DEFAULT        (0x0lu << 16)
// CMD6 arg[23:20] function group 6, 0xF or 0x0
#define SD_CMD6_GRP6_NO_INFLUENCE   (0xFlu << 20)
#define SD_CMD6_GRP6_DEFAULT        (0x0lu << 20)
// CMD6 arg[30:24] reserved 0^M
// CMD6 arg[31   ] Mode, 0: Check, 1: Switch
#define SD_CMD6_MODE_CHECK          (0lu << 31)
#define SD_CMD6_MODE_SWITCH         (1lu << 31)

// variables
static unsigned char crc;
static unsigned long timeout;
static unsigned char response;
static unsigned char CardType;
static uint16_t RCA;
static uint8_t  SCR[8];
static uint8_t  CSD[16];
static uint8_t  CID[16];
static uint8_t  switch_status[512/8];

// internal functions
static unsigned char MMC_WaitReady() RAMFUNC;
static unsigned char MMC_Command(unsigned char cmd, unsigned long arg, unsigned long flags) RAMFUNC;
static unsigned char MMC_AppCommand(unsigned char cmd, unsigned long arg, unsigned long flags);
static unsigned char MMC_PIORead(unsigned char *buffer, int len);
static unsigned char MMC_WaitTransferEnd() RAMFUNC;

RAMFUNC unsigned char MMC_CheckCard() {
  // check for removal of card
  if((CardType != CARDTYPE_NONE) && !mmc_inserted()) {
    CardType = CARDTYPE_NONE;
    return 0;
  }
  return 1;
}

RAMFUNC static char check_card() {
  // check of card has been removed and try to re-initialize it
  if(CardType == CARDTYPE_NONE) {
    iprintf("Card was removed, try to init it\n");

    if(!mmc_inserted())
      return 0;

    if(!MMC_Init())
      return 0;
  }
  return 1;
}

#define MMC_SLOW_DIV (MCLK/400000 - 2)
#define MMC_FAST_DIV (MCLK/24000000 - 2)
#define MMC_HS_DIV   (MCLK/48000000 - 2)

// init memory card
unsigned char MMC_Init(void)
{
    unsigned char i;
    unsigned char cd;
    uint32_t resp;

    CardType = CARDTYPE_NONE;

    if(!mmc_inserted()) {
      iprintf("No card inserted\r");
      return(CARDTYPE_NONE);
    }

    RCA = 0;
    PMC->PMC_PCER0 = (1 << ID_HSMCI0);
    HSMCI0->HSMCI_CR = HSMCI_CR_MCIEN;
    HSMCI0->HSMCI_CFG = ~HSMCI_CFG_HSMODE;
    HSMCI0->HSMCI_SDCR = HSMCI_SDCR_SDCBUS_1;
    HSMCI0->HSMCI_DTOR = 0x0F | HSMCI_DTOR_DTOMUL_1048576; // Set for maximum timeout for block transfers
    HSMCI0->HSMCI_DMA = 0;

    // Set slow clock
    // (MCK) divided by 2 × CLKDIV + CLKODD + 2.
    HSMCI0->HSMCI_MR = HSMCI_MR_CLKDIV(MMC_SLOW_DIV/2) | ((MMC_SLOW_DIV & 1) << 16) | HSMCI_MR_PWSDIV(0x07) | HSMCI_MR_RDPROOF | HSMCI_MR_WRPROOF;
    WaitTimer(20);  // 20ms delay

    MMC_Command(0, 0, HSMCI_CMDR_OPDCMD | HSMCI_CMDR_SPCMD_INIT); // Special command

    if (!MMC_Command(CMD0, 0, HSMCI_CMDR_OPDCMD)) {
        iprintf("No memory card detected!\r");
        return(CARDTYPE_NONE);
    }

    // idle state
    if (MMC_Command(CMD8, 0x1AA, HSMCI_CMDR_OPDCMD | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_RSPTYP_48_BIT)) {// check if the card can operate with 2.7-3.6V power
        if ((HSMCI0->HSMCI_RSPR[0] & 0xFFFF) == 0x01AA) {
            // the card can work at 2.7-3.6V
            iprintf("SD card >= 2.00 detected\r");

            timeout = GetTimer(1000); // initialization timeout 1s, 4s doesn't work with the original arm timer
            while (1) {
                if (CheckTimer(timeout)) {
                    iprintf("ACMD41 timed out!\r");
                    return(CARDTYPE_NONE);
                }
                // now we must wait until CMD41 returns 0 (or timeout elapses)
                if (MMC_AppCommand(CMD41, (1 << 30) /*| (1 << 24)*/ | (0xFF8000), HSMCI_CMDR_OPDCMD | HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) { // ACMD41 with HCS (and S18R bit - no, as UHS-I is not supported)
                    resp = HSMCI0->HSMCI_RSPR[0];
                    if (resp & (1 << 31)) break; // powerup
                }
            }
            // initialization completed
            cd = (resp & (1 << 30)) ? CARDTYPE_SDHC : CARDTYPE_SD; // if CCS set then the card is SDHC compatible
#if 0
            if (resp & (1 << 24)) {
                // 1.8V signaling level supported
                iprintf("Switching to 1.8V signaling level\n");
                if (!MMC_Command(CMD11, 0, HSMCI_CMDR_OPDCMD | HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) {
                    iprintf("CMD11 (VOLTAGE_SWITCH) failed!\r");
                    return(CARDTYPE_NONE);
                }
            }
#endif
            if (!MMC_Command(CMD2, 0, HSMCI_CMDR_OPDCMD | HSMCI_CMDR_RSPTYP_136_BIT | HSMCI_CMDR_MAXLAT)) {
                iprintf("CMD2 (SEND_CID_ALL) failed!\r");
                return(CARDTYPE_NONE);
            }

            if (!MMC_Command(CMD3, 0, HSMCI_CMDR_OPDCMD | HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) {
                iprintf("CMD3 (GET_RCA) failed!\r");
                return(CARDTYPE_NONE);
            }
            RCA = (HSMCI0->HSMCI_RSPR[0] >> 16) & 0xFFFF;

            if (!MMC_Command(CMD9, RCA << 16, HSMCI_CMDR_RSPTYP_136_BIT | HSMCI_CMDR_MAXLAT)) {
                iprintf("CMD9 (SEND CSD) failed!\r");
                return(CARDTYPE_NONE);
            }
            for(i=0; i<4; i++) {
                uint32_t resp = HSMCI0->HSMCI_RSPR[0];
                CSD[4*i+3] = resp & 0xFF;
                CSD[4*i+2] = (resp >> 8) & 0xFF;
                CSD[4*i+1] = (resp >> 16) & 0xFF;
                CSD[4*i  ] = (resp >> 24) & 0xFF;
            }

            if (!MMC_Command(CMD10, RCA << 16, HSMCI_CMDR_RSPTYP_136_BIT | HSMCI_CMDR_MAXLAT)) {
                iprintf("CMD10 (SEND CID) failed!\r");
                return(CARDTYPE_NONE);
            }
            for(i=0; i<4; i++) {
                uint32_t resp = HSMCI0->HSMCI_RSPR[0];
                CID[4*i+3] = resp & 0xFF;
                CID[4*i+2] = (resp >> 8) & 0xFF;
                CID[4*i+1] = (resp >> 16) & 0xFF;
                CID[4*i  ] = (resp >> 24) & 0xFF;
            }

            // Select card
            if (!MMC_Command(CMD7, RCA << 16, HSMCI_CMDR_RSPTYP_R1B | HSMCI_CMDR_MAXLAT)) {
                iprintf("CMD7 (SELECT CARD) failed!\r");
                return(CARDTYPE_NONE);
            }

            if (cd == CARDTYPE_SD) {
                if (!MMC_Command(CMD16, 512, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) {
                    iprintf("CMD16 (SET_BLOCKLEN) failed!\r");
                    return(CARDTYPE_NONE);
                }
            }

            // read SCR register
            HSMCI0->HSMCI_BLKR = HSMCI_BLKR_BCNT(1) | HSMCI_BLKR_BLKLEN(8);
            if (!MMC_AppCommand(CMD51, 0, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_TRDIR | HSMCI_CMDR_TRCMD_START_DATA | HSMCI_CMDR_TRTYP_SINGLE)) {
                iprintf("ACMD51 (SEND_SCR) failed!\r");
                return(CARDTYPE_NONE);
            }
            MMC_PIORead(SCR, 8);
            //hexdump(SCR, 8, 0);

            if (CSD[4] & 0x40) { /// CCC command class 10 (switch)
                iprintf("Switching to high speed mode\n");
                // Switch to High Speed mode
                HSMCI0->HSMCI_CFG |= HSMCI_CFG_HSMODE;
                HSMCI0->HSMCI_BLKR = HSMCI_BLKR_BCNT(1) | HSMCI_BLKR_BLKLEN(sizeof(switch_status));
                if (!MMC_Command(CMD6, SD_CMD6_MODE_SWITCH | SD_CMD6_GRP6_NO_INFLUENCE | SD_CMD6_GRP5_NO_INFLUENCE | SD_CMD6_GRP4_NO_INFLUENCE |
                                       SD_CMD6_GRP3_NO_INFLUENCE | SD_CMD6_GRP2_DEFAULT | SD_CMD6_GRP1_HIGH_SPEED,
                                       HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_TRDIR | HSMCI_CMDR_TRCMD_START_DATA | HSMCI_CMDR_TRTYP_SINGLE)) {
                    iprintf("CMD6 (SWITCH_FUNC) failed!\r");
                    return(CARDTYPE_NONE);
                }
                MMC_PIORead(switch_status, sizeof(switch_status));

                // Switch to 48 MHz clock
                HSMCI0->HSMCI_MR = HSMCI_MR_CLKDIV(MMC_HS_DIV/2) | ((MMC_HS_DIV & 1) << 16) | HSMCI_MR_PWSDIV(0x07) | HSMCI_MR_RDPROOF | HSMCI_MR_WRPROOF;
            } else {
                iprintf("Normal speed card\n");
                // Switch to 24 MHz clock
                HSMCI0->HSMCI_MR = HSMCI_MR_CLKDIV(MMC_FAST_DIV/2) | ((MMC_FAST_DIV & 1) << 16) | HSMCI_MR_PWSDIV(0x07) | HSMCI_MR_RDPROOF | HSMCI_MR_WRPROOF;
            }

            if ((SCR[1] & 0x0f) == 5) {
                // switch to 4-wire mode

                iprintf("Switching to 4-wire mode\n");
                if (!MMC_AppCommand(CMD6, 2, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) {
                    iprintf("ACMD6 (SET_BUS_WIDTH) failed!\r");
                    //return(CARDTYPE_NONE);
                }
                HSMCI0->HSMCI_SDCR = HSMCI_SDCR_SDCBUS_4;

                if (!MMC_AppCommand(CMD42, 0, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT)) {
                    iprintf("ACMD42 (SET_CLR_CARD_DETECT) failed!\r");
                    //return(CARDTYPE_NONE);
                }
            }

            iprintf("MMC clock: %d kHz\n", MCLK/((HSMCI0->HSMCI_MR & 0xFF) * 2 + ((HSMCI0->HSMCI_MR >> 16) & 1) + 2) / 1000);
            CardType = cd;
            return(CardType);
        }
        iprintf("SD card has no supported voltage range!\r");
        return(CARDTYPE_NONE);
    }

    iprintf("SD card version < 2.00 detected, not supported yet!\r");
    return(CARDTYPE_NONE);
}

RAMFUNC static unsigned char MMC_ReadBlocks(unsigned char *buffer, unsigned long lba, unsigned long blocks)
{
    if(!buffer) return 0; // direct transfer is not supported

    if (CardType != CARDTYPE_SDHC) // SDHC cards are addressed in sectors not bytes
        lba = lba << 9; // otherwise convert sector adddress to byte address


    HSMCI0->HSMCI_BLKR = HSMCI_BLKR_BCNT(blocks) | HSMCI_BLKR_BLKLEN(512);
    HSMCI0->HSMCI_DMA = HSMCI_DMA_DMAEN | HSMCI_DMA_CHKSIZE_8;
    //if (!MMC_Command(blocks == 1 ? CMD17 : CMD18, lba, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_TRDIR | HSMCI_CMDR_TRCMD_START_DATA | (blocks == 1 ? HSMCI_CMDR_TRTYP_SINGLE : HSMCI_CMDR_TRTYP_MULTIPLE))) {
    if (!MMC_Command(CMD18, lba, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_TRDIR | HSMCI_CMDR_TRCMD_START_DATA | HSMCI_CMDR_TRTYP_MULTIPLE)) {
        return(0);
    }

    XDMAC0->XDMAC_GD = XDMAC_GD_DI0;
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CC = XDMAC_CC_TYPE_PER_TRAN
                                          | XDMAC_CC_MBSIZE_SINGLE
                                          | XDMAC_CC_DSYNC_PER2MEM
                                          | XDMAC_CC_CSIZE_CHK_8
                                          | XDMAC_CC_DWIDTH_WORD
                                          | XDMAC_CC_SIF_AHB_IF1
                                          | XDMAC_CC_DIF_AHB_IF0
                                          | XDMAC_CC_SAM_FIXED_AM
                                          | XDMAC_CC_DAM_INCREMENTED_AM
                                          | XDMAC_CC_PERID(0);
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CSA = (uint32_t)&(HSMCI0->HSMCI_FIFO[0]);
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CDA = (uint32_t)buffer;
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CUBC = XDMAC_CUBC_UBLEN(blocks*512/4);
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CIS; // read interrupt reg to clear any flags prior to enabling channel
    XDMAC0->XDMAC_GE = XDMAC_GE_EN0;        // start DMA
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CIS; // clear any flags

    unsigned char retval = MMC_WaitTransferEnd();
    XDMAC0->XDMAC_GD = XDMAC_GD_DI0;
/*
    for (int i=0; i<blocks; i++) {
      hexdump(buffer, 512, 0);
      buffer+=512;
    }
*/
    /*if (blocks > 1)*/ MMC_Command(CMD12, 0, HSMCI_CMDR_RSPTYP_R1B | HSMCI_CMDR_MAXLAT);

    return(retval);
}

// Read single 512-byte block
RAMFUNC unsigned char MMC_Read(unsigned long lba, unsigned char *pReadBuffer)
{
    //iprintf("MMC_Read lba=%lu\n", lba);
    if (!MMC_WaitReady()) return 0;
    return MMC_ReadBlocks(pReadBuffer, lba, 1);
}

// read multiple 512-byte blocks
RAMFUNC unsigned char MMC_ReadMultiple(unsigned long lba, unsigned char *pReadBuffer, unsigned long nBlockCount)
{
    //iprintf("MMC_ReadMultiple lba=%lu, nBlockCount=%d\n", lba, nBlockCount);
    if (!MMC_WaitReady()) return 0;
    return MMC_ReadBlocks(pReadBuffer, lba, nBlockCount);
}

static unsigned char MMC_WriteBlocks(unsigned long lba, const unsigned char *pWriteBuffer, unsigned long blocks)
{
    XDMAC0->XDMAC_GD = XDMAC_GD_DI0;
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CC = XDMAC_CC_TYPE_PER_TRAN
                                          | XDMAC_CC_MBSIZE_SINGLE
                                          | XDMAC_CC_DSYNC_MEM2PER
                                          | XDMAC_CC_CSIZE_CHK_8
                                          | XDMAC_CC_DWIDTH_WORD
                                          | XDMAC_CC_SIF_AHB_IF0
                                          | XDMAC_CC_DIF_AHB_IF1
                                          | XDMAC_CC_SAM_INCREMENTED_AM
                                          | XDMAC_CC_DAM_FIXED_AM
                                          | XDMAC_CC_PERID(0);

    if (CardType != CARDTYPE_SDHC) // SDHC cards are addressed in sectors not bytes
        lba = lba << 9; // otherwise convert sector adddress to byte address

    HSMCI0->HSMCI_BLKR = HSMCI_BLKR_BCNT(blocks) | HSMCI_BLKR_BLKLEN(512);
    HSMCI0->HSMCI_DMA = HSMCI_DMA_DMAEN | HSMCI_DMA_CHKSIZE_8;
    if (!MMC_Command(blocks == 1 ? CMD24 : CMD25, lba, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT | HSMCI_CMDR_TRCMD_START_DATA | HSMCI_CMDR_TRDIR_WRITE | (blocks == 1 ? HSMCI_CMDR_TRTYP_SINGLE : HSMCI_CMDR_TRTYP_MULTIPLE))) {
        iprintf("MMC: write failed (lba=%d, blks=%d)\n", lba, blocks);
        return(0);
    }

    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CSA = (uint32_t)pWriteBuffer;
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CDA = (uint32_t)&(HSMCI0->HSMCI_FIFO[0]);
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CUBC = XDMAC_CUBC_UBLEN(blocks*512/4);
    XDMAC0->XDMAC_CH[DMA_CH_MMC].XDMAC_CIS; //read interrupt reg to clear any flags prior to enabling channel
    XDMAC0->XDMAC_GE = XDMAC_GE_EN0;

    unsigned char retval = MMC_WaitTransferEnd();
    XDMAC0->XDMAC_GD = XDMAC_GD_DI0;
    if (blocks > 1) MMC_Command(CMD12, 0, HSMCI_CMDR_RSPTYP_R1B | HSMCI_CMDR_MAXLAT);

    return(retval);
}

// write 512-byte block
unsigned char MMC_Write(unsigned long lba, const unsigned char *pWriteBuffer)
{
    if (!MMC_WaitReady()) return 0;;
    return MMC_WriteBlocks(lba, pWriteBuffer, 1);
}

unsigned char MMC_WriteMultiple(unsigned long lba, const unsigned char *pWriteBuffer, unsigned long nBlockCount)
{
    if (!MMC_WaitReady()) return 0;;
    return MMC_WriteBlocks(lba, pWriteBuffer, nBlockCount);
}

// Read CSD register
unsigned char MMC_GetCSD(unsigned char *csd)
{
    memcpy(csd, &CSD, sizeof(CSD));
    return 1;
}

// Read CID register
unsigned char MMC_GetCID(unsigned char *cid)
{
    memcpy(cid, &CID, sizeof(CID));
    return 1;
}

// MMC get capacity
unsigned long MMC_GetCapacity()
{
    unsigned long result=0;

    if ((CSD[0] & 0xC0)==0x40) {   //CSD Version 2.0 - SDHC
        result=(CSD[7]&0x3f)<<26;
        result|=CSD[8]<<18;
        result|=CSD[9]<<10;
        result+=1024;
        return(result);
    } else {
        int blocksize=CSD[5]&15;   // READ_BL_LEN
        blocksize=1<<(blocksize-9);// Now a scalar:  physical block size / 512.
        result=(CSD[6]&3)<<10;
        result|=CSD[7]<<2;
        result|=(CSD[8]>>6)&3;     // result now contains C_SIZE
        int cmult=(CSD[9]&3)<<1;
        cmult|=(CSD[10]>>7) & 1;
        ++result;
        result<<=cmult+2;
        result*=blocksize;         // Scale by the number of 512-byte chunks per block.
        return(result);
    }
}

static unsigned char MMC_PIORead(unsigned char *buffer, int len)
{
    int wordsToRead = len >> 2;
    unsigned long *p = (unsigned long*) buffer;
    unsigned long to;

    while(wordsToRead--) {
        to = GetTimer(500);
        while((HSMCI0->HSMCI_SR & HSMCI_SR_RXRDY)==0) {
            if(CheckTimer(to)) {
                iprintf("MMC_PIORead: timeout\n");
                return(0);
            }
        }; // Wait for data to be received
        *p++ = HSMCI0->HSMCI_RDR;
    }
    return(1);
}

RAMFUNC static unsigned char MMC_WaitTransferEnd()
{
    unsigned long status;
    do {
        status = HSMCI0->HSMCI_SR;
        if (status & MCI_ERRORS_MASK) {
            //iprintf("Transfer error, status: %08x\n", status);
            return(0);
        }
    } while (!(status & HSMCI_SR_XFRDONE));
    return(1);
}

RAMFUNC static unsigned char MMC_WaitReady()
{
    unsigned long to;

    // check of card has been removed and try to re-initialize it
    if(!check_card()) return 0;

    to = GetTimer(500);
    //wait for data ready status
    do {
        MMC_Command(CMD13, RCA << 16, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT);
        if (HSMCI0->HSMCI_RSPR[0] & (1lu << 8)) {
            break;
        }
        if (CheckTimer(to)) {
            return 0;
        }
    } while (1);
    return 1;
}

static unsigned char MMC_AppCommand(unsigned char cmd, unsigned long arg, unsigned long flags)
{
    if (!MMC_Command(CMD55, RCA << 16, HSMCI_CMDR_RSPTYP_48_BIT | HSMCI_CMDR_MAXLAT )) { // CMD55 must precede any ACMD command
        iprintf("CMD55 (APP_CMD) failed!\r");
        return(0);
    }
    return (MMC_Command(cmd, arg, flags));
}

// MMC command
RAMFUNC static unsigned char MMC_Command(unsigned char cmd, unsigned long arg, unsigned long flags)
{

    unsigned long to = GetTimer(100);

    //iprintf("MMC cmd: %02d args: %08x flags: %08x\n", cmd & 0x3f, arg, flags);

    unsigned long status;
    HSMCI0->HSMCI_ARGR = arg;
    HSMCI0->HSMCI_CMDR = HSMCI_CMDR_CMDNB(cmd) | flags;
    do {
        status = HSMCI0->HSMCI_SR;
        //iprintf(" status: %08x\n", status);
    } while (!(status & HSMCI_SR_CMDRDY));

    if ((flags & HSMCI_CMDR_RSPTYP_R1B) == HSMCI_CMDR_RSPTYP_R1B) {
        while(!(HSMCI0->HSMCI_SR & HSMCI_SR_NOTBUSY));
    }
    if (status & MCI_ERRORS_MASK) {
        //iprintf("MMC error! cmd: %02d args: %08x flags: %08x status: %08x\n", cmd & 0x3f, arg, flags, status);
        //iprintf("MMC response: %02x %02x %02x %02x\n", HSMCI0->HSMCI_RSPR[0], HSMCI0->HSMCI_RSPR[1], HSMCI0->HSMCI_RSPR[2], HSMCI0->HSMCI_RSPR[3]);
    }

    return !(status & MCI_ERRORS_MASK);
}

unsigned char MMC_IsSDHC(void) {
  return(CardType == CARDTYPE_SDHC);
}
