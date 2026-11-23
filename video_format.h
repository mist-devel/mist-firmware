/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VIDEO_FORMAT_H
#define VIDEO_FORMAT_H
#include <stdint.h>

#define VIDEO_FORMAT_RGB444 0
#define VIDEO_FORMAT_RGB555 1
#define VIDEO_WIDTH_MASK 0x7fffU
#define VIDEO_FORMAT_FLAG 0x8000U

unsigned video_stride(unsigned width, unsigned format);
int video_decode_line(const uint8_t *data, unsigned bytes, unsigned format, int raw,
                      unsigned pixels, uint8_t *canonical);
void video_put16(uint8_t *p, uint16_t value);
uint16_t video_le16(const uint8_t *p);
#endif
