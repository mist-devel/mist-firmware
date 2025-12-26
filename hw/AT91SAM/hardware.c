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

#include <ctype.h>
#include <stdio.h>
#include "AT91SAM7S256.h"
#include "attrs.h"
#include "hardware.h"
#include "user_io.h"
#include "xmodem.h"
#include "ikbd.h"
#include "usb.h"
#include "usbrtc.h"

void __init_hardware(void)
{
    *AT91C_WDTC_WDMR = AT91C_WDTC_WDDIS; // disable watchdog
    *AT91C_RSTC_RMR = (0xA5 << 24) | AT91C_RSTC_URSTEN;   // enable external user reset input
    *AT91C_MC_FMR = FWS << 8; // Flash wait states

    // configure clock generator
    *AT91C_CKGR_MOR = AT91C_CKGR_MOSCEN | (40 << 8);  
    while (!(*AT91C_PMC_SR & AT91C_PMC_MOSCS));

    *AT91C_CKGR_PLLR = AT91C_CKGR_OUT_0 | AT91C_CKGR_USBDIV_1 | (25 << 16) | (40 << 8) | 5; // DIV=5 MUL=26 USBDIV=1 (2) PLLCOUNT=40
    while (!(*AT91C_PMC_SR & AT91C_PMC_LOCK));

    *AT91C_PMC_MCKR = AT91C_PMC_PRES_CLK_2; // master clock register: clock source selection
    while (!(*AT91C_PMC_SR & AT91C_PMC_MCKRDY));

    *AT91C_PMC_MCKR = AT91C_PMC_CSS_PLL_CLK | AT91C_PMC_PRES_CLK_2; // master clock register: clock source selection
    while (!(*AT91C_PMC_SR & AT91C_PMC_MCKRDY));

    *AT91C_PIOA_PER = 0xFFFFFFFF; // enable pio on all pins
    *AT91C_PIOA_SODR = DISKLED;   // led off

#ifdef USB_PUP
    // disable usb d+/d- pullups if present
    *AT91C_PIOA_OER = USB_PUP;
    *AT91C_PIOA_PPUDR = USB_PUP;
    *AT91C_PIOA_SODR = USB_PUP;
#endif

    // enable joystick ports
#ifdef JOY0
    *AT91C_PIOA_PPUER = JOY0;
#endif

#ifdef JOY1
    *AT91C_PIOA_PPUER = JOY1;
#endif

#ifdef SD_WP
    // enable SD card signals
    *AT91C_PIOA_PPUER = SD_WP | SD_CD;
#endif

    *AT91C_PIOA_SODR = MMC_SEL | FPGA0 | FPGA1 | FPGA2; // set output data register

    // output enable register
    *AT91C_PIOA_OER = DISKLED | MMC_SEL | FPGA0 | FPGA1 | FPGA2;
    // pull-up disable register
    *AT91C_PIOA_PPUDR = DISKLED | MMC_SEL | FPGA0 | FPGA1 | FPGA2;

#ifdef XILINX_CCLK
    // xilinx interface
    *AT91C_PIOA_SODR  = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    *AT91C_PIOA_OER   = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    *AT91C_PIOA_PPUDR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B | 
      XILINX_INIT_B | XILINX_DONE;
#endif

#ifdef ALTERA_DCLK
    // altera interface
    *AT91C_PIOA_SODR  = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG;
    *AT91C_PIOA_OER   = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG;
    *AT91C_PIOA_PPUDR = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG |
      ALTERA_NSTATUS | ALTERA_DONE;
#endif

#ifdef MMC_CLKEN
    // MMC_CLKEN may be present 
    // (but is not used anymore, so it's only setup passive)
    *AT91C_PIOA_SODR = MMC_CLKEN;
    *AT91C_PIOA_PPUDR = MMC_CLKEN;
#endif

#ifdef USB_SEL
    *AT91C_PIOA_SODR = USB_SEL;
    *AT91C_PIOA_OER = USB_SEL;
    *AT91C_PIOA_PPUDR = USB_SEL;
#endif

    // Enable peripheral clock in the PMC
    AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_PIOA;
}

