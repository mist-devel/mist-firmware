/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-10-10   - any length (any multiple of 8 bytes) fpga core file support
// 2009-12-10   - changed command header id
// 2010-04-14   - changed command header id

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "errors.h"
#include "hardware.h"
#include "fdd.h"
#include "user_io.h"
#include "config.h"
#include "boot.h"
#include "osd.h"
#include "fpga.h"
#include "tos.h"
#include "arc_file.h"
#include "mist_cfg.h"
#include "settings.h"
#include "usb/joymapping.h"

#ifndef DEFAULT_CORE_NAME
#define DEFAULT_CORE_NAME "CORE.RBF"
#endif

uint8_t rstval = 0;

#define CMD_HDRID 0xAACA

// TODO!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0");

extern char s[FF_LFN_BUF + 1];
extern adfTYPE df[4];

char minimig_ver_beta;
char minimig_ver_major;
char minimig_ver_minor;
char minimig_ver_minion;

char BootPrint(const char *text);

#ifdef XILINX_CCLK

// single byte serialization of FPGA configuration datastream
void ShiftFpga(unsigned char data)
{
    AT91_REG *ppioa_codr = AT91C_PIOA_CODR;
    AT91_REG *ppioa_sodr = AT91C_PIOA_SODR;

    // bit 0
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x80)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 1
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x40)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 2
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x20)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 3
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x10)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 4
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x08)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 5
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x04)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 6
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x02)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 7
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x01)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

}

// Xilinx FPGA configuration
// was before unsigned char ConfigureFpga(void)
unsigned char ConfigureFpga(const char *name)
{
    unsigned long  t;
    unsigned long  n;
    unsigned char *ptr;
    FIL file;
    UINT br;

    // set outputs
    *AT91C_PIOA_SODR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    // enable outputs
    *AT91C_PIOA_OER = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;

    // reset FGPA configuration sequence
    // specs: PROG_B pulse min 0.3 us
    t = 15;
    while (--t)
        *AT91C_PIOA_CODR = XILINX_PROG_B;

    *AT91C_PIOA_SODR = XILINX_PROG_B;

    // now wait for INIT to go high
    // specs: max 2ms
    t = 100000;
    while (!(*AT91C_PIOA_PDSR & XILINX_INIT_B))
    {
        if (--t == 0)
        {
            iprintf("FPGA init is NOT high!\r");
            FatalError(3);
        }
    }

    iprintf("FPGA init is high\r");

    if (*AT91C_PIOA_PDSR & XILINX_DONE)
    {
        iprintf("FPGA done is high before configuration!\r");
        FatalError(3);
    }

    if(!name)
    //  name = "CORE.BIN";
        name = "X7A102T.BIN";

    // open bitstream file
    if (f_open(&file, name, FA_READ) != FR_OK)
    {
        iprintf("No FPGA configuration file found!\r");
        FatalError(4);
    }

    iprintf("FPGA bitstream file %s opened, file size = %llu\r", name, f_size(&file));
    iprintf("[");

    // send all bytes to FPGA in loop
    t = 0;
    n = f_size(&file) >> 3;
    ptr = sector_buffer;
    do
    {
        // read sector if 512 (64*8) bytes done
        if ((t & 0x3F) == 0)
        {
            if (t & (1<<10))
                DISKLED_OFF
            else
                DISKLED_ON

            if ((t & 0x1FF) == 0)
                iprintf("*");

            if (f_read(&file, sector_buffer, 512, &br) != FR_OK) {
                f_close(&file);
                return(0);
            }

            ptr = sector_buffer;
        }

        // send data in packets of 8 bytes
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);

        t++;

    }
    while (t < n);
    f_close(&file);

    // return outputs to a state suitable for user_io.c
    *AT91C_PIOA_SODR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;

    iprintf("]\r");
    iprintf("FPGA bitstream loaded\r");
    DISKLED_OFF;

    // check if DONE is high
    if (*AT91C_PIOA_PDSR & XILINX_DONE)
        return(1);

    iprintf("FPGA done is NOT high!\r");
    FatalError(5);
    return 0;
}
#endif


#ifdef ALTERA_DCLK
static inline void ShiftFpga(unsigned char data)
{
    unsigned char i;
    for ( i = 0; i < 8; i++ )
    {
        /* Dump to DATA0 and insert a positive edge pulse at the same time */
        ALTERA_DATA0_RESET;
        ALTERA_DCLK_RESET;
        if(data & 1) ALTERA_DATA0_SET;
        ALTERA_DCLK_SET;
        data >>= 1;
    }
}

