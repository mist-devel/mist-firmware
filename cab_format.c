#include "cab_format.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

uint32_t cab_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

uint32_t cab_crc32(uint32_t crc, const uint8_t *p, unsigned count)
{
	while (count--) {
		crc ^= *p++;
		for (unsigned bit=0; bit<8; bit++) crc = (crc>>1) ^ ((crc&1) ? 0xedb88320UL : 0);
	}
	return crc;
}

int cab_decode_header(const uint8_t raw[32], uint64_t size, cab_header_t *header)
{
	uint32_t version=cab_le32(raw+8);
	if ((version<1 || version>3) || memcmp(raw,version==3 ? "JTCAB03\0" : version==2 ? "JTCAB02\0" : "JTCAB01\0",8) ||
	    (version<3 && cab_le32(raw+20)!=0) || cab_le32(raw+28)!=32) return 0;
	header->frames=cab_le32(raw+12);
	header->events=cab_le32(raw+16);
	header->crc=cab_le32(raw+24);
	header->dumps=cab_le32(raw+20);
	header->version=version;
	header->record_bytes=version>1 ? 12 : 8;
	return header->frames && header->frames<=0x7fffffff && header->events &&
	       header->events<=header->frames && (version<3 || (header->dumps && header->dumps<=header->frames)) &&
	       size==32ULL+(uint64_t)header->record_bytes*header->events;
}

int cab_valid_record(const uint8_t *raw, unsigned version)
{
	uint32_t mask=version==3 ? 0x007fffffUL : version==2 ? 0x003fffffUL : 0x001fffffUL;
	return version>=1 && version<=3 && cab_le32(raw)!=0 && !(cab_le32(raw+4)&~mask);
}

uint32_t cab_parse_dump_length(const char *item)
{
	char *end;
	if (!item || item[0]!='R' || !isdigit((unsigned char)item[1])) return 0;
	errno=0;
	unsigned long bytes=strtoul(item+1,&end,0);
	return !errno && *end==',' && bytes && bytes<=0x1000000UL ? bytes : 0;
}
