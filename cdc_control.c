/*
  cdc_control.c

*/

#include "cdc_enumerate.h"
#include "cdc_control.h"
#include "hardware.h"
#include "user_io.h"
#include "tos.h"
#include "debug.h"

// if cdc itself is to be debugged the debug output cannot be redirected to USB
#ifndef CDC_DEBUG
char cdc_control_debug = 0;
#endif

char cdc_control_redirect = 0;

static char buffer[32];
static unsigned char fill = 0;
static unsigned long flush_timer = 0;

extern const char version[];

void cdc_control_open(void) {
  iprintf("CDC control open\n");

  usb_cdc_open();
}

// send everything in buffer 
void cdc_control_flush(void) {
  if(fill) usb_cdc_write(buffer, fill);
  fill = 0;
}

void cdc_control_tx(char c) {
  // buffer full? flush it!
  if(fill == sizeof(buffer))
    cdc_control_flush();

  flush_timer = GetTimer(100);
  buffer[fill++] = c;
}

static void cdc_puts(char *str) {
  unsigned char i=0;
  
  while(*str) {
    if(*str == '\n')
      cdc_control_tx('\r');

    cdc_control_tx(*str++);
  }

  cdc_control_tx('\r');
  cdc_control_tx('\n');

  cdc_control_flush();
}

void cdc_control_poll(void) {
  // flush out queue every now and then
  if(flush_timer && CheckTimer(flush_timer)) {
    cdc_control_flush();
    flush_timer = 0;
  }

  // low level usb handling happens inside usb_cdc_poll
  if(usb_cdc_poll()) {
    char key;

    // check for user input
    if(usb_cdc_read(&key, 1)) {

      if(cdc_control_redirect == CDC_REDIRECT_RS232)
	user_io_serial_tx(key);
      else {
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
#ifndef CDC_DEBUG
	  cdc_puts("\033[7mD\033[0mebug");
#endif
	  cdc_puts("R\033[7mS\033[0m232 redirect");
	  cdc_puts("\033[7mP\033[0marallel redirect");
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
	
#ifndef CDC_DEBUG
	case 'd':
	  cdc_puts("Debug enabled");
	  cdc_control_debug = 1;
	  break;
#endif
	  
	case 's':
	  cdc_puts("RS232 redirect enabled");
	  cdc_control_redirect = CDC_REDIRECT_RS232;
	  break;

	case 'p':
	  cdc_puts("Parallel redirect enabled");
	  cdc_control_redirect = CDC_REDIRECT_PARALLEL;
	  break;
	}
      }
    }
  }
}
