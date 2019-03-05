// config.c

#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "hardware.h"
#include "mmc.h"
#include "boot.h"
#include "fat.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "firmware.h"
#include "menu.h"
#include "config.h"
#include "user_io.h"
#include "usb/usb.h"

configTYPE config;
fileTYPE file;
extern char s[40];
char configfilename[12];
char DebugMode=0;
unsigned char romkey[3072];
RAFile romfile;


// TODO fix SPIN macros all over the place!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0")


//// UploadKickstart() ////
char UploadKickstart(char *name)
{
  int keysize=0;
  char filename[12];

  strncpy(filename, name, 8); // copy base name
  strcpy(&filename[8], "ROM"); // add extension

  BootPrint("Checking for Amiga Forever key file:");
  if(FileOpen(&file,"ROM     KEY")) {
    keysize=file.size;
    if(file.size<sizeof(romkey)) {
      int c=0;
      while(c<keysize) {
        FileRead(&file, &romkey[c]);
        c+=512;
        FileNextSector(&file);
      }
      BootPrint("Loaded Amiga Forever key file");
    } else {
      BootPrint("Amiga Forever keyfile is too large!");
    }
  }
  BootPrint("Loading file: ");
  BootPrint(filename);

  if (RAOpen(&romfile, filename)) {
    if (romfile.size == 0x100000) {
      // 1MB Kickstart ROM
      BootPrint("Uploading 1MB Kickstart ...");
      SendFileV2(&romfile, NULL, 0, 0xe00000, romfile.size>>10);
      SendFileV2(&romfile, NULL, 0, 0xf80000, romfile.size>>10);
      return(1);
    } else if(romfile.size == 0x80000) {
      // 512KB Kickstart ROM
      BootPrint("Uploading 512KB Kickstart ...");
      if (minimig_v1()) {
        PrepareBootUpload(0xF8, 0x08);
        SendFile(&romfile);
      } else {
        SendFileV2(&romfile, NULL, 0, 0xf80000, romfile.size>>9);
        RAOpen(&romfile, filename);
        SendFileV2(&romfile, NULL, 0, 0xe00000, romfile.size>>9);
      }
      return(1);
    } else if ((romfile.size == 0x8000b) && keysize) {
      // 512KB Kickstart ROM
      BootPrint("Uploading 512 KB Kickstart (Probably Amiga Forever encrypted...)");
      if (minimig_v1()) {
        PrepareBootUpload(0xF8, 0x08);
        SendFileEncrypted(&romfile,romkey,keysize);
      } else {
        SendFileV2(&romfile, romkey, keysize, 0xf80000, romfile.size>>9);
        RAOpen(&romfile, filename);
        SendFileV2(&romfile, romkey, keysize, 0xe00000, romfile.size>>9);
      }
      return(1);
    } else if (romfile.size == 0x40000) {
      // 256KB Kickstart ROM
      BootPrint("Uploading 256 KB Kickstart...");
      if (minimig_v1()) {
        PrepareBootUpload(0xF8, 0x04);
        SendFile(&romfile);
      } else {
        SendFileV2(&romfile, NULL, 0, 0xf80000, romfile.size>>9);
        RAOpen(&romfile, filename); // TODO will this work
        SendFileV2(&romfile, NULL, 0, 0xfc0000, romfile.size>>9);
      }
      return(1);
    } else if ((romfile.size == 0x4000b) && keysize) {
      // 256KB Kickstart ROM
      BootPrint("Uploading 256 KB Kickstart (Probably Amiga Forever encrypted...");
      if (minimig_v1()) {
        PrepareBootUpload(0xF8, 0x04);
        SendFileEncrypted(&romfile,romkey,keysize);
      } else {
        SendFileV2(&romfile, romkey, keysize, 0xf80000, romfile.size>>9);
        RAOpen(&romfile, filename); // TODO will this work
        SendFileV2(&romfile, romkey, keysize, 0xfc0000, romfile.size>>9);
      }
      return(1);
    } else {
      BootPrint("Unsupported ROM file size!");
    }
  } else {
    siprintf(s, "No \"%s\" file!", filename);
    BootPrint(s);
  }
  return(0);
}


