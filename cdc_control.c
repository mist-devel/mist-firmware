/*
  cdc_control.c

*/

#include "cdc_enumerate.h"
#include "cdc_control.h"
#include "hardware.h"
#include "tos.h"
#include "debug.h"

extern const char version[];

void cdc_control_open(void) {
  iprintf("CDC control open\n");

  usb_cdc_open();
}

static void cdc_tx(char c, char flush) {
  static char buffer[32];
  static unsigned char fill = 0;

  if(c)
    buffer[fill++] = c;

  if(fill && ((fill == sizeof(buffer)) || flush)) {
    usb_cdc_write(buffer, fill);
    fill = 0;
  }
}

static void cdc_puts(char *str) {
  unsigned char i=0;
  
  while(*str) {
    if(*str == '\n')
      cdc_tx('\r', 0);

    cdc_tx(*str++, 0);
  }

  cdc_tx('\r', 0);
  cdc_tx('\n', 1);
}

void cdc_control_poll(void) {
  // low level usb handling happens inside usb_cdc_poll
  if(usb_cdc_poll()) {
    char key;

    // check for user input
    if(usb_cdc_read(&key, 1)) {

      // force lower case
      if((key >= 'A') && (key <= 'Z'))
	key = key - 'A' + 'a';

      switch(key) {
      case '\r':
	cdc_puts("\n\033[7m <<< MIST board controller >>> \033[0m");
	cdc_puts("Firmware version ATH" VDATE);
	cdc_puts("Commands:");
	cdc_puts("\033[7mR\033[0meset");
	cdc_puts("\033[7mC\033[0moldreset");
	cdc_puts("");
	break;

      case 'r':
	cdc_puts("Reset ...");
	tos_reset(0);
	break;

      case 'c':
	cdc_puts("Coldreset ...");
	tos_reset(1);
	break;
      }
    }
  }
}
