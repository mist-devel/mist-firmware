#include <stddef.h>
#include <ctype.h>
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
  char c1, c2;
  int v;

  do
    {
    c1 = *s1++;
    c2 = *s2++;
    v = (unsigned int)tolower(c1) - (unsigned int)tolower(c2);
    }
  while (v == 0 && c1 != '\0' && --n > 0);

  return v;
}