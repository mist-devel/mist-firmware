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

// 2009-11-22 - read/write multiple implemented

// 2020-11-14 - AMR: Simplified and combined read / readm + write / writem.  AROS IDE now works.


#include <stdio.h>
#include <string.h>

#include "swab.h"
#include "errors.h"
#include "hardware.h"
#include "fat_compat.h"
#include "hdd.h"
#include "hdd_internal.h"
#include "mmc.h"
#include "menu.h"
#include "fpga.h"
#include "debug.h"


hardfileTYPE  *hardfile[HARDFILES];

// hardfile structure
hdfTYPE hdf[HARDFILES];

static void SwapBytes(char *c, unsigned int len)
{
  char temp;

  while(len) {
    temp = *c;
    *c=c[1];
    c[1]=temp;
    len-=2;
    c+=2;
  }
}

// RDBChecksum()
static void RDBChecksum(unsigned long *p)
{
  unsigned long count=p[1];
  unsigned long c2;
  long result=0;
  p[2]=0;
  for(c2=0;c2<count;++c2) result+=p[c2];
  p[2]=(unsigned long)-result;
}


// FakeRDB()
// if the hardfile doesn't have a RigidDiskBlock, we synthesize one
static void FakeRDB(int unit,int block)
{
  int i;
  // start by clearing the sector buffer
  memset(sector_buffer, 0, 512);

  // if we're asked for LBA 0 we create an RDSK block, and if LBA 1, a PART block
  switch(block) {
    case 0: {
      // RDB
      hdd_debugf("FAKE: RDB");
      struct RigidDiskBlock *rdb=(struct RigidDiskBlock *)sector_buffer;
      rdb->rdb_ID = 'R'<<24 | 'D' << 16 | 'S' << 8 | 'K';
      rdb->rdb_Summedlongs=0x40;
      rdb->rdb_HostID=0x07;
      rdb->rdb_BlockBytes=0x200;
      rdb->rdb_Flags=0x12;                 // (Disk ID valid, no LUNs after this one)
      rdb->rdb_BadBlockList=0xffffffff;    // We don't provide a bad block list
      rdb->rdb_PartitionList=1;
      rdb->rdb_FileSysHeaderList=0xffffffff;
      rdb->rdb_DriveInit=0xffffffff;
      rdb->rdb_Reserved1[0]=0xffffffff;
      rdb->rdb_Reserved1[1]=0xffffffff;
      rdb->rdb_Reserved1[2]=0xffffffff;
      rdb->rdb_Reserved1[3]=0xffffffff;
      rdb->rdb_Reserved1[4]=0xffffffff;
      rdb->rdb_Reserved1[5]=0xffffffff;
      rdb->rdb_Cylinders=hdf[unit].cylinders;
      rdb->rdb_Sectors=hdf[unit].sectors;
      rdb->rdb_Heads=hdf[unit].heads;
      rdb->rdb_Interleave=1;
      rdb->rdb_Park=rdb->rdb_Cylinders;
      rdb->rdb_WritePreComp=rdb->rdb_Cylinders;
      rdb->rdb_ReducedWrite=rdb->rdb_Cylinders;
      rdb->rdb_StepRate=3;
      rdb->rdb_RDBBlocksLo=0;
      rdb->rdb_RDBBlocksHi=1;
      rdb->rdb_LoCylinder=1;
      rdb->rdb_HiCylinder=rdb->rdb_Cylinders-1;
      rdb->rdb_CylBlocks=rdb->rdb_Heads * rdb->rdb_Sectors;
      rdb->rdb_AutoParkSeconds=0;
      rdb->rdb_HighRDSKBlock=1;
      strcpy(rdb->rdb_DiskVendor,"Do not ");
      strcpy(rdb->rdb_DiskProduct, "repartition!");
      // swap byte order of strings to be able to "unswap" them after checksum
      unsigned long *p = (unsigned long*)rdb;
      for(i=0;i<(8+16)/4;i++) p[40+i] = swab32(p[40+i]);
      RDBChecksum((unsigned long *)rdb);
      // swap byte order of first 0x40 long values
      for(i=0;i<0x40;i++) p[i] = swab32(p[i]);
      break;
    }
    case 1: {
      // Partition
      hdd_debugf("FAKE: Partition");
      struct PartitionBlock *pb=(struct PartitionBlock *)sector_buffer;
      pb->pb_ID = 'P'<<24 | 'A' << 16 | 'R' << 8 | 'T';
      pb->pb_Summedlongs=0x40;
      pb->pb_HostID=0x07;
      pb->pb_Next=0xffffffff;
      pb->pb_Flags=0x1; // bootable
      pb->pb_DevFlags=0;
      strcpy(pb->pb_DriveName,unit?"1HD\003":"0HD\003");  // "DH0"/"DH1" BCPL string
      pb->pb_Environment.de_TableSize=0x10;
      pb->pb_Environment.de_SizeBlock=0x80;
      pb->pb_Environment.de_Surfaces=hdf[unit].heads;
      pb->pb_Environment.de_SectorPerBlock=1;
      pb->pb_Environment.de_BlocksPerTrack=hdf[unit].sectors;
      pb->pb_Environment.de_Reserved=2;
      pb->pb_Environment.de_LowCyl=1;
      pb->pb_Environment.de_HighCyl=hdf[unit].cylinders-1;
      pb->pb_Environment.de_NumBuffers=30;
      pb->pb_Environment.de_MaxTransfer=0xffffff;
      pb->pb_Environment.de_Mask=0x7ffffffe;
      pb->pb_Environment.de_DosType=0x444f5301;
      RDBChecksum((unsigned long *)pb);
      // swap byte order of first 0x40 entries
      unsigned long *p = (unsigned long*)pb;
      for(i=0;i<0x40;i++) p[i] = swab32(p[i]);
      break;
    }
    default: {
      break;
    }
  }
}


