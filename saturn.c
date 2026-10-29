// saturn.c
//
// MiST/SiDi128 virtual CD drive (CDD) for the Saturn core.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "saturn.h"
#include "cue_parser.h"
#include "user_io.h"
#include "utils.h"
#include "debug.h"
#include "idxfile.h"
#include "fat_compat.h"

#if 0
#define saturn_debugf(a, ...) iprintf("\033[1;35mSATURN: " a "\033[0m\n",## __VA_ARGS__)
#else
#define saturn_debugf(...)
#endif

// data_io_saturn.sv opcodes
#define CD_STAT_GET     0x60
#define CD_FRAME_SEND   0x67
#define CD_STAT_SEND    0x61
#define CD_COMMAND_GET  0x62
#define CD_DATA_SEND    0x64
#define CD_MODE_SEND    0x65
#define CD_BOOT_SEND    0x66

#define STB_IDLE        0x01
#define STB_COMMAND     0x04
#define STB_RESET       0x20
#define STB_OVERRUN     0x40
#define STB_SLOT_FREE   0x08
#define STB_PACED       0x10

// drive commands / status (saturn.h of MiSTer Main)
#define SATURN_COMM_NOP       0x00
#define SATURN_COMM_SEEK_RING 0x02
#define SATURN_COMM_TOC       0x03
#define SATURN_COMM_STOP      0x04
#define SATURN_COMM_READ      0x06
#define SATURN_COMM_PAUSE     0x08
#define SATURN_COMM_SEEK      0x09
#define SATURN_COMM_FW_SCAN   0x0A
#define SATURN_COMM_RW_SCAN   0x0B

#define SATURN_STAT_NOP       0x00
#define SATURN_STAT_TOC       0x06
#define SATURN_STAT_STOP      0x12
#define SATURN_STAT_SEEK      0x22
#define SATURN_STAT_AUDIO     0x34
#define SATURN_STAT_DATA      0x36
#define SATURN_STAT_IDLE      0x46
#define SATURN_STAT_OPEN      0x80
#define SATURN_STAT_NODISK    0x83
#define SATURN_STAT_SEEK_RING 0xB2

#define BCD(v) ((uint8_t)((((v)/10) << 4) | ((v)%10)))

// ------------------------------------------------------------------------------------------------
// TOC: the common CUE parser (cue_parser.c, built with CUE_MULTI_FILE on SiDi128) - one or several .bin files,
// INDEX positions kept per track. The data track and the CD-DA track being played stay open in the sd_image
// slots 2 and 3, which the Saturn core does not use otherwise.
// ------------------------------------------------------------------------------------------------
#ifndef CUE_MULTI_FILE
#error "saturn.c needs the multi-file CUE parser (CUE_MULTI_FILE)"
#endif

typedef enum { Idle, Open, ReadTOC, Read, Pause, Stop, Seek, SeekRead, SeekRing } satstate_t;

typedef struct { uint8_t m, s, f; } sat_msf_t;   // uint8_t as in MiSTer

static int toc_index_by_lba(int track, int lba)
{
	if (lba - toc.tracks[track].start < 0) return 0;
	int n = toc.tracks[track].index_num;
	if (n > CUE_MAX_IDX) n = CUE_MAX_IDX;
	int i = 2;
	while ((i < n) && (lba - toc.tracks[track].start >= toc.tracks[track].indexes[i])) i++;
	i--;
	return i;
}

// ------------------------------------------------------------------------------------------------
// drive (satcdd_t)
// ------------------------------------------------------------------------------------------------
static struct {
	int loaded;
	satstate_t state;
	int speed;
	int lba, track, index, seek_lba, toc_pos;
	char lid_open, stop_pend, seek_pend, read_pend, final_read, seek_ring, seek_ring2, pause_pend, read_toc;
	int seek_delay;
	int audioFirst;
	uint8_t stat[12];
	uint8_t comm[12];
} cdd;

// sector data: the shared sector_buffer (8 KB on SiDi128), used only inside saturn_poll() as megacd.c does
#define secbuf0 ((uint8_t*)sector_buffer)
#define secbuf1 ((uint8_t*)sector_buffer + 4096)
static int core_reset_pending = 0;

