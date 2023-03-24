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
#include "utils.h"
#include "errors.h"
#include "hardware.h"
#include "fat_compat.h"
#include "FatFs/diskio.h"
#include "hdd.h"
#include "hdd_internal.h"
#include "menu.h"
#include "fpga.h"
#include "scsi.h"
#include "cue_parser.h"
#include "debug.h"

hardfileTYPE  *hardfile[HARDFILES];

// hardfile structure
hdfTYPE hdf[HARDFILES];

#define AUDIO_PLAYING  0x11
#define AUDIO_PAUSED   0x12
#define AUDIO_COMPLETE 0x13
#define AUDIO_ERROR    0x14
#define AUDIO_NOSTAT   0x15

typedef struct
{
  unsigned char key;
  unsigned char asc;
  unsigned char ascq;
  unsigned short blocksize;
  unsigned int currentlba;
  unsigned int endlba;
  unsigned char audiostatus;
} cdrom_t;

static cdrom_t cdrom;

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

// IdentifiyDevice()
// builds Identify Packet Device struct
static void IdentifyPacketDevice(unsigned short *pBuffer, unsigned char unit)
{
  memset(pBuffer, 0, 512);
  pBuffer[0] = 0x8580;
  memcpy((char*)&pBuffer[10], "iMTSiMiniMCgRDMO    ", 20); // serial number - byte swapped
  memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
  memcpy((char*)&pBuffer[27], "                                        ", 40); // Model number - byte swapped
  pBuffer[49] = 0x0100;
  pBuffer[53] = 1;
  pBuffer[82] = 0x4214;  // command set supported (NOP, DEVICE RESET, PACKET, Removable media)
  pBuffer[126] = 0xfffe; // byte count = 0 behavior
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

static void WritePacket(unsigned char unit, const unsigned char *buf, unsigned short bufsize, unsigned short bytelimit, char lastpacket)
{
  unsigned short bytes;
  do {
    bytes = MIN(bufsize, bytelimit);
    while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer
    WriteTaskFile(0, 0x02, 0, bytes & 0xff, (bytes>>8) & 0xff, 0xa0 | ((unit & 0x01)<<4));
    if (bytes) {
      EnableFpga();
      SPI(CMD_IDE_DATA_WR); // write data command
      SPI(0x00);
      SPI(0x00);
      SPI(0x00);
      SPI(0x00);
      SPI(0x00);
      spi_write(buf, bytes);
      DisableFpga();
    }
    buf += bytes;
    bufsize -= bytes;
    if (lastpacket && !bufsize)
      WriteStatus(IDE_STATUS_IRQ | IDE_STATUS_END);
    else
      WriteStatus(IDE_STATUS_IRQ);
  } while (bufsize);
}

static void cdrom_reset()
{
  cdrom.key = cdrom.asc = cdrom.ascq = 0;
  cdrom.currentlba = 0;
  cdrom.audiostatus = AUDIO_NOSTAT;
  cdrom.blocksize = 2048;
}

static void cdrom_setsense(unsigned char key, unsigned char asc, unsigned char ascq)
{
  cdrom.key = key;
  cdrom.asc = asc;
  cdrom.ascq = ascq;
}

static void cdrom_ok()
{
  cdrom.key = cdrom.asc = cdrom.ascq = 0;
}

static void cdrom_send_error(unsigned char unit)
{
  WriteTaskFile((cdrom.key << 4) | 0x04, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
  WriteStatus(IDE_STATUS_END | IDE_STATUS_ERR | IDE_STATUS_IRQ);
}

static void cdrom_generate_header(unsigned char *pBuffer, unsigned int lba)
{
  // TODO: implement
}

static void cdrom_generate_ecc(unsigned char *pBuffer, unsigned int lba)
{
  // TODO: implement
}

static void cdrom_playaudio()
{
  UINT br;
  unsigned char track = cue_gettrackbylba(cdrom.currentlba);
  if ((toc.tracks[track].type != SECTOR_AUDIO) || (toc.tracks[track].sector_size != 2352)) {
    cdrom.audiostatus = AUDIO_ERROR;
    return;
  }
  DISKLED_ON
  int offset = (cdrom.currentlba - toc.tracks[track].start) * toc.tracks[track].sector_size + toc.tracks[track].offset;
  f_lseek(&toc.file->file, offset);
  f_read(&toc.file->file, sector_buffer, 2352, &br);
  EnableFpga();
  SPI(CMD_IDE_CDDA_WR); // write cdda command
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  spi_write(sector_buffer, 2352);
  DisableFpga();
  DISKLED_OFF
  if (cdrom.currentlba == cdrom.endlba)
    cdrom.audiostatus = AUDIO_COMPLETE;
  else
    cdrom.currentlba++;
}

static void PKT_Read(unsigned char unit, unsigned int lba, unsigned int len, unsigned short bytelimit, unsigned short blocksize)
{
  UINT br;
  unsigned char *pBuffer;
  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }
  cdrom.audiostatus = AUDIO_NOSTAT;
  cdrom_ok();
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type

  while (len--) {
    unsigned char track = cue_gettrackbylba(lba);
    int offset = (lba - toc.tracks[track].start) * toc.tracks[track].sector_size + toc.tracks[track].offset;

    if ((blocksize == 2048 && toc.tracks[track].type != SECTOR_DATA_MODE1 && toc.tracks[track].type != SECTOR_DATA_MODE2) ||
        (blocksize != 2048 && blocksize !=2352) ||
        (toc.tracks[track].sector_size != 2048 && toc.tracks[track].sector_size != 2352 && toc.tracks[track].sector_size != 2336)) {
      cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x26, 2);
      cdrom_send_error(unit);
      return;
    }
    pBuffer = sector_buffer;
    cdrom.currentlba = lba;
    if (blocksize == 2048 && toc.tracks[track].sector_size == 2352) offset+=16;
    if (blocksize == 2048 && toc.tracks[track].sector_size >= 2336 && toc.tracks[track].type == SECTOR_DATA_MODE2) offset+=8; // CD-XA with 8 subheader bytes
    if (blocksize == 2352 && (toc.tracks[track].sector_size == 2048 || toc.tracks[track].sector_size == 2336)) {
       cdrom_generate_header(pBuffer, lba);
       pBuffer+=16;
    }
    hdd_debugf("lba: %d track: %d, offset: %d, blocksize: %d sector_size: %d", lba, track, offset, blocksize, toc.tracks[track].sector_size);
    f_lseek(&toc.file->file, offset);
    f_read(&toc.file->file, pBuffer, MIN(toc.tracks[track].sector_size, blocksize), &br);
    if (blocksize == 2352 && toc.tracks[track].sector_size == 2048) {
       cdrom_generate_ecc(sector_buffer, lba);
    }

    lba++;
    WritePacket(unit, sector_buffer, blocksize, bytelimit, !len);
  }
}