// IdentifiyDevice()
// builds Identify Device struct
static void IdentifyDevice(unsigned short *pBuffer, unsigned char unit)
{
  char *p, i, x;
  unsigned long total_sectors = hdf[unit].cylinders * hdf[unit].heads * hdf[unit].sectors;
  memset(pBuffer, 0, 512);

  switch(hdf[unit].type) {
    case HDF_FILE | HDF_SYNTHRDB:
    case HDF_FILE:
      pBuffer[0] = 1 << 6; // hard disk
      pBuffer[1] = hdf[unit].cylinders; // cyl count
      pBuffer[3] = hdf[unit].heads; // head count
      pBuffer[6] = hdf[unit].sectors; // sectors per track
      // FIXME - can get serial no from card itself.
      memcpy((char*)&pBuffer[10], "iMTSiMiniMHgrafdli e", 20); // serial number - byte swapped
      memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
      p = (char*)&pBuffer[27];
      // FIXME - likewise the model name can be fetched from the card.
      if (hdf[unit].type & HDF_SYNTHRDB) {
        memcpy(p, "DON'T                                   ", 40);
        p += 8;
        memcpy(p, "REPARTITION!    ", 16);
      } else {
        memcpy(p, "YAQUBE                                  ", 40); // model name - byte swapped
        p += 8;
        for (i = 0; (x = hardfile[unit]->name[i]) && i < 16; i++) // copy file name as model name
          p[i] = x;
      }
      SwapBytes((char*)&pBuffer[27], 40);
      break;
    case HDF_CARD:
    case HDF_CARDPART0:
    case HDF_CARDPART1:
    case HDF_CARDPART2:
    case HDF_CARDPART3:
      pBuffer[0] = 1 << 6;                                           // hard disk
      pBuffer[1] = hdf[unit].cylinders;                              // cyl count
      pBuffer[3] = hdf[unit].heads;                                  // head count
      pBuffer[6] = hdf[unit].sectors;                                // sectors per track
      // FIXME - can get serial no from card itself.
      memcpy((char*)&pBuffer[10], "iMTSiMiniMSg0D      ", 20);       // serial number - byte swapped
      pBuffer[23]+=hdf[unit].type-HDF_CARD;
      memcpy((char*)&pBuffer[23], ".100    ", 8);                    // firmware version - byte swapped
      p = (char*)&pBuffer[27];
      // FIXME - likewise the model name can be fetched from the card.
      memcpy(p, "YAQUBE                                  ", 40);     // model name - byte swapped
      p += 8;
      if (hdf[unit].type==HDF_CARD)
        memcpy(p, "SD/MMC Card", 11);                                // copy file name as model name
      else {
        memcpy(p, "Card Part 1", 11);                                // copy file name as model name
        p[10]+=hdf[unit].partition;
      }
      SwapBytes((char*)&pBuffer[27], 40);
      break;
  }

  pBuffer[47] = 0x8010; // maximum sectors per block in Read/Write Multiple command
  pBuffer[49] = 0x0200; // support LBA addressing
  pBuffer[53] = 1;
  pBuffer[54] = hdf[unit].cylinders;
  pBuffer[55] = hdf[unit].heads;
  pBuffer[56] = hdf[unit].sectors;
  pBuffer[57] = (unsigned short)total_sectors;
  pBuffer[58] = (unsigned short)(total_sectors >> 16);
  pBuffer[60] = (unsigned short)total_sectors;
  pBuffer[61] = (unsigned short)(total_sectors >> 16);
}


