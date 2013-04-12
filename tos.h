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
#define MIST_NAK_DMA      0x07   // don't acknowledge a dma command, but release bus

// tos sysconfig bits:
// 0     - RESET
// 1-3   - Memory configuration
// 4-5   - CPU configuration
// 6-7   - Floppy A+B write protection
// 8     - Color/Monochrome mode
// 9     - PAL mode in 56 or 50 Hz
// 10-17 - ACSI device enable

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
#define TOS_CONTROL_CPU_RESET     0x00000001
#define TOS_CONTROL_FDC_WR_PROT_A 0x00000040
#define TOS_CONTROL_FDC_WR_PROT_B 0x00000080
#define TOS_CONTROL_VIDEO_COLOR   0x00000100   // input to mfp
#define TOS_CONTROL_PAL50HZ       0x00000200   // display pal at 50hz (56 hz otherwise)

// up to eight acsi devices can be enabled
#define TOS_ACSI0_ENABLE          0x00000400
#define TOS_ACSI1_ENABLE          0x00000800
#define TOS_ACSI2_ENABLE          0x00001000
#define TOS_ACSI3_ENABLE          0x00002000
#define TOS_ACSI4_ENABLE          0x00004000
#define TOS_ACSI5_ENABLE          0x00008000
#define TOS_ACSI6_ENABLE          0x00010000
#define TOS_ACSI7_ENABLE          0x00020000

extern unsigned long tos_system_ctrl;

void tos_upload(char *);
void tos_show_state();
void tos_update_sysctrl(unsigned long);
char *tos_get_disk_name(char);
char tos_disk_is_inserted(char index);
void tos_insert_disk(char i, fileTYPE *file);
void tos_eject_all();
void tos_select_hdd_image(fileTYPE *file);
void tos_reset(char cold);
char *tos_get_image_name();
char *tos_get_cartridge_name();
char tos_cartridge_is_inserted();
void tos_load_cartridge(char *);

#endif
