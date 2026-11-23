#include "cab_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	uint8_t h[32]={'J','T','C','A','B','0','1',0,1,0,0,0,5,0,0,0,1,0,0,0};
	cab_header_t parsed;
	h[28]=32;
	assert(cab_decode_header(h,40,&parsed) && parsed.frames==5 && parsed.events==1);
	assert(!cab_decode_header(h,39,&parsed));
	h[20]=1; assert(!cab_decode_header(h,40,&parsed)); h[20]=0;
	h[12]=0; assert(!cab_decode_header(h,40,&parsed)); h[12]=5;
	h[16]=0; assert(!cab_decode_header(h,32,&parsed)); h[16]=1;
	h[8]=2; assert(!cab_decode_header(h,40,&parsed));
	assert((cab_crc32(0xffffffffUL,(const uint8_t*)"123456789",9)^0xffffffffUL)==0xcbf43926UL);
	uint8_t record[8]={1,0,0,0,0,0,0,0};
	assert(cab_valid_record(record,1));
	record[0]=0; assert(!cab_valid_record(record,1)); record[0]=1;
	record[6]=0x20; assert(!cab_valid_record(record,1)); record[6]=0x10;
	assert(cab_valid_record(record,1));
	h[6]='2'; h[8]=2;
	assert(cab_decode_header(h,44,&parsed) && parsed.version==2 && parsed.record_bytes==12);
	assert(!cab_decode_header(h,40,&parsed));
	h[6]='1'; assert(!cab_decode_header(h,44,&parsed)); h[6]='2';
	h[8]=4; assert(!cab_decode_header(h,44,&parsed)); h[8]=2;
	h[16]=6; assert(!cab_decode_header(h,104,&parsed)); h[16]=1;
	uint8_t dip_record[12]={1,0,0,0,0,0,0x20,0,0xff,0xff,0xff,0xff};
	assert(cab_valid_record(dip_record,2));
	assert(!cab_valid_record(dip_record,1));
	dip_record[6]=0x40; assert(!cab_valid_record(dip_record,2));
	assert(!cab_valid_record(dip_record,4));
	h[6]='3'; h[8]=3; h[20]=5;
	assert(cab_decode_header(h,44,&parsed) && parsed.dumps==5);
	h[20]=0; assert(!cab_decode_header(h,44,&parsed));
	h[20]=6; assert(!cab_decode_header(h,44,&parsed)); h[20]=5;
	dip_record[6]=0x40;
	assert(cab_valid_record(dip_record,3) && !cab_valid_record(dip_record,2));
	dip_record[6]=0x80; assert(!cab_valid_record(dip_record,3));
	assert(cab_parse_dump_length("R2048,Save NVRAM")==2048);
	assert(cab_parse_dump_length("R0x1234,Save NVRAM")==0x1234);
	assert(!cab_parse_dump_length("R0,Save NVRAM"));
	assert(!cab_parse_dump_length("R-1,Save NVRAM"));
	assert(!cab_parse_dump_length("R12garbage,Save NVRAM"));
	assert(!cab_parse_dump_length("R99999999999999999999999,x"));
	assert(!cab_parse_dump_length("R16777217,x"));
	assert(!cab_parse_dump_length("R2048"));
	assert(!cab_parse_dump_length(NULL));
	puts("CAB packed format: header/length/version/flags/CRC/record bounds PASS");
	return 0;
}
