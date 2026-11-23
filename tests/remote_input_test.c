#include "remote_input.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
	remote_input_reset();
	uint32_t epoch = remote_input_epoch();
	assert(epoch && !remote_input_owner());
	assert(remote_input_take_dirty() == 0x3f);
	assert(!remote_input_take_dirty());
	assert(remote_input_physical(1, 0x12345, REMOTE_INPUT_MASK) == 0x12345);
	assert(remote_input_physical(1, 0x80, 0xff) == 0x12380);
	int8_t axes[4] = {-128, 127, -1, 0};
	remote_input_record_analog(1, axes);
	assert(remote_input_analog(1)[0] == -128);
	assert(!remote_input_set(0, 1, 0, 0));
	assert(!remote_input_set(1, 6, 0, 0));
	assert(!remote_input_set(1, 1, 0x100000, 0));
	assert(remote_input_set(1, 1, 0x10, 100));
	assert(remote_input_take_dirty() == 0x3f);
	assert(remote_input_sequence() == 1);
	assert(remote_input_value(1) == 0x10 && !remote_input_value(0));
	assert(remote_input_physical(1, 0x22, REMOTE_INPUT_MASK) == 0x10);
	assert(!remote_input_analog(1)[0]);
	assert(!remote_input_set(2, 1, 0x20, 101));
	assert(!remote_input_keepalive(2, 101));
	assert(!remote_input_release(2));
	assert(remote_input_remaining(100) == REMOTE_INPUT_LEASE_MS);
	assert(remote_input_keepalive(1, 1000));
	remote_input_poll(5999);
	assert(remote_input_owner() == 1);
	remote_input_poll(6000);
	assert(!remote_input_owner() && remote_input_value(1) == 0x22);
	assert(remote_input_analog(1)[0] == -128);
	assert(remote_input_take_dirty() == 0x3f);
	assert(!remote_input_keepalive(1, 6000)); // Never revive expired held inputs.
	assert(remote_input_set(2, 0, 0x80000, UINT32_MAX - 100));
	assert(remote_input_remaining(UINT32_MAX - 50) == REMOTE_INPUT_LEASE_MS - 50);
	remote_input_poll(4898);
	assert(remote_input_owner() == 2);
	remote_input_poll(4899);
	assert(!remote_input_owner());
	assert(remote_input_set(1, 1, 0x10, 10000));
	remote_input_restart();
	assert(remote_input_epoch() == epoch + 1 && !remote_input_sequence());
	assert(!remote_input_owner() && remote_input_value(1) == 0x22);
	assert(remote_input_analog(1)[0] == -128);
	assert(remote_input_set(3, 1, 0, 11000));
	assert(remote_input_owner() == 3); // All-released state still owns the ports.
	remote_input_reset();
	assert(remote_input_epoch() == epoch + 2 && !remote_input_sequence());
	assert(!remote_input_owner() && !remote_input_value(1));
	assert(!remote_input_analog(1)[0]);
	assert(!remote_input_value(6) && !remote_input_physical(6, 0xff, 0xff));
	puts("remote input: ownership, physical restore, lease expiry/wrap and epochs PASS");
	return 0;
}
