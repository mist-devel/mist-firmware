#ifndef FPGA_H
#define FPGA_H

#include "fat_compat.h"

void fpga_init(const char *name);
unsigned char ConfigureFpga(const char*);
void SendFile(FIL *file);
void SendFileEncrypted(FIL *file,unsigned char *key,int keysize);
void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size);
char BootDraw(char *data, unsigned short len, unsigned short offset);
char BootPrint(const char *text);
char PrepareBootUpload(unsigned char base, unsigned char size);
void BootExit(void);
void ClearMemory(unsigned long base, unsigned long size);
unsigned char GetFPGAStatus(void);

// minimig reset stuff
#define SPI_RST_USR         0x1
#define SPI_RST_CPU         0x2
#define SPI_CPU_HLT         0x4
extern uint8_t rstval;

#endif

