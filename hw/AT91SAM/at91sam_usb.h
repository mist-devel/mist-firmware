//*----------------------------------------------------------------------------
//*      ATMEL Microcontroller Software Support  -  ROUSSET  -
//*----------------------------------------------------------------------------
//* The software is delivered "AS IS" without warranty or condition of any
//* kind, either express, implied or statutory. This includes without
//* limitation any warranty or condition with respect to merchantability or
//* fitness for any particular purpose, or against the infringements of
//* intellectual property rights of others.
//*----------------------------------------------------------------------------
//* File Name           : cdc_enumerate.h
//* Object              : Handle CDC enumeration
//*
//* 1.0 Apr 20 200 	: ODi Creation
//*----------------------------------------------------------------------------
#ifndef AT91SAM_USB_H
#define AT91SAM_USB_H

#include <inttypes.h>

#define AT91C_EP_OUT_SIZE 0x40
#define AT91C_EP_OUT 1

#define AT91C_EP_IN_SIZE 0x40
#define AT91C_EP_IN  2

typedef void (*AT91F_USB_Enumerate)(void);

void AT91F_USB_Open(AT91F_USB_Enumerate setup_callback);
uint8_t AT91F_USB_Is_Configured(void);
uint16_t AT91F_USB_Read(char *pData, uint16_t length);
uint16_t AT91F_USB_Write(const char *pData, uint16_t length);
void AT91F_USB_SendData(const char *pData, uint16_t length);
void AT91F_USB_SendStr(const char *str, uint32_t max);
void AT91F_USB_SendWord(uint16_t data);
void AT91F_USB_SendZlp();
void AT91F_USB_SendStall();

#endif // AT91SAM_USB_H
