#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "cue_parser.h"

//#define CUEFILE "Golden Axe.cue"
//#define CUEFILE "Golden Axe (Japan).cue"
//#define CUEFILE "[TGXCD1042] [Mixed mode CD] [--].cue"
//#define CUEFILE "Sherlock Holmes Consulting Detective (USA).cue"
#define CUEFILE "Space Ava 201.cue"

void iprintf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    vprintf(format, arg);
    va_end(arg);
}

void cue_parser_debugf(char *str, const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    vsprintf(str, format, arg);
    va_end(arg);
}

int main() {
    char res;
    msf_t msf;

    if (res=cue_parse(CUEFILE)) {
      printf("Error (%d)\n!", res);
    }
}
