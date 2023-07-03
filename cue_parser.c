// cue_parser.c
// 2021, Gyorgy Szombathelyi

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "cue_parser.h"
#ifdef CUE_PARSER_TEST
#define cue_parser_debugf(a, ...) printf(a"\n", ## __VA_ARGS__)
#else
#include "debug.h"
#include "idxfile.h"
#endif

//// defines ////
#define TOKEN_FILE              "FILE"
#define TOKEN_BINARY            "BINARY"
#define TOKEN_TRACK             "TRACK"
#define TOKEN_AUDIO             "AUDIO"
#define TOKEN_MODE1_2048        "MODE1/2048"
#define TOKEN_MODE1_2352        "MODE1/2352"
#define TOKEN_MODE2_2352        "MODE2/2352"
#define TOKEN_MODE2_2336        "MODE2/2336"
#define TOKEN_PREGAP            "PREGAP"
#define TOKEN_INDEX             "INDEX"

#define MODE_NONE               0
#define MODE_FILE               1
#define MODE_TRACK              2
#define MODE_PREGAP             3
#define MODE_INDEX              4

#define CUE_WORD_SIZE           128

#define CUE_EOT                 4
#define CUE_NOWORD              3
//// macros ////
#define CHAR_IS_NUM(c)          (((c) >= '0') && ((c) <= '9'))
#define CHAR_IS_ALPHA_LOWER(c)  (((c) >= 'a') && ((c) <= 'z'))
#define CHAR_IS_ALPHA_UPPER(c)  (((c) >= 'A') && ((c) <= 'Z'))
#define CHAR_IS_ALPHA(c)        (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c))
#define CHAR_IS_ALPHANUM(c)     (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c) || CHAR_IS_NUM(c))
#define CHAR_IS_SPECIAL(c)      (((c) == '[') || ((c) == ']') || ((c) == '-') || ((c) == '_') || ((c) == ',') || ((c) == '=') || ((c) == '~') || ((c) == ':') || ((c) == '/'))
#define CHAR_IS_VALID(c)        (CHAR_IS_ALPHANUM(c) || CHAR_IS_SPECIAL(c))
#define CHAR_IS_WHITESPACE(c)   (((c) == ' ') || ((c) == '\t') || ((c) == '\r') || ((c) == '\n'))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))
#define CHAR_TO_UPPERCASE(c)    ({ char _c = (c); if (CHAR_IS_ALPHA_LOWER(_c)) _c = _c - 'a' + 'A'; _c;})
#define CHAR_TO_LOWERCASE(c)    ({ char _c = (c); if (CHAR_IS_ALPHA_UPPER(_c)) _c = _c - 'A' + 'a'; _c;})

#ifdef CUE_PARSER_TEST
FILE* cue_fp = NULL;
char  sector_buffer[512] = {0};
int   cue_size = 0;
#else
static FIL cue_file;
#endif

static int cue_pt = 0;

toc_t toc;

const char *cue_error_msg[] = {
  "\n   Cannot open CUE file.\n",
  "\n     Invalid CUE file.\n",
  "\n   Unsupported CUE file.\n",
  "\n   Cannot open the image.\n"
};

void LBA2MSF(int lba, msf_t* msf) {
  msf->m = (lba / 75) / 60;
  msf->s = (lba / 75) % 60;
  msf->f = (lba % 75);
}

int MSF2LBA(unsigned char m, unsigned char s, unsigned char f) {
  return (f + s * 75 + m * 60 * 75 - 150);
}

static char ParseMSF(const char *s, msf_t *msf) {
  char c;

  if (*s && CHAR_IS_NUM(*s)) msf->m = 10*(*s++ - '0'); else return 0;
  if (*s && CHAR_IS_NUM(*s)) msf->m+= (*s++ - '0'); else return 0;
  if (*s && *s == ':') s++; else return 0;
  if (*s && CHAR_IS_NUM(*s)) msf->s = 10*(*s++ - '0'); else return 0;
  if (*s && CHAR_IS_NUM(*s)) msf->s+= (*s++ - '0'); else return 0;
  if (*s && *s == ':') s++; else return 0;
  if (*s && CHAR_IS_NUM(*s)) msf->f = 10*(*s++ - '0'); else return 0;
  if (*s && CHAR_IS_NUM(*s)) msf->f+= (*s++ - '0'); else return 0;
  if (*s) return 0;
  return 1;
}