//// UploadActionReplay() ////
char UploadActionReplay()
{
  if(minimig_v1()) {
    if (RAOpen(&romfile, "AR3     ROM")) {
      if (romfile.file.size == 0x40000) {
        // 256 KB Action Replay 3 ROM
        BootPrint("\nUploading Action Replay ROM...");
        PrepareBootUpload(0x40, 0x04);
        SendFile(&romfile);
        ClearMemory(0x440000, 0x40000);
        return(1);
      } else {
        BootPrint("\nUnsupported AR3.ROM file size!!!");
        /* FatalError(6); */
        return(0);
      }
    }
  } else {
    if (RAOpen(&romfile, "HRTMON  ROM")) {
      int adr, data;
      puts("Uploading HRTmon ROM... ");
      SendFileV2(&romfile, NULL, 0, 0xa10000, (romfile.file.size+511)>>9);
      // HRTmon config
      adr = 0xa10000 + 20;
      spi_osd_cmd32le_cont(OSD_CMD_WR, adr);
      data = 0x00800000; // mon_size, 4 bytes
      SPI((data>>24)&0xff); SPI((data>>16)&0xff); SPIN(); SPIN(); SPIN(); SPIN(); SPI((data>>8)&0xff); SPI((data>>0)&0xff);
      data = 0x00; // col0h, 1 byte
      SPI((data>>0)&0xff);
      data = 0x5a; // col0l, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x0f; // col1h, 1 byte
      SPI((data>>0)&0xff);
      data = 0xff; // col1l, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x01; // right, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x00; // keyboard, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x01; // key, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = config.enable_ide ? 1 : 0; // ide, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x01; // a1200, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = config.chipset&CONFIG_AGA ? 1 : 0; // aga, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x01; // insert, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x0f; // delay, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x01; // lview, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0x00; // cd32, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = config.chipset&CONFIG_NTSC ? 1 : 0; // screenmode, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 1; // novbr, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 0; // entered, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      data = 1; // hexmode, 1 byte
      SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      adr = 0xa10000 + 68;
      spi_osd_cmd32le_cont(OSD_CMD_WR, adr);
      data = ((config.memory&0x3) + 1) * 512 * 1024; // maxchip, 4 bytes TODO is this correct?
      SPI((data>>24)&0xff); SPI((data>>16)&0xff); SPIN(); SPIN(); SPIN(); SPIN(); SPI((data>>8)&0xff); SPI((data>>0)&0xff);
      SPIN(); SPIN(); SPIN(); SPIN();
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      return(1);
    } else {
      puts("\rhrtmon.rom not found!\r");
      return(0);
    }
  }
  return(0);
}


//// SetConfigurationFilename() ////
void SetConfigurationFilename(int config)
{
  if(config)
    siprintf(configfilename,"MINIMIG%dCFG",config);
  else
    strcpy(configfilename,"MINIMIG CFG");
}


//// ConfigurationExists() ////
unsigned char ConfigurationExists(char *filename)
{
  if(!filename) {
    // use slot-based filename if none provided
    filename=configfilename;
  }
  if (FileOpen(&file, filename)) {
    return(1);
  }
  return(0);
}


