#include "stdio.h"
#include "string.h"
#include "hardware.h"

#include "menu.h"
#include "tos.h"
#include "fat.h"
#include "fpga.h"
#include "debug.h"

#define TOS_BASE_ADDRESS_192k    0xfc0000
#define TOS_BASE_ADDRESS_256k    0xe00000
#define CART_BASE_ADDRESS        0xfa0000
#define VIDEO_BASE_ADDRESS       0x010000

unsigned long tos_system_ctrl = TOS_MEMCONFIG_4M | TOS_CONTROL_BLITTER;

static unsigned char font[2048];  // buffer for 8x16 atari font

// default name of TOS image
static char tos_img[12]  = "TOS     IMG";
static char cart_img[12] = "";

// two floppies
static struct {
  fileTYPE file;
  unsigned char sides;
  unsigned char spt;
} fdd_image[2];

// one harddisk
fileTYPE hdd_image[2];

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
    iprintff("\n");
    ptr  += b2c;
    size -= b2c;
    n    += b2c;
  }
}

static void mist_bus_request(char req) {
  EnableFpga();
  SPI(req?MIST_BUS_REQ:MIST_BUS_REL);
  DisableFpga();
}

static void mist_memory_read(char *data, unsigned long words) {
  mist_bus_request(1);

  EnableFpga();
  SPI(MIST_READ_MEMORY);

  // transmitted bytes must be multiple of 2 (-> words)
  while(words--) {
    *data++ = SPI(0);
    *data++ = SPI(0);
  }

  DisableFpga();

  mist_bus_request(0);
}

static void mist_memory_read_block(char *data) {
  EnableFpga();
  SPI(MIST_READ_MEMORY);

  SPI_block_read(data);

  DisableFpga();
}

static void mist_memory_write(char *data, unsigned long words) {
  mist_bus_request(1);

  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI_WRITE(*data++);
    SPI_WRITE(*data++);
  }

  DisableFpga();

  mist_bus_request(0);
}

static void mist_memory_write_block(char *data) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  SPI_block_write(data);

  DisableFpga();
}

