/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../video_schedule.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void expect(uint32_t frame, uint32_t period, uint32_t count,
                   int cab_active, uint32_t cab_total,
                   int result, uint32_t expected)
{
    uint32_t first = 0xdeadbeef;
    assert(video_schedule_first(frame, period, count, cab_active,
                                cab_total, &first) == result);
    if (result == VIDEO_SCHEDULE_OK) assert(first == expected);
    else assert(first == 0xdeadbeef);
}

int main(void)
{
    expect(0,   100, 20, 1, 2000, VIDEO_SCHEDULE_OK, 100);
    expect(1,   100, 20, 1, 2000, VIDEO_SCHEDULE_OK, 100);
    expect(250, 100, 18, 1, 2000, VIDEO_SCHEDULE_OK, 300);
    expect(1999,100, 1, 1, 2000, VIDEO_SCHEDULE_OK, 2000);
    expect(1999,100, 2, 1, 2000, VIDEO_SCHEDULE_CAB_RANGE, 0);
    expect(1950,100, 2, 1, 2000, VIDEO_SCHEDULE_CAB_RANGE, 0);
    expect(UINT32_MAX-50,100,1,1,UINT32_MAX,
           VIDEO_SCHEDULE_OVERFLOW,0);
    expect(UINT32_MAX-50,100,1,0,0,VIDEO_SCHEDULE_OVERFLOW,0);
    expect(UINT32_MAX-12,5,3,0,0,VIDEO_SCHEDULE_OVERFLOW,0);
    expect(10, 0, 1, 0, 0, VIDEO_SCHEDULE_INVALID, 0);
    expect(10, 1, 0, 0, 0, VIDEO_SCHEDULE_INVALID, 0);
    expect(10, 3, 2, 0, 0, VIDEO_SCHEDULE_OK, 13);
    puts("video schedule: CAB alignment, bounds and overflow PASS");
    return 0;
}