//// LoadConfiguration() ////
unsigned char LoadConfiguration(char *filename)
{
  static const char config_id[] = "MNMGCFG0";
  char updatekickstart=0;
  char result=0;
  unsigned char key, i;

  if(!filename) {
    // use slot-based filename if none provided
    filename=configfilename;
  }

  // load configuration data
  if (FileOpen(&file, filename)) {
    BootPrint("Opened configuration file\n");
    iprintf("Configuration file size: %lu\r", file.size);
    if (file.size == sizeof(config)) {
      FileRead(&file, sector_buffer);
      configTYPE *tmpconf=(configTYPE *)&sector_buffer;
      // check file id and version
      if (strncmp(tmpconf->id, config_id, sizeof(config.id)) == 0) {
        // A few more sanity checks...
        if(tmpconf->floppy.drives<=4) {
          // If either the old config and new config have a different kickstart file,
          // or this is the first boot, we need to upload a kickstart image.
          if(strncmp(tmpconf->kickstart.name,config.kickstart.name,8)!=0) {
            updatekickstart=true;
          }
          memcpy((void*)&config, (void*)sector_buffer, sizeof(config));
          result=1; // We successfully loaded the config.
        } else {
          BootPrint("Config file sanity check failed!\n");
        }
      } else {
        BootPrint("Wrong configuration file format!\n");
      }
    } else {
      iprintf("Wrong configuration file size: %lu (expected: %lu)\r", file.size, sizeof(config));
    }
  }
  if(!result) {
    BootPrint("Can not open configuration file!\n");
    BootPrint("Setting config defaults\n");
    // set default configuration
    memset((void*)&config, 0, sizeof(config));  // Finally found default config bug - params were reversed!
    strncpy(config.id, config_id, sizeof(config.id));
    strncpy(config.kickstart.name, "KICK    ", sizeof(config.kickstart.name));
    config.kickstart.long_name[0] = 0;
    config.memory = 0x15;
    config.cpu = 0;
    config.chipset = 0;
    config.floppy.speed=CONFIG_FLOPPY2X;
    config.floppy.drives=1;
    config.enable_ide=0;
    config.hardfile[0].enabled = 1;
    strncpy(config.hardfile[0].name, "HARDFILE", sizeof(config.hardfile[0].name));
    config.hardfile[0].long_name[0]=0;
    strncpy(config.hardfile[1].name, "HARDFILE", sizeof(config.hardfile[1].name));
    config.hardfile[1].long_name[0]=0;
    config.hardfile[1].enabled = 2;  // Default is access to entire SD card
    updatekickstart=true;
    BootPrint("Defaults set\n");
  }

  // print config to boot screen
  if (minimig_v2()) {
    char cfg_str[41];
    siprintf(cfg_str, "CPU:     %s", config_cpu_msg[config.cpu & 0x03]); BootPrintEx(cfg_str);
    siprintf(cfg_str, "Chipset: %s", config_chipset_msg [(config.chipset >> 2) & (minimig_v1()?3:7)]); BootPrintEx(cfg_str);
    siprintf(cfg_str, "Memory:  CHIP: %s  FAST: %s  SLOW: %s", config_memory_chip_msg[(config.memory >> 0) & 0x03], config_memory_fast_msg[(config.memory >> 4) & 0x03], config_memory_slow_msg[(config.memory >> 2) & 0x03]); BootPrintEx(cfg_str);
  }

  // wait up to 3 seconds for keyboard to appear. If it appears wait another
  // two seconds for the user to press a key
  int8_t keyboard_present = 0;
  for(i=0;i<3;i++) {
    unsigned long to = GetTimer(1000);
    while(!CheckTimer(to))
      usb_poll();

    // check if keyboard just appeared
    if(!keyboard_present && hid_keyboard_present()) {
      // BootPrintEx("Press F1 for NTSC, F2 for PAL");
      keyboard_present = 1;
      i = 0;
    }
  }
  
  key = OsdGetCtrl();
  if (key == KEY_F1) {
    // BootPrintEx("Forcing NTSC video ...");
    // force NTSC mode if F1 pressed
    config.chipset |= CONFIG_NTSC;
  }

  if (key == KEY_F2) {
    // BootPrintEx("Forcing PAL video ...");
    // force PAL mode if F2 pressed
    config.chipset &= ~CONFIG_NTSC;
  }

  ApplyConfiguration(updatekickstart);

  return(result);
}


