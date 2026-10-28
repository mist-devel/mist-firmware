/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VIDEO_SCHEDULE_H
#define VIDEO_SCHEDULE_H

#include <stdint.h>

enum {
    VIDEO_SCHEDULE_OK = 0,
    VIDEO_SCHEDULE_INVALID = 1,
    VIDEO_SCHEDULE_OVERFLOW = 2,
    VIDEO_SCHEDULE_CAB_RANGE = 3
};

/* Align CAB captures to absolute period boundaries. Standalone captures keep
 * the historical relative schedule beginning one period after INFO. */
static inline int video_schedule_first(uint32_t frame, uint32_t period,
                                       uint32_t count, int cab_active,
                                       uint32_t cab_total, uint32_t *first)
{
    uint32_t target;

    if (!period || !count || !first) return VIDEO_SCHEDULE_INVALID;
    if (cab_active) {
        uint32_t base = frame - frame % period;
        if (base > UINT32_MAX - period) return VIDEO_SCHEDULE_OVERFLOW;
        target = base + period;
    } else {
        if (frame > UINT32_MAX - period) return VIDEO_SCHEDULE_OVERFLOW;
        target = frame + period;
    }

    if (count - 1 > (UINT32_MAX - target) / period)
        return VIDEO_SCHEDULE_OVERFLOW;
    if (cab_active && (target > cab_total ||
                       count - 1 > (cab_total - target) / period))
        return VIDEO_SCHEDULE_CAB_RANGE;

    *first = target;
    return VIDEO_SCHEDULE_OK;
}

#endif