static void PKT_Read12(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_Read12 (bytelimit=%d)", unit, bytelimit);
  unsigned int lba = cmd[5] | (cmd[4] << 8) | (cmd[3] << 16) | (cmd[2] << 24);
  unsigned int len = cmd[9] | (cmd[8] << 8) | (cmd[7] << 16) | (cmd[6] << 24);
  cdrom.audiostatus = AUDIO_NOSTAT;
  PKT_Read(unit, lba, len, bytelimit, cdrom.blocksize);
}

static void PKT_Read10(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_Read10 (bytelimit=%d)", unit, bytelimit);
  unsigned int lba = cmd[5] | (cmd[4] << 8) | (cmd[3] << 16) | (cmd[2] << 24);
  unsigned int len = cmd[8] | (cmd[7] << 8);
  cdrom.audiostatus = AUDIO_NOSTAT;
  PKT_Read(unit, lba, len, bytelimit, cdrom.blocksize);
}

static void PKT_DoReadCD(unsigned char *cmd, unsigned char unit, unsigned short bytelimit, unsigned int lba, unsigned int len)
{
  unsigned char errorfield = (cmd[9] & 0x06) >> 1;
  unsigned char edcecc = (cmd[9] & 0x08) >> 3;
  unsigned char userdata = (cmd[9] & 0x10) >> 4;
  unsigned char header = (cmd[9] & 0x60) >> 5;
  unsigned char sync = (cmd[9] & 0x80) >> 7;
  // FIXME: userdata only or full sector (without subchannels) are allowed
  if (!userdata || ((edcecc || header || sync) != (edcecc && header && sync))) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x24, 0);
    cdrom_send_error(unit);
    return;
  }
  unsigned char track = cue_gettrackbylba(lba);
  unsigned short blocksize = 2048;
  if (toc.tracks[track].type == SECTOR_AUDIO || (edcecc && header && sync))
    blocksize = 2352;
  hdd_debugf("ReadCD: lba=%d len=%d blocksize=%d", lba, len, blocksize);
  PKT_Read(unit, lba, len, bytelimit, blocksize);
}

static void PKT_ReadCD(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ReadCD (bytelimit=%d)", unit, bytelimit);
  unsigned int lba = cmd[5] | (cmd[4] << 8) | (cmd[3] << 16) | (cmd[2] << 24);
  unsigned int len = cmd[8] | (cmd[7] << 8) | (cmd[6] << 16);
  cdrom.audiostatus = AUDIO_NOSTAT;
  PKT_DoReadCD(cmd, unit, bytelimit, lba, len);
}

static void PKT_ReadCDMSF(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ReadCDMSF (bytelimit=%d)", unit, bytelimit);
  unsigned int start = MSF2LBA(cmd[3], cmd[4], cmd[5]);
  unsigned int end = MSF2LBA(cmd[6], cmd[7], cmd[8]);
  cdrom.audiostatus = AUDIO_NOSTAT;
  if (start > end) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x24, 0);
    cdrom_send_error(unit);
    return;
  }
  PKT_DoReadCD(cmd, unit, bytelimit, start, end-start);
}

