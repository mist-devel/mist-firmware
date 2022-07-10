// external storage interface

#ifndef STORAGE_EX_H
#define STORAGE_EX_H

#include <stdbool.h>
#include <inttypes.h>

extern unsigned char usb_host_storage_read(unsigned long lba, unsigned char *pReadBuffer, uint16_t len);
extern unsigned char usb_host_storage_write(unsigned long lba, const unsigned char *pWriteBuffer, uint16_t len);
extern unsigned int usb_host_storage_capacity();

#endif // STORAGE_EX_H
