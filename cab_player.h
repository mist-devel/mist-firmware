#ifndef CAB_PLAYER_H
#define CAB_PLAYER_H
#include <stdint.h>
#include "FatFs/ff.h"

#define FEAT_CAB_STREAM 0x00020000UL
#define FEAT_CAB_DIPS   0x00040000UL
#define FEAT_CAB_DUMP   0x00080000UL

typedef struct {
	uint32_t epoch, frames, accepted, applied, raster, inputs;
	uint32_t dipsw;
	uint32_t dump_seq, dump_frame, dump_raster;
	uint8_t state, error, queued, credits, dipsw_valid;
} cab_fpga_status_t;

const char *cab_player_begin(const char *path, uint32_t owner);
void cab_player_poll(void);
int cab_player_stop(void);
int cab_player_active(void);
int cab_player_file_in_use(const FIL *file);
const cab_fpga_status_t *cab_player_status(void);
const char *cab_player_error(void);
uint32_t cab_player_frames(void);
uint32_t cab_player_dump_bytes(void);
uint32_t cab_player_dump_total(void);
uint32_t cab_player_dumps_done(void);
uint32_t cab_player_dump_written(void);
const char *cab_player_dump_dir(void);
int cab_player_output_in_use(const FIL *file);
int cab_player_output_path_in_use(const char *path);
#endif
