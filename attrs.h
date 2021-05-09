#ifndef ATTRS_H
#define ATTRS_H

#define RAMFUNC __attribute__ ((long_call, section (".ramsection")))
#define FAST __attribute__((optimize("-Ofast")))

#endif // ATTRS_H
