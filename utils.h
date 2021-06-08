#ifndef _UTILS_H_
#define _UTILS_H_

unsigned char bin2bcd(unsigned char in);
unsigned char bcd2bin(unsigned char in);
int _strnicmp(const char *s1, const char *s2, size_t n);
void hexdump(void *data, uint16_t size, uint16_t offset);

#endif
