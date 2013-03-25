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

static struct {
  fileTYPE file;
  unsigned char sides;
  unsigned char spt;
} disk[2];

static unsigned char floppy_buffer[512];

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

static void mist_set_control(unsigned short ctrl) {
  EnableFpga();
  SPI(MIST_SET_CONTROL);
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

static void mist_get_dmastate() {
  static unsigned char buffer[10];
  int i;

  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  for(i=0;i<10;i++)
    buffer[i] = SPI(0);
  DisableFpga();

  // check if fdc is busy
  if(buffer[8] & 1) {
    // extract contents
    unsigned int dma_address = 256 * 256 * buffer[0] + 256 * buffer[1] + buffer[2];
    unsigned char scnt = buffer[3];
    unsigned char fdc_cmd = buffer[4];
    unsigned char fdc_track = buffer[5];
    unsigned char fdc_sector = buffer[6];
    unsigned char fdc_data = buffer[7];
    unsigned char drv_sel = 3-((buffer[8]>>2)&3); 
    unsigned char drv_side = 1-((buffer[8]>>1)&1); 
    
    // check if a matching disk image has been inserted
    if(drv_sel && disk[drv_sel-1].file.size) {

      // if the fdc has been asked to write protect the disks, then
      // write sector commands should never reach the oi controller

      // read/write sector command
      if((fdc_cmd & 0xc0) == 0x80) {

	// convert track/sector/side into disk offset
	unsigned int offset = drv_side;
	offset += fdc_track * disk[drv_sel-1].sides;
	offset *= disk[drv_sel-1].spt;
	offset += fdc_sector-1;
	
	while(scnt) {
	  DISKLED_ON;

	  FileSeek(&disk[drv_sel-1].file, offset, SEEK_SET);
	  mist_memory_set_address(dma_address);

	  if((fdc_cmd & 0xe0) == 0x80) { 
	    // read from disk ...
	    FileRead(&disk[drv_sel-1].file, floppy_buffer);	  
	    // ... and copy to ram
	    mist_memory_write(floppy_buffer, 256);
	  } else {
	    // read from ram ...
	    mist_memory_read(floppy_buffer, 256);
	    // ... and write to disk
	    FileWrite(&(disk[drv_sel-1].file), floppy_buffer);
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
    char name[] = "DISK_A  ST ";
    name[5] = 'A'+i;

    fileTYPE file;
    if(FileOpen(&file, name))
      tos_insert_disk(i, &file);
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

  if(!disk[index].file.size) {
    strcpy(buffer, "* no disk *");
    return buffer;
  }

  // copy and append nul
  strncpy(buffer, disk[index].file.name, 8);
  for(c=buffer+7;*c==' ';c--); c++;
  *c++ = '.';
  strncpy(c, disk[index].file.name+8, 3);
  for(c+=2;*c==' ';c--); c++;
  *c++='\0';

  return buffer;
}

char tos_disk_is_inserted(char index) {
  return (disk[index].file.size != 0);
}

void tos_insert_disk(char i, fileTYPE *file) {
  iprintf("%c: eject\n", i+'A');

  // toggle write protect bit to help tos detect a media change
  int wp_bit = (!i)?TOS_CONTROL_FDC_WR_PROT_A:TOS_CONTROL_FDC_WR_PROT_B;

  // any disk ejected is "write protected" (as nothing covers the write protect mechanism)
  mist_set_control(tos_system_ctrl | wp_bit);

  // first "eject" disk
  disk[i].file.size = 0;
  disk[i].sides = 1;
  disk[i].spt = 0;

  // no new disk given?
  if(!file) return;

  // open floppy
  disk[i].file = *file;
  iprintf("%c: insert %.11s\n", i+'A', disk[i].file.name);

  // check image size and parameters
    
  // check if image size suggests it's a two sided disk
  if(disk[i].file.size > 80*9*512)
    disk[i].sides = 2;
    
  // try common sector/track values
  int s, t;
  for(s=9;s<=12;s++)
    for(t=80;t<=85;t++)
      if(512*s*t*disk[i].sides == disk[i].file.size)
	disk[i].spt = s;
  
  if(!disk[i].spt) {
    iprintf("%c: image has unknown size\n", i+'A');
    
    // todo: try to extract that info from the image itself
    
    disk[i].file.size = 0;
  } else {
    // restore state of write protect bit
    tos_update_sysctrl(tos_system_ctrl);
    iprintf("%c: detected %d sides with %d sectors per track\n", 
	    i+'A', disk[i].sides, disk[i].spt);
  }
}
