#ifndef VIDEO_RECORDER_TEST_SUPPORT_H
#define VIDEO_RECORDER_TEST_SUPPORT_H
#include <stdint.h>
#include "FatFs/ff.h"

extern unsigned char sector_buffer[8192];
uint8_t video_test_spi(uint8_t value);
void spi_uio_cmd_cont(unsigned command);
void DisableIO(void);
uint32_t GetRTTC(void);
uint32_t user_io_get_core_features(void);
int user_io_osd_is_visible(void);
uint32_t remote_input_owner(void);
int remote_input_set(uint32_t owner,uint32_t mask0,uint32_t mask1,uint32_t now);
int remote_input_keepalive(uint32_t owner,uint32_t now);
void remote_input_release(uint32_t owner);
int cab_player_active(void);
uint32_t cab_player_dump_total(void);
uint32_t cab_player_frames(void);
const char *CurrentDirectoryName(void);
unsigned video_stack_used(void);
int8_t fat_medium_present(void);
uint8_t usb_storage_is_configured(void);
int user_io_raw_sd_in_use(void);
#define SPI(value) video_test_spi((uint8_t)(value))
#endif