static void LBAToMSF(int lba, sat_msf_t *msf)
{
	msf->m = (lba / 75) / 60;
	msf->s = (lba / 75) % 60;
	msf->f = (lba % 75);
}

static void SetChecksum(uint8_t *stat)
{
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) sum += stat[i];
	stat[11] = ~sum;
}

static void set_stat(uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3, uint8_t s4, uint8_t s5,
	uint8_t s6, uint8_t s7, uint8_t s8, uint8_t s9, uint8_t s10)
{
	cdd.stat[0] = s0; cdd.stat[1] = s1; cdd.stat[2] = s2; cdd.stat[3] = s3; cdd.stat[4] = s4; cdd.stat[5] = s5;
	cdd.stat[6] = s6; cdd.stat[7] = s7; cdd.stat[8] = s8; cdd.stat[9] = s9; cdd.stat[10] = s10;
}

static void sat_reset(void)
{
	cdd.state = Open;
	cdd.lid_open = 1;
	cdd.stop_pend = cdd.seek_pend = cdd.read_pend = cdd.seek_ring = cdd.seek_ring2 = 0;
	cdd.pause_pend = cdd.read_toc = cdd.final_read = 0;
	cdd.track = cdd.index = 0;
	cdd.lba = cdd.seek_lba = 0;
	cdd.speed = 0;
	cdd.audioFirst = 0;
	set_stat(SATURN_STAT_OPEN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	SetChecksum(cdd.stat);
}

static void sat_unload(void)
{
	cue_close();
	cdd.loaded = 0;
	memset(&toc, 0, sizeof(toc));
}

static int CalcSeekDelay(int lba_old, int lba_new)
{
	const int track_old = cue_gettrackbylba(lba_old);
	const int track_new = cue_gettrackbylba(lba_new);
	const int min = 4, max = 13;
	int diff = lba_new - lba_old;
	int n = diff / 2000;
	if (abs(diff) <= 4) n = 2;
	else if (n <= min) n = min;
	else if (n > max) n = max;
	if (track_old != track_new) n += 27;
	else if (abs(diff) > 10000) n += 20;
	if (diff < -4) n += 2;
	return n;
}

static int GetSectorOffsetByIndex(int tno, int idx)
{
	int track = tno - 1;
	if (track < 0) track = 0;
	else if (track > toc.last) track = toc.last;
	if (idx > 99) idx = 99;
	if (idx <= 0) return 0;
	if (idx >= CUE_MAX_IDX) return 0;
	return toc.tracks[track].indexes[idx];
}

static int GetFAD(const uint8_t *cmd) { return cmd[3] | (cmd[2] << 8) | (cmd[1] << 16); }

static void CommandExec(void)
{
	int cmd_fad = GetFAD(cdd.comm);
	int cmd_idx = cdd.comm[4];
	int cmd_tno = cdd.comm[8];

	if (cmd_tno >= 1 && cmd_tno - 1 < toc.last && cmd_idx > toc.tracks[cmd_tno - 1].index_num) {
		cmd_idx -= toc.tracks[cmd_tno - 1].index_num;
		cmd_tno += 1;
		cmd_fad = toc.tracks[cmd_tno - 1].start + 150;
	}
	int fad = cmd_fad + GetSectorOffsetByIndex(cmd_tno, cmd_idx);

	switch (cdd.comm[0]) {
	case SATURN_COMM_NOP:
		break;

	case SATURN_COMM_SEEK_RING:
		cdd.seek_lba = fad - 150;
		cdd.track = cue_gettrackbylba(cdd.seek_lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.seek_lba);
		cdd.seek_ring = 1;
		cdd.read_pend = 0;
		cdd.pause_pend = 0;
		cdd.speed = cdd.comm[10] == 1 ? 1 : 2;
		break;

	case SATURN_COMM_TOC:
		cdd.toc_pos = 0;
		cdd.read_toc = 1;
		cdd.speed = cdd.comm[10] == 1 ? 1 : 2;
		break;

	case SATURN_COMM_STOP:
		cdd.stop_pend = 1;
		cdd.read_pend = 0;
		break;

	case SATURN_COMM_READ: {
		int lba_old = cdd.lba;
		cdd.seek_lba = fad - 150 - 4;
		cdd.lba = fad - 150 - 4;
		cdd.track = cue_gettrackbylba(cdd.seek_lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.seek_lba);
		cdd.read_pend = 1;
		cdd.seek_pend = 1;
		cdd.seek_delay = CalcSeekDelay(lba_old - 4, fad - 150);
		cdd.pause_pend = 0;
		cdd.speed = cdd.comm[10] == 1 ? 1 : 2;
		cdd.audioFirst = 1;
		saturn_debugf("read fad %d track %d", fad, cdd.track + 1);
		}
		break;

	case SATURN_COMM_PAUSE:
		cdd.pause_pend = 1;
		cdd.seek_pend = 0;
		cdd.read_pend = 0;
		break;

	case SATURN_COMM_SEEK: {
		int lba_old = cdd.lba;
		cdd.seek_lba = fad - 150;
		cdd.lba = fad - 150;
		cdd.track = cue_gettrackbylba(cdd.seek_lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.seek_lba);
		cdd.seek_pend = 1;
		cdd.seek_delay = CalcSeekDelay(lba_old - 4, fad - 150);
		cdd.final_read = cdd.read_pend;
		cdd.read_pend = 0;
		cdd.pause_pend = 0;
		cdd.speed = cdd.comm[10] == 1 ? 1 : 2;
		}
		break;

	default:
		cdd.seek_pend = 0;
		cdd.read_pend = 0;
		cdd.pause_pend = 0;
		break;
	}
}

static void Process(uint8_t *time_mode)
{
	static int seek_ring_time = 1;
	sat_msf_t amsf = { 0, 2, 0 }, msf = { 0, 2, 0 };
	uint8_t idx = 0;
	uint8_t q = (cdd.lba < toc.end && toc.tracks[cdd.track].type) ? 0x40 : 0x00;

	if (cdd.lid_open) {
		cdd.state = Open;
		set_stat(SATURN_STAT_OPEN, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0);
		*time_mode = 1;
	}
	else if (cdd.read_toc) {
		cdd.state = ReadTOC;
		if (cdd.toc_pos < 0x100) {
			LBAToMSF(toc.tracks[cdd.toc_pos].start + 150, &msf);
			idx = BCD(cdd.toc_pos + 1);
			q = toc.tracks[cdd.toc_pos & 0xFF].type ? 0x40 : 0x00;
			cdd.toc_pos++;
			if (cdd.toc_pos >= toc.last) cdd.toc_pos = 0x100;
		} else {
			if (cdd.toc_pos == 0x100) {          // A0
				msf.m = 1; msf.s = 0; msf.f = 0;
				idx = 0xA0;
				q = toc.tracks[0].type ? 0x40 : 0x00;
			} else if (cdd.toc_pos == 0x101) {   // A1
				msf.m = toc.last; msf.s = 0; msf.f = 0;
				idx = 0xA1;
				q = toc.tracks[toc.last - 1].type ? 0x40 : 0x00;
			} else if (cdd.toc_pos == 0x102) {   // A2
				LBAToMSF(toc.end + 150, &msf);
				idx = 0xA2;
				q = 0x00;
				cdd.read_toc = 0;
			}
			cdd.toc_pos++;
		}
		set_stat(SATURN_STAT_TOC, q | 0x01, 0x00, idx, 0, 0, 0, 0, BCD(msf.m), BCD(msf.s), BCD(msf.f));
		*time_mode = 1;
	}
	else if (cdd.seek_pend) {
		cdd.state = cdd.final_read ? Read : Seek;
		LBAToMSF(cdd.lba + 150, &amsf);
		if (cdd.lba < 0) LBAToMSF(-cdd.lba, &msf);
		else LBAToMSF(abs(cdd.lba - toc.tracks[cdd.track].start), &msf);
		set_stat(SATURN_STAT_SEEK, q | 0x01, cdd.lba < toc.end + 150 ? BCD(cdd.track + 1) : 0xAA,
			cdd.lba < 0 ? 0x00 : BCD(cdd.index), BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x00,
			BCD(amsf.m), BCD(amsf.s), BCD(amsf.f));
		cdd.seek_delay--;
		if (cdd.seek_delay <= 0) { cdd.seek_delay = 0; cdd.seek_pend = 0; }
		cdd.final_read = 0;
		*time_mode = 1;
	}
	else if (cdd.seek_ring) {
		cdd.state = SeekRing;
		int fad = cdd.lba + 150;
		set_stat(SATURN_STAT_SEEK_RING, 0, 0, fad >> 16, fad >> 8, fad, 0, 0, 0, 0, 0);
		if (seek_ring_time) seek_ring_time--;
		else { cdd.seek_ring = 0; cdd.seek_ring2 = 1; seek_ring_time = 1; }
		*time_mode = 1;
	}
	else if (cdd.seek_ring2) {
		cdd.state = SeekRing;
		int fad = cdd.lba + 150;
		set_stat(SATURN_STAT_SEEK_RING | 0x04, 0x48, 0x5A, fad >> 16, fad >> 8, fad, 0x0A, 0x0A, 0x09, 0x09, 0x00);
		if (seek_ring_time) seek_ring_time--;
		else { cdd.seek_ring2 = 0; seek_ring_time = 1; }
		*time_mode = 1;
	}
	else if (cdd.read_pend) {
		cdd.state = Read;
		LBAToMSF(cdd.lba + 150, &amsf);
		if (cdd.lba < 0) LBAToMSF(-cdd.lba, &msf);
		else LBAToMSF(abs(cdd.lba - toc.tracks[cdd.track].start), &msf);
		set_stat(SATURN_STAT_DATA, q | 0x01, cdd.lba < toc.end + 150 ? BCD(cdd.track + 1) : 0xAA,
			cdd.lba < 0 ? 0x00 : BCD(cdd.index), BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x00,
			BCD(amsf.m), BCD(amsf.s), BCD(amsf.f));
		*time_mode = cdd.speed;
	}
	else if (cdd.pause_pend) {
		cdd.state = Idle;
		LBAToMSF(cdd.lba + 150, &amsf);
		LBAToMSF(abs(cdd.lba - toc.tracks[cdd.track].start) + 150, &msf);
		set_stat(SATURN_STAT_IDLE, q | 0x01, cdd.lba < toc.end + 150 ? BCD(cdd.track + 1) : 0xAA,
			cdd.lba < 0 ? 0x00 : BCD(cdd.index), BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x00,
			BCD(amsf.m), BCD(amsf.s), BCD(amsf.f));
		cdd.pause_pend = 0;
		*time_mode = 1;
	}
	else if (cdd.stop_pend) {
		cdd.state = Stop;
		set_stat(SATURN_STAT_STOP, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0);
		cdd.stop_pend = 0;
		*time_mode = 1;
	}
	else {
		cdd.state = Idle;
		LBAToMSF(cdd.lba + 150, &amsf);
		LBAToMSF(abs(cdd.lba - toc.tracks[cdd.track].start) + 150, &msf);
		set_stat(SATURN_STAT_IDLE, q | 0x01, cdd.lba < toc.end + 150 ? BCD(cdd.track + 1) : 0xAA,
			cdd.lba < 0 ? 0x00 : BCD(cdd.index), BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x00,
			BCD(amsf.m), BCD(amsf.s), BCD(amsf.f));
		*time_mode = 1;
	}
	SetChecksum(cdd.stat);
}

// ---- sector transport ----
static void send_buffer(uint8_t bn, const uint8_t *data)
{
	EnableFpga();
	SPI(CD_DATA_SEND);
	SPI(bn & 3);
	spi_write((const char*)data, 2352);
	DisableFpga();
}

static void send_mode(uint16_t mode)
{
	EnableFpga();
	SPI(CD_MODE_SEND);
	SPI(mode & 0xFF);
	SPI(mode >> 8);
	DisableFpga();
}

static int read_file(int t, int offs, uint8_t *buf, int len)
{
	UINT br = 0;
	FIL *f = cue_track_file(t);
	if (!f || offs < 0) { memset(buf, 0, len); return 0; }
	DISKLED_ON
	f_lseek(f, offs);
	f_read(f, buf, len, &br);
	DISKLED_OFF
	if ((int)br < len) memset(buf + br, 0, len - br);
	return br;
}

static void ReadData(uint8_t *buf)
{
	cd_track_t *t = &toc.tracks[cdd.track];
	if (t->type) {
		int lba_ = cdd.lba >= 0 ? cdd.lba : 0;
		int offs = (lba_ - t->start) * t->sector_size + t->offset;
		if (t->sector_size == 2048) read_file(cdd.track, offs, buf + 16, 2048);
		else                        read_file(cdd.track, offs, buf, 2352);
	}
}

static uint32_t crc_tab[256];
static int crc_init = 0;
static uint32_t DataSectorCalcCRC(const uint8_t *buf, int len)
{
	if (!crc_init) {
		for (int i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xD8018001 : 0);
			crc_tab[i] = c;
		}
		crc_init = 1;
	}
	uint32_t crc = 0;
	for (int i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 8) ^ crc_tab[crc & 0xFF];
	}
	return crc;
}

