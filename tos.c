#include "stdio.h"
#include "string.h"
#include "hardware.h"

#include "tos.h"
#include "fat.h"
#include "fpga.h"

#define TOS_BASE_ADDRESS_192k    0xfc0000
#define TOS_BASE_ADDRESS_256k    0xe00000
#define CART_BASE_ADDRESS        0xfa0000
#define VIDEO_BASE_ADDRESS       0x010000

unsigned long tos_system_ctrl = TOS_MEMCONFIG_4M;

static unsigned char font[2048];  // buffer for 8x16 atari font

// two floppies
static struct {
  fileTYPE file;
  unsigned char sides;
  unsigned char spt;
} fdd_image[2];

// one harddisk
fileTYPE hdd_image;

static unsigned char dma_buffer[512];

static const char *acsi_cmd_name(int cmd) {
  static const char *cmdname[] = {
    "Test Drive Ready", "Restore to Zero", "Cmd $2", "Request Sense",
    "Format Drive", "Read Block limits", "Reassign Blocks", "Cmd $7", 
    "Read Sector", "Cmd $9", "Write Sector", "Seek Block", 
    "Cmd $C", "Cmd $D", "Cmd $E", "Cmd $F", 
    "Cmd $10", "Cmd $11", "Inquiry", "Verify", 
    "Cmd $14", "Mode Select", "Cmd $16", "Cmd $17", 
    "Cmd $18", "Cmd $19", "Mode Sense", "Start/Stop Unit", 
    "Cmd $1C", "Cmd $1D", "Cmd $1E", "Cmd $1F"
  };
  
  return cmdname[cmd];
}

static void mist_memory_set_address(unsigned long a) {
  a >>= 1;   // make word address

  EnableFpga();
  SPI(MIST_SET_ADDRESS);
  SPI((a >> 24) & 0xff);
  SPI((a >> 16) & 0xff);
  SPI((a >>  8) & 0xff);
  SPI((a >>  0) & 0xff);
  DisableFpga();
}


static void mist_set_control(unsigned long ctrl) {
  EnableFpga();
  SPI(MIST_SET_CONTROL);
  SPI((ctrl >> 24) & 0xff);
  SPI((ctrl >> 16) & 0xff);
  SPI((ctrl >>  8) & 0xff);
  SPI((ctrl >>  0) & 0xff);
  DisableFpga();
}

static void hexdump(void *data, unsigned long size, unsigned long offset) {
  int i, b2c;
  unsigned long n=0;
  char *ptr = data;

  if(!size) return;

  while(size>0) {
    iprintf("%08x: ", n + offset);

    b2c = (size>16)?16:size;
    for(i=0;i<b2c;i++)      iprintf("%02x ", 0xff&ptr[i]);
    iprintf("  ");
    for(i=0;i<(16-b2c);i++) iprintf("   ");
    for(i=0;i<b2c;i++)      iprintf("%c", isprint(ptr[i])?ptr[i]:'.');
    iprintf("\n");
    ptr  += b2c;
    size -= b2c;
    n    += b2c;
  }
}

static void mist_memory_read(char *data, unsigned long words) {
  EnableFpga();
  SPI(MIST_READ_MEMORY);

  // transmitted bytes must be multiple of 2 (-> words)
  while(words--) {
    *data++ = SPI(0);
    *data++ = SPI(0);
  }

  DisableFpga();
}

static void mist_memory_write(char *data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI(*data++);
    SPI(*data++);
  }

  DisableFpga();
}

void mist_memory_set(char data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI(data);
    SPI(data);
  }

  DisableFpga();
}