static char cue_getch()
{
  #ifndef CUE_PARSER_TEST
  UINT br;
  #endif
  if (!(cue_pt&0x1ff)) {
    // reload buffer
    #ifdef CUE_PARSER_TEST
    fread(sector_buffer, sizeof(char), sizeof(sector_buffer), cue_fp);
    #else
    f_read(&cue_file, sector_buffer, 512, &br);
    #endif
  }

  #ifdef CUE_PARSER_TEST
  if (cue_pt >= cue_size) return 0;
  #else
  if (cue_pt >= f_size(&cue_file)) return 0;
  #endif
  else return sector_buffer[(cue_pt++)&0x1ff];
}

static int cue_getword(char* word)
{
  char c;
  char literal=0;
  int i=0;

  while(1) {
    c = cue_getch();
    if ((!c) || CHAR_IS_LINEEND(c) || (CHAR_IS_WHITESPACE(c) && !literal)) break;
    else if (CHAR_IS_QUOTE(c)) literal ^= 1;
    else if ((literal || (CHAR_IS_VALID(c))) && i<(CUE_WORD_SIZE-1)) word[i++] = c;
  }
  word[i] = '\0';
  return c==0 ? CUE_EOT : i == 0 ? CUE_NOWORD : literal ? 1 : 0;
}