// Altera FPGA configuration
unsigned char ConfigureFpga(const char *name)
{
    unsigned long i;
    unsigned char *ptr;
    FIL file;
    UINT br;

    // set outputs
    ALTERA_DCLK_SET;
    ALTERA_NCONFIG_SET;
    ALTERA_DATA0_SET;

    if(!name)
      name = DEFAULT_CORE_NAME;

    // open bitstream file
    if (f_open(&file, name, FA_READ) != FR_OK)
    {
        iprintf("No FPGA configuration file found!\r");
        return ERROR_BITSTREAM_OPEN;
    }

    iprintf("FPGA bitstream file %s opened, file size = %llu\r", name, f_size(&file));
    iprintf("[");

    // send all bytes to FPGA in loop
    ptr = sector_buffer;

    ALTERA_START_CONFIG
    /* Drive a transition of 0 to 1 to NCONFIG to indicate start of configuration */
    for(i=0;i<10;i++)
      ALTERA_NCONFIG_RESET;  // must be low for at least 500ns

    ALTERA_NCONFIG_SET;

    // now wait for NSTATUS to go high
    // specs: max 800us
    i = 1000000;
    while (!ALTERA_NSTATUS_STATE)
    {
        if (--i == 0)
        {
            ALTERA_STOP_CONFIG
            iprintf("FPGA NSTATUS is NOT high!\r");
            f_close(&file);
            return ERROR_UPDATE_INIT_FAILED;
        }
    }

    DISKLED_ON;

    int n = f_size(&file) >> 3;

    /* Loop through every single byte */
    for ( i = 0; i < f_size(&file); )
    {
        // read sector if SECTOR_BUFFER_SIZE bytes done
        if ((i & (SECTOR_BUFFER_SIZE-1)) == 0)
        {
            if (i & (1<<13))
                DISKLED_OFF
            else
                DISKLED_ON

            if ((i & (SECTOR_BUFFER_SIZE*4-1)) == 0)
                iprintf("*");

            if (f_read(&file, sector_buffer, SECTOR_BUFFER_SIZE, &br) != FR_OK) {
                f_close(&file);
                return ERROR_READ_BITSTREAM_FAILED;
            }

            ptr = sector_buffer;
        }

        int bytes2copy = (i < f_size(&file) - 8)?8:f_size(&file)-i;
        i += bytes2copy;
        while(bytes2copy) {
          ShiftFpga(*ptr++);
          bytes2copy--;
        }

        /* Check for error through NSTATUS for every 10KB programmed and the last byte */
        if ( !(i % 10240) || (i == f_size(&file) - 1) ) {
            if ( !ALTERA_NSTATUS_STATE ) {
                ALTERA_STOP_CONFIG

                iprintf("FPGA NSTATUS is NOT high!\r");
                f_close(&file);
                return ERROR_UPDATE_PROGRESS_FAILED;
            }
        }
    }
    ALTERA_STOP_CONFIG

    f_close(&file);

    iprintf("]\r");
    iprintf("FPGA bitstream loaded\r");
    DISKLED_OFF;

    // check if DONE is high
    if (!ALTERA_DONE_STATE) {
      iprintf("FPGA Configuration done but contains error... CONF_DONE is LOW\r");
      return ERROR_UPDATE_FAILED;
    }


    /* Start initialization */
    /* Clock another extra DCLK cycles while initialization is in progress
       through internal oscillator or driving clock cycles into CLKUSR pin */
    /* These extra DCLK cycles do not initialize the device into USER MODE */
    /* It is not required to drive extra DCLK cycles at the end of configuration */
    /* The purpose of driving extra DCLK cycles here is to insert some delay
       while waiting for the initialization of the device to complete before
       checking the CONFDONE and NSTATUS signals at the end of whole
       configuration cycle */

    for ( i = 0; i < 50; i++ )
    {
        ALTERA_DCLK_RESET;
        ALTERA_DCLK_SET;
    }

    /* Initialization end */

    if ( !ALTERA_NSTATUS_STATE || !ALTERA_DONE_STATE ) {

      iprintf("FPGA Initialization finish but contains error: NSTATUS is %s and CONF_DONE is %s.\r",
             ALTERA_NSTATUS_STATE?"HIGH":"LOW", ALTERA_DONE_STATE?"HIGH":"LOW" );
      return ERROR_UPDATE_FAILED;
    }

    return ERROR_NONE;
}
#endif


