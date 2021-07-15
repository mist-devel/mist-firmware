typedef struct
{
    unsigned long flags;
    unsigned long base;
    unsigned long size;
    unsigned long crc;
} romTYPE;

typedef struct
{
    unsigned char id[8];
    unsigned char version[16];
    romTYPE       rom;
    unsigned long padding[117];
    unsigned long crc;
} UPGRADE;

unsigned long CalculateCRC32(unsigned long crc, unsigned char *pBuffer, unsigned long nSize);
unsigned char CheckFirmware(char *name);
void WriteFirmware(char *name) RAMFUNC;
char *GetFirmwareVersion(char *name);