void mist_memory_set(char data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI_WRITE(data);
    SPI_WRITE(data);
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

  tos_debugf("ACSI: target %d, \"%s\"\n", target, acsi_cmd_name(cmd));
  tos_debugf("ACSI: lba %lu, length %u\n", lba, length);
  tos_debugf("DMA: scnt %u, addr %p\n", scnt, dma_address);

  // only a harddisk on ACSI 0 is supported
  // ACSI 0 is only supported if a image is loaded
  if((target < 2) && (hdd_image[target].size != 0)) {
    mist_memory_set_address(dma_address);
  
    switch(cmd) {
    case 0x08: // read sector
      DISKLED_ON;
      while(length) {
	FileSeek(&hdd_image[target], lba++, SEEK_SET);
	FileRead(&hdd_image[target], dma_buffer);	  
	mist_memory_write(dma_buffer, 256);
	length--;
      }
      DISKLED_OFF;
      break;
      
    case 0x0a: // write sector
      DISKLED_ON;
      while(length) {
	mist_memory_read(dma_buffer, 256);
	FileSeek(&hdd_image[target], lba++, SEEK_SET);
	FileWrite(&hdd_image[target], dma_buffer);	  
	length--;
      }
      DISKLED_OFF;
      break;

    case 0x12: // inquiry
      tos_debugf("ACSI: Inquiry %11s\n", hdd_image[target].name);
      memset(dma_buffer, 0, 512);
      dma_buffer[2] = 1;                                   // ANSI version
      dma_buffer[4] = length-8;                            // len
      memcpy(dma_buffer+8,  "MIST    ", 8);                // Vendor
      memcpy(dma_buffer+16, "                ", 16);       // Clear device entry
      memcpy(dma_buffer+16, hdd_image[target].name, 11);   // Device
      mist_memory_write(dma_buffer, length/2);      
      break;
      
    case 0x1a: // mode sense
      { unsigned int blocks = hdd_image[target].size / 512;
	tos_debugf("ACSI: mode sense, blocks = %u\n", blocks);
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
      tos_debugf("ACSI: Unsupported command\n");
      break;
    }
  } else
    tos_debugf("ACSI: Request for unsupported target\n");

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

  if(!str) {
    y = 0;
    return;
  }

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

static void tos_clr() {
  mist_memory_set_address(VIDEO_BASE_ADDRESS);
  mist_memory_set(0, 16000);

  tos_write(NULL);
}

// the built-in OSD font, being used if everything else fails
extern unsigned char charfont[256][8];

static void tos_font_load() {
  fileTYPE file;
  if(FileOpen(&file,"SYSTEM  FNT")) {
    if(file.size == 4096) {
      int i;
      for(i=0;i<4;i++) {
	FileRead(&file, font+i*512);
	FileNextSector(&file);
      }

      return;
    } 
  }

  // if we couldn't load something, then just convert the 
  // built-on OSD font, so we see at least something
  unsigned char c, l, n;
  // copy 128 chars
  for(c=0;c<128;c++) {
    // each character is 8 pixel tall
    for(l=0;l<8;l++) {
      unsigned char *d = font + c*16 + 2*l;
      *d = 0;

      for(n=0;n<8;n++)
	if(charfont[c][n] & (1 << l))
	  *d |= 0x80 >> n;

      *(d+1) = *d;
    }
  }
}

void tos_load_cartridge(char *name) {
  fileTYPE file;

  if(name)
    strncpy(cart_img, name, 11);

  // upload cartridge 
  if(cart_img[0] && FileOpen(&file, cart_img)) {
    int i;
    char buffer[512];
	
    tos_debugf("%s:\n  size = %d\n", cart_img, file.size);

    int blocks = file.size / 512;
    tos_debugf("  blocks = %d\n", blocks);

    tos_debugf("Uploading: [");
    mist_memory_set_address(CART_BASE_ADDRESS);
    
    DISKLED_ON;
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);
      mist_memory_write(buffer, 256);
      
      if(!(i & 7)) tos_debugf(".");
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    DISKLED_OFF;
    tos_debugf("]\n");
    
    tos_debugf("%s uploaded\r", cart_img);
    return; 
  }

  // erase that ram area to remove any previously uploaded
  // image
  tos_debugf("Erasing cart memory\n");
  mist_memory_set_address(CART_BASE_ADDRESS);
  mist_memory_set(0, 128*1024/2);
}

char tos_cartridge_is_inserted() {
  return cart_img[0];
}

void tos_upload(char *name) {
  
  if(name)
    strncpy(tos_img, name, 11);

  // put cpu into reset
  tos_system_ctrl |= TOS_CONTROL_CPU_RESET;
  mist_set_control(tos_system_ctrl);

  tos_font_load();
  tos_clr();

  //      tos_color_test();

  // do the MiST core handling
  tos_write("\x0e\x0f MIST core \x0e\x0f ");
  tos_write("Uploading TOS ... ");
  tos_debugf("Uploading TOS ...\n");

  DISKLED_ON;

  // upload and verify tos image
  fileTYPE file;
  if(FileOpen(&file, tos_img)) {
    int i;
    char buffer[512];
    unsigned long time;
    unsigned long tos_base = TOS_BASE_ADDRESS_192k;
	
    tos_debugf("TOS.IMG:\n  size = %d\n", file.size);

    if(file.size >= 256*1024)
      tos_base = TOS_BASE_ADDRESS_256k;
    else if(file.size != 192*1024)
      tos_debugf("WARNING: Unexpected TOS size!\n");

    int blocks = file.size / 512;
    tos_debugf("  blocks = %d\n", blocks);

    tos_debugf("  address = $%08x\n", tos_base);

    // clear first 16k
    mist_memory_set_address(0);
    mist_memory_set(0x00, 8192);

#if 0  // spi transfer tests
    tos_debugf("SPI transfer test\n");

    // draw some max power pattern on screen
    mist_memory_set_address(VIDEO_BASE_ADDRESS);
    mist_memory_set(0x55, 16000);

    FileRead(&file, buffer);
    int run_ok = 0, run_fail = 0;

    while(1) {
      int j;
      char b2[512];

      for(j=0;j<512;j++) {
	buffer[j] ^= 0x55;
	b2[j] = 0xa5;
      }

      mist_memory_set_address(0);
      mist_memory_set(0xaa, 256);

      mist_memory_set_address(0);
      mist_memory_write_block(buffer);
      //      mist_memory_write(buffer, 256);

      mist_memory_set_address(0);
      //      mist_memory_read_block(b2);
      mist_memory_read(b2, 256);

      char ok = 1;
      for(j=0;j<512;j++) 
	if(buffer[j] != b2[j]) 
	  ok = 0;

      if(ok) run_ok++;
      else   run_fail++;

      if(!ok) {
	hexdump(buffer, 512, 0);
	hexdump(b2, 512, 0);
	for(;;);
      }

      if(!((run_ok + run_fail)%10))
	tos_debugf("ok %d, failed %d\r", run_ok, run_fail);
    }
#endif

#if 0
    tos_debugf("Erasing:   ");
    
    // clear memory to increase chances of catching write problems
    mist_memory_set_address(tos_base);
    mist_memory_set(0x00, file.size/2);
    tos_debugf("done\n");
#endif

    time = GetTimer(0);
    tos_debugf("Uploading: [");
    
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
      
      if(!(i & 7)) tos_debugf(".");
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    tos_debugf("]\n");
    
    time = GetTimer(0) - time;
    tos_debugf("TOS.IMG uploaded in %lu ms (%d kB/s / %d kBit/s)\r", 
	    time >> 20, file.size/(time >> 20), 8*file.size/(time >> 20));
    
  } else
    tos_debugf("Unable to find tos.img\n");
  
#if 0
  {
    char rx[512], buffer[512];
    int i,j;
    int blocks = file.size / 512;
    
    FileSeek(&file, 0, SEEK_SET);
    
    mist_memory_set_address(TOS_BASE_ADDRESS);
    
    tos_debugf("Verifying: [");
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);

      mist_memory_read(rx, 256);
      
      if(!(i & 7)) tos_debugf("+");
      
      for(j=0;j<512;j++) {
	if(buffer[j] != rx[j]) {
	  tos_debugf("Verify error block %d, byte %x\n", i, j);

	  tos_debugf("should be:\n");
	  hexdump(buffer, 512, 0);

	  tos_debugf("is:\n");
	  hexdump(rx, 512, 0);

	  // try to re-read to check whether read or write failed
	  mist_memory_set_address(TOS_BASE_ADDRESS+i*512);
	  mist_memory_read(rx, 256);

	  tos_debugf("re-read: %s\n", (buffer[j] != rx[j])?"failed":"ok");
	  hexdump(rx, 512, 0);


	  while(1);
	}
      }
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    tos_debugf("]\n");
  }
#endif
  DISKLED_OFF;

  // This is the initial boot if no name was given. Otherwise the
  // user reloaded a new os
  if(!name) {
    // load
    tos_load_cartridge(NULL);

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
    if(FileOpen(&file, "HARDDISKHD ")) {
      tos_write("Found hard disk image ");
      tos_select_hdd_image(0, &file);
    }
  }

  tos_write("Booting ... ");

  // let cpu run (release reset)
  tos_system_ctrl &= ~TOS_CONTROL_CPU_RESET;
  mist_set_control(tos_system_ctrl);
}

