#include <stdio.h>
#include "idxfile.h"
#include "hardware.h"

IDXFile sd_image[SD_IMAGES];

void IDXIndex(IDXFile *pIDXF) {
    // builds index to speed up hard file seek
    FIL *file = &pIDXF->file;
    unsigned long  time = GetRTTC();
    FRESULT res;

    pIDXF->clmt[0] = SZ_TBL;
    file->cltbl = pIDXF->clmt;
    DISKLED_ON
    res = f_lseek(file, CREATE_LINKMAP);
    DISKLED_OFF
    if (res != FR_OK) {
      iprintf("Error indexing (%d), continuing without indices\n", res);
      file->cltbl = 0;
    } else {
      time = GetRTTC() - time;
      iprintf("File indexed in %lu ms, index size = %lu\n", time, pIDXF->clmt[0]);
    }
}

unsigned char IDXOpen(IDXFile *file, const char *name, char mode) {
  return f_open(&(file->file), name, mode);
}

void IDXClose(IDXFile *file) {
  f_close(&(file->file));
}

unsigned char IDXSeek(IDXFile *file, unsigned long lba) {
  return f_lseek(&(file->file), (FSIZE_t) lba << 9);
}