// A buffer of 256 bytes makes index handling pretty trivial
volatile static unsigned char tx_buf[256];
volatile static unsigned char tx_rptr, tx_wptr;

volatile static unsigned char rx_buf[256];
volatile static unsigned char rx_rptr, rx_wptr;

void Usart0IrqHandler(void) {
  // Read USART status
  unsigned char status = AT91C_BASE_US0->US_CSR;

  // received something?
  if(status & AT91C_US_RXRDY) {
    // read byte from usart
    unsigned char c = AT91C_BASE_US0->US_RHR;

    // only store byte if rx buffer is not full
    if((unsigned char)(rx_wptr + 1) != rx_rptr) {
      // there's space in buffer: use it
      rx_buf[rx_wptr++] = c;
    }
  }
    
  // ready to transmit further bytes?
  if(status & AT91C_US_TXRDY) {

    // further bytes to send in buffer? 
    if(tx_wptr != tx_rptr)
      // yes, simply send it and leave irq enabled
      AT91C_BASE_US0->US_THR = tx_buf[tx_rptr++];
    else
      // nothing else to send, disable interrupt
      AT91C_BASE_US0->US_IDR = AT91C_US_TXRDY;
  }
}

// check usart rx buffer for data
void USART_Poll(void) {
  if(Buttons() & 2)
    xmodem_poll();

  while(rx_wptr != rx_rptr) {
    // this can a little be optimized by sending whole buffer parts 
    // at once and not just single bytes. But that's probably not
    // worth the effort.
    char chr = rx_buf[rx_rptr++];

    if(Buttons() & 2) {
      // if in debug mode use xmodem for file reception
      xmodem_rx_byte(chr);
    } else {
      iprintf("USART RX %d (%c)\n", chr, chr);

      // data available -> send via user_io to core
      user_io_serial_tx(&chr, 1);
    }
  }
}

void USART_Write(unsigned char c) {
#if 0
  while(!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
  AT91C_BASE_US0->US_THR = c;
#else
  if((AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY) && (tx_wptr == tx_rptr)) {
    // transmitter ready and buffer empty? -> send directly
    AT91C_BASE_US0->US_THR = c;
  } else {
    // transmitter is not ready: block until space in buffer
    while((unsigned char)(tx_wptr + 1) == tx_rptr);

    // there's space in buffer: use it
    tx_buf[tx_wptr++] = c;
  }

  AT91C_BASE_US0->US_IER = AT91C_US_TXRDY;  // enable interrupt
#endif
}

void USART_Init(unsigned long baudrate) {
    // Configure PA5 and PA6 for USART0 use
    AT91C_BASE_PIOA->PIO_PDR = AT91C_PA5_RXD0 | AT91C_PA6_TXD0;

    // Enable the peripheral clock in the PMC
    AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_US0;

    // Reset and disable receiver & transmitter
    AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;

    // Configure USART0 mode
    AT91C_BASE_US0->US_MR = AT91C_US_USMODE_NORMAL | AT91C_US_CLKS_CLOCK | AT91C_US_CHRL_8_BITS | 
      AT91C_US_PAR_NONE | AT91C_US_NBSTOP_1_BIT | AT91C_US_CHMODE_NORMAL;

    // Configure USART0 rate
    AT91C_BASE_US0->US_BRGR = MCLK / 16 / baudrate;

    // Enable receiver & transmitter
    AT91C_BASE_US0->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;

    // tx buffer is initially empty
    tx_rptr = tx_wptr = 0;

    // and so is rx buffer
    rx_rptr = rx_wptr = 0;

    // Set the USART0 IRQ handler address in AIC Source
    AT91C_BASE_AIC->AIC_SVR[AT91C_ID_US0] = (unsigned int)Usart0IrqHandler; 
    AT91C_BASE_AIC->AIC_IECR = (1<<AT91C_ID_US0);

    AT91C_BASE_US0->US_IER = AT91C_US_RXRDY;  // enable rx interrupt
}

unsigned long CheckButton(void)
{
#ifdef BUTTON
    return((~*AT91C_PIOA_PDSR) & BUTTON);
#else
    return MenuButton();
#endif
}

void timer0_c_irq_handler(void) {
  //* Acknowledge interrupt status
  unsigned int dummy = AT91C_BASE_TC0->TC_SR;

  ikbd_update_time();
}

void Timer_Init(void) {  
  unsigned int dummy;

  //* Open timer0
  AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_TC0;
  
  //* Disable the clock and the interrupts
  AT91C_BASE_TC0->TC_CCR = AT91C_TC_CLKDIS ;
  AT91C_BASE_TC0->TC_IDR = 0xFFFFFFFF ;
  
  //* Clear status bit
  dummy = AT91C_BASE_TC0->TC_SR;

  //* Set the Mode of the Timer Counter
  AT91C_BASE_TC0->TC_CMR = 0x04;  // :1024
  
  //* Enable the clock
  AT91C_BASE_TC0->TC_CCR = AT91C_TC_CLKEN ;
  
  
  
  //* Open Timer 0 interrupt
  
  //* Disable the interrupt on the interrupt controller
  AT91C_BASE_AIC->AIC_IDCR = 1 << AT91C_ID_TC0;
  //* Save the interrupt handler routine pointer and the interrupt priority
  AT91C_BASE_AIC->AIC_SVR[AT91C_ID_TC0] = (unsigned int)timer0_c_irq_handler;
  //* Store the Source Mode Register
  AT91C_BASE_AIC->AIC_SMR[AT91C_ID_TC0] = 1 | AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL;
  //* Clear the interrupt on the interrupt controller
  AT91C_BASE_AIC->AIC_ICCR = 1 << AT91C_ID_TC0;
  
  AT91C_BASE_TC0->TC_IER = AT91C_TC_CPCS;  //  IRQ enable CPC
  AT91C_BASE_AIC->AIC_IECR = 1 << AT91C_ID_TC0;
  
  //* Start timer0
  AT91C_BASE_TC0->TC_CCR = AT91C_TC_SWTRG ;
  
  *AT91C_PITC_PIMR = AT91C_PITC_PITEN | ((MCLK / 16 / 1000 - 1) & AT91C_PITC_PIV); // counting period 1ms
}

// 12 bits accuracy at 1ms = 4096 ms 
RAMFUNC unsigned long GetTimer(unsigned long offset)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    systimer += offset << 20;
    return (systimer); // valid bits [31:20]
}