static void DataSectorSend(const uint8_t *header, int speed)
{
	static int buf_num_read = 0, buf_num_write = 0;
	uint8_t *d = secbuf0;

	ReadData(d);
	if (header) memcpy(d, header, 16);
	uint8_t sec_mode = d[15];
	uint32_t crc = DataSectorCalcCRC(d, (sec_mode == 2 ? 2348 : 2064));
	if (sec_mode != 0x02) {
		d[2064] = crc >> 0; d[2065] = crc >> 8; d[2066] = crc >> 16; d[2067] = crc >> 24;
		memset(d + 2068, 0, 2352 - 2068);
	}
	int boot = (d[12] == 0x00 && d[13] == 0x02 && d[14] == 0x00 && d[15] == 0x01);

	send_buffer(buf_num_write, d);
	buf_num_write = (buf_num_write + 1) & 3;

	uint16_t mode = (speed == 2 ? 0x0101 : 0x0000) | (boot ? 0x0808 : 0x0000) | (buf_num_read << 4) | (buf_num_read << 12);
	buf_num_read = (buf_num_read + 1) & 3;
	send_mode(mode);
}

static void MakeSecureRingData(uint8_t *buf)
{
	uint16_t lfsr = 1;
	for (int i = 12; i < 2348; i++) {
		uint8_t a = (i & 1) ? 0x59 : 0xa8;
		for (int j = 0; j < 8; j++) {
			a ^= (lfsr & 1);
			a = (a >> 1) | (a << 7);
			uint16_t x = (lfsr >> 1) ^ lfsr;
			lfsr |= x << 15;
			lfsr >>= 1;
		}
		buf[i] = a;
	}
}