static unsigned long get_long(char *buffer, int offset) {
  unsigned long retval = 0;
  int i;
  
  for(i=0;i<4;i++)
    retval = (retval << 8) + *(unsigned char*)(buffer+offset+i);

  return retval;
}

void tos_poll() {
  mist_get_dmastate();  
}

void tos_update_sysctrl(unsigned long n) {
  tos_system_ctrl = n;
  mist_set_control(tos_system_ctrl);
}

static char buffer[13];  // local buffer to assemble file name (8+3+2)
static void nice_name(char *dest, char *src) {
  char *c;

  // copy and append nul
  strncpy(dest, src, 8);
  for(c=dest+7;*c==' ';c--); c++;
  *c++ = '.';
  strncpy(c, src+8, 3);
  for(c+=2;*c==' ';c--); c++;
  *c++='\0';
}

char *tos_get_disk_name(char index) {
  fileTYPE file;
  char *c;

  if(index <= 1) 
    file = fdd_image[index].file;
  else
    file = hdd_image[index-2];
  
  if(!file.size) {
    strcpy(buffer, "* no disk *");
    return buffer;
  }
  
  nice_name(buffer, file.name);
  return buffer;
}

char *tos_get_image_name() {
  nice_name(buffer, tos_img);
  return buffer;
}

char *tos_get_cartridge_name() {
  if(!cart_img[0])  // no cart name set
    strcpy(buffer, "* no cartridge *");
  else
    nice_name(buffer, cart_img);

  return buffer;
}

char tos_disk_is_inserted(char index) {
  if(index <= 1) 
    return (fdd_image[index].file.size != 0);

  return hdd_image[index-2].size != 0;
}

void tos_select_hdd_image(char i, fileTYPE *file) {
  tos_debugf("Select ACSI%c image %11s\n", '0'+i, file->name);

  // try to open harddisk image
  hdd_image[i].size = 0;
  tos_system_ctrl &= ~(TOS_ACSI0_ENABLE<<i);

  if(file) {
    tos_system_ctrl |= (TOS_ACSI0_ENABLE<<i);
    hdd_image[i] = *file;
  }

  // update system control
  mist_set_control(tos_system_ctrl);
}

void tos_insert_disk(char i, fileTYPE *file) {
  if(i > 1) {
    tos_select_hdd_image(i-2, file);
    return;
  }

  tos_debugf("%c: eject\n", i+'A');

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
  tos_debugf("%c: insert %.11s\n", i+'A', fdd_image[i].file.name);

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
    tos_debugf("%c: detected %d sides with %d sectors per track\n", 
	    i+'A', fdd_image[i].sides, fdd_image[i].spt);
  }
}

// force ejection of all disks (SD card has been removed)
void tos_eject_all() {
  int i;
  for(i=0;i<2;i++) 
    tos_insert_disk(i, NULL);

  // ejecting an SD card while a hdd image is mounted may be a bad idea
  for(i=0;i<2;i++) {
    if(hdd_image[i].size) {
      InfoMessage("Card removed: Disabling Harddisk!");
      hdd_image[i].size = 0;
    }
  }
}

void tos_reset(char cold) {
  tos_update_sysctrl(tos_system_ctrl |  TOS_CONTROL_CPU_RESET);  // set reset

  if(cold) {
    // clear first 16k
    mist_memory_set_address(8);
    mist_memory_set(0x00, 8192-4);
  }

  tos_update_sysctrl(tos_system_ctrl & ~TOS_CONTROL_CPU_RESET);  // release reset
}