void SendFile(FIL *file)
{
    unsigned char  c1, c2;
    unsigned long  j;
    unsigned long  n;
    unsigned char *p;

    iprintf("[");
    n = (f_size(file) + 511) >> 9; // sector count (rounded up)
    while (n--)
    {
        // read data sector from memory card
        FileReadBlock(file,sector_buffer);

        do
        {
            // read FPGA status
            EnableFpga();
            c1 = SPI(0);
            c2 = SPI(0);
            SPI(0);
            SPI(0);
            SPI(0);
            SPI(0);
            DisableFpga();
        }
        while (!(c1 & CMD_RDTRK));

        if ((n & 15) == 0)
            iprintf("*");

        // send data sector to FPGA
        EnableFpga();
        c1 = SPI(0);
        c2 = SPI(0);
        SPI(0);
        SPI(0);
        SPI(0);
        SPI(0);
        p = sector_buffer;

        for (j = 0; j < 512; j++)
            SPI(*p++);

        DisableFpga();
    }
    iprintf("]\r");
}


void SendFileEncrypted(FIL *file,unsigned char *key,int keysize)
{
    UINT br;
    unsigned char  c1, c2;
    unsigned char headersize;
    unsigned int keyidx=0;
    unsigned long  j;
    unsigned long  n;
    unsigned char *p;

    iprintf("[");
    headersize=f_size(file)&255;	// ROM should be a round number of kilobytes; overspill will likely be the Amiga Forever header.

    f_read(file,sector_buffer,headersize, &br); // Read extra bytes

    n = (f_size(file) + (511-headersize)) >> 9; // sector count (rounded up)
    while (n--)
    {
        FileReadBlock(file,sector_buffer);
        for (j = 0; j < 512; j++)
        {
            sector_buffer[j]^=key[keyidx++];
            if(keyidx>=keysize)
            keyidx-=keysize;
        }

        do
        {
            // read FPGA status
            EnableFpga();
            c1 = SPI(0);
            c2 = SPI(0);
            SPI(0);
            SPI(0);
            SPI(0);
            SPI(0);
            DisableFpga();
        }
        while (!(c1 & CMD_RDTRK));

        if ((n & 15) == 0)
            iprintf("*");

        // send data sector to FPGA
        EnableFpga();
        c1 = SPI(0);
        c2 = SPI(0);
        SPI(0);
        SPI(0);
        SPI(0);
        SPI(0);
        p = sector_buffer;

        for (j = 0; j < 512; j++)
            SPI(*p++);
        DisableFpga();
    }
    iprintf("]\r");
}

char kick1xfoundstr[] = "Kickstart v1.x found\n";
const char applymemdetectionpatchstr[] = "Applying Kickstart 1.x memory detection patch\n";

const char *kickfoundstr = NULL, *applypatchstr = NULL;

void PatchKick1xMemoryDetection() {

  if (!strncmp(sector_buffer + 0x18, "exec 33.192 (8 Oct 1986)", 24)) {
    kick1xfoundstr[13] = '2';
    kickfoundstr = kick1xfoundstr;
    goto applypatch;
  }
  if (!strncmp(sector_buffer + 0x18, "exec 34.2 (28 Oct 1987)", 23)) {
    kick1xfoundstr[13] = '3';
    kickfoundstr = kick1xfoundstr;
    goto applypatch;
  }

  goto out;

applypatch:
  if ((sector_buffer[0x154] == 0x66) && (sector_buffer[0x155] == 0x78)) {
    applypatchstr = applymemdetectionpatchstr;
    sector_buffer[0x154] = 0x60;
  }

out:
  return;
}

