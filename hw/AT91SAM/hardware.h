#include "AT91SAM7S256.h"

#ifndef HARDWARE_H
#define HARDWARE_H

#include <inttypes.h>

#define MCLK 48000000
#define FWS 1 // Flash wait states
#define FLASH_PAGESIZE 256

#define DISKLED       AT91C_PIO_PA29
#define DISKLED_ON    *AT91C_PIOA_CODR = DISKLED;
#define DISKLED_OFF   *AT91C_PIOA_SODR = DISKLED;
#define MMC_SEL       AT91C_PIO_PA31
#define USB_SEL       AT91C_PIO_PA11
#define USB_PUP       AT91C_PIO_PA16
#define SD_WP         AT91C_PIO_PA1
#define SD_CD         AT91C_PIO_PA0


// fpga programming interface
#define FPGA_OER      *AT91C_PIOA_OER
#define FPGA_SODR     *AT91C_PIOA_SODR
#define FPGA_CODR     *AT91C_PIOA_CODR
#define FPGA_PDSR     *AT91C_PIOA_PDSR
#define FPGA_DONE_PDSR  FPGA_PDSR
#define FPGA_DATA0_CODR FPGA_CODR
#define FPGA_DATA0_SODR FPGA_SODR

#ifdef EMIST
// xilinx programming interface
#define XILINX_DONE   AT91C_PIO_PA4
#define XILINX_DIN    AT91C_PIO_PA9
#define XILINX_INIT_B AT91C_PIO_PA8
#define XILINX_PROG_B AT91C_PIO_PA7
#define XILINX_CCLK   AT91C_PIO_PA15
#else
// altera programming interface
#define ALTERA_DONE    AT91C_PIO_PA4
#define ALTERA_DATA0   AT91C_PIO_PA9
#define ALTERA_NCONFIG AT91C_PIO_PA8
#define ALTERA_NSTATUS AT91C_PIO_PA7
#define ALTERA_DCLK    AT91C_PIO_PA15

#define ALTERA_NCONFIG_SET   FPGA_SODR = ALTERA_NCONFIG
#define ALTERA_NCONFIG_RESET FPGA_CODR = ALTERA_NCONFIG
#define ALTERA_DCLK_SET      FPGA_SODR = ALTERA_DCLK
#define ALTERA_DCLK_RESET    FPGA_CODR = ALTERA_DCLK
#define ALTERA_DATA0_SET     FPGA_DATA0_SODR = ALTERA_DATA0;
#define ALTERA_DATA0_RESET   FPGA_DATA0_CODR = ALTERA_DATA0;

#define ALTERA_NSTATUS_STATE (FPGA_PDSR & ALTERA_NSTATUS)
#define ALTERA_DONE_STATE    (FPGA_DONE_PDSR & ALTERA_DONE)

#endif

// db9 joystick ports
#define JOY1_UP        AT91C_PIO_PA28
#define JOY1_DOWN      AT91C_PIO_PA27
#define JOY1_LEFT      AT91C_PIO_PA26
#define JOY1_RIGHT     AT91C_PIO_PA25
#define JOY1_BTN1      AT91C_PIO_PA24
#define JOY1_BTN2      AT91C_PIO_PA23
#define JOY1  (JOY1_UP|JOY1_DOWN|JOY1_LEFT|JOY1_RIGHT|JOY1_BTN1|JOY1_BTN2)

#define JOY0_UP        AT91C_PIO_PA22
#define JOY0_DOWN      AT91C_PIO_PA21
#define JOY0_LEFT      AT91C_PIO_PA20
#define JOY0_RIGHT     AT91C_PIO_PA19
#define JOY0_BTN1      AT91C_PIO_PA18
#define JOY0_BTN2      AT91C_PIO_PA17
#define JOY0  (JOY0_UP|JOY0_DOWN|JOY0_LEFT|JOY0_RIGHT|JOY0_BTN1|JOY0_BTN2)

// chip selects for FPGA communication
#define FPGA0 AT91C_PIO_PA10
#define FPGA1 AT91C_PIO_PA3
#define FPGA2 AT91C_PIO_PA2

#define FPGA3         AT91C_PIO_PA9   // same as ALTERA_DATA0

#define VBL           AT91C_PIO_PA7

#define USB_LOAD_VAR         *(int*)(0x0020FF04)
#define USB_LOAD_VALUE       12345678

#define DEBUG_MODE_VAR       *(int*)(0x0020FF08)
#define DEBUG_MODE_VALUE     87654321
#define DEBUG_MODE           (DEBUG_MODE_VAR == DEBUG_MODE_VALUE)

#define VIDEO_KEEP_VALUE     0x87654321
#define VIDEO_KEEP_VAR       (*(int*)0x0020FF10)
#define VIDEO_ALTERED_VAR    (*(uint8_t*)0x0020FF14)
#define VIDEO_SD_DISABLE_VAR (*(uint8_t*)0x0020FF15)
#define VIDEO_YPBPR_VAR      (*(uint8_t*)0x0020FF16)

#define USB_BOOT_VALUE       0x8007F007
#define USB_BOOT_VAR         (*(int*)0x0020FF18)

#define SECTOR_BUFFER_SIZE   4096

char mmc_inserted(void);
char mmc_write_protected(void);
void USART_Init(unsigned long baudrate);
void USART_Write(unsigned char c);
unsigned char USART_Read(void);

unsigned long CheckButton(void);
void Timer_Init(void);
unsigned long GetTimer(unsigned long offset);
unsigned long CheckTimer(unsigned long t);
void WaitTimer(unsigned long time);

void TIMER_wait(unsigned long ms);

void USART_Poll(void);

void inline MCUReset() {*AT91C_RSTC_RCR = 0xA5 << 24 | AT91C_RSTC_PERRST | AT91C_RSTC_PROCRST | AT91C_RSTC_EXTRST;}

void InitRTTC();

int inline GetRTTC() {return (int)(AT91C_BASE_RTTC->RTTC_RTVR);}

int GetSPICLK();

void InitADC(void);
void PollADC();
// user, menu, DIP2, DIP1
unsigned char Buttons();
unsigned char MenuButton();
unsigned char UserButton();

void InitDB9();
char GetDB9(char index, unsigned char *joy_map);

char GetRTC(unsigned char *d);
char SetRTC(unsigned char *d);

void UnlockFlash();
void WriteFlash(int page);

#ifdef FPGA3
// the MiST has the user inout on the arm controller
void EnableIO(void);
void DisableIO(void);
#endif

#define DEBUG_FUNC_IN() 
#define DEBUG_FUNC_OUT() 

#ifdef __GNUC__
void __init_hardware(void);
#endif

#endif // HARDWARE_H
