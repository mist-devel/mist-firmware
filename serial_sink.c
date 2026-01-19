/*
 * serial_sink.c
 */

#include <stdio.h>
#include <string.h>

#include "serial_sink.h"

static serial_sink_t* SINKS[NUM_SINKS];

void console_start() {
  iprintf("\033[1;36m");
}

void console_end() {
  iprintf("\033[0m");
}

void console_echo(uint8_t value) {
  if (value != 0xff && value != 0x00) {
    iprintf("%c", value);
  }
}

static serial_sink_t console_sink = {0, 8,
     &console_start,
     &console_echo,
     &console_end
};

void serial_sink_init() {
  memset(SINKS, 0, sizeof(SINKS));
  serial_sink_register(&console_sink);
}

bool serial_sink_register(serial_sink_t *sink) {
  if (sink && sink->index < NUM_SINKS) {
    if (!SINKS[sink->index]) {
      SINKS[sink->index] = sink;
      return true;
    } else {
      iprintf("Trying to overwrite serial sink with index %d\n", sink->index);
    }
  }
  return false;
}

serial_sink_t *serial_sink_get(uint8_t index) {
  return SINKS[index];
}