RAMFUNC unsigned long CheckTimer(unsigned long time)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    time -= systimer;
    return(time > (1UL << 31));
}

void WaitTimer(unsigned long time)
{
    time = GetTimer(time);
    while (!CheckTimer(time));
}

inline char mmc_inserted() {
  return !(*AT91C_PIOA_PDSR & SD_CD);
}

char mmc_write_protected() {
  return (*AT91C_PIOA_PDSR & SD_WP);
}

void InitRTTC() {
  // reprogram the realtime timer to run at 1Khz
  AT91C_BASE_RTTC->RTTC_RTMR = 0x8000 / 1000;
}

int GetSPICLK() {
  return (MCLK / ((AT91C_SPI_CSR[0] & AT91C_SPI_SCBR) >> 8) / 1000000);
}

// permanent state of adc inputs used for dip switches
static unsigned char adc_state = 0;
AT91PS_ADC a_pADC = AT91C_BASE_ADC;
AT91PS_PMC a_pPMC = AT91C_BASE_PMC;

static void PollOneADC() {
  static unsigned char adc_cnt = 0xff;

  // fetch result from previous run
  if(adc_cnt != 0xff) {
    unsigned int result = 0;

    // wait for end of convertion
    while(!(AT91C_BASE_ADC->ADC_SR & (1 << (4+adc_cnt))));
      switch (adc_cnt) {
        case 0: result = AT91C_BASE_ADC->ADC_CDR4; break;
        case 1: result = AT91C_BASE_ADC->ADC_CDR5; break;
        case 2: result = AT91C_BASE_ADC->ADC_CDR6; break;
        case 3: result = AT91C_BASE_ADC->ADC_CDR7; break;
      }

      if(result < 128) adc_state |=  (1<<adc_cnt);
      if(result > 128) adc_state &= ~(1<<adc_cnt);
  }

  adc_cnt = (adc_cnt + 1)&3;

  // Enable desired chanel
  AT91C_BASE_ADC->ADC_CHER = 1 << (4+adc_cnt);

  // Start conversion
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_START;
}

