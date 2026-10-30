#include "video_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t pixels[1024], raw444[]={0x12,0x34,0x56,0x78,0x90};
    assert(video_stride(1,VIDEO_FORMAT_RGB444)==2);
    assert(video_stride(3,VIDEO_FORMAT_RGB444)==5);
    assert(video_stride(512,VIDEO_FORMAT_RGB555)==1024);
    assert(video_decode_line(raw444,5,VIDEO_FORMAT_RGB444,1,3,pixels));
    assert(!memcmp(raw444,pixels,5));
    raw444[4]=0x91;
    assert(!video_decode_line(raw444,5,VIDEO_FORMAT_RGB444,1,3,pixels));
    raw444[4]=0x90;
    assert(!video_decode_line(raw444,4,VIDEO_FORMAT_RGB444,1,3,pixels));

    uint8_t rle444[]={0xbc,0xea,0x23,0xf1,0,0};
    assert(video_decode_line(rle444,6,VIDEO_FORMAT_RGB444,0,31,pixels));
    assert(pixels[0]==0xab && pixels[1]==0xca && pixels[2]==0xbc);
    assert(!video_decode_line(rle444,6,VIDEO_FORMAT_RGB444,0,30,pixels));
    assert(!video_decode_line(rle444,4,VIDEO_FORMAT_RGB444,0,31,pixels));
    assert(!video_decode_line(rle444,5,VIDEO_FORMAT_RGB444,0,31,pixels));
    uint8_t run512_444[]={0x23,0xf1,0xf0,0x01};
    assert(video_decode_line(run512_444,4,VIDEO_FORMAT_RGB444,0,512,pixels));
    assert(!video_decode_line(run512_444,4,VIDEO_FORMAT_RGB444,0,511,pixels));

    uint8_t raw555[]={0x01,0x00,0xff,0x7f,0x35,0x42};
    assert(video_decode_line(raw555,sizeof(raw555),VIDEO_FORMAT_RGB555,1,3,pixels));
    assert(!memcmp(raw555,pixels,sizeof(raw555)));
    raw555[3]|=0x80;
    assert(!video_decode_line(raw555,sizeof(raw555),VIDEO_FORMAT_RGB555,1,3,pixels));
    assert(!video_decode_line(raw555,4,VIDEO_FORMAT_RGB555,1,3,pixels));

    // 555 RLE: two distinct literals, a 4-pixel run, then a low-bit variant.
    uint8_t rle555[]={0x01,0x00, 0x02,0x00, 0x34,0x92, 0x02,0x00, 0x35,0x12};
    assert(video_decode_line(rle555,sizeof(rle555),VIDEO_FORMAT_RGB555,0,7,pixels));
    uint16_t expected[]={1,2,0x1234,0x1234,0x1234,0x1234,0x1235};
    for(unsigned i=0;i<7;i++) assert(video_le16(pixels+2*i)==expected[i]);
    assert(!video_decode_line(rle555,8,VIDEO_FORMAT_RGB555,0,7,pixels));
    uint8_t overrun[]={0x01,0x80,0xff,0xff};
    assert(!video_decode_line(overrun,sizeof(overrun),VIDEO_FORMAT_RGB555,0,2,pixels));
    uint8_t short_repeat[]={0x01,0x80,0x00};
    assert(!video_decode_line(short_repeat,sizeof(short_repeat),VIDEO_FORMAT_RGB555,0,2,pixels));
    uint8_t short_pixels[]={0x01,0x00};
    assert(!video_decode_line(short_pixels,sizeof(short_pixels),VIDEO_FORMAT_RGB555,0,2,pixels));
    assert(!video_decode_line(short_pixels,sizeof(short_pixels),99,1,1,pixels));

    uint8_t raw_max[1024];
    for(unsigned i=0;i<512;i++) {
        uint16_t p=(uint16_t)((i*37+13)&0x7fff);
        video_put16(raw_max+2*i,p);
    }
    assert(video_decode_line(raw_max,sizeof(raw_max),VIDEO_FORMAT_RGB555,1,512,pixels));
    assert(!memcmp(raw_max,pixels,sizeof(raw_max)));
    puts("video format: RGB444/RGB555 raw, RLE, malformed rows and 1024-byte limit PASS");
    return 0;
}
