//*----------------------------------------------------------------------------
//*      ATMEL Microcontroller Software Support  -  ROUSSET  -
//*----------------------------------------------------------------------------
//* The software is delivered "AS IS" without warranty or condition of any
//* kind, either express, implied or statutory. This includes without
//* limitation any warranty or condition with respect to merchantability or
//* fitness for any particular purpose, or against the infringements of
//* intellectual property rights of others.
//*----------------------------------------------------------------------------
//* File Name           : cdc_enumerate.c
//* Object              : Handle CDC enumeration
//*
//* 1.0 Apr 20 200 	: ODi Creation
//* 1.1 14/Sep/2004 JPP : Minor change
//* 1.1 15/12/2004  JPP : suppress warning
//*----------------------------------------------------------------------------

// 12. Apr. 2006: added modification found in the mikrocontroller.net gcc-Forum 
//                additional line marked with /* +++ */
// 1. Sept. 2006: fixed case: board.h -> Board.h

#include "AT91SAM7S256.h"
#include <string.h>

#include "at91sam_usb.h"
#include "hardware.h"
#include "debug.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define WORD(a) (a)&0xff, ((a)>>8)&0xff

// Private members
static unsigned int  currentRcvBank;
static AT91F_USB_Enumerate stp_callback;

static void usb_irq_handler(void) {
  AT91_REG isr = AT91C_BASE_UDP->UDP_ISR & AT91C_BASE_UDP->UDP_IMR;

  //  tx_hex("i: ", isr);

  // handle all known interrupt sources
  if(isr & AT91C_UDP_ENDBUSRES) {
    AT91C_BASE_UDP->UDP_ICR = AT91C_UDP_ENDBUSRES;

    //    tx_str("USB bus reset\r\n");

    // reset all endpoints
    AT91C_BASE_UDP->UDP_RSTEP  = (unsigned int)-1;
    AT91C_BASE_UDP->UDP_RSTEP  = 0;
    // Enable the function
    AT91C_BASE_UDP->UDP_FADDR = AT91C_UDP_FEN;
    // Configure endpoint 0
    AT91C_BASE_UDP->UDP_CSR[0] = (AT91C_UDP_EPEDS | AT91C_UDP_EPTYPE_CTRL);

    // enable ep0 interrupt
    AT91C_BASE_UDP->UDP_IER = AT91C_UDP_EPINT0;
  }

  // data received for endpoint 0
  if(isr & AT91C_UDP_EPINT0) {
    AT91C_BASE_UDP->UDP_ICR = AT91C_UDP_EPINT0;
    (*stp_callback)();
  }

  // clear all remaining irqs
  AT91C_BASE_UDP->UDP_ICR = AT91C_BASE_UDP->UDP_ISR;
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_Open
//* \brief
//*----------------------------------------------------------------------------
void AT91F_USB_Open(AT91F_USB_Enumerate setup_callback) {
  // Set the PLL USB Divider
  AT91C_BASE_CKGR->CKGR_PLLR |= AT91C_CKGR_USBDIV_1 ;

  // Specific Chip USB Initialisation
  // Enables the 48MHz USB clock UDPCK and System Peripheral USB Clock
  AT91C_BASE_PMC->PMC_SCER = AT91C_PMC_UDP;
  AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_UDP);

  // Enable UDP PullUp (USB_DP_PUP) : enable & Clear of the corresponding PIO
  // Set in PIO mode and Configure in Output with pullup
  AT91C_BASE_PIOA->PIO_PER = USB_PUP;
  AT91C_BASE_PIOA->PIO_OER = USB_PUP;
  AT91C_BASE_PIOA->PIO_CODR = USB_PUP;

  currentRcvBank       = AT91C_UDP_RX_DATA_BK0;

  /* Enable usb_interrupt */
  AT91C_AIC_SMR[AT91C_ID_UDP] = AT91C_AIC_SRCTYPE_INT_HIGH_LEVEL | 4;
  AT91C_AIC_SVR[AT91C_ID_UDP] = (unsigned int)usb_irq_handler;
  *AT91C_AIC_IECR = (1 << AT91C_ID_UDP);
  stp_callback = setup_callback;
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_Is_Configured
//* \brief Test if the device is configured
//*----------------------------------------------------------------------------
uint8_t AT91F_USB_Is_Configured(void) {
  return AT91C_BASE_UDP->UDP_GLBSTATE & AT91C_UDP_CONFG;
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_Read
//* \brief Read available data from Endpoint OUT
//*----------------------------------------------------------------------------
uint16_t AT91F_USB_Read(char *pData, uint16_t length) {
  uint16_t packetSize, nbBytesRcv = 0;

  if ( !AT91F_USB_Is_Configured() )
    return 0;

  if ( AT91C_BASE_UDP->UDP_CSR[AT91C_EP_OUT] & currentRcvBank ) {
    packetSize = MIN(AT91C_BASE_UDP->UDP_CSR[AT91C_EP_OUT] >> 16, length);

    while(packetSize--)
      pData[nbBytesRcv++] = AT91C_BASE_UDP->UDP_FDR[AT91C_EP_OUT];

    AT91C_BASE_UDP->UDP_CSR[AT91C_EP_OUT] &= ~(currentRcvBank);

    // toggle banks
    if(currentRcvBank == AT91C_UDP_RX_DATA_BK0)
      currentRcvBank = AT91C_UDP_RX_DATA_BK1;
    else
      currentRcvBank = AT91C_UDP_RX_DATA_BK0;
  }

  return nbBytesRcv;
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_Write
//* \brief Send through endpoint 2
//*----------------------------------------------------------------------------

static void wait4tx(char ep) {
  long to = GetTimer(2);  // wait max 2ms for tx to succeed

  // wait for host to acknowledge data reception
  while ( !(AT91C_BASE_UDP->UDP_CSR[ep] & AT91C_UDP_TXCOMP) ) 
    if(CheckTimer(to)) return;

  // clear flag (clear irq)
  AT91C_BASE_UDP->UDP_CSR[ep] &= ~AT91C_UDP_TXCOMP;

  // wait for register clear to succeed
  while (AT91C_BASE_UDP->UDP_CSR[ep] & AT91C_UDP_TXCOMP);
}

// copy bytes to the endpoint fifo and start transmission
static uint16_t ep_tx(char ep, const char *pData, uint16_t length) {
  uint16_t b2c = MIN(length, ep?AT91C_EP_IN_SIZE:8);
  uint16_t retval = b2c;

  // copy all bytes into send buffer
  while (b2c--) AT91C_BASE_UDP->UDP_FDR[ep] = *pData++;
  AT91C_BASE_UDP->UDP_CSR[ep] |= AT91C_UDP_TXPKTRDY;

  return retval;
}

uint16_t AT91F_USB_Write(const char *pData, uint16_t length) {

  if ( !AT91F_USB_Is_Configured() )
    return length;

  while(length) {
    uint16_t sent = ep_tx(AT91C_EP_IN, pData, length);
    length -= sent;
    pData += sent;
    wait4tx(AT91C_EP_IN);
  }

  return length;
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendData
//* \brief Send Data through the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendData(const char *pData, uint16_t length) {
  AT91_REG csr;

  do {
    long to = GetTimer(2);  // wait max 2ms for tx to succeed

    uint32_t sent = ep_tx(0, pData, length);
    length -= sent;
    pData += sent;
 
    // wait for transmission with timeout
    do {
      csr = AT91C_BASE_UDP->UDP_CSR[0];

      // Data IN stage has been stopped by a status OUT
      if (csr & AT91C_UDP_RX_DATA_BK0) {
        AT91C_BASE_UDP->UDP_CSR[0] &= ~(AT91C_UDP_RX_DATA_BK0);
        return;
      }
    } while (( !(csr & AT91C_UDP_TXCOMP) ) && !CheckTimer(to));

    // clear flag (clear irq)
    AT91C_BASE_UDP->UDP_CSR[0] &= ~AT91C_UDP_TXCOMP;

    // wait for register clear to succeed
    while (AT91C_BASE_UDP->UDP_CSR[0] & AT91C_UDP_TXCOMP);

  } while (length);
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendStr
//* \brief Send a string through the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendStr(const char *str, uint32_t max) {
  uint8_t len = 2*strlen(str)+2;
  char cmd[len], *p;

  cmd[0] = len;
  cmd[1] = 0x03;

  p = cmd+2;
  while(*str) {
    *p++ = *str++;
    *p++ = 0;
  }

  AT91F_USB_SendData(cmd, MIN(len, max));
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendWord
//* \brief Send a 16 bit word through the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendWord(uint16_t data) {
  AT91F_USB_SendData((char *) &data, sizeof(data));
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendZlp
//* \brief Send zero length packet through the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendZlp() {
  AT91C_BASE_UDP->UDP_CSR[0] |= AT91C_UDP_TXPKTRDY;
  wait4tx(0);
}

//*----------------------------------------------------------------------------
//* \fn    AT91F_USB_SendStall
//* \brief Stall the control endpoint
//*----------------------------------------------------------------------------
void AT91F_USB_SendStall() {
  AT91C_BASE_UDP->UDP_CSR[0] |= AT91C_UDP_FORCESTALL;
  while ( !(AT91C_BASE_UDP->UDP_CSR[0] & AT91C_UDP_ISOERROR) );
  AT91C_BASE_UDP->UDP_CSR[0] &= ~(AT91C_UDP_FORCESTALL | AT91C_UDP_ISOERROR);
  while (AT91C_BASE_UDP->UDP_CSR[0] & (AT91C_UDP_FORCESTALL | AT91C_UDP_ISOERROR));
}
