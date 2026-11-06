#ifndef CAB_WATCHDOG_H
#define CAB_WATCHDOG_H

#include <stdint.h>

#define CAB_FRAME_STALL_TIMEOUT_MS 2000U

static inline uint32_t cab_progress_watchdog_refresh(uint32_t now, uint32_t last_progress,
                                                    int frame_advanced,
                                                    int same_owner_video_active)
{
	return (frame_advanced || same_owner_video_active) ? now : last_progress;
}

static inline int cab_progress_watchdog_expired(uint32_t now, uint32_t last_progress)
{
	return (uint32_t)(now - last_progress) > CAB_FRAME_STALL_TIMEOUT_MS;
}

#endif
