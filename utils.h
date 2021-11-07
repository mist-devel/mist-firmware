#ifndef _UTILS_H_
#define _UTILS_H_

#include <stddef.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

unsigned char decval(unsigned char in, unsigned char min, unsigned char max);
unsigned char incval(unsigned char in, unsigned char min, unsigned char max);
unsigned char bin2bcd(unsigned char in);
unsigned char bcd2bin(unsigned char in);
int _strnicmp(const char *s1, const char *s2, size_t n);
void hexdump(void *data, uint16_t size, uint16_t offset);

#endif
