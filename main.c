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

#include "AT91SAM7S256.h"
#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "hardware.h"
#include "mmc.h"
#include "fat.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "firmware.h"
#include "menu.h"
#include "config.h"
#include "user_io.h"
#include "boot_logo.h"
#include "tos.h"

// #include "xmenu.h"

#ifdef MIST
#include "usb.h"
#endif

const char version[] = {"$VER:ATH" VDATE};

extern hdfTYPE hdf[2];

unsigned char Error;
extern adfTYPE df[4];
char s[40];

void FatalError(unsigned long error)
{
    unsigned long i;

    printf("Fatal error: %lu\r", error);

    while (1)
    {
        for (i = 0; i < error; i++)
        {
            DISKLED_ON;
            WaitTimer(250);
            DISKLED_OFF;
            WaitTimer(250);
        }
        WaitTimer(1000);
    }
}

void HandleFpga(void)
{
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
    HandleHDD(c1, c2);

    UpdateDriveStatus();
}

extern void inserttestfloppy();

int main(void)
{
    unsigned char rc;
    unsigned char key;
    unsigned long time;
    unsigned short spiclk;

#ifdef __GNUC__
    __init_hardware();

    // make sure printf works over rs232
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif   

    DISKLED_ON;

    Timer_Init();

    USART_Init(115200);

    printf("\rMinimig by Dennis van Weeren");
    printf("\rARM Controller by Jakub Bednarski\r\r");
    printf("Version %s\r\r", version+5);

    SPI_Init();

    if (!MMC_Init())
      FatalError(1);
    
    spiclk = MCLK / ((AT91C_SPI_CSR[0] & AT91C_SPI_SCBR) >> 8) / 1000000;
    printf("spiclk: %u MHz\r", spiclk);

#ifdef USB_SEL
    usb_init();
#endif

    if (!FindDrive())
        FatalError(2);

    ChangeDirectory(DIRECTORY_ROOT);

    time = GetTimer(0);
    
    user_io_init();

#ifdef MIST
    if(!user_io_dip_switch1())
#endif
    {
      if (ConfigureFpga()) {
        time = GetTimer(0) - time;
        printf("FPGA configured in %lu ms\r", time >> 20);
      } else {
        printf("FPGA configuration failed\r");
        FatalError(3);
      }

      WaitTimer(100); // let's wait some time till reset is inactive so we can get a valid keycode
    }

    user_io_detect_core_type();
    if(user_io_core_type() == CORE_TYPE_MINIMIG) {
      puts("Running minimig setup");

      draw_boot_logo();
      BootPrintEx("**** MINIMIG for MiST ****");
      BootPrintEx("Minimig by Dennis van Weeren");
      BootPrintEx("Updates by Jakub Bednarski, Tobias Gubener, Sascha Boing, A.M. Robinson");
      BootPrintEx("DE1 port by Rok Krajnc (rok.krajnc@gmail.com)");
      BootPrintEx("MiST port by Till Harbaum (till@harbaum.org)");
      BootPrintEx(" ");
      BootPrintEx("For support, see http://www.minimig.net");
      BootPrint(" ");
      
      ChangeDirectory(DIRECTORY_ROOT);
      
      //eject all disk
      df[0].status = 0;
      df[1].status = 0;
      df[2].status = 0;
      df[3].status = 0;
      
      BootPrint(" ");
      BootPrintEx("Booting ...");
      printf("Booting ...\r");
      
      WaitTimer(6000);
      config.kickstart.name[0]=0;
      SetConfigurationFilename(0); // Use default config
      LoadConfiguration(0);  // Use slot-based config filename

    } // end of minimig setup

    if(user_io_core_type() == CORE_TYPE_MIST) {
      puts("Running mist setup");

      tos_upload(NULL);

      // end of mist setup
    }

    int cnt = 0;
    
    while (1) {
      user_io_poll();

#ifdef USB_SEL
      usb_poll();
#endif

      // MIST (atari) core supports the same UI as Minimig
      if(user_io_core_type() == CORE_TYPE_MIST) {
	if(!MMC_CheckCard()) 
	  tos_eject_all();

	HandleUI();
      }

      // call original minimig handlers if minimig core is found
      if(user_io_core_type() == CORE_TYPE_MINIMIG) {
	if(!MMC_CheckCard()) 
	  EjectAllFloppies();

	HandleFpga();
	HandleUI();
      }
    }
    return 0;
}