//// ApplyConfiguration() ////
void ApplyConfiguration(char reloadkickstart)
{
  ConfigCPU(config.cpu);

  if(reloadkickstart) {
    if(minimig_v1()) {
      ConfigChipset(config.chipset | CONFIG_TURBO); // set CPU in turbo mode
      ConfigFloppy(1, CONFIG_FLOPPY2X); // set floppy speed
      OsdReset(RESET_BOOTLOADER);

      if (!UploadKickstart(config.kickstart.name)) {
        strcpy(config.kickstart.name, "KICK    ");
        if (!UploadKickstart(config.kickstart.name)) {
          strcpy(config.kickstart.name, "AROS    ");
          if (!UploadKickstart(config.kickstart.name)) {
            FatalError(6);
          }  
        }
      }

      //if (!CheckButton() && !config.disable_ar3) {
        // load Action Replay
        UploadActionReplay();
      //}
    }
  } else {
    ConfigChipset(config.chipset);
    ConfigFloppy(config.floppy.drives, config.floppy.speed);
  }

  hardfile[0] = &config.hardfile[0];
  hardfile[1] = &config.hardfile[1];

  // Whether or not we uploaded a kickstart image we now need to set various parameters from the config.
  if(OpenHardfile(0)) {
    switch(hdf[0].type) {
      // Customise message for SD card acces
      case (HDF_FILE | HDF_SYNTHRDB):
        siprintf(s, "\nHardfile 0 (with fake RDB): %.8s.%.3s", hdf[0].file.name, &hdf[0].file.name[8]);
        break;
      case HDF_FILE:
        siprintf(s, "\nHardfile 0: %.8s.%.3s", hdf[0].file.name, &hdf[0].file.name[8]);
        break;
      case HDF_CARD:
        siprintf(s, "\nHardfile 0: using entire SD card");
        break;
      default:
        siprintf(s, "\nHardfile 0: using SD card partition %d",hdf[0].type-HDF_CARD);  // Number from 1
        break;
    }
    BootPrint(s);
    siprintf(s, "CHS: %u.%u.%u", hdf[0].cylinders, hdf[0].heads, hdf[0].sectors);
    BootPrint(s);
    siprintf(s, "Size: %lu MB", ((((unsigned long) hdf[0].cylinders) * hdf[0].heads * hdf[0].sectors) >> 11));
    BootPrint(s);
    siprintf(s, "Offset: %ld", hdf[0].offset);
    BootPrint(s);
  }

  if(OpenHardfile(1)) {
    switch(hdf[1].type) {
      case (HDF_FILE | HDF_SYNTHRDB):
        siprintf(s, "\nHardfile 1 (with fake RDB): %.8s.%.3s", hdf[1].file.name, &hdf[1].file.name[8]);
        break;
      case HDF_FILE:
        siprintf(s, "\nHardfile 1: %.8s.%.3s", hdf[1].file.name, &hdf[1].file.name[8]);
        break;
      case HDF_CARD:
        siprintf(s, "\nHardfile 1: using entire SD card");
        break;
      default:
        siprintf(s, "\nHardfile 1: using SD card partition %d",hdf[1].type-HDF_CARD);  // Number from 1
        break;
    }
    BootPrint(s);
    siprintf(s, "CHS: %u.%u.%u", hdf[1].cylinders, hdf[1].heads, hdf[1].sectors);
    BootPrint(s);
    siprintf(s, "Size: %lu MB", ((((unsigned long) hdf[1].cylinders) * hdf[1].heads * hdf[1].sectors) >> 11));
    BootPrint(s);
    siprintf(s, "Offset: %ld", hdf[1].offset);
    BootPrint(s);
  }

  ConfigIDE(config.enable_ide, config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);

  siprintf(s, "CPU clock     : %s", config.chipset & 0x01 ? "turbo" : "normal");
  BootPrint(s);
  siprintf(s, "Chip RAM size : %s", config_memory_chip_msg[config.memory & 0x03]);
  BootPrint(s);
  siprintf(s, "Slow RAM size : %s", config_memory_slow_msg[config.memory >> 2 & 0x03]);
  BootPrint(s);
  siprintf(s, "Fast RAM size : %s", config_memory_fast_msg[config.memory >> 4 & 0x03]);
  BootPrint(s);

  siprintf(s, "Floppy drives : %u", config.floppy.drives + 1);
  BootPrint(s);
  siprintf(s, "Floppy speed  : %s", config.floppy.speed ? "fast": "normal");
  BootPrint(s);

  BootPrint("");

  siprintf(s, "\nA600 IDE HDC is %s.", config.enable_ide ? "enabled" : "disabled");
  BootPrint(s);
  siprintf(s, "Master HDD is %s.", config.hardfile[0].present ? config.hardfile[0].enabled ? "enabled" : "disabled" : "not present");
  BootPrint(s);
  siprintf(s, "Slave HDD is %s.", config.hardfile[1].present ? config.hardfile[1].enabled ? "enabled" : "disabled" : "not present");
  BootPrint(s);

#if 0
  if (cluster_size < 64) {
    BootPrint("\n***************************************************");
    BootPrint(  "*  It's recommended to reformat your memory card  *");
    BootPrint(  "*   using 32 KB clusters to improve performance   *");
    BootPrint(  "*           when using large hardfiles.           *");  // AMR
    BootPrint(  "***************************************************");
  }
  iprintf("Bootloading is complete.\r");
#endif

  BootPrint("\nExiting bootloader...");

  ConfigMemory(config.memory);
  ConfigCPU(config.cpu);

  if(minimig_v1()) {
    MM1_ConfigFilter(config.filter.lores, config.filter.hires);
    MM1_ConfigScanlines(config.scanlines);

    if(reloadkickstart) {
      WaitTimer(5000);
      BootExit();
    } else {
      OsdReset(RESET_NORMAL);
    }

    ConfigChipset(config.chipset);
    ConfigFloppy(config.floppy.drives, config.floppy.speed);
  } else {
    ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
    ConfigChipset(config.chipset);
    ConfigFloppy(config.floppy.drives, config.floppy.speed);

    if(reloadkickstart) {
      UploadActionReplay();

      iprintf("Reloading kickstart ...\r");
      TIMER_wait(1000);
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval |= (SPI_RST_CPU | SPI_CPU_HLT);
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      if (!UploadKickstart(config.kickstart.name)) {
        strcpy(config.kickstart.name, "KICK    ");
        if (!UploadKickstart(config.kickstart.name)) {
          FatalError(6);
        }
      }
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval |= (SPI_RST_USR | SPI_RST_CPU);
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval = 0;
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
    } else {
      iprintf("Resetting ...\r");
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval |= (SPI_RST_USR | SPI_RST_CPU);
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval = 0;
      SPI(rstval);
      DisableOsd();
      SPIN(); SPIN(); SPIN(); SPIN();
    }
  }
}


