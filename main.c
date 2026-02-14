 /*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

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

// 2008-10-04   - porting to ARM
// 2008-10-06   - support for 4 floppy drives
// 2008-10-30   - hdd write support
// 2009-05-01   - subdirectory support
// 2009-06-26   - SDHC and FAT32 support
// 2009-08-10   - hardfile selection
// 2009-09-11   - minor changes to hardware initialization routine
// 2009-10-10   - any length fpga core file support
// 2009-11-14   - adapted floppy gap size
//              - changes to OSD labels
// 2009-12-24   - updated version number
// 2010-01-09   - changes to floppy handling
// 2010-07-28   - improved menu button handling
//              - improved FPGA configuration routines
//              - added support for OSD vsync
// 2010-08-15   - support for joystick emulation
// 2010-08-18   - clean-up

#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "hardware.h"
#include "mmc.h"
#include "fat_compat.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "data_io.h"
#include "c64files.h"
#include "snes.h"
#include "zx_col.h"
#include "arc_file.h"
#include "serial_sink.h"
#include "menu-8bit.h"
#include "font.h"
#include "tos.h"
#include "usb.h"
#include "debug.h"
#include "mist_cfg.h"
#include "usbdev.h"
#include "cdc_control.h"
#include "storage_control.h"
#include "FatFs/diskio.h"
#ifdef HAVE_QSPI
#include "qspi.h"
#endif
#include "eth.h"

#ifndef _WANT_IO_LONG_LONG
#error "newlib lacks support of long long type in IO functions. Please use a toolchain that was compiled with option --enable-newlib-io-long-long."
#endif

const char version[] = {"$VER:ATH" VDATE};

unsigned char Error;
char s[FF_LFN_BUF + 1];

unsigned long storage_size = 0;

void FatalError(unsigned long error) {
  unsigned long i;
  
  iprintf("Fatal error: %lu\r", error);
  
  while (1) {
    for (i = 0; i < error; i++) {
      DISKLED_ON;
      WaitTimer(250);
      DISKLED_OFF;
      WaitTimer(250);
    }
    WaitTimer(1000);
    MCUReset();
  }
}

void HandleFpga(void) {
  unsigned char  c1, c2;
  
  EnableFpga();
  c1 = SPI(0); // cmd request and drive number
  c2 = SPI(0); // track number
  SPI(0);
  SPI(0);
  SPI(0);
  SPI(0);
  DisableFpga();
  
  HandleFDD(c1, c2);
  HandleHDD(c1, c2, 1);
  
  UpdateDriveStatus();
}

extern void inserttestfloppy();

#ifdef USB_STORAGE
int GetUSBStorageDevices()
{
  uint32_t to = GetTimer(2000);

  // poll usb 2 seconds or until a mass storage device becomes ready
  while(!storage_devices && !CheckTimer(to))
    usb_poll();

  return storage_devices;
}
#endif

int main(void)
{
    uint8_t mmc_ok = 0;

#ifdef __GNUC__
    __init_hardware();

    // make sure printf works over rs232
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif   

    DISKLED_ON;

    data_io_init();
    page_plugin_init();
    c64files_init();
    snes_init();
    zx_init();
    serial_sink_init();
    Timer_Init();

    USART_Init(115200);

    iprintf("\rMinimig by Dennis van Weeren");
    iprintf("\rARM Controller by Jakub Bednarski\r\r");
    iprintf("Version %s\r\r", version+5);

    spi_init();
#ifdef HAVE_QSPI
    qspi_init();
#endif

    if(MMC_Init()) mmc_ok = 1;
    else           spi_fast();

    iprintf("spiclk: %u MHz\r", GetSPICLK());

    usb_init();

    InitADC();

#ifdef USB_STORAGE
    if(UserButton()) USB_BOOT_VAR = (USB_BOOT_VAR == USB_BOOT_VALUE) ? 0 : USB_BOOT_VALUE;

    if(USB_BOOT_VAR == USB_BOOT_VALUE)
      if (!GetUSBStorageDevices()) {
        if(!mmc_ok)
          FatalError(ERROR_FILE_NOT_FOUND);
      } else
        fat_switch_to_usb();  // redirect file io to usb
    else {
#endif
      if(!mmc_ok) {
#ifdef USB_STORAGE
        if(!GetUSBStorageDevices())
          FatalError(ERROR_FILE_NOT_FOUND);

        fat_switch_to_usb();  // redirect file io to usb
#else
        FatalError(ERROR_FILE_NOT_FOUND);
#endif
      }
#ifdef USB_STORAGE
    }
#endif

    if (!FindDrive())
        FatalError(ERROR_INVALID_DATA);

    disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &storage_size);
    storage_size >>= 11;

    eth_init();

    ChangeDirectoryName("/");

    arc_reset();

    font_load();

    user_io_init();

    // tos config also contains cdc redirect settings used by minimig
    tos_config_load(-1);

    int64_t mod = -1;

    if((USB_LOAD_VAR != USB_LOAD_VALUE) && !user_io_dip_switch1()) {
        mod = arc_open("/CORE.ARC");
    } else {
        user_io_detect_core_type();
        if(user_io_core_type() != CORE_TYPE_UNKNOWN && !user_io_create_config_name(s, "ARC", CONFIG_ROOT)) {
            // when loaded from USB, try to load the development ARC file
            iprintf("Load development ARC: %s\n", s);
            mod = arc_open(s);
        }
    }

    unsigned char err;
    if(mod < 0 || !strlen(arc_get_rbfname())) {
        err = fpga_init(NULL); // error opening default ARC, try with default RBF
    } else {
        user_io_set_core_mod(mod);
        strncpy(s, arc_get_rbfname(), sizeof(s)-5);
        strcat(s,".RBF");
        err = fpga_init(s);
    }
    if (err != ERROR_NONE) FatalError(err);

    usb_dev_open();

    while (1) {
      mmc_ok = fat_medium_present();

      cdc_control_poll();
      storage_control_poll();

      user_io_poll();

      usb_poll();

      eth_poll();

      // MIST (atari) core supports the same UI as Minimig
      if((user_io_core_type() == CORE_TYPE_MIST) ||
         (user_io_core_type() == CORE_TYPE_MIST2)) {
         if(!mmc_ok)
             tos_eject_all();

         HandleUI();
      }

      // call original minimig handlers if minimig core is found
      if((user_io_core_type() == CORE_TYPE_MINIMIG) ||
        (user_io_core_type() == CORE_TYPE_MINIMIG2)) {
        if(!mmc_ok)
            EjectAllFloppies();

        HandleFpga();
        HandleUI();
      }

      // 8 bit cores can also have a ui if a valid config string can be read from it
      if((user_io_core_type() == CORE_TYPE_8BIT) && 
	 user_io_is_8bit_with_config_string())
	HandleUI();

      // Archie core will get its own treatment one day ...
      if(user_io_core_type() == CORE_TYPE_ARCHIE)
	HandleUI();
    }
    return 0;
}
