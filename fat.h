#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include "spi.h"

#define MAXDIRENTRIES 8
// Sector buffer size for 4 consecutive sectors
#define SECTOR_BUFFER_SIZE 4

typedef struct
{
    uint32_t sector;
    uint32_t index;
} entryTYPE;

typedef struct
{
    char name[11];                 /* name of file */
    unsigned char attributes;      /* file attributes */
    entryTYPE     entry;           /* file entry location */
    uint32_t      sector;          /* sector index in file */
    uint32_t      size;            /* file size */
    uint32_t      cluster;         /* current cluster */
    uint32_t      start_cluster;   /* first cluster of file */
    unsigned char device;          /* device index (0=sd, 1=usb) */
    int32_t       cluster_change;  /* counter to keep track of file length changes */
    char          long_name[261];
}  fileTYPE;

struct PartitionEntry
{
	unsigned char geometry[8];		// ignored
	uint32_t      startlba;
	uint32_t      sectors;
} __attribute__ ((packed));

struct MasterBootRecord
{
	unsigned char bootcode[446];	// ignored
	struct PartitionEntry Partition[4];	// We copy these (and byteswap if need be)
	unsigned short Signature;		// This lets us detect an MBR (and the need for byteswapping).
} __attribute__ ((packed));

extern struct PartitionEntry partitions[4];	// FirstBlock and LastBlock will be byteswapped as necessary
extern int partitioncount;

typedef struct
{
    unsigned char       Name[8];            /* filename, blank filled */
#define SLOT_EMPTY      0x00                /* slot has never been used */
#define SLOT_E5         0x05                /* the real value is 0xe5 */
#define SLOT_DELETED    0xe5                /* file in this slot deleted */
    unsigned char       Extension[3];       /* extension, blank filled */
    unsigned char       Attributes;         /* file attributes */
#define ATTR_NORMAL     0x00                /* normal file */
#define ATTR_READONLY   0x01                /* file is readonly */
#define ATTR_HIDDEN     0x02                /* file is hidden */
#define ATTR_SYSTEM     0x04                /* file is a system file */
#define ATTR_VOLUME     0x08                /* entry is a volume label */
#define ATTR_DIRECTORY  0x10                /* entry is a directory name */
#define ATTR_ARCHIVE    0x20                /* file is new or modified */
#define ATTR_LFN        0x0F                /* long file name entry */
    unsigned char       LowerCase;          /* NT VFAT lower case flags */
#define LCASE_BASE      0x08                /* filename base in lower case */
#define LCASE_EXT       0x10                /* filename extension in lower case */
    unsigned char       CreateHundredth;    /* hundredth of seconds in CTime */
    unsigned short      CreateTime;         /* create time */
    unsigned short      CreateDate;         /* create date */
    unsigned short      AccessDate;         /* access date */
    unsigned short      HighCluster;        /* high bytes of cluster number */
    unsigned short      ModifyTime;         /* last update time */
    unsigned short      ModifyDate;         /* last update date */
    unsigned short      StartCluster;       /* starting cluster of file */
    uint32_t            FileSize;           /* size of file in bytes */
}  __attribute__ ((packed)) DIRENTRY;

typedef union {
    unsigned short fat16[256];
    uint32_t  fat32[128];
} FATBUFFER;

struct InfoSector {
  uint32_t       magic;		/* Magic for info sector ('RRaA') */
  unsigned char  junk[0x1dc];
  uint32_t       reserved1;	/* Nothing as far as I can tell */
  uint32_t       signature;	/* 0x61417272 ('rrAa') */
  uint32_t       free_clusters;	/* Free cluster count.  -1 if unknown */
  uint32_t       next_cluster;	/* Most recently allocated cluster. */
  uint32_t       reserved2[3];
  unsigned short reserved3;
  unsigned short boot_sign;
} __attribute__ ((packed));

#define FILETIME(h,m,s) (((h<<11)&0xF800)|((m<<5)&0x7E0)|((s/2)&0x1F))
#define FILEDATE(y,m,d) ((((y-1980)<<9)&0xFE00)|((m<<5)&0x1E0)|(d&0x1F))

// global sector buffer, data for read/write actions is stored here.
// BEWARE, this buffer is also used and thus trashed by all other functions
extern unsigned char sector_buffer[512 * SECTOR_BUFFER_SIZE]; // sector buffer
extern unsigned char cluster_size;
extern uint32_t cluster_mask;
extern unsigned char fat32;

// constants
#define DIRECTORY_ROOT 0

// file seeking
#define SEEK_SET  0
#define SEEK_CUR  1

// scanning flags
#define SCAN_INIT  0       // start search from beginning of directory
#define SCAN_NEXT  1       // find next file in directory
#define SCAN_PREV -1       // find previous file in directory
#define SCAN_NEXT_PAGE   2 // find next 8 files in directory
#define SCAN_PREV_PAGE  -2 // find previous 8 files in directory
#define SCAN_INIT_FIRST  3 // search for an entry with given cluster number
#define SCAN_INIT_NEXT   4 // search for entries higher than the first one

// options flags
#define SCAN_DIR     1 // include subdirectories
#define SCAN_LFN     2 // include long file names
#define FIND_DIR     4 // find first directory beginning with given character
#define FIND_FILE    8 // find first file entry beginning with given character
#define SCAN_SYSDIR 16 // include subdirectories with system attribute


// functions
unsigned char FindDrive(void);
uint32_t      NextCluster(uint32_t cluster);
unsigned char FileNextSector(fileTYPE *file) RAMFUNC;
unsigned char FileNextSectorExpand(fileTYPE *file);
unsigned char FileOpen(fileTYPE *file, const char *name);
unsigned char FileOpenDir(fileTYPE *file, const char *name, unsigned long dirCluster);
unsigned char FileSeek(fileTYPE *file, unsigned long offset, unsigned long origin);
unsigned char FileRead(fileTYPE *file, unsigned char *pBuffer) RAMFUNC;
unsigned char FileReadEx(fileTYPE *file, unsigned char *pBuffer, unsigned long nSize);
unsigned char FileWrite(fileTYPE *file, unsigned char *pBuffer);
unsigned char FileWriteEnd(fileTYPE *file);

unsigned char FileCreate(unsigned long iDirectory, fileTYPE *file);
unsigned char UpdateEntry(fileTYPE *file);

char ScanDirectory(unsigned long mode, char *extension, unsigned char options);
void ChangeDirectory(unsigned long iStartCluster);

void fat_switch_to_usb(void);
int8_t fat_medium_present(void);
int8_t fat_uses_mmc(void);

unsigned char FileNew(fileTYPE *file, char *name, int size);

#endif

