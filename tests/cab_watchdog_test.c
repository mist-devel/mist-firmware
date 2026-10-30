#include "../cab_watchdog.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
	uint32_t last=100;
	last=cab_progress_watchdog_refresh(5000,last,0,1); /* same-owner capture is active */
	assert(last==5000);
	assert(!cab_progress_watchdog_expired(7000,last)); /* full post-capture resume grace */
	assert(cab_progress_watchdog_expired(7001,last));
	last=cab_progress_watchdog_refresh(9000,last,1,0); /* raster resumed */
	assert(last==9000);
	assert(!cab_progress_watchdog_expired(11000,last));
	last=cab_progress_watchdog_refresh(12000,last,0,0); /* different owner is not progress */
	assert(last==9000);
	assert(cab_progress_watchdog_expired(12001,last));
	last=UINT32_MAX-100U;
	assert(!cab_progress_watchdog_expired(50,last)); /* wrap-safe */
	return 0;
}
