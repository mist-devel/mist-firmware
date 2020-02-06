#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "mist_cfg.h"

void siprintf(char *str, const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    vsprintf(str, format, arg);
    va_end(arg);
}

int main() {

    memset(&mist_cfg, 0, sizeof(mist_cfg));
    memset(&minimig_cfg, 0, sizeof(minimig_cfg));
    ini_parse(&mist_ini_cfg);
}