static void RingDataSend(const uint8_t *header, int speed)
{
	uint8_t *d = secbuf0;
	memset(d, 0xFF, 12); d[0] = d[11] = 0x00;
	MakeSecureRingData(d);
	memcpy(d + 12, header, 12);
	memset(d + 2348, 0, 4);
	send_buffer(0, d);
	send_mode((speed == 2 ? 0x0101 : 0x0000) | 0x0404);
}

static void AudioSectorSend(int first)
{
	static int buf_num_read = 0, buf_num_write = 0;
	if (first) buf_num_read = buf_num_write = 0;

	cd_track_t *t = &toc.tracks[cdd.track];
	int offs = (cdd.lba - t->start) * 2352 + t->offset;
	if (first) {
		read_file(cdd.track, offs, secbuf0, 2352);
		read_file(cdd.track, offs + 2352, secbuf1, 2352);
		send_buffer(buf_num_write, secbuf0);
		send_buffer(buf_num_write + 1, secbuf1);
		buf_num_write++;
	} else {
		read_file(cdd.track, offs + 2352, secbuf0, 2352);
		send_buffer(buf_num_write, secbuf0);
	}
	buf_num_write = (buf_num_write + 1) & 3;

	uint16_t mode = 0x0202 | (buf_num_read << 4) | (buf_num_read << 12);
	buf_num_read = (buf_num_read + 1) & 3;
	send_mode(mode);
}