static void handle_acsi(unsigned char *buffer) {
  unsigned char target = buffer[9] >> 5;
  unsigned char cmd = buffer[9] & 0x1f;
  unsigned int dma_address = 256 * 256 * buffer[0] + 
    256 * buffer[1] + buffer[2];
  unsigned char scnt = buffer[3];
  unsigned long lba = 256 * 256 * (buffer[10] & 0x1f) +
    256 * buffer[11] + buffer[12];
  unsigned short length = buffer[13];
  if(length == 0) length = 256;
  
  iprintf("ACSI: target %d, \"%s\"\n", target, acsi_cmd_name(cmd));
  iprintf("ACSI: lba %lu, length %u\n", lba, length);
  iprintf("DMA: scnt %u, addr %p\n", scnt, dma_address);
  
  // only a harddisk on ACSI 0 is supported
  // ACSI 0 is only supported if a image is loaded
  if((target == 0) && (hdd_image.size != 0)) {
    mist_memory_set_address(dma_address);
  
    switch(cmd) {
    case 0x08: // read sector
      DISKLED_ON;
      while(length) {
	FileSeek(&hdd_image, lba++, SEEK_SET);
	FileRead(&hdd_image, dma_buffer);	  
	mist_memory_write(dma_buffer, 256);
	length--;
      }
      DISKLED_OFF;
      break;
      
    case 0x0a: // write sector
      DISKLED_ON;
      while(length) {
	mist_memory_read(dma_buffer, 256);
	FileSeek(&hdd_image, lba++, SEEK_SET);
	FileWrite(&hdd_image, dma_buffer);	  
	length--;
      }
      DISKLED_OFF;
      break;
      
    case 0x12: // inquiry
      memset(dma_buffer, 0, 512);
      dma_buffer[2] = 1;                              // ANSI version
      dma_buffer[4] = length-8;                        // len
      memcpy(dma_buffer+8,  "MIST    ", 8);           // Vendor
      memcpy(dma_buffer+16, "                ", 16);  // Clear device entry
      memcpy(dma_buffer+16, hdd_image.name, 11);      // Device
      mist_memory_write(dma_buffer, length/2);      
      break;
      
    case 0x1a: // mode sense
      { unsigned int blocks = hdd_image.size / 512;
	iprintf("ACSI: mode sense, blocks = %u\n", blocks);
	memset(dma_buffer, 0, 512);
	dma_buffer[3] = 8;            // size of extent descriptor list
	dma_buffer[5] = blocks >> 16;
	dma_buffer[6] = blocks >> 8;
	dma_buffer[7] = blocks;
	dma_buffer[10] = 2;           // byte 1 of block size in bytes (512)
	mist_memory_write(dma_buffer, length/2);      
      }
      break;
      
    default:
      iprintf("ACSI: Unsupported command\n");
      break;
    }
  } else
    iprintf("ACSI: Request for unsupported target\n");

  EnableFpga();
  SPI(MIST_ACK_DMA);
  DisableFpga(); 
}

static void handle_fdc(unsigned char *buffer) {
  // extract contents
  unsigned int dma_address = 256 * 256 * buffer[0] + 
    256 * buffer[1] + buffer[2];
  unsigned char scnt = buffer[3];
  unsigned char fdc_cmd = buffer[4];
  unsigned char fdc_track = buffer[5];
  unsigned char fdc_sector = buffer[6];
  unsigned char fdc_data = buffer[7];
  unsigned char drv_sel = 3-((buffer[8]>>2)&3); 
  unsigned char drv_side = 1-((buffer[8]>>1)&1); 
  
  // check if a matching disk image has been inserted
  if(drv_sel && fdd_image[drv_sel-1].file.size) {
    
    // if the fdc has been asked to write protect the disks, then
    // write sector commands should never reach the oi controller
    
    // read/write sector command
    if((fdc_cmd & 0xc0) == 0x80) {
      
      // convert track/sector/side into disk offset
      unsigned int offset = drv_side;
      offset += fdc_track * fdd_image[drv_sel-1].sides;
      offset *= fdd_image[drv_sel-1].spt;
      offset += fdc_sector-1;
      
      while(scnt) {
	DISKLED_ON;
	
	FileSeek(&fdd_image[drv_sel-1].file, offset, SEEK_SET);
	mist_memory_set_address(dma_address);
	
	if((fdc_cmd & 0xe0) == 0x80) { 
	  // read from disk ...
	  FileRead(&fdd_image[drv_sel-1].file, dma_buffer);	  
	  // ... and copy to ram
	  mist_memory_write(dma_buffer, 256);
	} else {
	  // read from ram ...
	  mist_memory_read(dma_buffer, 256);
	  // ... and write to disk
	  FileWrite(&(fdd_image[drv_sel-1].file), dma_buffer);
	}
	
	DISKLED_OFF;
	
	scnt--;
	dma_address += 512;
	offset += 1;
      }
      
      EnableFpga();
      SPI(MIST_ACK_DMA);
      DisableFpga(); 
    }
  }
}  