// chs2lba()
static unsigned long chs2lba(unsigned short cylinder, unsigned char head, unsigned short sector, unsigned char unit, char lbamode)
{
  if (lbamode){
    return ((head<<24) + (cylinder<<8) + sector);
  }else
    return (cylinder * hdf[unit].heads + head) * hdf[unit].sectors + sector - 1;
}


// HardFileSeek()
static unsigned char HardFileSeek(hdfTYPE *pHDF, unsigned long lba)
{
  FSIZE_t seek_pos = (FSIZE_t) lba << 9;
  FRESULT res;
  res = f_lseek(&pHDF->idxfile->file, seek_pos);
  if (res != FR_OK || f_tell(&pHDF->idxfile->file) != seek_pos) {
    hdd_debugf("Seek error: %llu, %llu", seek_pos, f_tell(&pHDF->idxfile->file));
    return 0;
  }
  return 1;
}


// WriteTaskFile()
static void WriteTaskFile(unsigned char error, unsigned char sector_count, unsigned char sector_number, unsigned char cylinder_low, unsigned char cylinder_high, unsigned char drive_head)
{
  EnableFpga();

  SPI(CMD_IDE_REGS_WR); // write task file registers command
  SPI(0x00);
  SPI(0x00); // dummy
  SPI(0x00);
  SPI(0x00); // dummy
  SPI(0x00);

  SPI(0x00); // dummy

  SPI(0x00);
  SPI(0x00);
  SPI(error);         // error
  SPI(0x00);
  SPI(sector_count);  // sector count
  SPI(0x00);
  SPI(sector_number); // sector number
  SPI(0x00);
  SPI(cylinder_low);  // cylinder low
  SPI(0x00);
  SPI(cylinder_high); // cylinder high
  SPI(0x00);
  SPI(drive_head);    // drive/head

  DisableFpga();
}


// WriteStatus()
static void WriteStatus(unsigned char status)
{
  EnableFpga();

  SPI(CMD_IDE_STATUS_WR);
  SPI(status);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);

  DisableFpga();
}


