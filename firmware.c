 /*
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

#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "hardware.h"
#include "irqflags.h"
#include "barriers.h"
#include "fat_compat.h"
#include "firmware.h"

unsigned long CalculateCRC32(unsigned long crc, unsigned char *pBuffer, unsigned long nSize) {
   int i, j;
   unsigned long byte, mask;

   while (nSize--) {
      byte = *pBuffer++;            // Get next byte.
      crc = crc ^ byte;
      for (j = 7; j >= 0; j--) {    // Do eight times.
         mask = -(crc & 1);
         crc = (crc >> 1) ^ (0xEDB88320 & mask);
      }
   }
   return crc;
}

unsigned char CheckFirmware(char *name)
{
    UPGRADE *pUpgrade = (UPGRADE*)sector_buffer;
    unsigned long crc;
    unsigned long size;
    unsigned long rom_size;
    unsigned long rom_crc;
    unsigned long read_size;
    FIL file;

    Error = ERROR_FILE_NOT_FOUND;
    if (f_open(&file, name, FA_READ) == FR_OK)
    {
        Error = ERROR_INVALID_DATA;
        iprintf("Upgrade file size     : %llu\r", f_size(&file));
        iprintf("Upgrade header size   : %lu\r", (unsigned long)sizeof(UPGRADE));

        if (f_size(&file) >= sizeof(UPGRADE))
        {
            FileReadBlock(&file, sector_buffer);
            crc = ~CalculateCRC32(-1, sector_buffer, sizeof(UPGRADE) - 4);
            iprintf("Upgrade ROM size      : %lu\r", pUpgrade->rom.size);
            iprintf("Upgrade header CRC    : %08lX\r", pUpgrade->crc);
            iprintf("Calculated header CRC : %08lX\r", crc);
            if (pUpgrade->crc == crc)
            {
                if (strncmp((const char*)pUpgrade->id, "MNMGUPG", 7) == 0 && pUpgrade->id[7] == 0)
                {
                    if (pUpgrade->rom.size == f_size(&file) - sizeof(UPGRADE))
                    {
                        rom_size = pUpgrade->rom.size;
                        rom_crc = pUpgrade->rom.crc;

                        crc = -1; // initial CRC32 value
                        size = rom_size;
                        while (size)
                        {
                            if (size > 512)
                               read_size = 512;
                            else
                               read_size = size;

                            FileReadBlock(&file, sector_buffer);
                            crc = CalculateCRC32(crc, sector_buffer, read_size);
                            size -= read_size;
                        }
                        iprintf("Calculated ROM CRC    : %08lX\r", ~crc);
                        iprintf("ROM CRC from header   : %08lX\r", rom_crc);
                        if (~crc == rom_crc)
                        { // upgrade file CRC is OK so go back to the beginning of the file
                            f_close(&file);
                            Error = ERROR_NONE;
                            return 1;
                        }
                        else iprintf("ROM CRC mismatch! from header: %08lX, calculated: %08lX\r", rom_crc, ~crc);
                    }
                    else iprintf("ROM size mismatch! from header: %lu, from file: %llu\r", pUpgrade->rom.size, f_size(&file)-sizeof(UPGRADE));
                }
                else iprintf("Invalid upgrade file header!\r");
            }
            else iprintf("Header CRC mismatch! from header: %08lX, calculated: %08lX\r", pUpgrade->crc, crc);
        }
        else iprintf("Upgrade file size too small: %llu\r", f_size(&file));
        f_close(&file);
    }
    else iprintf("Cannot open firmware file!\r");
    return 0;
}

char *GetFirmwareVersion(char *name) {
  static char v[16];
  FIL file;

  if ((f_open(&file, name, FA_READ) != FR_OK) || (f_size(&file) < sizeof(UPGRADE)))
    return NULL;

  FileReadBlock(&file, sector_buffer);
  strncpy(v, ((UPGRADE*)sector_buffer)->version, 16);
  v[15] = 0;
  f_close(&file);

  return v;
}

// enable some nasty hacks to prevent gcc calling memset/memcpy during flash as these
// are library functions placed in flash and thus must not be called while flash is being
// overwritten
#define GCC_OPTIMZES_TOO_MUCH

#pragma section_code_init
RAMFUNC void WriteFirmware(char *name)
{
    unsigned long read_size;
    unsigned long i;
    unsigned long k;
    unsigned long page;
    unsigned long *pSrc;
    unsigned long *pDst;
    FSIZE_t size;
    FIL file;
    static DWORD clmt[99];


    // Since the file may have changed in the meantime, it needs to be
    // opened again...
    if (f_open(&file, name, FA_READ) != FR_OK) return;
    clmt[0] = 99;
    file.cltbl = clmt;
    if ((f_lseek(&file, CREATE_LINKMAP) != FR_OK) ||
        (f_lseek(&file, sizeof(UPGRADE)) != FR_OK) ||
        (f_tell(&file) != sizeof(UPGRADE))) {
        f_close(&file);
        return;
    }
    size = f_size(&file) - sizeof(UPGRADE);
    // All interrupts have to be disabled.
    arch_irq_disable();
//    asm volatile ("mrs r12, CPSR; orr r12, r12, #0xC0; msr CPSR_c, r12"
//        : /* No outputs */
//        : /* No inputs */
//        : "r12", "cc");


    // Hack to foul FatFs to not handle a final partial sector (to avoid a memcpy)
    file.obj.objsize = (file.obj.objsize + 511) & 0xfffffe00;
    page = 0;
    pDst = 0;

    UnlockFlash();

    while (size)
    {
        if (size > 512)
            read_size = 512;
        else
            read_size = size;

        // On _any_ error the upgrade will fail :-(
        // then the firmware needs to be upgraded by another way!
        FileReadNextBlock(&file, sector_buffer);

#ifndef GCC_OPTIMZES_TOO_MUCH  // the latest gcc 4.8.0 calls memset for this
        // it doesn't hurt to not do this at all

        // fill the rest of buffer
        for (i = read_size; i < 512; i++)
            sector_buffer[i] = 0xFF;
#endif

        // programming time: 13.2 ms per disk sector (512B)
        pSrc = (unsigned long*)sector_buffer;
        k = 512/FLASH_PAGESIZE;
        while (k--)
        {
            if(size & 2048) DISKLED_ON
            else DISKLED_OFF;

            i = FLASH_PAGESIZE / 4;
            while (i--) {
                *pDst++ = *pSrc++;
                dmb();
            }

            WriteFlash(page);
            page++;
        }

        size -= read_size;
    }

    DISKLED_OFF;
    MCUReset(); // restart
    for(;;);
}
#pragma section_no_code_init