static void PKT_PlayAudio(unsigned char unit, unsigned int start, unsigned int end)
{
  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }
  if (start > toc.end || end > toc.end || start > end) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x21, 0);
    cdrom_send_error(unit);
    return;
  }
  unsigned char track = cue_gettrackbylba(start);
  if (toc.tracks[track].type != SECTOR_AUDIO) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x64, 0);
    cdrom_send_error(unit);
    return;
  }
  cdrom.currentlba = start;
  cdrom.endlba = end;
  cdrom.audiostatus = AUDIO_PLAYING;
  cdrom_ok();
  WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void PKT_PlayAudioMSF(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_PlayAudioMSF", unit);
  unsigned int start = (cmd[2] == 0xff && cmd[3] == 0xff && cmd[5] == 0xff) ? cdrom.currentlba : MSF2LBA(cmd[3], cmd[4], cmd[5]);
  unsigned int end = MSF2LBA(cmd[6], cmd[7], cmd[8]);
  cdrom.audiostatus = AUDIO_NOSTAT;
  PKT_PlayAudio(unit, start, end);
}

static void PKT_PlayAudio10(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_PlayAudio10", unit);
  unsigned int start = (cmd[2] << 24) | (cmd[3] << 16) | (cmd[4] << 8) | cmd[5];
  if (start == 0xffffffff) start = cdrom.currentlba;
  unsigned int end = start + (cmd[7] << 8) + cmd[8];
  cdrom.audiostatus = AUDIO_NOSTAT;
  PKT_PlayAudio(unit, start, end);
}

// obsolete, but used by Napalm
static void PKT_PlayAudioTrackIndex(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_PlayAudioTrackIndex", unit);
  unsigned char starttrack = cmd[4];
  unsigned char endtrack = cmd[7];
  cdrom.audiostatus = AUDIO_NOSTAT;
  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }
  if (starttrack > endtrack || starttrack >= toc.last || !starttrack || !endtrack || endtrack >= toc.last) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x21, 0);
    cdrom_send_error(unit);
    return;
  }
  PKT_PlayAudio(unit, toc.tracks[starttrack-1].start, toc.tracks[endtrack-1].end);
}

static void PKT_PauseResume(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_PauseResume", unit);
  int resume = cmd[8] & 0x01;

  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }

  if ((resume && cdrom.audiostatus != AUDIO_PAUSED) || (!resume && cdrom.audiostatus != AUDIO_PLAYING)) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x2c, 0);
    cdrom_send_error(unit);
  } else {
    cdrom.audiostatus = resume ? AUDIO_PLAYING : AUDIO_PAUSED;
    cdrom_ok();
    WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
  }
}

static void PKT_StopPlayScan(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_StopPlayScan", unit);
  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }
  cdrom.audiostatus = AUDIO_NOSTAT;
  cdrom_ok();
  WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void PKT_SubChannel(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_SubChannel (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = (cmd[7] << 8) | cmd[8];
  unsigned char track;
  unsigned char msftime = cmd[1] & 0x02;
  unsigned short respsize = 0;

  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }

  track = cue_gettrackbylba(cdrom.currentlba);
  if (track >= toc.last) track = toc.last - 1;
  memset(sector_buffer, 0, 24);
  sector_buffer[1] = cdrom.audiostatus;
  switch (cmd[3]) {
    case 1:
      // current position
      respsize = (cmd[2] & 0x40) ? 16 : 4;
      sector_buffer[4] = 0x01;
      sector_buffer[5] = (1 << 4) | (toc.tracks[track-1].type == SECTOR_AUDIO ? 0 : 4);
      sector_buffer[6] = track + 1;
      sector_buffer[7] = cdrom.currentlba >= toc.tracks[track].start ? 1 : 0; // TODO: support more than 1 indices
      unsigned int rellba = cdrom.currentlba - toc.tracks[track].start;
      if (msftime) {
        msf_t MSF;
        LBA2MSF(cdrom.currentlba+150, &MSF);
        sector_buffer[9] = MSF.m;
        sector_buffer[10] = MSF.s;
        sector_buffer[11] = MSF.f;
        LBA2MSF(rellba, &MSF);
        sector_buffer[13] = MSF.m;
        sector_buffer[14] = MSF.s;
        sector_buffer[15] = MSF.f;
      } else {
        sector_buffer[8] = (cdrom.currentlba >> 24) & 0xff;
        sector_buffer[9] = (cdrom.currentlba >> 16) & 0xff;
        sector_buffer[10] = (cdrom.currentlba >> 8) & 0xff;
        sector_buffer[11] = cdrom.currentlba & 0xff;
        sector_buffer[12] = (rellba >> 24) & 0xff;
        sector_buffer[13] = (rellba >> 16) & 0xff;
        sector_buffer[14] = (rellba >> 8) & 0xff;
        sector_buffer[15] = rellba & 0xff;
      }
      break;
    case 2:
      // Media Catalog number
      respsize = 24;
      sector_buffer[4] = 0x02;
      break;
    case 3:
      // ISRC
      respsize = 24;
      sector_buffer[4] = 0x02;
      break;
    default:
      // reserved
      break;
  }
  sector_buffer[3] = respsize - 4;
  cdrom_ok();
  bufsize = MIN(bufsize, respsize);
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type
  WritePacket(unit, sector_buffer, bufsize, bytelimit, 1);
}

