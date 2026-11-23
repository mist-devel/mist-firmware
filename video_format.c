/* SPDX-License-Identifier: GPL-3.0-or-later
 * RGB444/RGB555 native line decoding. */
#include "video_format.h"
#include <string.h>

unsigned video_stride(unsigned width, unsigned format)
{
    return format==VIDEO_FORMAT_RGB555 ? width*2U : (width*3U+1U)>>1;
}

void video_put16(uint8_t *p, uint16_t value)
{
    p[0]=(uint8_t)value;
    p[1]=(uint8_t)(value>>8);
}

uint16_t video_le16(const uint8_t *p)
{
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

static void pixel_out444(uint8_t *p,unsigned x,unsigned color)
{
    unsigned at=(x>>1)*3;
    if(!(x&1)) {
        p[at]=(uint8_t)(color>>4);
        p[at+1]=(uint8_t)(color<<4);
    } else {
        p[at+1]|=(uint8_t)(color>>8);
        p[at+2]=(uint8_t)color;
    }
}

static int decode444(const uint8_t *data,unsigned bytes,int raw,unsigned pixels,uint8_t *packed)
{
    unsigned stride=video_stride(pixels,VIDEO_FORMAT_RGB444);
    memset(packed,0,stride);
    if(raw) {
        if(bytes!=stride) return 0;
        if((pixels&1) && (data[bytes-1]&15)) return 0;
        memcpy(packed,data,bytes);
        return 1;
    }
    if(bytes&1) return 0;
    unsigned x=0,offset=0;
    while(offset<bytes) {
        unsigned word=video_le16(data+offset);
        unsigned count=(word>>12)+1;
        offset+=2;
        if(count==16) {
            if(offset+2>bytes) return 0;
            count=16+video_le16(data+offset);
            offset+=2;
        }
        if(count>pixels-x) return 0;
        while(count--) pixel_out444(packed,x++,word&0x0fff);
    }
    return x==pixels;
}

static int decode555(const uint8_t *data,unsigned bytes,int is_raw,unsigned pixels,uint8_t *raw)
{
    unsigned stride=pixels*2U;
    if(is_raw) {
        if(bytes!=stride) return 0;
        for(unsigned i=0;i<pixels;i++) if(data[2*i+1]&0x80) return 0;
        memcpy(raw,data,bytes);
        return 1;
    }
    if(bytes&1) return 0;
    unsigned x=0,offset=0;
    while(offset<bytes) {
        unsigned token=video_le16(data+offset);
        offset+=2;
        unsigned count=1;
        if(token&0x8000) {
            if(offset+2>bytes) return 0;
            count=video_le16(data+offset)+2U;
            offset+=2;
        }
        if(count>pixels-x) return 0;
        uint16_t pixel=(uint16_t)(token&0x7fff);
        for(unsigned i=0;i<count;i++) {
            raw[2*x]=(uint8_t)pixel;
            raw[2*x+1]=(uint8_t)(pixel>>8);
            x++;
        }
    }
    return x==pixels;
}

int video_decode_line(const uint8_t *data,unsigned bytes,unsigned format,int raw,
                      unsigned pixels,uint8_t *canonical)
{
    if(!pixels || pixels>512 || !data || !canonical) return 0;
    if(format==VIDEO_FORMAT_RGB555) return decode555(data,bytes,raw,pixels,canonical);
    if(format==VIDEO_FORMAT_RGB444) return decode444(data,bytes,raw,pixels,canonical);
    return 0;
}
