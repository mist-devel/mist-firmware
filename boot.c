// boot.c
// bootscreen functions
// 2014, rok.krajnc@gmail.com


#include "string.h"
#include "stdio.h"
#include "boot.h"
#include "hardware.h"
#include "osd.h"
#include "spi.h"
#include "fat_compat.h"
#include "font.h"

// TODO!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0");

static void mem_upload_init(unsigned long addr) {
  spi_osd_cmd32le_cont(OSD_CMD_WR, addr);
}

static void mem_upload_fini() {
  DisableOsd();

  // do we really still need these if it's within a function?
  SPIN(); SPIN(); 
  SPIN(); SPIN();
}

static void mem_write16(unsigned short x) {
  SPI((((x)>>8)&0xff)); SPI(((x)&0xff));

  // do we really still need these if it's within a function?
  SPIN(); SPIN();
  SPIN(); SPIN();
}

//// boot cursor positions ////
unsigned short bcurx=0;
unsigned short bcury=96;

static int bootscreen_adr = 0x80000 + /*120*/104*640/8;

void BootHome() {
  bootscreen_adr = 0x80000 + /*120*/104*640/8;
}

//// BootEnableMem() ////
static void BootEnableMem()
{
  // TEMP enable 1MB memory
  spi_osd_cmd8(OSD_CMD_MEM, 0x5);
  SPIN(); SPIN(); SPIN(); SPIN();
  //EnableOsd();
  //SPI(OSD_CMD_RST);
  //rstval = (SPI_CPU_HLT | SPI_RST_CPU);
  //SPI(rstval);
  //DisableOsd();
  //SPIN(); SPIN(); SPIN(); SPIN();
  //while ((read32(REG_SYS_STAT_ADR) & 0x2));
}

//// BootClearScreen() ////
static void BootClearScreen(int adr, int size)
{
  int i;
  mem_upload_init(adr);
  for (i=0; i<size; i++) {
    mem_write16(0x0000);
    //mem_write16(i);
  }
  mem_upload_fini();
}


//// BootUploadLogo() ////
static void BootUploadLogo()
{
  FIL file;
  int x,y;
  int i=0;
  int adr;

  if (f_open(&file, LOGO_FILE, FA_READ) == FR_OK) {
    FileReadBlock(&file, sector_buffer);
    mem_upload_init(SCREEN_BPL1+LOGO_OFFSET);
    adr = SCREEN_BPL1+LOGO_OFFSET;
    for (y=0; y<LOGO_HEIGHT; y++) {
      for (x=0; x<LOGO_WIDTH/16; x++) {
        if (i == 512) {
          mem_upload_fini();
          FileReadBlock(&file, sector_buffer);
          mem_upload_init(adr);
          i = 0;
        }
        SPI(sector_buffer[i++]);
        SPI(sector_buffer[i++]);
        SPIN(); SPIN(); SPIN(); SPIN();
        //for (tmp=0; tmp<0x80000; tmp++);
        //printf("i=%03d  x=%03d  y=%03d  dat[0]=0x%08x  dat[1]=0x%08x\r", i, x, y, sector_buffer[i], sector_buffer[i+1]);
        adr += 2;
      }
      mem_upload_fini();
      mem_upload_init(SCREEN_BPL1+LOGO_OFFSET+(y+1)*(SCREEN_WIDTH/8));
      adr = SCREEN_BPL1+LOGO_OFFSET+(y+1)*(SCREEN_WIDTH/8);
    }
    mem_upload_fini();
    mem_upload_init(SCREEN_BPL2+LOGO_OFFSET);
    adr = SCREEN_BPL2+LOGO_OFFSET;
    for (y=0; y<LOGO_HEIGHT; y++) {
      for (x=0; x<LOGO_WIDTH/16; x++) {
        if (i == 512) {
          mem_upload_fini();
          FileReadBlock(&file, sector_buffer);
          mem_upload_init(adr);
          i = 0;
        }
        SPI(sector_buffer[i++]);
        SPI(sector_buffer[i++]);
        SPIN(); SPIN(); SPIN(); SPIN();
        adr += 2;
      }
      mem_upload_fini();
      mem_upload_init(SCREEN_BPL2+LOGO_OFFSET+(y+1)*(SCREEN_WIDTH/8));
      adr = SCREEN_BPL2+LOGO_OFFSET+(y+1)*(SCREEN_WIDTH/8);
    }
    mem_upload_fini();
    f_close(&file);
  }
}