static void PKT_ReadCapacity(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ReadCapacity (bytelimit=%d)", unit, bytelimit);
  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }

  sector_buffer[0] = (toc.end >> 24) & 0xff;
  sector_buffer[1] = (toc.end >> 16) & 0xff;
  sector_buffer[2] = (toc.end >> 8) & 0xff;
  sector_buffer[3] = toc.end & 0xff;
  sector_buffer[4] = sector_buffer[5] = 0;
  sector_buffer[6] = (2048 >> 8) & 0xff;
  sector_buffer[7] = 2048 & 0xff;
  //hexdump(sector_buffer, 8, 0);
  cdrom_ok();
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type
  WritePacket(unit, sector_buffer, 8, bytelimit, 1);
}

static void PKT_ReadTOC(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ReadTOC (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = (cmd[7] << 8) | cmd[8];
  unsigned char track = cmd[6];
  unsigned char msftime = cmd[1] & 0x02;
  unsigned short tocsize = 4;
  unsigned char *p = sector_buffer;
  int lba;

  if (!toc.valid) {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
    return;
  }
  switch(cmd[2] & 0x0f) {
    case 0:
      hdd_debugf("TOC format 0");
      if (track > toc.last && track != 0xAA) {
        cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x26, 2);
        cdrom_send_error(unit);
        return;
      }
      sector_buffer[2] = 1;
      sector_buffer[3] = toc.last;
      p += 4;
      for (int i=1; i<=toc.last; i++) {
        if (i>=track) {
          p[0] = p[3] =  0;
          p[1] = (1 << 4) | (toc.tracks[i-1].type == SECTOR_AUDIO ? 0 : 4);
          p[2] = i;
          lba = toc.tracks[i-1].start;
          hdd_debugf("track %d lba: %d", i, lba);
          if (msftime) {
            msf_t MSF;
            LBA2MSF(lba+150, &MSF);
            p[4] = 0;
            p[5] = MSF.m;
            p[6] = MSF.s;
            p[7] = MSF.f;
          } else {
            p[4] = (lba >> 24) & 0xff;
            p[5] = (lba >> 16) & 0xff;
            p[6] = (lba >> 8) & 0xff;
            p[7] = lba & 0xff;
          }
          p+=8;
          tocsize+=8;
        }
      }

      p[0] = p[3] = 0;
      p[1] = 0x14;
      p[2] = 0xAA;
      lba = toc.end;
      if (msftime) {
        msf_t MSF;
        LBA2MSF(lba+150, &MSF);
        p[4] = 0;
        p[5] = MSF.m;
        p[6] = MSF.s;
        p[7] = MSF.f;
      } else {
        p[4] = (lba >> 24) & 0xff;
        p[5] = (lba >> 16) & 0xff;
        p[6] = (lba >> 8) & 0xff;
        p[7] = lba & 0xff;
      }
      tocsize += 8;
      break;
    case 1:
      hdd_debugf("TOC format 1");
      tocsize = 12;
      memset(sector_buffer, 0, tocsize);
      sector_buffer[2] = sector_buffer[3] = 1; // first/last session numbers
      sector_buffer[5] = (1 << 4) | (toc.tracks[0].type == SECTOR_AUDIO ? 0 : 4);
      sector_buffer[6] = 1;
      lba = toc.tracks[0].start;
      if (msftime) {
          msf_t MSF;
          LBA2MSF(lba+150, &MSF);
          sector_buffer[9] = MSF.m;
          sector_buffer[10] = MSF.s;
          sector_buffer[11] = MSF.f;
        } else {
          sector_buffer[8] = (lba >> 24) & 0xff;
          sector_buffer[9] = (lba >> 16) & 0xff;
          sector_buffer[10] = (lba >> 8) & 0xff;
          sector_buffer[11] = lba & 0xff;
        }
      break;
    default:
      cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x26, 1);
      cdrom_send_error(unit);
      return;
  }
  sector_buffer[0] = ((tocsize-2)>>8) & 0xff;
  sector_buffer[1] = (tocsize-2) & 0xff;

  cdrom_ok();
  bufsize = MIN(bufsize, tocsize);
  //hexdump(sector_buffer, bufsize, 0);
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type
  WritePacket(unit, sector_buffer, bufsize, bytelimit, 1);
}

