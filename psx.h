#ifndef _PSX_H_
#define _PSX_H_

#include <stdint.h>

void psx_mount_cd(const unsigned char *name);
void psx_read_cd(uint8_t drive_index, unsigned int lba);

#endif