//// cue_parse() ////
#ifdef CUE_PARSER_TEST
char cue_parse(const char *filename)
#else
char cue_parse(const char *filename, IDXFile *image)
#endif
{
  char word[CUE_WORD_SIZE] = {0};
  int word_status;
  char mode = 0, submode = 0, error = CUE_RES_OK, index = 0, bin_valid = 0;
  int track = 0, x, pregap = 0, tracklen;
  msf_t msf;
  int lba, lastindex1 = 0;
  char e[3];

  memset(&toc, 0, sizeof(toc));

  const char *ext = GetExtension(filename);
  e[0] = e[1] = e[2] = ' ';
  if (ext) {
    if (ext[0]) e[0] = toupper(ext[0]);
    if (ext[1]) e[1] = toupper(ext[1]);
    if (ext[2]) e[2] = toupper(ext[2]);
  }
  if (!memcmp(e, "ISO", 3)) {
    // open iso file
    #ifdef CUE_PARSER_TEST
    bin_valid = 1;
    #else
    toc.file = image;
    if (IDXOpen(toc.file, filename, FA_READ) == FR_OK) {
      bin_valid = 1;
      track = 1;
      toc.tracks[0].sector_size = 2048;
      toc.tracks[0].type = SECTOR_DATA_MODE1;
      toc.tracks[0].offset = 0;
      toc.tracks[0].start = 0;
    } else {
      error = CUE_RES_BINERR;
    }
    #endif
  } else {
    // open cue file
    #ifdef CUE_PARSER_TEST
    if ((cue_fp = fopen(filename, "rb")) == NULL) {
    #else
    if (f_open(&cue_file, filename, FA_READ) != FR_OK) {
    #endif
      cue_parser_debugf("Can't open file %s !", filename);
      return CUE_RES_NOTFOUND;
    }

    #ifdef CUE_PARSER_TEST
    fseek(cue_fp, 0L, SEEK_END);
    cue_size = ftell(cue_fp);
    fseek(cue_fp, 0L, SEEK_SET);
    #else
    cue_parser_debugf("Opened file %s with size %llu bytes.", filename, f_size(&cue_file));
    #endif
    cue_pt = 0;

    // parse cue
    while (1) {
      // get line
      word_status = cue_getword(word);
      if (word_status != CUE_NOWORD) {
        //cue_parser_debugf("next word(%d): \"%s\".", word_status, word);
        switch (mode) {
          case MODE_NONE:
            submode = 0;
            if (!strcmp(word, TOKEN_FILE))   mode = MODE_FILE; else
            if (!strcmp(word, TOKEN_TRACK))  mode = MODE_TRACK; else
            if (!strcmp(word, TOKEN_PREGAP)) mode = MODE_PREGAP; else
            if (!strcmp(word, TOKEN_INDEX))  mode = MODE_INDEX;
            break;
          case MODE_FILE:
            if (submode == 0) {
              pregap = 0;
              cue_parser_debugf("Filename: %s", word);
              if (bin_valid) {
                // only one .bin supported
                error = CUE_RES_UNS;
              } else {
              #ifdef CUE_PARSER_TEST
                bin_valid = 1;
              }
              #else
                toc.file = image;
                if (IDXOpen(toc.file, word, FA_READ) == FR_OK)
                  bin_valid = 1;
                else
                  error = CUE_RES_BINERR;
              }
              #endif
              } else if (submode == 1) {
              cue_parser_debugf("Filemode: %s", word);
              mode = 0;
            }
            submode++;
            break;
          case MODE_TRACK:
            if (submode == 0) {
              x = strtol(word, 0, 10);
              cue_parser_debugf("Trackno: %d -> %d (%s)", track, x, word);
              if (!x || x > 99 || x != (track + 1)) error = CUE_RES_INVALID; else track = x;
            } else if (submode == 1) {
              cue_parser_debugf("Trackmode: %s", word);
              if (!strcmp(word, TOKEN_AUDIO)) {
                toc.tracks[track-1].sector_size = 2352;
                toc.tracks[track-1].type = SECTOR_AUDIO;
              } else if (!strcmp(word, TOKEN_MODE1_2352)) {
                toc.tracks[track-1].sector_size = 2352;
                toc.tracks[track-1].type = SECTOR_DATA_MODE1;
              } else if (!strcmp(word, TOKEN_MODE2_2352)) {
                toc.tracks[track-1].sector_size = 2352;
                toc.tracks[track-1].type = SECTOR_DATA_MODE2;
              } else if (!strcmp(word, TOKEN_MODE2_2336)) {
                toc.tracks[track-1].sector_size = 2336;
                toc.tracks[track-1].type = SECTOR_DATA_MODE2;
              } else if (!strcmp(word, TOKEN_MODE1_2048)) {
                toc.tracks[track-1].sector_size = 2048;
                toc.tracks[track-1].type = SECTOR_DATA_MODE1;
              } else {
                error = CUE_RES_INVALID;
              }
              mode = 0;
            }
            submode++;
            break;
          case MODE_PREGAP:
            cue_parser_debugf("Pregap size: %s", word);
              if (!ParseMSF(word, &msf)) {
                error = CUE_RES_INVALID;
              } else {
                pregap += MSF2LBA(msf.m, msf.s, msf.f) + 150;
              }
            mode = 0;
            break;
          case MODE_INDEX:
            if (submode == 0) {
              cue_parser_debugf("Index: %s", word);
              index = strtol(word, 0, 10);
            } else if (submode == 1) {
              if (!ParseMSF(word, &msf)) {
                error = CUE_RES_INVALID;
              } else {
                lba = MSF2LBA(msf.m, msf.s, msf.f);
                if (index == 0) {
                  if (track > 1 && !toc.tracks[track - 2].end) {
                    toc.tracks[track - 2].end =  lba + 150 + pregap;
                  }
                } else if (index == 1) {
                  toc.tracks[track - 1].start = lba + 150 + pregap;
                  if (track > 1) {
                    tracklen = lba - lastindex1;
                    toc.tracks[track - 1].offset = toc.tracks[track - 2].offset + (tracklen * toc.tracks[track - 2].sector_size);
                    if (!toc.tracks[track-2].end) toc.tracks[track - 2].end = toc.tracks[track - 1].start - 1;
                  } else {
                    toc.tracks[0].offset = (lba + 150) * toc.tracks[0].sector_size;
                  }
                  lastindex1 = lba;
                }
              }
              cue_parser_debugf("Pos: (%s) = %d:%d:%d (%d)", word, msf.m, msf.s, msf.f, error);
              mode = 0;
            }
            submode++;
            break;
        }
      }
      // if end of file or error, stop
      if (word_status == CUE_EOT || error) break;
    }

    #ifdef CUE_PARSER_TEST
    // close file
    fclose(cue_fp);
    #else
    f_close(&cue_file);
    #endif
  }

  #ifndef CUE_PARSET_TEST
  if (!bin_valid)
    error = CUE_RES_BINERR;
  else if (error) 
    f_close(&toc.file->file);
  else {
    IDXIndex(toc.file);
    if (track > 0) {
      tracklen = (f_size(&toc.file->file) - toc.tracks[track - 1].offset) / toc.tracks[track - 1].sector_size;
      toc.tracks[track - 1].end = toc.tracks[track - 1].start + tracklen;
    }
  }
  #endif
  if (error) {
    toc.last = 0;
  } else {
    toc.last = track;
    toc.end = toc.tracks[track-1].end;
    toc.valid = 1;
  }

  iprintf("Tracks in the CUE file %s : %d\n", filename, toc.last);
  for (int i = 0; i < toc.last ; i++) {
    LBA2MSF(toc.tracks[i].start, &msf);
    iprintf("Track %i, start: %d - %02d:%02d:%02d (%d) end: %d sector size:%d\n",
      i+1, toc.tracks[i].start, msf.m, msf.s, msf.f, toc.tracks[i].offset, toc.tracks[i].end, toc.tracks[i].sector_size);
  }
  return error;
}

int cue_gettrackbylba(int lba) {
  int index = 0;
  while ((toc.tracks[index].end <= lba) && (index < toc.last)) index++;
  return index;
}