static void mist_get_dmastate() {
  static unsigned char buffer[16];
  int i;
  
  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  for(i=0;i<16;i++)
    buffer[i] = SPI(0);
  DisableFpga();

  // check if acsi is busy
  if(buffer[8] & 0x10) 
    handle_acsi(buffer);

  // check if fdc is busy
  if(buffer[8] & 0x01) 
    handle_fdc(buffer);
}

static void tos_clr() {
  mist_memory_set_address(VIDEO_BASE_ADDRESS);
  mist_memory_set(0, 16000);
}

// color test, used to test the shifter without CPU/TOS
#define COLORS   20
#define PLANES   4

static void tos_color_test() {
  unsigned short buffer[COLORS][PLANES];

  int y;
  for(y=0;y<13;y++) {
    int i, j;
    for(i=0;i<COLORS;i++)
      for(j=0;j<PLANES;j++)
	buffer[i][j] = ((y+i) & (1<<j))?0xffff:0x0000;

    for(i=0;i<16;i++) {
      mist_memory_set_address(VIDEO_BASE_ADDRESS + (16*y+i)*160);
      mist_memory_write((char*)buffer, COLORS*PLANES);
    }
  }
}

static void tos_write(char *str) {
  static int y = 0;
  int l;
  int c = strlen(str);

  {
    char buffer[c];

    // 16 pixel lines
    for(l=0;l<16;l++) {
      char *p = str, *f=buffer;
      while(*p)
	*f++ = font[16 * *p++ + l];
      
      mist_memory_set_address(VIDEO_BASE_ADDRESS + 80*(y+l));
      mist_memory_write(buffer, c/2);
    }
  }
  y+=16;
}

static void tos_font_load() {
  fileTYPE file;
  if(FileOpen(&file,"SYSTEM  FNT")) {
    if(file.size == 4096) {
      int i;
      for(i=0;i<4;i++) {
	FileRead(&file, font+i*512);
	FileNextSector(&file);
      }

      tos_clr();

      //      tos_color_test();

      tos_write("\016\017 MIST core \016\017 ");

    } else
      iprintf("SYSTEM.FNT has wrong size\n");      
  } else
    iprintf("SYSTEM.FNT not found\n");      
}

void tos_upload() {

  // put cpu into reset
  mist_set_control(tos_system_ctrl | TOS_CONTROL_CPU_RESET);

  tos_font_load();

  // do the MiST core handling
  tos_write("Uploading TOS ... ");
  iprintf("Uploading TOS ...\n");
  
  DISKLED_ON;
  
  // upload and verify tos image
  fileTYPE file;
  if(FileOpen(&file,"TOS     IMG")) {
    int i;
    char buffer[512];
    unsigned long time;
    unsigned long tos_base = TOS_BASE_ADDRESS_192k;
	
    iprintf("TOS.IMG:\n  size = %d\n", file.size);

    if(file.size == 256*1024)
      tos_base = TOS_BASE_ADDRESS_256k;
    else if(file.size != 192*1024)
      iprintf("WARNING: Unexpected TOS size!\n");

    int blocks = file.size / 512;
    iprintf("  blocks = %d\n", blocks);

    iprintf("  address = $%08x\n", tos_base);

    // extract base address
    FileRead(&file, buffer);

    // clear first 16k
    mist_memory_set_address(0);
    mist_memory_set(0x00, 8192);

#if 0
    iprintf("Erasing:   ");
    
    // clear memory to increase chances of catching write problems
    mist_memory_set_address(tos_base);
    mist_memory_set(0x00, file.size/2);
    iprintf("done\n");
#endif
	
    time = GetTimer(0);
    iprintf("Uploading: [");
    
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);

      // copy first 8 bytes to address 0 as well
      if(i == 0) {
	mist_memory_set_address(0);

	// write first 4 words
	mist_memory_write(buffer, 4);

	// set real tos base address
	mist_memory_set_address(tos_base);
      }
      
      mist_memory_write(buffer, 256);
      
      if(!(i & 7)) iprintf(".");
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    iprintf("]\n");
    
    time = GetTimer(0) - time;
    iprintf("TOS.IMG uploaded in %lu ms\r", time >> 20);
    
  } else
    iprintf("Unable to find tos.img\n");
  
