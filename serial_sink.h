/*
 * serial_sink.h
 * 
 */

#ifndef SERIAL_SINK_H
#define SERIAL_SINK_H

#include <inttypes.h>
#include <stdbool.h>

#define NUM_SINKS       8

typedef struct {
  uint8_t index;
  uint8_t burst;
  void (*begin)(void);
  void (*process_data)(uint8_t data);
  void (*end)(void);
} serial_sink_t;

void serial_sink_init();
bool serial_sink_register(serial_sink_t *sink);
serial_sink_t *serial_sink_get(uint8_t index);

#endif /* SERIAL_SINK_H */