//// BootUploadBall() ////
static void BootUploadBall()
{
  FIL file;
  int x;
  int i=0;
  int adr;

  if (f_open(&file, BALL_FILE, FA_READ) == FR_OK) {
    FileReadBlock(&file, sector_buffer);
    mem_upload_init(BALL_ADDRESS);
    adr = BALL_ADDRESS;
    for (x=0; x<BALL_SIZE/2; x++) {
      if (i == 512) {
        mem_upload_fini();
        FileReadBlock(&file, sector_buffer);
        mem_upload_init(adr);
        i = 0;
      }
      SPI(sector_buffer[i++]);
      SPI(sector_buffer[i++]);
      SPIN(); SPIN(); SPIN(); SPIN();
      adr += 2;
    }
    mem_upload_fini();
    f_close(&file);
  }
}


//// BootUploadCopper() ////
static void BootUploadCopper()
{
  FIL file;
  int x;
  int i=0;
  int adr;

  if (f_open(&file, COPPER_FILE, FA_READ) == FR_OK) {
    FileReadBlock(&file, sector_buffer);
    mem_upload_init(COPPER_ADDRESS);
    adr = COPPER_ADDRESS;
    for (x=0; x<COPPER_SIZE/2; x++) {
      if (i == 512) {
        mem_upload_fini();
        FileReadBlock(&file, sector_buffer);
        mem_upload_init(adr);
        i = 0;
      }
      SPI(sector_buffer[i++]);
      SPI(sector_buffer[i++]);
      SPIN(); SPIN(); SPIN(); SPIN();
      adr += 2;
    }
    mem_upload_fini();
    f_close(&file);
  } else {
    mem_upload_init(COPPER_ADDRESS);
    mem_write16(0x00e0); mem_write16(0x0008);
    mem_write16(0x00e2); mem_write16(0x0000);
    mem_write16(0x00e4); mem_write16(0x0008);
    mem_write16(0x00e6); mem_write16(0x5000);
    mem_write16(0x0100); mem_write16(0xa200);
    mem_write16(0xffff); mem_write16(0xfffe);
    mem_upload_fini();
  }
}