#if 0
  {
    char rx[512], buffer[512];
    int i,j;
    int blocks = file.size / 512;
    
    FileSeek(&file, 0, SEEK_SET);
    
    mist_memory_set_address(TOS_BASE_ADDRESS);
    
    iprintf("Verifying: [");
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);

      mist_memory_read(rx, 256);
      
      if(!(i & 7)) iprintf("+");
      
      for(j=0;j<512;j++) {
	if(buffer[j] != rx[j]) {
	  iprintf("Verify error block %d, byte %x\n", i, j);

	  iprintf("should be:\n");
	  hexdump(buffer, 512, 0);

	  iprintf("is:\n");
	  hexdump(rx, 512, 0);

	  // try to re-read to check whether read or write failed
	  mist_memory_set_address(TOS_BASE_ADDRESS+i*512);
	  mist_memory_read(rx, 256);

	  iprintf("re-read: %s\n", (buffer[j] != rx[j])?"failed":"ok");
	  hexdump(rx, 512, 0);


	  while(1);
	}
      }
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    iprintf("]\n");
  }
#endif
  DISKLED_OFF;

  // upload cartridge 
  if(FileOpen(&file,"CART    IMG")) {
    int i;
    char buffer[512];
	
    iprintf("CART.IMG:\n  size = %d\n", file.size);

    int blocks = file.size / 512;
    iprintf("  blocks = %d\n", blocks);

    iprintf("Uploading: [");
    mist_memory_set_address(CART_BASE_ADDRESS);
    
    DISKLED_ON;
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);
      mist_memory_write(buffer, 256);
      
      if(!(i & 7)) iprintf(".");
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    DISKLED_OFF;
    iprintf("]\n");
    
    iprintf("CART.IMG uploaded\r");
  } else {
    iprintf("Unable to find cart.img\n");

    // erase that ram area to remove any previously uploaded
    // image
    mist_memory_set_address(CART_BASE_ADDRESS);
    mist_memory_set(0, 128*1024/2);
  }

  // try to open both floppies
  int i;
  for(i=0;i<2;i++) {
    char msg[] = "Found floppy disk image for drive X: ";
    char name[] = "DISK_A  ST ";
    msg[34] = name[5] = 'A'+i;

    fileTYPE file;
    if(FileOpen(&file, name)) {
      tos_write(msg);
      tos_insert_disk(i, &file);
    }
  }

  // try to open harddisk image
  hdd_image.size = 0;
  if(FileOpen(&hdd_image, "HARDDISKHD ")) {
    tos_write("Found hard disk image ");
    tos_system_ctrl |= TOS_ACSI0_ENABLE;
  }

  tos_write("Booting ... ");

  // let cpu run (release reset)
  mist_set_control(tos_system_ctrl);
}

static unsigned long get_long(char *buffer, int offset) {
  unsigned long retval = 0;
  int i;
  
  for(i=0;i<4;i++)
    retval = (retval << 8) + *(unsigned char*)(buffer+offset+i);

  return retval;
}

