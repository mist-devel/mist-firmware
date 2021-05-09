#include <stdio.h>
#include "idxfile.h"

IDXFile hdd_image[HDD_IMAGES];

void IDXIndex(IDXFile *pIDXF) {
    // builds index to speed up hard file seek
    FIL *file = &pIDXF->file;
    unsigned long  time = GetTimer(0);
    FRESULT res;

    pIDXF->clmt[0] = SZ_TBL;
    file->cltbl = pIDXF->clmt;
    res = f_lseek(file, CREATE_LINKMAP);
    if (res != FR_OK) {
      iprintf("Error indexing\n");
    } else {
      time = GetTimer(0) - time;
      iprintf("File indexed in %lu ms, index size = %d\n", time >> 16, pIDXF->clmt[0]);
    }
}

unsigned char IDXOpen(IDXFile *file, const char *name) {
  return f_open(&(file->file), name, FA_READ | FA_WRITE);
}

unsigned char IDXSeek(IDXFile *file, unsigned long lba) {
  return f_lseek(&(file->file), lba * 512);
}
