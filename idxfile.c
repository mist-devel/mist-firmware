#include <stdio.h>
#include "idxfile.h"

void IDXIndex(IDXFile *pIDXF) {
    // builds index to speed up hard file seek
    fileTYPE *file = &pIDXF->file;
    unsigned long *index = pIDXF->index;
    unsigned long i;
    unsigned long j;
    unsigned long  time = GetTimer(0);

    pIDXF->index_size = 16; // indexing size
    j = 1 << pIDXF->index_size;
    i = pIDXF->file.size >> 10; // divided by index table size (1024)
    while (j < i) // find greater or equal power of two
    {
        j <<= 1;
        pIDXF->index_size++;
    }

    for (i = 0; i < file->size; i += j)
    {
        FileSeek(file, i >> 9, SEEK_SET); // FileSeek seeks in 512-byte sectors
        *index++ = file->cluster;
    }
    
    time = GetTimer(0) - time;
    iprintf("File indexed in %lu ms, index size = %d\n", time >> 16, pIDXF->index_size);
}

unsigned char IDXOpen(IDXFile *file, const char *name) {
  unsigned char retval = FileOpen(&(file->file), name);

  if(retval) IDXIndex(file);
  
  return retval;
}

unsigned char IDXSeek(IDXFile *pIDXF, unsigned long lba)
{
    if ((pIDXF->file.sector ^ lba) & cluster_mask)
    { // different clusters
        if ((pIDXF->file.sector > lba) || ((pIDXF->file.sector ^ lba) & (cluster_mask << (fat32 ? 7 : 8)))) // 7: 128 FAT32 links per sector, 8: 256 FAT16 links per sector
        { // requested cluster lies before current pointer position or in different FAT sector
            pIDXF->file.cluster = pIDXF->index[lba >> (pIDXF->index_size - 9)];// minus 9 because lba is in 512-byte sectors
            pIDXF->file.sector = lba & (-1 << (pIDXF->index_size - 9));
        }
    }
    return FileSeek(&pIDXF->file, lba, SEEK_SET);
}