// SendFileV2 (for minimig_v2)
void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size)
{
  UINT br;
  int i,j;
  unsigned int keyidx=0;

  iprintf("File size: %dkB\r", size>>1);
  iprintf("[");
  if (keysize) {
    // read header
    f_read(file, sector_buffer, 0xb, &br);
  }
  for (i=0; i<size; i++) {
    if (!(i&31)) iprintf("*");
    FileReadBlock(file, sector_buffer);
    if (keysize) {
      // decrypt ROM
      for (j=0; j<512; j++) {
        sector_buffer[j] ^= key[keyidx++];
        if(keyidx >= keysize) keyidx -= keysize;
      }
    }

    // patch kickstart 1.x to force memory detection every time the AMIGA is reset
    if (minimig_cfg.kick1x_memory_detection_patch && (i == 0 || i == 512)) {
      kickfoundstr = NULL;
      applypatchstr = NULL;
      PatchKick1xMemoryDetection();
    }

    EnableOsd();
    unsigned int adr = address + i*512;
    SPI(OSD_CMD_WR);
    SPIN(); SPIN(); SPIN(); SPIN();
    SPI(adr&0xff); adr = adr>>8;
    SPI(adr&0xff); adr = adr>>8;
    SPIN(); SPIN(); SPIN(); SPIN();
    SPI(adr&0xff); adr = adr>>8;
    SPI(adr&0xff); adr = adr>>8;
    SPIN(); SPIN(); SPIN(); SPIN();
    for (j=0; j<512; j=j+4) {
      SPI(sector_buffer[j+0]);
      SPI(sector_buffer[j+1]);
      SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN();
      SPI(sector_buffer[j+2]);
      SPI(sector_buffer[j+3]);
      SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN(); SPIN();
    }
    DisableOsd();
  }
  iprintf("]\r");

  if (kickfoundstr) {
    iprintf(kickfoundstr);
  }
  if (applypatchstr) {
    iprintf(applypatchstr);
  }
}



// draw on screen
char BootDraw(char *data, unsigned short len, unsigned short offset)
{
  DEBUG_FUNC_IN();

    unsigned char c1, c2, c3, c4;
    unsigned char cmd;
    const char *p;
    unsigned short n;
    unsigned short i;

    n = (len+3)&(~3);
    i = 0;

    cmd = 1;
    while (1)
    {
        EnableFpga();
        c1 = SPI(0x10); // track read command
        c2 = SPI(0x01); // disk present
        unsigned char x = SPI(0);
        unsigned char y = SPI(0);
        c3 = SPI(0);
        c4 = SPI(0);

	//	iprintf("FPGA state: %d %d (%d %d) %d %d\n", c1, c2, x, y, c3, c4);

        if (c1 & CMD_RDTRK)
        {
            if (cmd)
            { // command phase
                if (c3 == 0x80 && c4 == 0x06) // command packet size must be 12 bytes
                {
                    cmd = 0;
                    SPI(CMD_HDRID >> 8); // command header
                    SPI(CMD_HDRID & 0xFF);
                    SPI(0x00); // cmd: 0x0001 = print text
                    SPI(0x01);
                    // data packet size in bytes
                    SPI(0x00);
                    SPI(0x00);
                    SPI((n)>>8);
                    SPI((n)&0xff); // +2 because only even byte count is possible to send and we have to send termination zero byte
                    // offset
                    SPI(0x00);
                    SPI(0x00);
                    SPI(offset>>8);
                    SPI(offset&0xff);
                }
                else
                    break;
            }
            else
            { // data phase
                if (c3 == 0x80 && c4 == ((n) >> 1))
                {
                    p = data;
                    n = c4 << 1;
                    while (n--)
                    {
                        c4 = *p;
                        SPI((i>=len) ? 0 : c4);
                        p++;
                        i++;
                    }
                    DisableFpga();
                    return 1;
                }
                else
                    break;
            }
        }
        DisableFpga();
    }
    DisableFpga();
    return 0;

  DEBUG_FUNC_OUT();
}


