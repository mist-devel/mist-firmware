#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include <stdint.h>

#define REMOTE_INPUT_PORTS 6
#define REMOTE_INPUT_MASK 0x000fffffUL
#define REMOTE_INPUT_LEASE_MS 5000

// Immediate digital input state. No frame timing or script storage is implied.
void remote_input_reset(void);
void remote_input_restart(void); // New game epoch, preserving physical input cache.
int remote_input_set(uint32_t owner, unsigned port, uint32_t map, uint32_t now);
int remote_input_keepalive(uint32_t owner, uint32_t now);
int remote_input_release(uint32_t owner);
void remote_input_poll(uint32_t now);
uint32_t remote_input_owner(void);
uint32_t remote_input_epoch(void);
uint32_t remote_input_sequence(void);
uint32_t remote_input_remaining(uint32_t now);
uint32_t remote_input_value(unsigned port);
uint32_t remote_input_physical(unsigned port, uint32_t map, uint32_t mask);
void remote_input_record_analog(unsigned port, const int8_t axes[4]);
const int8_t *remote_input_analog(unsigned port);
void remote_input_refresh(void);
unsigned remote_input_take_dirty(void);

#endif