void InitADC(void) {
  // Enable clock for interface
  AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_ADC;

  // Reset
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_SWRST;
  AT91C_BASE_ADC->ADC_CR = 0x0;

  // Set maximum startup time and hold time
  AT91C_BASE_ADC->ADC_MR = 0x0F1F0F00 | AT91C_ADC_LOWRES_8_BIT;

  // make sure we get the first values immediately
  PollOneADC();
  PollOneADC();
  PollOneADC();
  PollOneADC();
  PollOneADC();
}

// poll one adc channel every 25ms
void PollADC() {
  static long adc_timer = 0;

  if(CheckTimer(adc_timer)) {
    adc_timer = GetTimer(25);
    PollOneADC();
  }
}

// user, menu, DIP1, DIP2
unsigned char Buttons() {
  return (adc_state);
}

unsigned char MenuButton() {
  return (adc_state & 4);
}

unsigned char UserButton() {
  return (adc_state & 8);
}

void InitDB9() {}

// poll db9 joysticks
char GetDB9(char index, uint16_t *joy_map) {
  static int joy0_state = JOY0;
  static int joy1_state = JOY1;
  if (!index) {
    if((*AT91C_PIOA_PDSR & JOY0) != joy0_state) {
      joy0_state = *AT91C_PIOA_PDSR & JOY0;
      *joy_map = 0;
      if(!(joy0_state & JOY0_UP))    *joy_map |= JOY_UP;
      if(!(joy0_state & JOY0_DOWN))  *joy_map |= JOY_DOWN;
      if(!(joy0_state & JOY0_LEFT))  *joy_map |= JOY_LEFT;
      if(!(joy0_state & JOY0_RIGHT)) *joy_map |= JOY_RIGHT;
      if(!(joy0_state & JOY0_BTN1))  *joy_map |= JOY_BTN1;
      if(!(joy0_state & JOY0_BTN2))  *joy_map |= JOY_BTN2;
      return 1;
    } else
      return 0;
  } else {
    if((*AT91C_PIOA_PDSR & JOY1) != joy1_state) {
      joy1_state = *AT91C_PIOA_PDSR & JOY1;
      *joy_map = 0;
      if(!(joy1_state & JOY1_UP))    *joy_map |= JOY_UP;
      if(!(joy1_state & JOY1_DOWN))  *joy_map |= JOY_DOWN;
      if(!(joy1_state & JOY1_LEFT))  *joy_map |= JOY_LEFT;
      if(!(joy1_state & JOY1_RIGHT)) *joy_map |= JOY_RIGHT;
      if(!(joy1_state & JOY1_BTN1))  *joy_map |= JOY_BTN1;
      if(!(joy1_state & JOY1_BTN2))  *joy_map |= JOY_BTN2;
      return 1;
    } else
      return 0;
  }
}

char GetRTC(unsigned char *d) {
  return usb_rtc_get_time(d);
}

char SetRTC(unsigned char *d) {
  return usb_rtc_set_time(d);
}

void RAMFUNC UnlockFlash() {
  *AT91C_MC_FMR = 48 << 16 | FWS << 8; // MCLK cycles in 1us
  for (int i = 0; i < 16; i++)
    if (*AT91C_MC_FSR & 1 << 16 + i)
    { // page is locked
      while (!(*AT91C_MC_FSR & AT91C_MC_FRDY));  // wait for ready
      *AT91C_MC_FCR = 0x5A << 24 | i << 6 + 8 | AT91C_MC_FCMD_UNLOCK; // unlock page
      while (!(*AT91C_MC_FSR & AT91C_MC_FRDY));  // wait for ready
    }

  *AT91C_MC_FMR = 72 << 16 | FWS << 8; // MCLK cycles in 1.5us
}

void RAMFUNC WriteFlash(int page) {
  while (!(*AT91C_MC_FSR & AT91C_MC_FRDY));  // wait for ready
  *AT91C_MC_FCR = 0x5A << 24 | page << 8 | AT91C_MC_FCMD_START_PROG; // key: 0x5A
  while (!(*AT91C_MC_FSR & AT91C_MC_FRDY));  // wait for ready
}
