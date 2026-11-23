#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H
#include <stdint.h>
#include "FatFs/ff.h"
#define FEAT_VIDEO_CAPTURE 0x00100000UL
#ifdef CONFIG_CHIP_SAMV71
void video_rom_clear(void);
void video_rom_loaded(FIL *file, const char *path);
const char *video_recorder_begin(uint32_t owner, uint32_t count, uint32_t period, uint16_t step);
void video_recorder_poll(void);
void video_recorder_stop(void);
int video_recorder_active(void);
uint32_t video_recorder_owner(void);
uint32_t video_recorder_done(void);
uint32_t video_recorder_total(void);
const char *video_recorder_error(void);
const char *video_recorder_dir(void);
uint32_t video_recorder_width(void);
uint32_t video_recorder_height(void);
uint32_t video_recorder_requested(void);
uint32_t video_recorder_actual(void);
uint32_t video_recorder_rows(void);
uint16_t video_recorder_step(void);
int video_output_path_in_use(const char *path);
int video_snapshot_supported(void);
const char *video_snapshot_request(void);
int video_snapshot_take_result(const char **path, const char **error);
unsigned video_stack_used(void);
#else
static inline void video_rom_clear(void) {}
static inline void video_rom_loaded(FIL *f, const char *p) {(void)f;(void)p;}
static inline int video_recorder_active(void) {return 0;}
static inline uint32_t video_recorder_owner(void) {return 0;}
static inline int video_snapshot_supported(void) {return 0;}
static inline const char *video_snapshot_request(void) {return "Snapshots are unavailable";}
static inline int video_snapshot_take_result(const char **path,const char **error)
{(void)path;(void)error;return 0;}
#endif
#endif
