#ifndef TOS_H
#define TOS_H

#include "fat.h"

// FPGA spi cmommands
#define MIST_INVALID      0x00

// memory interface
#define MIST_SET_ADDRESS  0x01
#define MIST_WRITE_MEMORY 0x02
#define MIST_READ_MEMORY  0x03
#define MIST_SET_CONTROL  0x04
#define MIST_GET_DMASTATE 0x05   // reads state of dma and floppy controller
#define MIST_ACK_DMA      0x06   // acknowledges a dma command

// tos sysconfig bits:
// 0     - RESET
// 1-3   - Memory configuration
// 4-5   - CPU configuration
// 6-7   - Floppy A+B write protection
// 8     - Color/Monochrome mode

// memory configurations (0x02/0x04/0x08)
// (currently 4MB are fixed and cannot be changed)
#define TOS_MEMCONFIG_512K       (0<<1)   // not yet supported
#define TOS_MEMCONFIG_1M         (1<<1)   // not yet supported
#define TOS_MEMCONFIG_2M         (2<<1)   // not yet supported
#define TOS_MEMCONFIG_4M         (3<<1)   // not yet supported
#define TOS_MEMCONFIG_8M         (4<<1)
#define TOS_MEMCONFIG_14M        (5<<1)
#define TOS_MEMCONFIG_RES0       (6<<1)   // reserved
#define TOS_MEMCONFIG_RES1       (7<<1)   // reserved

// cpu configurations (0x10/0x20)
#define TOS_CPUCONFIG_68000      (0<<4)
#define TOS_CPUCONFIG_68010      (1<<4)
#define TOS_CPUCONFIG_RESERVED   (2<<4)
#define TOS_CPUCONFIG_68020      (3<<4)

// control bits (all control bits have unknown state after core startup)
#define TOS_CONTROL_CPU_RESET     0x0001
#define TOS_CONTROL_FDC_WR_PROT_A 0x0040
#define TOS_CONTROL_FDC_WR_PROT_B 0x0080
#define TOS_CONTROL_VIDEO_COLOR   0x0100   // input to mfp
#define TOS_CONTROL_PAL50HZ       0x0200   // display pal at 50hz (56 hz otherwise)

extern unsigned long tos_system_ctrl;

void tos_upload();
void tos_show_state();
void tos_update_sysctrl(unsigned long);
char *tos_get_disk_name(char);
char tos_disk_is_inserted(char index);
void tos_insert_disk(char i, fileTYPE *file);

#endif