void tos_show_state() {
  static unsigned long mtimer = 0;

  mist_get_dmastate();  

#if 0
  if(CheckTimer(mtimer)) {
    mtimer = GetTimer(2000);
    
    int i;
    char buffer[1024];
    
    mist_memory_set_address(0);
    mist_memory_read(buffer, 64);
    hexdump(buffer, 128, 0);

    // tos system varables are from $400 
    mist_memory_set_address(0x400);
    mist_memory_read(buffer, 512);
    
    iprintf("\n--- SYSTEM VARIABLES ---\n");
    iprintf("memvalid:  $%lx (should be $752019F3)\n", get_long(buffer,0x20));
    iprintf("memcntrl:  $%x  (memory controller low nibble)\n", buffer[0x24]); 
    iprintf("phystop:   $%lx (Physical RAM top)\n", get_long(buffer,0x2e));
    iprintf("memval2:   $%lx (should be $237698AA)\n", get_long(buffer,0x3a));
    iprintf("sshiftmd:  $%x  (Shadow shiftmd, LMH/012)\n", buffer[0x4c]); 
    iprintf("_v_bas_ad: $%lx (Screen memory base)\n", get_long(buffer,0x4e));
    iprintf("_vbclock:  $%lx (vbl counter)\n", get_long(buffer,0x62));
    iprintf("_dskbufp:  $%lx (1k disk buffer)\n", get_long(buffer,0xc6));
    iprintf("_frclock:  $%lx (frame counter)\n", get_long(buffer,0x66));
    iprintf("_hz_200:   $%lx (Raw 200Hz timer)\n", get_long(buffer,0xba));
    iprintf("_sysbase:  $%lx (begin of tos)\n", get_long(buffer,0xf2));
  }
#endif
}

void tos_update_sysctrl(unsigned long n) {
  tos_system_ctrl = n;
  mist_set_control(tos_system_ctrl);
}

char *tos_get_disk_name(char index) {
  static char buffer[13];  // local buffer to assemble file name (8+3+2)
  char *c;

  if(!fdd_image[index].file.size) {
    strcpy(buffer, "* no disk *");
    return buffer;
  }

  // copy and append nul
  strncpy(buffer, fdd_image[index].file.name, 8);
  for(c=buffer+7;*c==' ';c--); c++;
  *c++ = '.';
  strncpy(c, fdd_image[index].file.name+8, 3);
  for(c+=2;*c==' ';c--); c++;
  *c++='\0';

  return buffer;
}

char tos_disk_is_inserted(char index) {
  return (fdd_image[index].file.size != 0);
}

void tos_insert_disk(char i, fileTYPE *file) {
  iprintf("%c: eject\n", i+'A');

  // toggle write protect bit to help tos detect a media change
  int wp_bit = (!i)?TOS_CONTROL_FDC_WR_PROT_A:TOS_CONTROL_FDC_WR_PROT_B;

  // any disk ejected is "write protected" (as nothing covers the write protect mechanism)
  mist_set_control(tos_system_ctrl | wp_bit);

  // first "eject" disk
  fdd_image[i].file.size = 0;
  fdd_image[i].sides = 1;
  fdd_image[i].spt = 0;

  // no new disk given?
  if(!file) return;

  // open floppy
  fdd_image[i].file = *file;
  iprintf("%c: insert %.11s\n", i+'A', fdd_image[i].file.name);

  // check image size and parameters
    
  // check if image size suggests it's a two sided disk
  if(fdd_image[i].file.size > 80*9*512)
    fdd_image[i].sides = 2;
    
  // try common sector/track values
  int m, s, t;
  for(m=0;m<=2;m++)  // multiplier for hd/ed disks
    for(s=9;s<=12;s++)
      for(t=80;t<=85;t++)
	if(512*(1<<m)*s*t*fdd_image[i].sides == fdd_image[i].file.size)
	  fdd_image[i].spt = s*(1<<m);
  
  if(!fdd_image[i].spt) {
    // read first sector from disk
    if(MMC_Read(0, dma_buffer)) {
      fdd_image[i].spt = dma_buffer[24] + 256 * dma_buffer[25];
      fdd_image[i].sides = dma_buffer[26] + 256 * dma_buffer[27];
    } else
      fdd_image[i].file.size = 0;
  }

  if(fdd_image[i].file.size) {
    // restore state of write protect bit
    tos_update_sysctrl(tos_system_ctrl);
    iprintf("%c: detected %d sides with %d sectors per track\n", 
	    i+'A', fdd_image[i].sides, fdd_image[i].spt);
  }
}
