#ifndef CAB_FORMAT_H
#define CAB_FORMAT_H
#include <stdint.h>

typedef struct { uint32_t frames, events, crc, dumps; uint8_t version, record_bytes; } cab_header_t;
uint32_t cab_le32(const uint8_t *p);
uint32_t cab_crc32(uint32_t crc, const uint8_t *p, unsigned count);
int cab_decode_header(const uint8_t raw[32], uint64_t size, cab_header_t *header);
int cab_valid_record(const uint8_t *raw, unsigned version);
uint32_t cab_parse_dump_length(const char *item);
#endif
