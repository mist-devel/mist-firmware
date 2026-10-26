#ifndef _SATURN_H_
#define _SATURN_H_

// MiST/SiDi128 virtual CD drive for the Saturn_SiDi128 core (see saturn.c)

void saturn_init(void);
void saturn_poll(void);
char saturn_cue_mount(const char *name);
char saturn_cue_mounted(void);

#endif