// ATA_Recalibrate()
static inline void ATA_Recalibrate(unsigned char* tfr, unsigned char unit)
{
  // Recalibrate 0x10-0x1F (class 3 command: no data)
  hdd_debugf("IDE%d: Recalibrate", unit);
  WriteTaskFile(0, 0, 1, 0, 0, tfr[6] & 0xF0);
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_Diagnostic()
static inline void ATA_Diagnostic(unsigned char* tfr)
{
  // Execute Drive Diagnostic (0x90)
  hdd_debugf("IDE: Drive Diagnostic");
  WriteTaskFile(1, 0, 0, 0, 0, 0);
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_IdentifyDevice()
static inline void ATA_IdentifyDevice(unsigned char* tfr, unsigned char unit)
{
  int i;
  unsigned short *id = (unsigned short *)sector_buffer;
  // Identify Device (0xec)
  hdd_debugf("IDE%d: Identify Device", unit);
  IdentifyDevice(id, unit);
  WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
  WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
  EnableFpga();
  SPI(CMD_IDE_DATA_WR); // write data command
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  for (i = 0; i < 256; i++) {
    SPI((unsigned char)id[i]);
    SPI((unsigned char)(id[i] >> 8));
  }
  DisableFpga();
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_Initialize()
static inline void ATA_Initialize(unsigned char* tfr, unsigned char unit)
{
  // Initialize Device Parameters (0x91)
  hdd_debugf("Initialize Device Parameters");
  hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
  WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_SetMultipleMode()
static inline void ATA_SetMultipleMode(unsigned char* tfr, unsigned char unit)
{
  // Set Multiple Mode (0xc6)
  hdd_debugf("Set Multiple Mode");
  hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
  hdf[unit].sectors_per_block = tfr[2];
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_ReadSectors()
static inline void ATA_ReadSectors(unsigned char* tfr, unsigned short sector, unsigned short cylinder, unsigned char head, unsigned char unit, unsigned short sector_count, unsigned char multiple, char lbamode)
{
  // Read Sectors (0x20)
  long lba;
  int i;
  int block_count;

  lba=chs2lba(cylinder, head, sector, unit, lbamode);
  hdd_debugf("IDE%d: read %s, %d.%d.%d:%d, %d", unit, (lbamode ? "LBA" : "CHS"), cylinder, head, sector, lba, sector_count);

  while (sector_count)
  {
    block_count = multiple ? sector_count : 1;
    if (multiple && block_count > hdf[unit].sectors_per_block)
      block_count = hdf[unit].sectors_per_block;

    WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
    while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer

    WriteStatus(IDE_STATUS_IRQ);

    switch(hdf[unit].type)
    {
      case HDF_FILE | HDF_SYNTHRDB:
      case HDF_FILE:
      if (f_size(&hdf[unit].idxfile->file))
      {
        int blk=block_count;
        // Deal with FakeRDB and the potential for a read_multiple to cross the boundary into actual data.
        while(blk && (lba+hdf[unit].offset<0 || ((unit == 0) && (hdf[unit].type == HDF_FILE) && (lba == 0)))) {
          if ((lba+hdf[unit].offset) < 0)
            FakeRDB(unit,lba);
          else // Adjust flags of a real RDB if present.  Is this necessary? If it worked before it was accidental due to malformed "if"
          {
            HardFileSeek(&hdf[unit], lba + hdf[unit].offset);
            // read sector into buffer
            FileReadBlock(&hdf[unit].idxfile->file, sector_buffer);

            // adjust checksum by the difference between old and new flag value
            struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
            rdb->rdb_ChkSum = swab32(swab32(rdb->rdb_ChkSum) + swab32(rdb->rdb_Flags) - 0x12);

            // adjust flags
            rdb->rdb_Flags=swab32(0x12);
          }
          EnableFpga();
          spi8(CMD_IDE_DATA_WR); // write data command
          spi_n(0x00, 5);
          spi_block_write(sector_buffer);
          DisableFpga();
          ++lba;
          --blk;
        }
        if(blk) // Any blocks left?
        {
          HardFileSeek(&hdf[unit], lba + hdf[unit].offset);
          FileReadBlockEx(&hdf[unit].idxfile->file, 0, blk); // NULL enables direct transfer to the FPGA
          lba+=blk;
        }
      }
      else
        WriteStatus(IDE_STATUS_RDY|IDE_STATUS_ERR);
      break;

      case HDF_CARD:
      case HDF_CARDPART0:
      case HDF_CARDPART1:
      case HDF_CARDPART2:
      case HDF_CARDPART3:
        MMC_ReadMultiple(lba+hdf[unit].offset,0,block_count);
        lba+=block_count;
        break;
    }

    /* Advance CHS address - address of last read remains. */
    while(block_count--)
    {
      if (sector_count!=1)
      {
        if (sector == hdf[unit].sectors)
        {
          sector = 1;
          head++;
          if (head == hdf[unit].heads)
          {
            head = 0;
            cylinder++;
          }
        }
        else
        sector++;
      }
      --sector_count;
    }
    if (lbamode) {
      sector = lba & 0xff;
      cylinder = lba >> 8;
      head = lba >> 24;
    }
    /* Update task file with CHS address */
    WriteTaskFile(0, tfr[2], sector, cylinder, (cylinder >> 8), (tfr[6] & 0xF0) | head);

  }
  WriteStatus(IDE_STATUS_END);
}


// ATA_WriteSectors()
static inline void ATA_WriteSectors(unsigned char* tfr, unsigned short sector, unsigned short cylinder, unsigned char head, unsigned char unit, unsigned short sector_count, char multiple, char lbamode)
{
  unsigned short i;
  unsigned short block_count, block_size, sectors;
  unsigned char *buf;
  long lba=chs2lba(cylinder, head, sector, unit, lbamode);

  // write sectors
  WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type
  hdd_debugf("IDE%d: write %s, %d.%d.%d:%d, %d", unit, (lbamode ? "LBA" : "CHS"), cylinder, head, sector, lba, sector_count);

  lba+=hdf[unit].offset;
  if (hdf[unit].type & HDF_FILE) {
    HardFileSeek(&hdf[unit], (lba>-1) ? lba : 0);
  }

  while (sector_count) {
    block_count = multiple ? sector_count : 1;
    if (multiple && block_count > hdf[unit].sectors_per_block)
        block_count = hdf[unit].sectors_per_block;

    UINT bw;

    while(block_count)
    {
      block_size = (block_count > SECTOR_BUFFER_SIZE/512) ? (SECTOR_BUFFER_SIZE/512) : block_count;
      sectors = block_size;
      buf = sector_buffer;
      while(sectors--) {
        while (!(GetFPGAStatus() & CMD_IDEDAT)); // wait for full write buffer
        EnableFpga();
        SPI(CMD_IDE_DATA_RD); // read data command
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        spi_block_read(buf);
        DisableFpga();
        buf += 512;
      }
      switch(hdf[unit].type) {
        case HDF_FILE | HDF_SYNTHRDB:
        case HDF_FILE:
          if (f_size(&hdf[unit].idxfile->file) && (lba>-1)) {
            // Don't attempt to write to fake RDB
            f_write(&hdf[unit].idxfile->file, sector_buffer, 512*block_size, &bw);
          }
          lba+=block_size;
          break;
        case HDF_CARD:
        case HDF_CARDPART0:
        case HDF_CARDPART1:
        case HDF_CARDPART2:
        case HDF_CARDPART3:
          if (block_size == 1)
            MMC_Write(lba, sector_buffer);
          else
            MMC_WriteMultiple(lba, sector_buffer, block_size);
          lba+=block_size;
          break;
      }

      // decrease sector count
      sectors = block_size;
      while(sectors--) {
        if (sector_count!=1) {
          if (sector == hdf[unit].sectors) {
            sector = 1;
            head++;
            if (head == hdf[unit].heads) {
              head = 0;
              cylinder++;
            }
          } else {
            sector++;
          }
        }
        sector_count--; // decrease sector count
      }

      block_count-=block_size;
    }

    if (hdf[unit].type & HDF_FILE)
      f_sync(&hdf[unit].idxfile->file);

    if (lbamode) {
      sector = lba & 0xff;
      cylinder = lba >> 8;
      head = lba >> 24;
    }

    WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

    if (sector_count)
        WriteStatus(IDE_STATUS_IRQ);
    else
        WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
  }
}


// HandleHDD()
void HandleHDD(unsigned char c1, unsigned char c2, unsigned char cs1ena)
{
  unsigned char  tfr[8];
  unsigned short i;
  unsigned short sector;
  unsigned short cylinder;
  unsigned char  head;
  unsigned char  unit;
  unsigned short sector_count;
  unsigned char  lbamode;
  unsigned char  cs1 = 0;

  if (c1 & CMD_IDECMD) {
    DISKLED_ON;
    EnableFpga();
    SPI(CMD_IDE_REGS_RD); // read task file registers
    SPI(0x00);
    SPI(0x00);
    SPI(0x00);
    SPI(0x00);
    SPI(0x00);
    for (i = 0; i < 8; i++) {
      tfr[i] = SPI(0);
      if (i == 6 && cs1ena) cs1 = tfr[i] & 0x01;
      tfr[i] = SPI(0);
    }
    DisableFpga();
    unit = (cs1 << 1) | ((tfr[6] & 0x10) >> 4); // primary/secondary/master/slave selection
    if (0) hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);

    if (!hardfile[unit]->present) {
      hdd_debugf("IDE%d: not present", unit);
      WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
      DISKLED_OFF;
      return;
    }
    sector = tfr[3];
    cylinder = tfr[4] | (tfr[5] << 8);
    head = tfr[6] & 0x0F;
    lbamode = tfr[6] & 0x40;
    sector_count = tfr[2];
    if (sector_count == 0) sector_count = 0x100;

    if ((tfr[7] & 0xF0) == ACMD_RECALIBRATE) {
      ATA_Recalibrate(tfr,  unit);
    } else if (tfr[7] == ACMD_DIAGNOSTIC) {
      ATA_Diagnostic(tfr);
    } else if (tfr[7] == ACMD_IDENTIFY_DEVICE) {
      ATA_IdentifyDevice(tfr, unit);
    } else if (tfr[7] == ACMD_INITIALIZE_DEVICE_PARAMETERS) {
      ATA_Initialize(tfr, unit);
    } else if (tfr[7] == ACMD_SET_MULTIPLE_MODE) {
      ATA_SetMultipleMode(tfr, unit);
    } else if (tfr[7] == ACMD_READ_SECTORS) {
      ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count, 0, lbamode);
    } else if (tfr[7] == ACMD_READ_MULTIPLE) {
      ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count, 1, lbamode);
    } else if (tfr[7] == ACMD_WRITE_SECTORS) {
      ATA_WriteSectors(tfr, sector, cylinder, head, unit, sector_count ,0, lbamode);
    } else if (tfr[7] == ACMD_WRITE_MULTIPLE) {
      ATA_WriteSectors(tfr, sector, cylinder, head, unit, sector_count, 1, lbamode);
    } else {
      hdd_debugf("Unknown ATA command");
      hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
      WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
      WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
    }
    DISKLED_OFF;
  }
}


// GetHardfileGeometry()
// this function comes from WinUAE, should return the same CHS as WinUAE
void GetHardfileGeometry(hdfTYPE *pHDF)
{
  unsigned long total=0;
  unsigned long i, head, cyl, spt;
  unsigned long sptt[] = { 63, 127, 255, 0 };
  unsigned long cyllimit=65535;

  switch(pHDF->type) {
    case (HDF_FILE | HDF_SYNTHRDB):
      if (f_size(&pHDF->idxfile->file) == 0) return;
      // For WinUAE generated hardfiles we have a fixed sectorspertrack of 32, number of heads and cylinders are variable.
      // Make a first guess based on 1 head, then refine that guess until the geometry gives a plausible number of
      // cylinders and also has the correct number of blocks.
      total = f_size(&pHDF->idxfile->file) / 512;
      pHDF->sectors = 32;
      head=1;
      cyl = total/32;
      cyllimit-=1; // Need headroom for an RDB
      while(head<16 && (cyl>cyllimit || (head*cyl*32)!=total))
      {
        ++head;
        cyl=total/(32*head);
      }
      pHDF->heads = head;
      pHDF->cylinders = cyl+1;	// Add a cylinder for the fake RDB.

      if ((head*cyl*32)==total)	// Does the geometry match the size of the underlying hard file?
        return;
      // If not, fall back to regular hardfile geometry aproximations...
      break;
    case HDF_FILE:
      if (f_size(&pHDF->idxfile->file) == 0) return;
      total = f_size(&pHDF->idxfile->file) / 512;
      break;
    case HDF_CARD:
      total = MMC_GetCapacity();  // GetCapacity returns number of blocks, not bytes.
      break;
    case HDF_CARDPART0:
    case HDF_CARDPART1:
    case HDF_CARDPART2:
    case HDF_CARDPART3:
      total = partitions[pHDF->partition].sectors;
      break;
    default:
      break;
  }

  for (i = 0; sptt[i] != 0; i++) {
    spt = sptt[i];
    for (head = 4; head <= 16; head++) {
      cyl = total / (head * spt);
      if (total <= 1024 * 1024) {
        if (cyl <= 1023) break;
      } else {
        if (cyl < 16383)
            break;
        if (cyl < 32767 && head >= 5)
            break;
        if (cyl <= cyllimit)  // Should there some head constraint here?
            break;
      }
    }
    if (head <= 16) break;
  }
  if(pHDF->type == (HDF_FILE | HDF_SYNTHRDB))
  ++cyl;	// Add an extra cylinder for the fake RDB
  pHDF->cylinders = (unsigned short)cyl;
  pHDF->heads = (unsigned short)head;
  pHDF->sectors = (unsigned short)spt;
}



// OpenHardfile()
unsigned char OpenHardfile(unsigned char unit)
{
  hdf[unit].idxfile = &sd_image[unit];

  switch(hardfile[unit]->enabled) {
    case HDF_FILE | HDF_SYNTHRDB:
    case HDF_FILE:
      hdf[unit].type=hardfile[unit]->enabled;
        if (IDXOpen(hdf[unit].idxfile, hardfile[unit]->name, FA_READ | FA_WRITE) == FR_OK) {
          IDXIndex(hdf[unit].idxfile);
          GetHardfileGeometry(&hdf[unit]);
          hdd_debugf("HARDFILE %d:", unit);
          hdd_debugf("file: \"%s\"", hardfile[unit]->name);
          hdd_debugf("size: %llu (%lu MB)", f_size(&hdf[unit].idxfile->file), f_size(&hdf[unit].idxfile->file) >> 20);
          hdd_debugf("CHS: %u.%u.%u", hdf[unit].cylinders, hdf[unit].heads, hdf[unit].sectors);
          hdd_debugf(" (%lu MB)", ((((unsigned long) hdf[unit].cylinders) * hdf[unit].heads * hdf[unit].sectors) >> 11));
          if (hardfile[unit]->enabled & HDF_SYNTHRDB) {
            hdf[unit].offset=-(hdf[unit].heads*hdf[unit].sectors);
          } else {
            hdf[unit].offset=0;
          }
          hardfile[unit]->present = 1;
          return 1;
        }
      break;
    case HDF_CARD:
      hdf[unit].type=HDF_CARD;
      hardfile[unit]->present = 1;
      hdf[unit].offset=0;
      GetHardfileGeometry(&hdf[unit]);
      return 1;
      break;
    case HDF_CARDPART0:
    case HDF_CARDPART1:
    case HDF_CARDPART2:
    case HDF_CARDPART3:
      hdf[unit].type=hardfile[unit]->enabled;
      hdf[unit].partition=hdf[unit].type-HDF_CARDPART0;
      hardfile[unit]->present = 1;
      hdf[unit].offset=partitions[hdf[unit].partition].startlba;
      GetHardfileGeometry(&hdf[unit]);
      return 1;
      break;
  }
  hardfile[unit]->present = 0;
  return 0;
}


// GetHDFFileType()
unsigned char GetHDFFileType(const char *filename)
{
  FIL rdbfile;
  unsigned char res = HDF_FILETYPE_NOTFOUND;

  if (f_open(&rdbfile,filename, FA_READ) == FR_OK) {
    res = HDF_FILETYPE_UNKNOWN;
    int i;
    for(i=0;i<16;++i) {
      if (FileReadBlock(&rdbfile,sector_buffer) != FR_OK) break;
      if (sector_buffer[0]=='R' && sector_buffer[1]=='D' && sector_buffer[2]=='S' && sector_buffer[3]=='K') {
        res = HDF_FILETYPE_RDB;
        break;
      }
      if ((sector_buffer[0]=='D' && sector_buffer[1]=='O' && sector_buffer[2]=='S') ||
          (sector_buffer[0]=='P' && sector_buffer[1]=='F' && sector_buffer[2]=='S') ||
          (sector_buffer[0]=='S' && sector_buffer[1]=='F' && sector_buffer[2]=='S')) {
        res = HDF_FILETYPE_DOS;
        break;
      }
    }
  }
  f_close(&rdbfile);
  return(res);
}
