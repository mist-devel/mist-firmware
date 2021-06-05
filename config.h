#include "fat_compat.h"
#include "hdd.h"

typedef struct
{
    unsigned char lores;
    unsigned char hires;
} filterTYPE;

typedef struct
{
    unsigned char speed;
    unsigned char drives;
} floppyTYPE;

typedef struct
{
    char          id[8];
    unsigned long version;
    char          kickstart[80];
    filterTYPE    filter;
    unsigned char memory;
    unsigned char chipset;
    floppyTYPE    floppy;
    unsigned char disable_ar3;
    unsigned char enable_ide[2];
    unsigned char scanlines;
    unsigned char pad1;
    hardfileTYPE  hardfile[HARDFILES];
    unsigned char cpu;
    unsigned char   autofire;
} configTYPE;

extern configTYPE config;
extern char DebugMode;

char UploadKickstart(char *name);
char UploadActionReplay();
void SetConfigurationFilename(int config);	// Set configuration filename by slot number
unsigned char LoadConfiguration(char *filename, int printconfig);	// Can supply NULL to use filename previously set by slot number
unsigned char SaveConfiguration(char *filename);	// Can supply NULL to use filename previously set by slot number
unsigned char ConfigurationExists(char *filename);