static void Update(void)
{
	sat_msf_t msf;

	switch (cdd.state) {
	case Idle:
		cdd.lba++;
		if (cdd.lba > cdd.seek_lba + 4) cdd.lba -= 4;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.lba);
		break;

	case Open:
	case ReadTOC:
		break;

	case Read:
		LBAToMSF(cdd.lba + 150, &msf);
		if (cdd.lba >= toc.end) {
			uint8_t header[12] = { BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x02, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x28, 0x00 };
			RingDataSend(header, cdd.speed);
		}
		else if (toc.tracks[cdd.track].type) {
			if (toc.tracks[cdd.track].sector_size == 2048 || (cdd.lba - toc.tracks[cdd.track].start) < 0) {
				uint8_t header[16] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
					BCD(msf.m), BCD(msf.s), BCD(msf.f), 0x01 };
				DataSectorSend(header, cdd.speed);
			} else {
				DataSectorSend(NULL, cdd.speed);
			}
		}
		else {
			AudioSectorSend(cdd.audioFirst);
			cdd.audioFirst = 0;
		}
		cdd.lba++;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.lba);
		cdd.seek_lba = cdd.lba;
		break;

	case Pause:
	case Stop:
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.lba);
		break;

	case Seek:
	case SeekRead:
		if (!cdd.seek_pend) cdd.lba = cdd.seek_lba;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.lba);
		break;

	case SeekRing:
		cdd.lba = cdd.seek_lba;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.index = toc_index_by_lba(cdd.track, cdd.lba);
		break;
	}
}