//// BootCustomInit() ////
static void BootCustomInit()
{
  //move.w #$0000,$dff1fc  ; FMODE, slow fetch mode for AGA compatibility
  mem_upload_init(0xdff1fc);
  mem_write16(0x0000);
  mem_upload_fini();

  //move.w #$0002,$dff02e  ; COPCON, enable danger mode
  mem_upload_init(0xdff02e);
  mem_write16(0x0002);
  mem_upload_fini();

  //move.l #Copper1,$dff080  ; COP1LCH, copper 1 pointer
  //move.l #Copper2,$dff084  ; CPO2LCH, copper 2 pointer
  mem_upload_init(0xdff080);
  mem_write16(0x0008); mem_write16(0xe680);
  mem_write16(0x0008); mem_write16(0xe69c);
  mem_upload_fini();

  //move.w #$2c81,$dff08e  ; DIWSTRT, screen upper left corner
  //move.w #$f4c1,$dff090  ; DIWSTOP, screen lower right corner
  //move.w #$003c,$dff092  ; DDFSTRT, display data fetch start
  //move.w #$00d4,$dff094  ; DDFSTOP, display data fetch stop
  //move.w #$87c0,$dff096  ; DMACON, enable important bits
  //move.w #$0000,$dff098  ; CLXCON, TODO
  //move.w #$7fff,$dff09a  ; INTENA, disable all interrupts
  //move.w #$7fff,$dff09c  ; INTREQ, disable all interrupts
  //move.w #$0000,$dff09e  ; ADKCON, TODO
  mem_upload_init(0xdff08e);
  //mem_write16(0x1d64);
  //mem_write16(0x38c7);
  //mem_write16(0x0028);
  //mem_write16(0x00d8);
  mem_write16(0x2c81);
  mem_write16(0xf4c1);
  mem_write16(0x003c);
  mem_write16(0x00d4);
  mem_write16(0x87c0);
  mem_write16(0x0000);
  mem_write16(0x7fff);
  mem_write16(0x7fff);
  mem_upload_fini();

  //move.w #(bpl1>>16)&$ffff,$dff0e0  ; BPL1PTH
  //move.w #bpl1&$ffff,$dff0e2    ; BPL1PTL
  //move.w #(bpl2>>16)&$ffff,$dff0e4  ; BPL2PTH
  //move.w #bpl2&$ffff,$dff0e6    ; BPL2PTL
  mem_upload_init(0xdff0e0);
  mem_write16(0x0008); mem_write16(0x0000);
  mem_write16(0x0008); mem_write16(0x5000);
  mem_upload_fini();

  //move.w #$a200,$dff100  ; BPLCON0, two bitplanes & colorburst enabled
  //move.w #$0000,$dff102  ; BPLCON1, bitplane control scroll value
  //move.w #$0000,$dff104  ; BPLCON2, misc bitplane bits
  //move.w #$0000,$dff106  ; BPLCON3, TODO
  //move.w #$0000,$dff108  ; BPL1MOD, bitplane modulo for odd planes
  //move.w #$0000,$dff10a  ; BPL2MOD, bitplane modulo for even planes
  mem_upload_init(0xdff100);
  mem_write16(0xa200);
  mem_write16(0x0000);
  mem_write16(0x0000);
  mem_write16(0x0000);
  mem_write16(0x0000);
  mem_write16(0x0000);
  mem_upload_fini();

  //move.w #$09f0,$dff040  ; BLTCON0
  //move.w #$0000,$dff042  ; BLTCON1
  //move.w #$ffff,$dff044  ; BLTAFWM, blitter first word mask for srcA
  //move.w #$ffff,$dff046  ; BLTALWM, blitter last word mask for srcA
  mem_upload_init(0xdff040);
  mem_write16(0x09f0);
  mem_write16(0x0000);
  mem_write16(0xffff);
  mem_write16(0xffff);
  mem_upload_fini();

  //move.w #$0000,$dff064  ; BLTAMOD
  //move.w #BLITS,$dff066  ; BLTDMOD
  mem_upload_init(0xdff064);
  mem_write16(0x0000);
  mem_write16(BLITS);
  mem_upload_fini();

  //move.w #$0000,$dff180  ; COLOR00
  //move.w #$0aaa,$dff182  ; COLOR01
  //move.w #$0a00,$dff184  ; COLOR02
  //move.w #$0000,$dff186  ; COLOR03
  mem_upload_init(0xdff180);
  mem_write16(0x0000);
  mem_write16(0x0aaa);
  mem_write16(0x0a00);
  mem_write16(0x000a);
  mem_upload_fini();

  //move.w #$0000,$dff088  ; COPJMP1, restart copper at location 1 
  mem_upload_init(0xdff088);
  mem_write16(0x0000);
  mem_upload_fini(); 
}


//// BootInit() ////
void BootInit()
{
  BootEnableMem();
  BootClearScreen(SCREEN_ADDRESS, SCREEN_MEM_SIZE);
  BootUploadLogo();
  BootUploadBall();
  BootUploadCopper();
  BootCustomInit();
  //int i;
  //char s[3];
  //s[1] = 'A';
  //s[2] = 0;
  //for (i=0; i<16; i++) {
  //  s[0] = 49+i;
  //  BootPrintEx(s);
  //}
}

//// BootPrint() ////
void BootPrintEx(char * str)
{
  char buf[2];
  unsigned char i,j;
  unsigned char len;

  iprintf(str);
  iprintf("\r");

  len = strlen(str);
  len = (len>80) ? 80 : len;

  for(j=0; j<8; j++) {
    mem_upload_init(bootscreen_adr);
    for(i=0; i<len; i+=2) {
        SPI(char_row(str[i],j));
      if (i==(len-1))
        SPI(char_row(0,j));
      else
        SPI(char_row(str[i+1],j));
      SPIN(); SPIN(); SPIN(); SPIN();
    }
    mem_upload_fini();
    bootscreen_adr += 640/8;
  }
}

