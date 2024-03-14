/*
This file is part of MiST-firmware

MiST-firmware is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

MiST-firmware is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include "chip.h"
#include "samv71.h"
#include "core_cm7.h"

#define MCLK   144000000
#define PLLCLK 288000000
#define FWS 6
#define FLASH_PAGESIZE 512

#define DMA_CH_MMC           0
#define DMA_CH_SPI_TRANS     1
#define DMA_CH_SPI_REC       2
#define DMA_CH_QSPI_TRANS    3
#define DMA_CH_QSPI_REC      4

#define DISKLED              PIO_PD28
#define DISKLED_ON           PIOD->PIO_CODR = DISKLED;
#define DISKLED_OFF          PIOD->PIO_SODR = DISKLED;

#define USB_SEL              PIO_PD25
#define USB_INT              PIO_PD24

// fpga programming interface
#define FPGA_OER             PIOD->PIO_OER
#define FPGA_SODR            PIOD->PIO_SODR
#define FPGA_CODR            PIOD->PIO_CODR
#define FPGA_PDSR            PIOD->PIO_PDSR
#define FPGA_DONE_PDSR       PIOD->PIO_PDSR
#define FPGA_DONE_SODR       PIOD->PIO_SODR
#define FPGA_DONE_CODR       PIOD->PIO_CODR
#define FPGA_DATA0_SODR      PIOD->PIO_SODR
#define FPGA_DATA0_CODR      PIOD->PIO_CODR

#ifdef EMIST
// xilinx programming interface
#define XILINX_DONE          PIO_PB13
#define XILINX_DIN           PIO_PD12
#define XILINX_INIT_B        PIO_PA8
#define XILINX_PROG_B        PIO_PA7
#define XILINX_CCLK          PIO_PA15
#else
// altera programming interface
#define ALTERA_DONE          PIO_PD17
#define ALTERA_DATA0         PIO_PD12
#define ALTERA_NCONFIG       PIO_PD14
#define ALTERA_NSTATUS       PIO_PD16
#define ALTERA_DCLK          PIO_PD13

#define ALTERA_START_CONFIG  PIOD->PIO_PER = ALTERA_DATA0;
#define ALTERA_STOP_CONFIG   PIOD->PIO_PDR = ALTERA_DATA0;
#define ALTERA_NCONFIG_SET   PIOD->PIO_ODR = ALTERA_NCONFIG; PIOD->PIO_PUER = ALTERA_NCONFIG;
#define ALTERA_NCONFIG_RESET PIOD->PIO_PUDR = ALTERA_NCONFIG; PIOD->PIO_CODR = ALTERA_NCONFIG; PIOD->PIO_OER = ALTERA_NCONFIG;
#define ALTERA_DCLK_SET      PIOD->PIO_SODR = ALTERA_DCLK
#define ALTERA_DCLK_RESET    PIOD->PIO_CODR = ALTERA_DCLK
#define ALTERA_DATA0_SET     PIOD->PIO_SODR = ALTERA_DATA0;
#define ALTERA_DATA0_RESET   PIOD->PIO_CODR = ALTERA_DATA0;

#define ALTERA_NSTATUS_STATE (PIOD->PIO_PDSR & ALTERA_NSTATUS)
#define ALTERA_DONE_STATE    (PIOD->PIO_PDSR & ALTERA_DONE)

#endif

// chip selects for FPGA communication
#define FPGA0                PIO_PD27
#define FPGA1                PIO_PB2
#define FPGA3                PIO_PD12   // same as ALTERA_DATA0

// SD
#define SD_CD                PIO_PD18
#define SD_WP                PIO_PD19

// Buttons
#define BTN_PORT             PIOD
#define BTN_RESET            PIO_PD11
#define BTN_OSD              PIO_PD10
#define SW1                  PIO_PD30
#define SW2                  PIO_PD26

// Joystick
#define JOY0                 (PIOA->PIO_PDSR & (JOY0_LEFT | JOY0_RIGHT | JOY0_UP | JOY0_DOWN | JOY0_BTN1 | JOY0_BTN2))
#define JOY0_LEFT            PIO_PA24
#define JOY0_RIGHT           PIO_PA8
#define JOY0_UP              PIO_PA23
#define JOY0_DOWN            PIO_PA5
#define JOY0_BTN1            PIO_PA7
#define JOY0_BTN2            PIO_PA15

#define JOY0_SEL_PORT        PIOB
#define JOY0_SEL_PIN         PIO_PB0

#define JOY1                 (PIOA->PIO_PDSR & (JOY1_LEFT | JOY1_RIGHT | JOY1_UP | JOY1_DOWN | JOY1_BTN1 | JOY1_BTN2))
#define JOY1_LEFT            PIO_PA2
#define JOY1_RIGHT           PIO_PA22
#define JOY1_UP              PIO_PA0
#define JOY1_DOWN            PIO_PA1
#define JOY1_BTN1            PIO_PA21
#define JOY1_BTN2            PIO_PA16

#define JOY1_SEL_PORT        PIOB
#define JOY1_SEL_PIN         PIO_PB1

#define PHY_RESET            PIO_PA18
#define PHY_INT              PIO_PA19
#define PHY_SIGDET           PIO_PA20

// in non-cached RAM
#define USB_LOAD_VAR         *(int*)(0x2045F000)
#define USB_LOAD_VALUE       12345678

#define USB_BOOT_VALUE       0x8007F007
#define USB_BOOT_VAR         (*(int*)0x002045F013)

#define DEBUG_MODE_VAR       *(int*)(0x2045F008)
#define DEBUG_MODE_VALUE     87654321
#define DEBUG_MODE           (DEBUG_MODE_VAR == DEBUG_MODE_VALUE)

#define VIDEO_KEEP_VALUE     0x87654321
#define VIDEO_KEEP_VAR       (*(int*)0x2045F00C)
#define VIDEO_ALTERED_VAR    (*(uint8_t*)0x2045F010)
#define VIDEO_SD_DISABLE_VAR (*(uint8_t*)0x2045F011)
#define VIDEO_YPBPR_VAR      (*(uint8_t*)0x2045F012)

#define SECTOR_BUFFER_SIZE   8192

void __init_hardware();

char mmc_inserted();
char mmc_write_protected();

void USART_Init(unsigned long baudrate);
void USART_Write(unsigned char c);
unsigned char USART_Read();

unsigned long CheckButton();
void Timer_Init();
unsigned long GetTimer(unsigned long offset);
unsigned long CheckTimer(unsigned long t);
void WaitTimer(unsigned long time);

void USART_Poll();

void MCUReset();

void InitRTTC();
int GetRTTC();

int GetSPICLK();

void InitADC();
void PollADC();
// user, menu, DIP2, DIP1
unsigned char Buttons();
unsigned char MenuButton();
unsigned char UserButton();

void InitDB9();
char GetDB9(char index, uint16_t *joy_map);

char GetRTC(unsigned char *d);
char SetRTC(unsigned char *d);

void UnlockFlash();
void WriteFlash(unsigned long page);

#ifdef FPGA3
// the MiST has the user inout on the arm controller
void EnableIO();
void DisableIO();
#endif

#define DEBUG_FUNC_IN()
#define DEBUG_FUNC_OUT()

unsigned char CheckFirmware(char *name);
void WriteFirmware(char *name);
char *GetFirmwareVersion(char *name);

#endif