// ------------------------------------------------------------------------------------------------
// MiST interface
// ------------------------------------------------------------------------------------------------
static uint32_t CalcTimerOffset(uint8_t speed)
{
	static uint8_t adj0 = 1, adj1 = 1, adj2 = 1;
	uint32_t offs;
	if (speed == 2) {
		offs = 6 + ((adj2 & 0x03) != 0x00 ? 1 : 0);     // 6.6 ms
		adj2 <<= 1; if (adj2 >= 0x08) adj2 = 0x01;
		adj0 = adj1 = 0x1;
	} else if (speed == 1) {
		offs = 13 + ((adj1 & 0x01) != 0x00 ? 1 : 0);    // 13.3 ms
		adj1 <<= 1; if (adj1 >= 0x08) adj1 = 0x01;
		adj0 = adj2 = 0x1;
	} else {
		offs = 16 + ((adj0 & 0x03) != 0x00 ? 1 : 0);    // 16.7 ms
		adj0 <<= 1; if (adj0 >= 0x08) adj0 = 0x01;
		adj1 = adj2 = 0x1;
	}
	return offs;
}

static char initialised = 0;

// Drive frames run on an absolute 1 ms timeline
static unsigned long sat_overruns = 0;

void saturn_poll(void)
{
	static unsigned long due = 0, last = 0;
	static char started = 0;
	static uint8_t time_mode = 1;

	if (!initialised) { sat_reset(); initialised = 1; }

	if (core_reset_pending) {
		// new disc: hold the core in reset for a moment
		user_io_8bit_set_status(0, 1);
		core_reset_pending = 0;
	}

	uint8_t st;
	EnableFpga();
	SPI(CD_STAT_GET);
	st = SPI(0);
	DisableFpga();

	if (st & STB_PACED) {
		if (!(st & STB_SLOT_FREE)) return;
		if (st & STB_OVERRUN) {
			sat_overruns++;
			iprintf("Saturn: sector data overrun in the core (%lu)\n", sat_overruns);
		}
		if (st & STB_COMMAND) {
			EnableFpga();
			SPI(CD_COMMAND_GET);
			for (int i = 0; i < 12; i++) cdd.comm[i] = SPI(0);
			DisableFpga();
			CommandExec();
		}
		Process(&time_mode);
		Update();
		EnableFpga();
		SPI(CD_FRAME_SEND);
		for (int i = 0; i < 12; i++) SPI(cdd.stat[i]);
		SPI(time_mode);
		DisableFpga();
		return;
	}

	unsigned long now = GetTimer(0);
	if (started) {
		long late = (long)(now - due);
		if (late < 0) return;
		if ((long)(now - last) < 2) return;
		long cap = time_mode == 2 ? 13 : time_mode == 1 ? 27 : 33;
		if (late > cap) due = now;
	} else {
		due = now;
		started = 1;
	}
	last = now;

	if (st & STB_OVERRUN) {
		sat_overruns++;
		iprintf("Saturn: sector data overrun in the core (%lu)\n", sat_overruns);
	}

	if (st & STB_COMMAND) {
		EnableFpga();
		SPI(CD_COMMAND_GET);
		for (int i = 0; i < 12; i++) cdd.comm[i] = SPI(0);
		DisableFpga();
		CommandExec();
	}

	Process(&time_mode);
	due += CalcTimerOffset(time_mode);

	EnableFpga();
	SPI(CD_STAT_SEND);
	for (int i = 0; i < 12; i++) SPI(cdd.stat[i]);
	DisableFpga();

	Update();
}

