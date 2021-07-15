#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "utils.h"
#include "attrs.h"

unsigned char bin2bcd(unsigned char in) {
  return 16*(in/10) + (in % 10);
}

unsigned char bcd2bin(unsigned char in) {
  return 10*(in >> 4) + (in & 0x0f);
}

FAST int _strnicmp(const char *s1, const char *s2, size_t n)
{
  int s1l, s2l;
  char c1, c2;
  int v;

  s1l = strlen(s1);
  s2l = strlen(s2);

  do {
    c1 = *s1++;
    c2 = *s2++;
    if (!c1) break;
    v = (unsigned int)tolower(c1) - (unsigned int)tolower(c2);
  } while (v == 0 && --n > 0);

  if ((v==0) && n && (s1l != s2l)) {
    return((s1l > s2l) ? 1 : -1);
  }

  return v;
}

void hexdump(void *data, uint16_t size, uint16_t offset) {
  uint8_t i, b2c;
  uint16_t n=0;
  char *ptr = data;

  if(!size) return;

  while(size>0) {
    iprintf("%04x: ", n + offset);

    b2c = (size>16)?16:size;
    for(i=0;i<b2c;i++)      iprintf("%02x ", 0xff&ptr[i]);
    iprintf("  ");
    for(i=0;i<(16-b2c);i++) iprintf("   ");
    for(i=0;i<b2c;i++)      iprintf("%c", isprint(ptr[i])?ptr[i]:'.');
    iprintf("\n");
    ptr  += b2c;
    size -= b2c;
    n    += b2c;
  }
}
