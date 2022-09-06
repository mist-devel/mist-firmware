#ifndef __CUE_PARSER_H__
#define __CUE_PARSER_H__

#ifndef CUE_PARSER_TEST
#include "idxfile.h"
#include "FatFs/ff.h"
#endif

#define SECTOR_AUDIO 0
#define SECTOR_DATA_MODE1 1
#define SECTOR_DATA_MODE2 2

#define CUE_RES_OK       0
#define CUE_RES_NOTFOUND 1
#define CUE_RES_INVALID  2
#define CUE_RES_UNS      3
#define CUE_RES_BINERR   4

typedef struct
{
        int offset;
        int start;
        int end;
        int type;
        int sector_size;
} cd_track_t;

typedef struct
{
        int valid;
        int end;
        int last;
        cd_track_t tracks[100];
#ifndef CUE_PARSER_TEST
        IDXFile *file; // the .bin file
#endif
} toc_t;

typedef struct
{
        unsigned char m;
        unsigned char s;
        unsigned char f;
} msf_t;

extern toc_t toc;
extern const char *cue_error_msg[];

#ifdef CUE_PARSER_TEST
char cue_parse(const char *filename);
#else
char cue_parse(const char *filename, IDXFile *image);
#endif
void LBA2MSF(int lba, msf_t* msf);
int MSF2LBA(unsigned char m, unsigned char s, unsigned char f);
int cue_gettrackbylba(int lba);

#endif // __CUE_PARSER_H__

