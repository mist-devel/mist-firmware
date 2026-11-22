// p3do.c
//
// MiST/SiDi128 virtual CD drive for the 3DO_SiDi128 core.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "p3do.h"
#include "cue_parser.h"
#include "user_io.h"
#include "utils.h"
#include "debug.h"
#include "idxfile.h"
#include "fat_compat.h"

#if 0
#define p3do_debugf(a, ...) iprintf("\033[1;35m3DO : " a "\033[0m\n",## __VA_ARGS__)
#else
#define p3do_debugf(...)
#endif

#define CD_STAT_GET     0x60
#define CD_COMMAND_GET  0x62
#define CD_DATA_SEND    0x64
#define CD_INFO_SEND    0x65

#define STB_COMMAND     0x01
#define STB_RESET       0x02

#define P3DO_COMM_NOP   0x00
#define P3DO_COMM_READ  0x10
#define P3DO_COMM_NEXT  0x20

static struct {
	int loaded;
	int lba;
	int track;
	int read_pend;
	int block_reads;
	int speed;
	uint8_t comm[8];
} cdd;

#define cd_buf   ((uint8_t*)sector_buffer)
#define info_buf ((uint8_t*)sector_buffer + 4096)
#define CD_BUF_SIZE (2352 + 16)
#define INFO_BUF_SIZE 2048

static void SendBuffer(unsigned char opcode, const uint8_t *buf, int len)
{
	EnableFpga();
	SPI(opcode);
	spi_write((char*)buf, len);
	DisableFpga();
}

static void LBAToMSFbin(int lba, uint8_t *m, uint8_t *s, uint8_t *f)
{
	*m = (lba / 75) / 60;
	*s = (lba / 75) % 60;
	*f = lba % 75;
}

static void ReadData(int lba, int track)
{
	cd_track_t *t = &toc.tracks[track];
	UINT br = 0;

	memset(cd_buf, 0, CD_BUF_SIZE);
	if (track >= toc.last || t->type != SECTOR_DATA_MODE1) return;

	if (lba < 0) lba = 0;
	int offset = (lba - t->start) * t->sector_size + t->offset;
	if (offset < 0) return;

	DISKLED_ON
	f_lseek(&toc.file->file, offset);
	if (t->sector_size == 2048)
		f_read(&toc.file->file, cd_buf + 16, 2048, &br);
	else
		f_read(&toc.file->file, cd_buf, 2352, &br);
	DISKLED_OFF
}

static void DataSectorSend(void)
{
	ReadData(cdd.lba, cdd.track);

	uint16_t crc = 0;
	for (int i = 0; i < 2048; i++) crc += cd_buf[16 + i];
	cd_buf[16 + 2048 + 0] = crc & 0xFF;
	cd_buf[16 + 2048 + 1] = crc >> 8;

	SendBuffer(CD_DATA_SEND, cd_buf + 16, 2048 + 2);
}

static void SendDiscInfo(void)
{
	uint8_t m, s, f;

	memset(info_buf, 0, INFO_BUF_SIZE);
	LBAToMSFbin(toc.end + 150, &m, &s, &f);
	info_buf[1] = 0x00;          // disc id: CD-DA or CD-ROM
	info_buf[2] = 0x01;          // first track
	info_buf[3] = toc.last;      // last track
	info_buf[4] = m;
	info_buf[5] = s;
	info_buf[6] = f;

	for (int i = 0, offs = 0x10; i <= toc.last && offs < 1024; i++, offs += 0x10) {
		int start = (i < toc.last) ? toc.tracks[i].start : toc.end;
		int data  = (i < toc.last) && (toc.tracks[i].type != SECTOR_AUDIO);
		LBAToMSFbin(start + 150, &m, &s, &f);
		info_buf[offs + 1] = 0;
		info_buf[offs + 2] = data ? 0x04 : 0x00;
		info_buf[offs + 3] = i + 1;
		info_buf[offs + 4] = 0;
		info_buf[offs + 5] = m;
		info_buf[offs + 6] = s;
		info_buf[offs + 7] = f;
		info_buf[offs + 8] = 0;
	}

	ReadData(0, 0);
	memcpy(info_buf + 1024, cd_buf, 128);

	SendBuffer(CD_INFO_SEND, info_buf, 2048);
	p3do_debugf("disc info sent, %d tracks, end %d", toc.last, toc.end);
}

static void p3do_reset(void)
{
	cdd.lba = 0;
	cdd.track = 0;
	cdd.read_pend = 0;
	cdd.block_reads = 0;
	cdd.speed = 0;
}

static int GetLBAFromCommand(const uint8_t *c)
{
	int lba = (c[1] << 16) | (c[2] << 8) | c[3];
	if (c[4]) return lba;
	return (int)c[1] * 60 * 75 + (int)c[2] * 75 + (int)c[3];
}

static void CommandExec(void)
{
	switch (cdd.comm[0]) {
	case P3DO_COMM_NOP:
		break;

	case P3DO_COMM_READ:
		cdd.lba = GetLBAFromCommand(cdd.comm) - 150;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.read_pend = 1;
		cdd.block_reads = (cdd.comm[5] << 8) | cdd.comm[6];
		cdd.speed = (cdd.comm[7] == 1) ? 1 : 2;
		p3do_debugf("READ lba %d blocks %d track %d", cdd.lba, cdd.block_reads, cdd.track);
		break;

	case P3DO_COMM_NEXT:
		if (cdd.block_reads > 0) cdd.read_pend = 1;
		break;

	default:
		cdd.read_pend = 0;
		p3do_debugf("unknown command %02x", cdd.comm[0]);
		break;
	}
}

void p3do_poll(void)
{
	static unsigned long poll_timer = 0;
	uint8_t c;

	if (poll_timer && !CheckTimer(poll_timer)) return;
	poll_timer = GetTimer(3);

	EnableFpga();
	c = SPI(CD_STAT_GET);
	DisableFpga();

	if (c & STB_RESET) {
		p3do_reset();
		if (cdd.loaded) SendDiscInfo();
		return;
	}

	if (c & STB_COMMAND) {
		EnableFpga();
		SPI(CD_COMMAND_GET);
		for (int i = 0; i < 8; i++) cdd.comm[i] = SPI(0);
		DisableFpga();
		CommandExec();
	}

	if (cdd.loaded && cdd.read_pend) {
		cdd.read_pend = 0;
		DataSectorSend();
		cdd.lba++;
		cdd.track = cue_gettrackbylba(cdd.lba);
		cdd.block_reads--;
	}
}

void p3do_image_selected(const char *name)
{
	user_io_8bit_set_status(1, 1);

	p3do_reset();
	cdd.loaded = (name && *name && toc.valid && toc.last > 0) ? 1 : 0;
	if (cdd.loaded) SendDiscInfo();
	p3do_debugf("disc %s", cdd.loaded ? "inserted" : "removed");

	user_io_8bit_set_status(0, 1);
}