// print message on the boot screen
char BootPrint(const char *text)
{
    if(!minimig_v1()) {
      iprintf("%s\n", text);
      return 0;
    }

    unsigned char c1, c2, c3, c4;
    unsigned char cmd;
    const char *p;
    unsigned char n;

    return 0;

    p = text;
    n = 0;
    while (*p++ != 0)
        n++; // calculating string length

    cmd = 1;
    while (1)
    {
        EnableFpga();
        c1 = SPI(0x10); // track read command
        c2 = SPI(0x01); // disk present
        SPI(0);
        SPI(0);
        c3 = SPI(0);
        c4 = SPI(0);

        if (c1 & CMD_RDTRK)
        {
            if (cmd)
            { // command phase
                if (c3 == 0x80 && c4 == 0x06) // command packet size must be 12 bytes
                {
                    cmd = 0;
                    SPI(CMD_HDRID >> 8); // command header
                    SPI(CMD_HDRID & 0xFF);
                    SPI(0x00); // cmd: 0x0001 = print text
                    SPI(0x01);
                    // data packet size in bytes
                    SPI(0x00);
                    SPI(0x00);
                    SPI(0x00);
                    SPI(n+2); // +2 because only even byte count is possible to send and we have to send termination zero byte
                    // don't care
                    SPI(0x00);
                    SPI(0x00);
                    SPI(0x00);
                    SPI(0x00);
                }
                else
                    break;
            }
            else
            { // data phase
                if (c3 == 0x80 && c4 == ((n + 2) >> 1))
                {
                    p = text;
                    n = c4 << 1;
                    while (n--)
                    {
                        c4 = *p;
                        SPI(c4);
                        if (c4) // if current character is not zero go to next one
                            p++;
                    }
                    DisableFpga();
                    return 1;
                }
                else
                    break;
            }
        }
        DisableFpga();
    }
    DisableFpga();
    return 0;
}

char PrepareBootUpload(unsigned char base, unsigned char size)
// this function sends given file to Minimig's memory
// base - memory base address (bits 23..16)
// size - memory size (bits 23..16)
{
    unsigned char c1, c2, c3, c4;
    unsigned char cmd = 1;

    while (1)
    {
        EnableFpga();
        c1 = SPI(0x10); // track read command
        c2 = SPI(0x01); // disk present
        SPI(0);
        SPI(0);
        c3 = SPI(0);
        c4 = SPI(0);

        if (c1 & CMD_RDTRK)
        {
            if (cmd)
            { // command phase
                if (c3 == 0x80 && c4 == 0x06) // command packet size 12 bytes
                {
                    cmd = 0;
                    SPI(CMD_HDRID >> 8); // command header
                    SPI(CMD_HDRID & 0xFF);
                    SPI(0x00);
                    SPI(0x02); // cmd: 0x0002 = upload memory
                    // memory base address
                    SPI(0x00);
                    SPI(base);
                    SPI(0x00);
                    SPI(0x00);
                    // memory size
                    SPI(0x00);
                    SPI(size);
                    SPI(0x00);
                    SPI(0x00);
                }
                else
                    break;
            }
            else
            { // data phase
                DisableFpga();
                iprintf("Ready to upload ROM file...\r");
                // send rom image to FPGA
//                SendFile(file);
//                iprintf("ROM file uploaded.\r");
                return 0;
            }
        }
        DisableFpga();
    }
    DisableFpga();
    return -1;
}

void BootExit(void)
{
    unsigned char c1, c2, c3, c4;

    while (1)
    {
        EnableFpga();
        c1 = SPI(0x10); // track read command
        c2 = SPI(0x01); // disk present
        SPI(0);
        SPI(0);
        c3 = SPI(0);
        c4 = SPI(0);
        if (c1 & CMD_RDTRK)
        {
            if (c3 == 0x80 && c4 == 0x06) // command packet size 12 bytes
            {
                SPI(CMD_HDRID >> 8); // command header
                SPI(CMD_HDRID & 0xFF);
                SPI(0x00); // cmd: 0x0003 = restart
                SPI(0x03);
                // don't care
                SPI(0x00);
                SPI(0x00);
                SPI(0x00);
                SPI(0x00);
                // don't care
                SPI(0x00);
                SPI(0x00);
                SPI(0x00);
                SPI(0x00);
            }
            DisableFpga();
            return;
        }
        DisableFpga();
    }
}

void ClearMemory(unsigned long base, unsigned long size)
{
    unsigned char c1, c2, c3, c4;

    while (1)
    {
        EnableFpga();
        c1 = SPI(0x10); // track read command
        c2 = SPI(0x01); // disk present
        SPI(0);
        SPI(0);
        c3 = SPI(0);
        c4 = SPI(0);
        if (c1 & CMD_RDTRK)
        {
            if (c3 == 0x80 && c4 == 0x06)// command packet size 12 bytes
            {
                SPI(CMD_HDRID >> 8); // command header
                SPI(CMD_HDRID & 0xFF);
                SPI(0x00); // cmd: 0x0004 = clear memory
                SPI(0x04);
                // memory base
                SPI((unsigned char)(base >> 24));
                SPI((unsigned char)(base >> 16));
                SPI((unsigned char)(base >> 8));
                SPI((unsigned char)base);
                // memory size
                SPI((unsigned char)(size >> 24));
                SPI((unsigned char)(size >> 16));
                SPI((unsigned char)(size >> 8));
                SPI((unsigned char)size);
            }
            DisableFpga();
            return;
        }
        DisableFpga();
    }
}