void saturn_init(void)
{
	sat_unload();
	sat_reset();
	initialised = 1;
	core_reset_pending = 0;
}

// boot header (satcdd_t::GetBootHeader) -> core, for the Auto region; zeros when there is no disc
static void send_boot_header(void)
{
	uint8_t *h = (uint8_t*)sector_buffer;
	memset(h, 0, 256);
	if (cdd.loaded) {
		FIL *f = cue_track_file(0);
		UINT br;
		if (f) {
			f_lseek(f, toc.tracks[0].offset + (toc.tracks[0].sector_size == 2048 ? 0 : 16));
			f_read(f, h, 256, &br);
		}
	}
	EnableFpga();
	SPI(CD_BOOT_SEND);
	spi_write((const char*)h, 256);
	DisableFpga();
}

char saturn_cue_mounted(void)
{
	return cdd.loaded ? 1 : 0;
}

// <cue name>.sav in the CUE's directory
static void sat_mount_sav(const char *name)
{
	static char savname[256];
	static FIL f;
	UINT bw;

	if (!name || !*name) {
		user_io_file_mount(NULL, 0);
		return;
	}

	strncpy(savname, name, sizeof(savname) - 5);
	savname[sizeof(savname) - 5] = 0;
	char *dot = strrchr(savname, '.');
	char *slash = strrchr(savname, '/');
	if (dot && (!slash || dot > slash)) *dot = 0;
	strcat(savname, ".sav");

	if (f_open(&f, savname, FA_CREATE_NEW | FA_WRITE) == FR_OK) {
		static const char fmt[] = "BackUpRam Format";
		uint8_t *b = (uint8_t*)sector_buffer;
		int ok = 1;
		for (int blk = 0; blk < 128 && ok; blk++) {
			for (int i = 0; i < 256; i++) {
				int n = blk * 256 + i;
				b[2 * i] = 0xFF;
				b[2 * i + 1] = (n < 64) ? (uint8_t)fmt[n & 15] : 0x00;
			}
			if (f_write(&f, b, 512, &bw) != FR_OK || bw != 512) ok = 0;
		}
		f_close(&f);
		iprintf("Saturn: created %s%s\n", savname, ok ? "" : " (write error)");
	}

	user_io_file_mount((const unsigned char*)savname, 0);
	iprintf("Saturn: backup RAM <- %s\n", savname);
}

char saturn_cue_mount(const char *name)
{
	char res = CUE_RES_OK;
	if (!initialised) { sat_reset(); initialised = 1; }

	sat_unload();
	sat_reset();
	if (name && *name) {
		cue_set_aux_image(&sd_image[2]);
		cue_set_multi_file(1);
		res = cue_parse(name, &sd_image[3]);
		if (res == CUE_RES_OK) {
			if (toc.last < 100) toc.tracks[toc.last].start = toc.end;   // CommandExec may look one track past the last
			cdd.loaded = 1;
			cdd.lid_open = 0;
			cdd.stop_pend = 1;
			// reset the console, as MiSTer does when a disc is inserted
			user_io_8bit_set_status(1, 1);
			core_reset_pending = 1;
			sat_mount_sav(name);
		} else {
			sat_unload();
			sat_reset();
			sat_mount_sav(NULL);
		}
	} else {
		sat_mount_sav(NULL);
	}
	send_boot_header();
	iprintf("Saturn: %s (%d tracks)\n", cdd.loaded ? "disc inserted" : "no disc", cdd.loaded ? toc.last : 0);
	return res;
}
