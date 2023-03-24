// hdd.h


#ifndef __HDD_H__
#define __HDD_H__

#include <stdbool.h>
#include "idxfile.h"

// defines
#define CMD_IDECMD  0x04
#define CMD_IDEDAT  0x08

#define CMD_IDE_REGS_RD   0x80
#define CMD_IDE_REGS_WR   0x90
#define CMD_IDE_DATA_WR   0xA0
#define CMD_IDE_DATA_RD   0xB0
#define CMD_IDE_CDDA_RD   0xC0 // status bit read, since no free CMD_xxx :(
#define CMD_IDE_CDDA_WR   0xD0
#define CMD_IDE_STATUS_WR 0xF0

#define CMD_IDE_CFG_WR    0xFA

#define IDE_STATUS_END  0x80
#define IDE_STATUS_PKT  0x20
#define IDE_STATUS_IRQ  0x10
#define IDE_STATUS_RDY  0x08
#define IDE_STATUS_REQ  0x04
#define IDE_STATUS_ERR  0x01

#define ACMD_NOP                          0x00
#define ACMD_DEVICE_RESET                 0x08
#define ACMD_RECALIBRATE                  0x10
#define ACMD_DIAGNOSTIC                   0x90
#define ACMD_IDENTIFY_DEVICE              0xEC
#define ACMD_INITIALIZE_DEVICE_PARAMETERS 0x91
#define ACMD_READ_SECTORS                 0x20
#define ACMD_WRITE_SECTORS                0x30
#define ACMD_READ_VERIFY_SECTORS          0x40
#define ACMD_READ_MULTIPLE                0xC4
#define ACMD_WRITE_MULTIPLE               0xC5
#define ACMD_SET_MULTIPLE_MODE            0xC6
#define ACMD_PACKET                       0xA0
#define ACMD_IDENTIFY_PACKET_DEVICE       0xA1

#define HDF_DISABLED  0
#define HDF_FILE      1
#define HDF_CARD      2
#define HDF_CARDPART0 3
#define HDF_CARDPART1 4
#define HDF_CARDPART2 5
#define HDF_CARDPART3 6
#define HDF_CDROM     10
#define HDF_TYPEMASK  15
#define HDF_SYNTHRDB  128 // flag to indicate whether we should auto-synthesize a RigidDiskBlock

#define HDF_FILETYPE_UNKNOWN  0
#define HDF_FILETYPE_NOTFOUND 1
#define HDF_FILETYPE_RDB      2
#define HDF_FILETYPE_DOS      3

#define HARDFILES 4

#define TFR_ERR    1
#define TFR_SCOUNT 2
#define TFR_SNUM   3
#define TFR_CYLL   4
#define TFR_CYLH   5
#define TFR_SDH    6
#define TFR_STAT   7

// types
typedef struct
{
    unsigned char enabled; // 0: Disabled, 1: Hard file, 2: MMC (entire card), 3-6: Partition 1-4 of MMC card, 10-CDROM
    unsigned char present;
    char name[64];
} hardfileTYPE;

typedef struct
{
  int             type; // are we using a file, the entire SD card or a partition on the SD card?
  IDXFile         *idxfile;
  unsigned short  cylinders;
  unsigned short  heads;
  unsigned short  sectors;
  unsigned short  sectors_per_block;
  unsigned short  partition; // partition no.
  long            offset; // if a partition, the lba offset of the partition.  Can be negative if we've synthesized an RDB.
} hdfTYPE;

// variables
extern hardfileTYPE *hardfile[HARDFILES];
extern hdfTYPE hdf[HARDFILES];

// functions
void HandleHDD(unsigned char c1, unsigned char c2, unsigned char cs1ena);
unsigned char OpenHardfile(unsigned char unit, bool amiga);
unsigned char GetHDFFileType(const char *filename);
void SendHDFCfg();

#endif // __HDD_H__

