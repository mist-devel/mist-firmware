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

#ifdef CUE_MULTI_FILE
#define CUE_MAX_FILES    100
#define CUE_NAME_POOL    4096
#define CUE_MAX_IDX      4
#endif

typedef struct
{
        int offset;
        int start;
        int end;
        int type;
        int sector_size;
#ifdef CUE_MULTI_FILE
        int file;
        int indexes[CUE_MAX_IDX];
        int index_num;
#endif
} cd_track_t;

typedef struct
{
        int valid;
        int end;
        int last;
        cd_track_t tracks[100];
#ifndef CUE_PARSER_TEST
        IDXFile *file;
#endif
#ifdef CUE_MULTI_FILE
        int files;
        const char *file_name[CUE_MAX_FILES];
        unsigned int file_size[CUE_MAX_FILES];
        char name_pool[CUE_NAME_POOL];
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

#ifndef CUE_PARSER_TEST
#ifdef CUE_MULTI_FILE
void cue_set_aux_image(IDXFile *aux);
void cue_set_multi_file(char on);
FIL *cue_track_file(int track);
void cue_close(void);
int cue_file_is_open(const FIL *f);
#else
#define cue_track_file(track) (&toc.file->file)
#endif
#endif

#endif

