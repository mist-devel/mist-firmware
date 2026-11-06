#include "remote_input.h"
#include <string.h>

static struct {
	uint32_t owner, deadline, epoch, sequence;
	uint32_t remote[REMOTE_INPUT_PORTS], physical[REMOTE_INPUT_PORTS];
	int8_t analog[REMOTE_INPUT_PORTS][4];
	unsigned dirty;
} input;
static const int8_t neutral_axes[4];

void remote_input_refresh(void)
{
	input.dirty = (1U << REMOTE_INPUT_PORTS) - 1;
}

void remote_input_reset(void)
{
	uint32_t epoch = input.epoch + 1;
	memset(&input, 0, sizeof(input));
	input.epoch = epoch;
	remote_input_refresh();
}

int remote_input_release(uint32_t owner)
{
	if (!owner || input.owner != owner) return 0;
	input.owner = 0;
	memset(input.remote, 0, sizeof(input.remote));
	input.sequence++;
	remote_input_refresh();
	return 1;
}

void remote_input_restart(void)
{
	remote_input_release(input.owner);
	input.epoch++;
	input.sequence = 0;
	remote_input_refresh();
}

void remote_input_poll(uint32_t now)
{
	if (input.owner && (int32_t)(now - input.deadline) >= 0)
		remote_input_release(input.owner);
}

int remote_input_keepalive(uint32_t owner, uint32_t now)
{
	remote_input_poll(now);
	if (!owner || input.owner != owner) return 0;
	input.deadline = now + REMOTE_INPUT_LEASE_MS;
	return 1;
}

int remote_input_set(uint32_t owner, unsigned port, uint32_t map, uint32_t now)
{
	if (!owner || port >= REMOTE_INPUT_PORTS || (map & ~REMOTE_INPUT_MASK)) return 0;
	remote_input_poll(now);
	if (input.owner && input.owner != owner) return 0;
	if (!input.owner) remote_input_refresh();
	input.owner = owner;
	input.deadline = now + REMOTE_INPUT_LEASE_MS;
	input.remote[port] = map;
	input.dirty |= 1U << port;
	input.sequence++;
	return 1;
}

uint32_t remote_input_owner(void) { return input.owner; }
uint32_t remote_input_epoch(void) { return input.epoch; }
uint32_t remote_input_sequence(void) { return input.sequence; }

uint32_t remote_input_remaining(uint32_t now)
{
	if (!input.owner || (int32_t)(now - input.deadline) >= 0) return 0;
	return input.deadline - now;
}

uint32_t remote_input_value(unsigned port)
{
	if (port >= REMOTE_INPUT_PORTS) return 0;
	return input.owner ? input.remote[port] : input.physical[port];
}

uint32_t remote_input_physical(unsigned port, uint32_t map, uint32_t mask)
{
	if (port >= REMOTE_INPUT_PORTS) return 0;
	mask &= REMOTE_INPUT_MASK;
	input.physical[port] = (input.physical[port] & ~mask) | (map & mask);
	return remote_input_value(port);
}

void remote_input_record_analog(unsigned port, const int8_t axes[4])
{
	if (port < REMOTE_INPUT_PORTS) memcpy(input.analog[port], axes, 4);
}

const int8_t *remote_input_analog(unsigned port)
{
	return input.owner || port >= REMOTE_INPUT_PORTS ? neutral_axes : input.analog[port];
}

unsigned remote_input_take_dirty(void)
{
	unsigned dirty = input.dirty;
	input.dirty = 0;
	return dirty;
}