static void PKT_ModeSelect(unsigned char unit, unsigned short bytelimit, char sel10)
{
  bytelimit = MIN(bytelimit, SECTOR_BUFFER_SIZE);
  WriteTaskFile(0, 0, 0, bytelimit & 0xff, (bytelimit>>8) & 0xff, 0xa0 | ((unit & 0x01)<<4));
  WriteStatus(IDE_STATUS_REQ | IDE_STATUS_PKT | IDE_STATUS_IRQ); // wait for parameter list
  unsigned long to = GetTimer(100);
  while (!(GetFPGAStatus() & CMD_IDEDAT)) { // wait for full write buffer
    if (CheckTimer(to)) {
      cdrom_setsense(SENSEKEY_NOT_READY, 0x04, 0);
      cdrom_send_error(unit);
      return;
    }
  }
  EnableFpga();
  SPI(CMD_IDE_DATA_RD); // read parameter list
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  for (int i=0; i<bytelimit; i++) {
    sector_buffer[i] = SPI(0xFF);
  }
  DisableFpga();

  //hexdump(sector_buffer, bytelimit, 0);
  unsigned short dlen = sel10 ? (sector_buffer[1] | (sector_buffer[0] << 8)) : sector_buffer[0];
  unsigned short blen = sel10 ? (sector_buffer[7] | (sector_buffer[6] << 8)) : sector_buffer[3];
  unsigned char *pBuffer = sel10 ? sector_buffer + 8 : sector_buffer + 4;
  if ((sel10 && bytelimit < 8) || (!sel10 && bytelimit < 4)) {
    cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x1A, 0);
    cdrom_send_error(unit);
    return;
  }
  bytelimit -= sel10 ? 8 : 4;
  // parse block descriptor(s)
  // disabled, as ATAPI compliance doesn't use block descriptors (MMC-3 B.2.2)
#if 0
  while (blen >= 8) {
    if (bytelimit < 8) {
      cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x1A, 0);
      cdrom_send_error(unit);
      return;
    }
    cdrom.blocksize = pBuffer[7] | (pBuffer[6] << 8);
    iprintf("CDROM blocksize changed to: %d\n", cdrom.blocksize);
    pBuffer+=8;
    blen-=8;
    bytelimit-=8;
  }