//// SaveConfiguration() ////
unsigned char SaveConfiguration(char *filename)
{
  if(!filename) {
    // use slot-based filename if none provided
    filename=configfilename;
  }

  // save configuration data
  if (FileOpen(&file, filename)) {
    iprintf("Configuration file size: %lu\r", file.size);
    if (file.size != sizeof(config)) {
      file.size = sizeof(config);
      if (!UpdateEntry(&file)) {
        return(0);
      }
    }

    memset((void*)&sector_buffer, 0, sizeof(sector_buffer));
    memcpy((void*)&sector_buffer, (void*)&config, sizeof(config));
    FileWrite(&file, sector_buffer);
    return(1);
  } else {
    iprintf("Configuration file not found!\r");
    iprintf("Trying to create a new one...\r");
    strncpy(file.name, filename, 11);
    file.attributes = 0;
    file.size = sizeof(config);
    if (FileCreate(0, &file)) {
      iprintf("File created.\r");
      iprintf("Trying to write new data...\r");
      memset((void*)sector_buffer, 0, sizeof(sector_buffer));
      memcpy((void*)sector_buffer, (void*)&config, sizeof(config));

      if (FileWrite(&file, sector_buffer)) {
        iprintf("File written successfully.\r");
        return(1);
      } else {
        iprintf("File write failed!\r");
      }
    } else {
      iprintf("File creation failed!\r");
    }
  }
  return(0);
}