unsigned char GetFPGAStatus(void)
{
    unsigned char status;

    EnableFpga();
    status = SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    DisableFpga();

    return status;
}


unsigned char fpga_init(const char *name) {
  unsigned long time = GetRTTC();
  int loaded_from_usb = USB_LOAD_VAR;
  unsigned char ct;

  // load the global MISTCFG.INI here
  // loading between the FPGA init and detect_core_type breaks with some SD-Cards. Reason unknown.
  virtual_joystick_remap_init(false);
  settings_load(true);

  iprintf("loaded_from_usb = %d\n", USB_LOAD_VAR == USB_LOAD_VALUE);
  USB_LOAD_VAR = 0;

  if((loaded_from_usb != USB_LOAD_VALUE) && !user_io_dip_switch1()) {
    unsigned char err = ConfigureFpga(name);
    if (err != ERROR_NONE) return err;

    time = GetRTTC() - time;
    iprintf("FPGA configured in %lu ms\r", time);
  }

  // wait max 100 msec for a valid core type
  time = GetTimer(100);
  do {
    EnableIO();
    ct = SPI(0xff);
    DisableIO();
    SPI_MINIMIGV1_HACK
  } while( ((ct == 0) || (ct == 0xff)) && !CheckTimer(time));

  iprintf("ident = %x\n", ct);

  user_io_detect_core_type();
  user_io_init_core();
  mist_ini_parse();
  user_io_send_buttons(true);
  InitDB9();

  if((user_io_core_type() == CORE_TYPE_MINIMIG)||
     (user_io_core_type() == CORE_TYPE_MINIMIG2)) {

    puts("Running minimig setup");

    if(minimig_v2()) {
      user_io_8bit_set_status(minimig_cfg.clock_freq << 1, 0xffffffff);
      WaitTimer(100);
      EnableOsd();
      SPI(OSD_CMD_VERSION);
      minimig_ver_beta   = SPI(0xff);
      minimig_ver_major  = SPI(0xff);
      minimig_ver_minor  = SPI(0xff);
      minimig_ver_minion = SPI(0xff);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval = (SPI_RST_USR | SPI_RST_CPU | SPI_CPU_HLT);
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval = (SPI_RST_CPU | SPI_CPU_HLT);
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      WaitTimer(100);
      BootInit();
      WaitTimer(500);
      char rtl_ver[45];
      siprintf(rtl_ver, "**** MINIMIG-AGA%s v%d.%d.%d for MiST ****", minimig_ver_beta ? " BETA" : "", minimig_ver_major, minimig_ver_minor, minimig_ver_minion);
      BootPrintEx(rtl_ver);
      BootPrintEx(" ");
      BootPrintEx("MINIMIG-AGA for MiST by Rok Krajnc (rok.krajnc@gmail.com)");
      BootPrintEx("Original Minimig by Dennis van Weeren");
      BootPrintEx("Updates by Jakub Bednarski, Tobias Gubener, Sascha Boing, A.M. Robinson & others");
      BootPrintEx("MiST by Till Harbaum (till@harbaum.org)");
      BootPrintEx("For updates & code see https://github.com/rkrajnc/minimig-mist");
      BootPrintEx(" ");
      WaitTimer(1000);
    }

    ChangeDirectoryName("/");

    //eject all disk
    df[0].status = 0;
    df[1].status = 0;
    df[2].status = 0;
    df[3].status = 0;

    config.kickstart[0]=0;
    SetConfigurationFilename(arc_get_cfg_file_n());
    LoadConfiguration(0, 1);  // Use slot-based config filename

  } // end of minimig setup

  if((user_io_core_type() == CORE_TYPE_MIST) || (user_io_core_type() == CORE_TYPE_MIST2)) {
    puts("Running mist setup");

    tos_upload(NULL);

    // end of mist setup
  }

  if(user_io_core_type() == CORE_TYPE_ARCHIE) {
    puts("Running archimedes setup");
  } // end of archimedes setup
  return ERROR_NONE;
}