#endif
  // TODO: parse page descriptors
  cdrom_ok();
  WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void PKT_ModeSelect10(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ModeSelect10 (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = MIN(bytelimit, (cmd[7] << 8) | cmd[8]);
  PKT_ModeSelect(unit, bufsize, 1);
}

static void PKT_ModeSelect6(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ModeSelect6 (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = MIN(bytelimit, cmd[4]);
  PKT_ModeSelect(unit, bufsize, 0);
}

static void PKT_ModeSense(unsigned char *cmd, unsigned char unit, unsigned short bytelimit, unsigned short bufsize, unsigned char page)
{
  //TODO: implement
  cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x20, 0);
  cdrom_send_error(unit);
}

static void PKT_ModeSense10(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ModeSense10 (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = (cmd[7] << 8) | cmd[8];
  unsigned char page = cmd[2] & 0x3f;
  PKT_ModeSense(cmd, unit, bytelimit, bufsize, page);
}

static void PKT_ModeSense6(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_ModeSense6 (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = cmd[4];
  unsigned char page = cmd[2] & 0x3f;
  PKT_ModeSense(cmd, unit, bytelimit, bufsize, page);
}

static void PKT_Inquiry(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_Inquiry (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = (cmd[3] << 8) | cmd[4];
  INQUIRYDATA_t *inq = (INQUIRYDATA_t*)&sector_buffer;

  memset(sector_buffer, 0, 36);
  inq->DeviceType = 0x05; // MMC3
  inq->RemovableMedia = 1; // removable
  inq->Versions = 0x02;
  inq->ResponseDataFormat = 0x02;
  memcpy(inq->VendorId, "MiST    ", 8);
  memcpy(inq->ProductId, "CDROM           ", 16);
  bufsize = MIN(bufsize, 36);
  cdrom_ok();
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type
  WritePacket(unit, sector_buffer, bufsize, bytelimit, 1);
}

static void PKT_TestUnitReady(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_TestUnitReady", unit);
  if (toc.valid) {
    cdrom_ok();
    WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
  } else {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3a, 0);
    cdrom_send_error(unit);
  }
}

static void PKT_RequestSense(unsigned char *cmd, unsigned char unit, unsigned short bytelimit)
{
  hdd_debugf("IDE%d: PKT_RequestSense (bytelimit=%d)", unit, bytelimit);
  unsigned short bufsize = MIN(cmd[4] ? cmd[4] : 4, 16);
  SENSEDATA_t *sense = (SENSEDATA_t*)&sector_buffer;

  memset(sector_buffer, 0, 16);
  sense->ErrorCode = 0x70;
  sense->Valid = 1;
  sense->SenseKey = cdrom.key;
  sense->AdditionalSenseCode = cdrom.asc;
  sense->AdditionalSenseCodeQualifier = cdrom.ascq;
  WriteStatus(IDE_STATUS_RDY | IDE_STATUS_PKT); // pio in (class 1) command type
  WritePacket(unit, sector_buffer, bufsize, bytelimit, 1);
}

static void PKT_StartStopUnit(unsigned char *cmd, unsigned char unit)
{
  hdd_debugf("IDE%d: PKT_StartStopUnit", unit);
  char start = cmd[4] & 0x01;
  cdrom.audiostatus = AUDIO_NOSTAT;

  if ((start && toc.valid) || !start) {
    cdrom_ok();
    WriteTaskFile(0, 0x03, 0, 0, 0, 0xa0 | ((unit & 0x01)<<4));
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
  } else {
    cdrom_setsense(SENSEKEY_NOT_READY, 0x3A, 0);
    cdrom_send_error(unit);
  }
}

// ATA_Packet()
static inline void ATA_Packet(unsigned char *tfr, unsigned char unit, unsigned short bytelimit)
{
  unsigned char cmdpkt[12];
  hdd_debugf("IDE%d: ATA_Packet", unit);
  if (hdf[unit].type != HDF_CDROM) {
    tfr[TFR_ERR]  = 0x02; // command aborted
    WriteTaskFile(tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
    return;
  }
  if (!bytelimit || bytelimit == 0xffff) bytelimit = 0xfffe;
  tfr[TFR_ERR] = 0x00;
  tfr[TFR_SCOUNT] = 0x01; //C/D flag
  WriteTaskFile(tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
  WriteStatus(IDE_STATUS_REQ | IDE_STATUS_PKT); // wait for command packet
  unsigned long to = GetTimer(20);
  while (!(GetFPGAStatus() & CMD_IDEDAT)) { // wait for full write buffer
    if (CheckTimer(to)) {
      cdrom_setsense(SENSEKEY_NOT_READY, 0x04, 0);
      cdrom_send_error(unit);
      return;
    }
  }
  EnableFpga();
  SPI(CMD_IDE_DATA_RD); // read data command
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  for (int i=0; i<12; i++) {
    cmdpkt[i] = SPI(0xFF);
  }
  DisableFpga();
  hdd_debugf("CMD: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             cmdpkt[0], cmdpkt[1], cmdpkt[2], cmdpkt[3], cmdpkt[4], cmdpkt[5],
             cmdpkt[6], cmdpkt[7], cmdpkt[8], cmdpkt[9], cmdpkt[10], cmdpkt[11]);
  switch(cmdpkt[0]) {
    case 0:
      PKT_TestUnitReady(cmdpkt, unit);
      break;
    case 3:
      PKT_RequestSense(cmdpkt, unit, bytelimit);
      break;
    case 0x12:
      PKT_Inquiry(cmdpkt, unit, bytelimit);
      break;
    case 0x1a:
      PKT_ModeSense6(cmdpkt, unit, bytelimit);
      break;
    case 0x5a:
      PKT_ModeSense10(cmdpkt, unit, bytelimit);
      break;
    case 0x15:
      PKT_ModeSelect6(cmdpkt, unit, bytelimit);
      break;
    case 0x55:
      PKT_ModeSelect10(cmdpkt, unit, bytelimit);
      break;
    case 0x43:
      PKT_ReadTOC(cmdpkt, unit, bytelimit);
      break;
    case 0x25:
      PKT_ReadCapacity(cmdpkt, unit, bytelimit);
      break;
    case 0x28:
      PKT_Read10(cmdpkt, unit, bytelimit);
      break;
    case 0xA8:
      PKT_Read12(cmdpkt, unit, bytelimit);
      break;
    case 0xBE:
      PKT_ReadCD(cmdpkt, unit, bytelimit);
      break;
    case 0xB9:
      PKT_ReadCDMSF(cmdpkt, unit, bytelimit);
      break;
    case 0x42:
      PKT_SubChannel(cmdpkt, unit, bytelimit);
      break;
    case 0x45:
      PKT_PlayAudio10(cmdpkt, unit);
      break;
    case 0x47:
      PKT_PlayAudioMSF(cmdpkt, unit);
      break;
    case 0x48:
      PKT_PlayAudioTrackIndex(cmdpkt, unit); // obsolete, but used in some games (Napalm)
      break;
    case 0x4B:
      PKT_PauseResume(cmdpkt, unit);
      break;
    case 0x4E:
      PKT_StopPlayScan(cmdpkt, unit);
      break;
    case 0x1B:
      PKT_StartStopUnit(cmdpkt, unit);
      break;
    default:
      iprintf("HDD%d: Unknown PACKET command: %02x\n", unit, cmdpkt[0]);
      cdrom_setsense(SENSEKEY_ILLEGAL_REQUEST, 0x3a, 0);
      cdrom_send_error(unit);
      break;
  }
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
static void ATA_IdentifyDevice(unsigned char* tfr, unsigned char unit, char packet)
{
  int i;
  unsigned short *id = (unsigned short *)sector_buffer;
  // Identify Device (0xec)
  hdd_debugf("IDE%d: Identify %s Device", unit, packet ? "Packet" : "");
  tfr[TFR_SCOUNT] = 0x01;
  tfr[TFR_SNUM]   = 0x01;
  if (hdf[unit].type == HDF_CDROM) {
    tfr[TFR_ERR]  = 0x02; // command aborted
    tfr[TFR_CYLL] = 0x14;
    tfr[TFR_CYLH] = 0xEB;
    tfr[TFR_SDH]  = 0x00;
  } else {
    tfr[TFR_ERR]  = 0x00;
    tfr[TFR_CYLL] = 0x00;
    tfr[TFR_CYLH] = 0x00;
    tfr[TFR_SDH]  = 0x00;
  }
  if ((!packet && hdf[unit].type == HDF_CDROM) || (packet && hdf[unit].type != HDF_CDROM)) {
    WriteTaskFile(tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
    return;
  }

  if (packet)
    IdentifyPacketDevice(id, unit);
  else
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
  if (tfr[2] > 16) {
    WriteStatus(IDE_STATUS_END | IDE_STATUS_ERR | IDE_STATUS_IRQ);
    return;
  }
  hdf[unit].sectors_per_block = tfr[2];
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static inline void ATA_NOP(unsigned char *tfr, unsigned char unit)
{
  WriteTaskFile(tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static inline void ATA_DeviceReset(unsigned char *tfr, unsigned char unit)
{
  hdd_debugf("Device Reset");
  hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
  tfr[TFR_SCOUNT] = 0x01;
  tfr[TFR_SNUM]   = 0x01;
  tfr[TFR_ERR]    = 0x00;
  //tfr[TFR_SDH]    = 0x00;
  if (hdf[unit].type == HDF_CDROM) {
    tfr[TFR_CYLL] = 0x14;
    tfr[TFR_CYLH] = 0xEB;
  } else {
    tfr[TFR_CYLL] = 0x00;
    tfr[TFR_CYLH] = 0x00;
  }
  WriteTaskFile(tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
  WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

// ATA_ReadSectors()
static inline void ATA_ReadSectors(unsigned char* tfr, unsigned short sector, unsigned short cylinder, unsigned char head, unsigned char unit, unsigned short sector_count, bool multiple, char lbamode, bool verify)
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

    if (!verify) {
      WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
      while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer
    }
    /* Advance CHS address while DRQ is not asserted with the address of last (anticipated) read. */
    int block_count_tmp = block_count;
    while(block_count_tmp--)
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
      long newlba = lba+block_count;
      sector = newlba & 0xff;
      cylinder = newlba >> 8;
      head = newlba >> 24;
    }

    /* Update task file with CHS address */
    WriteTaskFile(0, tfr[2], sector, cylinder, (cylinder >> 8), (tfr[6] & 0xF0) | head);

    // Indicate the start of the transfer
    if (!verify) WriteStatus(IDE_STATUS_IRQ);

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
            if (!memcmp(&rdb->rdb_ID, "RDSK", 4)) {
              hdd_debugf("Adjusting rdb checksum for unit %d", unit);
              rdb->rdb_ChkSum = swab32(swab32(rdb->rdb_ChkSum) + swab32(rdb->rdb_Flags) - 0x12);

              // adjust flags
              rdb->rdb_Flags=swab32(0x12);
            }
          }
          if (!verify) {
            EnableFpga();
            spi8(CMD_IDE_DATA_WR); // write data command
            spi_n(0x00, 5);
            spi_block_write(sector_buffer);
            DisableFpga();
          }
          ++lba;
          --blk;
        }
        if(blk) // Any blocks left?
        {
          HardFileSeek(&hdf[unit], lba + hdf[unit].offset);
#ifndef SD_NO_DIRECT_MODE
          if (fat_uses_mmc() && !verify) {
            FileReadBlockEx(&hdf[unit].idxfile->file, 0, blk); // NULL enables direct transfer to the FPGA
          } else {
#endif
            int blocks = blk;
            while (blocks) {
              FileReadBlockEx(&hdf[unit].idxfile->file, sector_buffer, MIN(blocks, SECTOR_BUFFER_SIZE/512));
              if (!verify) {
                EnableFpga();
                spi8(CMD_IDE_DATA_WR); // write data command
                spi_n(0x00, 5);
                spi_write(sector_buffer, 512*MIN(blocks, SECTOR_BUFFER_SIZE/512));
                DisableFpga();
              }
              blocks-=MIN(blocks, SECTOR_BUFFER_SIZE/512);
            }
#ifndef SD_NO_DIRECT_MODE
          }
#endif
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
#ifndef SD_NO_DIRECT_MODE
        if (fat_uses_mmc() && !verify) {
          disk_read(fs.pdrv, 0, lba+hdf[unit].offset, block_count);
          lba+=block_count;
        } else {
#endif
          int blocks = block_count;
          while (blocks) {
            disk_read(fs.pdrv, sector_buffer, lba+hdf[unit].offset, MIN(blocks, SECTOR_BUFFER_SIZE/512));
            if (!verify) {
              EnableFpga();
              spi8(CMD_IDE_DATA_WR); // write data command
              spi_n(0x00, 5);
              spi_write(sector_buffer, 512*MIN(blocks, SECTOR_BUFFER_SIZE/512));
              DisableFpga();
            }
            blocks-=MIN(blocks, SECTOR_BUFFER_SIZE/512);
            lba+=MIN(blocks, SECTOR_BUFFER_SIZE/512);
          }
#ifndef SD_NO_DIRECT_MODE
        }
#endif
        break;
    }
  }
  if (verify) {
    WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
  } else {
    WriteStatus(IDE_STATUS_END);
  }
}


// ATA_WriteSectors()
static inline void ATA_WriteSectors(unsigned char* tfr, unsigned short sector, unsigned short cylinder, unsigned char head, unsigned char unit, unsigned short sector_count, bool multiple, char lbamode)
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
          disk_write(fs.pdrv, sector_buffer, lba, block_size);
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
      ATA_IdentifyDevice(tfr, unit, 0);
    } else if (tfr[7] == ACMD_IDENTIFY_PACKET_DEVICE) {
      ATA_IdentifyDevice(tfr, unit, 1);
    } else if (tfr[7] == ACMD_INITIALIZE_DEVICE_PARAMETERS) {
      ATA_Initialize(tfr, unit);
    } else if (tfr[7] == ACMD_SET_MULTIPLE_MODE) {
      ATA_SetMultipleMode(tfr, unit);
    } else if (tfr[7] == ACMD_READ_SECTORS) {
      ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count, false, lbamode, false);
    } else if (tfr[7] == ACMD_READ_MULTIPLE) {
      ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count, true, lbamode, false);
    } else if (tfr[7] == ACMD_WRITE_SECTORS) {
      ATA_WriteSectors(tfr, sector, cylinder, head, unit, sector_count ,false, lbamode);
    } else if (tfr[7] == ACMD_WRITE_MULTIPLE) {
      ATA_WriteSectors(tfr, sector, cylinder, head, unit, sector_count, true, lbamode);
    } else if (tfr[7] == ACMD_READ_VERIFY_SECTORS) {
      ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count, false, lbamode, true);
    } else if (tfr[7] == ACMD_PACKET) {
      ATA_Packet(tfr, unit, cylinder);
    } else if (tfr[7] == ACMD_DEVICE_RESET) {
      ATA_DeviceReset(tfr, unit);
    } else if (tfr[7] == ACMD_NOP) {
      ATA_NOP(tfr, unit);
    } else {
      hdd_debugf("Unknown ATA command");
      hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
      WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
      WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
    }
    DISKLED_OFF;
  }

  // CDDA
  if (!toc.valid) cdrom.audiostatus = AUDIO_NOSTAT;
  if (cdrom.audiostatus != AUDIO_PLAYING) return;
  EnableFpga();
  SPI(CMD_IDE_CDDA_RD); // read cdda FIFO status
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  SPI(0x00);
  c1=SPI(0x00);
  DisableFpga();
  if (c1 & 0x01) cdrom_playaudio();
}


// GetHardfileGeometry()
// this function comes from WinUAE, should return the same CHS as WinUAE
static void GetHardfileGeometry(hdfTYPE *pHDF, bool amiga)
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
      // Is hard file size within cyllimit * 32 geometry?
      if (total <= cyllimit * 32) {
        pHDF->heads = 1;
        pHDF->cylinders = (total / 32) + 1;	// Add a cylinder for the fake RDB.
        return;
      }
      // If not, fall back to regular hardfile geometry aproximations...
      break;
    case HDF_FILE:
      if (f_size(&pHDF->idxfile->file) == 0) return;
      total = f_size(&pHDF->idxfile->file) / 512;
      break;
    case HDF_CARD:
      disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &total);
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

  if (amiga) {
    // Amiga (WinUAE) compatible geometry
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
  } else {
    // PC (PCEm) compatible geometry
    if ((total % 17) == 0 && total <= 278528) {
      spt = 17;

      if (total <= 52224)
        head = 4;
      else if ((total % 6) == 0 && total <= 104448)
        head = 6;
      else {
        int c;
        for (c=5;c<16;c++) {
          if((total % c) == 0 && total <= 1024*c*17) break;
          if (c == 5) c++;
        }
        head = c;
      }
      cyl = total / head / 17;
    } else {
      spt = 63;
      head = 16;
      cyl = total / 16 / 63;
    }
  }

  if(pHDF->type == (HDF_FILE | HDF_SYNTHRDB))
  ++cyl;	// Add an extra cylinder for the fake RDB
  pHDF->cylinders = (unsigned short)cyl;
  pHDF->heads = (unsigned short)head;
  pHDF->sectors = (unsigned short)spt;
}



// OpenHardfile()
unsigned char OpenHardfile(unsigned char unit, bool amiga)
{
  hdf[unit].idxfile = &sd_image[unit];

  switch(hardfile[unit]->enabled) {
    case HDF_FILE | HDF_SYNTHRDB:
    case HDF_FILE:
      hdf[unit].type=hardfile[unit]->enabled;
        if (IDXOpen(hdf[unit].idxfile, hardfile[unit]->name, FA_READ | FA_WRITE) == FR_OK) {
          IDXIndex(hdf[unit].idxfile);
          GetHardfileGeometry(&hdf[unit], amiga);
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
      GetHardfileGeometry(&hdf[unit], amiga);
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
      GetHardfileGeometry(&hdf[unit], amiga);
      return 1;
      break;
    case HDF_CDROM:
      hdf[unit].type = HDF_CDROM;
      hardfile[unit]->present = 1;
      cdrom_reset();
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

void SendHDFCfg()
{
  int i;
  unsigned char cfg = 0;
  for (int i=0; i<HARDFILES; i++) {
    if(hardfile[i]->present) cfg |= (1<<i);
  }

  EnableFpga();
  SPI(CMD_IDE_CFG_WR);
  SPI(cfg);
  DisableFpga();
}
