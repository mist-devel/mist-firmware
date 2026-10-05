// megacd.c
//
// MiST/SiDi128 virtual CD drive (CDD) for the MegaCD_SiDi128 core.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "megacd.h"
#include "cue_parser.h"
#include "user_io.h"
#include "utils.h"
#include "debug.h"
#include "idxfile.h"

#if 0
#define megacd_debugf(a, ...) iprintf("\033[1;34mMEGACD : " a "\033[0m\n",## __VA_ARGS__)
#else
#define megacd_debugf(...)
#endif

// data_io_megacd.v opcodes
#define CD_STAT_GET     0x60
#define CD_STAT_SEND    0x61
#define CD_COMMAND_GET  0x62
#define CD_DATA_SEND    0x64
#define CD_AUDIO_SEND   0x65
#define CD_SUBCODE_SEND 0x66

// CD_STAT_GET reply bits (data_io_megacd.v cd_status)
#define STB_DATA_READY  0x01
#define STB_CDDA_READY  0x02
#define STB_COMMAND     0x04
#define STB_RESET       0x20

// CDD status
#define CD_STAT_STOP			0x00
#define CD_STAT_PLAY			0x01
#define CD_STAT_SEEK			0x02
#define CD_STAT_SCAN			0x03
#define CD_STAT_PAUSE			0x04
#define CD_STAT_OPEN			0x05
#define CD_STAT_NO_VALID_CHK	0x06
#define CD_STAT_NO_VALID_CMD	0x07
#define CD_STAT_ERROR			0x08
#define CD_STAT_TOC				0x09
#define CD_STAT_TRACK_MOVE		0x0A
#define CD_STAT_NO_DISC			0x0B
#define CD_STAT_END				0x0C
#define CD_STAT_TRAY			0x0E
#define CD_STAT_TEST			0x0F

// CDD command
#define CD_COMM_IDLE			0x00
#define CD_COMM_STOP			0x01
#define CD_COMM_TOC				0x02
#define CD_COMM_PLAY			0x03
#define CD_COMM_SEEK			0x04
#define CD_COMM_PAUSE			0x06
#define CD_COMM_RESUME			0x07
#define CD_COMM_FW_SCAN			0x08
#define CD_COMM_RW_SCAN			0x09
#define CD_COMM_TRACK_MOVE		0x0A
#define CD_COMM_TRACK_PLAY		0x0B
#define CD_COMM_TRAY_CLOSE		0x0C
#define CD_COMM_TRAY_OPEN		0x0D

#define CD_SCAN_SPEED 30

typedef struct
{
	uint32_t latency;
	uint8_t status;
	uint8_t isData;
	int loaded;
	int index;
	int lba;
	int scanOffset;
	uint8_t stat[10];
	uint8_t comm[10];
} megacd_t;

static megacd_t cdd = {
	.latency = 10,
	.status = CD_STAT_NO_DISC,
	.isData = 1,
	.stat = { CD_STAT_NO_DISC, 0, 0, 0, 0, 0, 0, 0, 0, 0x4 },
};

static char door_open = 0;
static char cdda_ready = 0;


static void SeekFile(void)
{
	if (!cdd.loaded || cdd.index >= toc.last) return;
	cd_track_t *t = &toc.tracks[cdd.index];
	int offset = (cdd.lba - t->start) * t->sector_size + t->offset;
	if (offset < 0) offset = 0;
	f_lseek(&toc.file->file, offset);
}

static void SendBuffer(unsigned char opcode, int len)
{
	EnableFpga();
	SPI(opcode);
	spi_write((char*)sector_buffer, len);
	DisableFpga();
}

static void SectorSend(void)
{
	cd_track_t *t = &toc.tracks[cdd.index];
	UINT br = 0;
	int offset = (cdd.lba - t->start) * t->sector_size + t->offset;
	int in_image = (offset >= 0);

	DISKLED_ON
	if (in_image) f_lseek(&toc.file->file, offset);

	if (t->type) {
		msf_t msf;
		if (t->sector_size == 2048 || !in_image) {
			memset(sector_buffer, 0xFF, 12);
			sector_buffer[0] = sector_buffer[11] = 0x00;
			LBA2MSF(cdd.lba + 150, &msf);
			sector_buffer[12] = bin2bcd(msf.m);
			sector_buffer[13] = bin2bcd(msf.s);
			sector_buffer[14] = bin2bcd(msf.f);
			sector_buffer[15] = 0x01;
			memset(sector_buffer + 16, 0, 2352 - 16);
			if (in_image) f_read(&toc.file->file, sector_buffer + 16, 2048, &br);
		} else {
			f_read(&toc.file->file, sector_buffer, 2352, &br);
		}
		DISKLED_OFF
		SendBuffer(CD_DATA_SEND, 2352);
	} else {
		if (cdd.lba >= t->start) cdd.isData = 0;
		if (in_image) f_read(&toc.file->file, sector_buffer, 2352, &br);
		else memset(sector_buffer, 0, 2352);
		DISKLED_OFF
		SendBuffer(CD_AUDIO_SEND, 2352);
	}
}


static void SeekToLBA(int lba, int play)
{
	int index = 0;

	cdd.latency = play ? 11 : 0;
	cdd.latency += (abs(lba - cdd.lba) * 120) / 270000;

	cdd.lba = lba;

	while ((toc.tracks[index].end <= lba) && (index < toc.last)) index++;
	cdd.index = index;

	SeekFile();
}

static uint64_t GetStatus(void)
{
	uint8_t n9 = ~(cdd.stat[0] + cdd.stat[1] + cdd.stat[2] + cdd.stat[3] + cdd.stat[4] + cdd.stat[5] + cdd.stat[6] + cdd.stat[7] + cdd.stat[8]);
	return ((uint64_t)(n9 & 0xF) << 36) |
		((uint64_t)(cdd.stat[8] & 0xF) << 32) |
		((uint64_t)(cdd.stat[7] & 0xF) << 28) |
		((uint64_t)(cdd.stat[6] & 0xF) << 24) |
		((uint64_t)(cdd.stat[5] & 0xF) << 20) |
		((uint64_t)(cdd.stat[4] & 0xF) << 16) |
		((uint64_t)(cdd.stat[3] & 0xF) << 12) |
		((uint64_t)(cdd.stat[2] & 0xF) << 8) |
		((uint64_t)(cdd.stat[1] & 0xF) << 4) |
		((uint64_t)(cdd.stat[0] & 0xF) << 0);
}

static void SetStatMSF(int lba)
{
	msf_t msf;
	LBA2MSF(lba, &msf);
	cdd.stat[2] = bin2bcd(msf.m) >> 4;
	cdd.stat[3] = bin2bcd(msf.m) & 0xF;
	cdd.stat[4] = bin2bcd(msf.s) >> 4;
	cdd.stat[5] = bin2bcd(msf.s) & 0xF;
	cdd.stat[6] = bin2bcd(msf.f) >> 4;
	cdd.stat[7] = bin2bcd(msf.f) & 0xF;
}

static int CurType(void)
{
	return (cdd.index < toc.last) ? toc.tracks[cdd.index].type : 0;
}

static void megacd_reset(void)
{
	cdd.latency = 10;
	cdd.index = 0;
	cdd.lba = 0;
	cdd.scanOffset = 0;
	cdd.isData = 1;
	cdd.status = CD_STAT_STOP;
	memset(cdd.stat, 0, sizeof(cdd.stat));
	cdd.stat[9] = 0xF;
	door_open = 0;
	SeekFile();
}

static void megacd_update(void)
{
	if (cdd.status == CD_STAT_STOP || cdd.status == CD_STAT_TRAY || cdd.status == CD_STAT_OPEN)
	{
		if (cdd.latency > 0) { cdd.latency--; return; }
		if (!cdd.loaded && cdd.status == CD_STAT_STOP)
			cdd.status = door_open ? CD_STAT_OPEN : CD_STAT_NO_DISC;
	}
	else if (cdd.status == CD_STAT_SEEK)
	{
		if (cdd.latency > 0) { cdd.latency--; return; }
		cdd.status = CD_STAT_PAUSE;
	}
	else if (cdd.status == CD_STAT_PLAY)
	{
		if (cdd.latency > 0) { cdd.latency--; return; }

		if (cdd.index >= toc.last)
		{
			cdd.status = CD_STAT_END;
			return;
		}

		if (!toc.tracks[cdd.index].type && !cdda_ready) return;

		SectorSend();

		cdd.lba++;
		if (cdd.lba >= toc.tracks[cdd.index].end)
		{
			cdd.index++;
			cdd.isData = 1;
			SeekFile();
		}
	}
	else if (cdd.status == CD_STAT_SCAN)
	{
		cdd.lba += cdd.scanOffset;

		if (cdd.lba >= toc.tracks[cdd.index].end)
		{
			cdd.index++;
			if (cdd.index < toc.last)
			{
				cdd.lba = toc.tracks[cdd.index].start;
			}
			else
			{
				cdd.lba = toc.end;
				cdd.status = CD_STAT_END;
				cdd.isData = 1;
				return;
			}
		}
		else if (cdd.lba < toc.tracks[cdd.index].start)
		{
			if (cdd.index > 0)
			{
				cdd.index--;
				cdd.lba = toc.tracks[cdd.index].end;
			}
			else
			{
				cdd.lba = 0;
			}
		}

		cdd.isData = toc.tracks[cdd.index].type;
		SeekFile();
	}
}

static void megacd_command(void)
{
	EnableFpga();
	SPI(CD_COMMAND_GET);
	for (int i = 0; i < 5; i++) {
		uint8_t c = SPI(0);
		cdd.comm[i*2]   = c & 0x0f;
		cdd.comm[i*2+1] = c >> 4;
	}
	DisableFpga();

	uint8_t crc = (~(cdd.comm[0] + cdd.comm[1] + cdd.comm[2] + cdd.comm[3] + cdd.comm[4] + cdd.comm[5] + cdd.comm[6] + cdd.comm[7] + cdd.comm[8])) & 0xF;
	if (cdd.comm[9] != crc)
		megacd_debugf("command checksum mismatch");

	if (!cdd.loaded) {
		switch (cdd.comm[0]) {
		case CD_COMM_STOP:
			cdd.status = CD_STAT_STOP;
			cdd.latency = 0;
			memset(cdd.stat, 0, 9);
			cdd.stat[0] = CD_STAT_STOP;
			break;
		case CD_COMM_TOC:
			memset(cdd.stat, 0, 9);
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = cdd.comm[3];
			break;
		case CD_COMM_TRAY_OPEN:
			door_open = 1;
			cdd.status = CD_STAT_OPEN;
			cdd.latency = 0;
			memset(cdd.stat, 0, 9);
			cdd.stat[0] = CD_STAT_OPEN;
			break;
		case CD_COMM_TRAY_CLOSE:
			door_open = 0;
			cdd.status = CD_STAT_NO_DISC;
			cdd.latency = 0;
			memset(cdd.stat, 0, 9);
			cdd.stat[0] = CD_STAT_STOP;
			break;
		default:
			cdd.stat[0] = cdd.status;
			break;
		}
		return;
	}

	switch (cdd.comm[0]) {
	case CD_COMM_IDLE:
		if (cdd.latency <= 3)
		{
			cdd.stat[0] = cdd.status;
			if (cdd.stat[1] == 0x0f)
			{
				cdd.stat[1] = 0x0;
				SetStatMSF(cdd.lba + 150);
				cdd.stat[8] = CurType() ? 0x04 : 0x00;
			} else if (cdd.stat[1] == 0x00) {
				SetStatMSF(cdd.lba + 150);
				cdd.stat[8] = CurType() ? 0x04 : 0x00;
			} else if (cdd.stat[1] == 0x01) {
				SetStatMSF(abs(cdd.lba - toc.tracks[cdd.index].start));
				cdd.stat[8] = CurType() ? 0x04 : 0x00;
			} else if (cdd.stat[1] == 0x02) {
				cdd.stat[2] = (cdd.index < toc.last) ? bin2bcd(cdd.index + 1) >> 4 : 0xA;
				cdd.stat[3] = (cdd.index < toc.last) ? bin2bcd(cdd.index + 1) & 0xF : 0xA;
			}
		}
		break;

	case CD_COMM_STOP:
		cdd.status = CD_STAT_STOP;
		cdd.isData = 1;
		memset(cdd.stat, 0, 9);
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_TOC:
		if (cdd.status == CD_STAT_STOP) cdd.status = CD_STAT_TOC;
		switch (cdd.comm[3]) {
		case 0:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x0;
			SetStatMSF(cdd.lba + 150);
			cdd.stat[8] = CurType() << 2;
			break;

		case 1:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x1;
			SetStatMSF(abs(cdd.lba - toc.tracks[cdd.index].start));
			cdd.stat[8] = CurType() << 2;
			break;

		case 2:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x2;
			cdd.stat[2] = (cdd.index < toc.last) ? bin2bcd(cdd.index + 1) >> 4 : 0xA;
			cdd.stat[3] = (cdd.index < toc.last) ? bin2bcd(cdd.index + 1) & 0xF : 0xA;
			cdd.stat[4] = cdd.stat[5] = cdd.stat[6] = cdd.stat[7] = cdd.stat[8] = 0;
			break;

		case 3:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x3;
			SetStatMSF(toc.end + 150);
			cdd.stat[8] = 0;
			break;

		case 4:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x4;
			cdd.stat[2] = 0;
			cdd.stat[3] = 1;
			cdd.stat[4] = bin2bcd(toc.last) >> 4;
			cdd.stat[5] = bin2bcd(toc.last) & 0xF;
			cdd.stat[6] = cdd.stat[7] = cdd.stat[8] = 0;
			break;

		case 5: {
			int track = cdd.comm[4] * 10 + cdd.comm[5];
			if (track < 1 || track > toc.last) track = 1;   // MiSTer indexes unchecked; stay in range
			SetStatMSF(toc.tracks[track - 1].start + 150);
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x5;
			cdd.stat[6] |= toc.tracks[track - 1].type << 3;
			cdd.stat[8] = bin2bcd(track) & 0xF;
			}
			break;

		case 6:
			cdd.stat[0] = cdd.status;
			cdd.stat[1] = 0x6;
			cdd.stat[2] = cdd.stat[3] = cdd.stat[4] = cdd.stat[5] = cdd.stat[6] = cdd.stat[7] = cdd.stat[8] = 0;
			break;

		default:
			break;
		}
		break;

	case CD_COMM_PLAY:
	case CD_COMM_SEEK: {
		int play = (cdd.comm[0] == CD_COMM_PLAY);
		int lba_ = MSF2LBA(cdd.comm[2] * 10 + cdd.comm[3], cdd.comm[4] * 10 + cdd.comm[5], cdd.comm[6] * 10 + cdd.comm[7]);
		SeekToLBA(lba_, play);
		cdd.isData = 1;
		cdd.status = play ? CD_STAT_PLAY : CD_STAT_SEEK;
		memset(cdd.stat, 0, 9);
		cdd.stat[0] = CD_STAT_SEEK;
		cdd.stat[1] = 0xf;
		}
		break;

	case CD_COMM_PAUSE:
		cdd.isData = 1;
		cdd.status = CD_STAT_PAUSE;
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_RESUME:
		cdd.status = CD_STAT_PLAY;
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_FW_SCAN:
		cdd.scanOffset = CD_SCAN_SPEED;
		cdd.status = CD_STAT_SCAN;
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_RW_SCAN:
		cdd.scanOffset = -CD_SCAN_SPEED;
		cdd.status = CD_STAT_SCAN;
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_TRACK_MOVE:
		cdd.isData = 1;
		cdd.status = CD_STAT_PAUSE;
		cdd.stat[0] = cdd.status;
		break;

	case CD_COMM_TRACK_PLAY: {
		int index = cdd.comm[2] * 10 + cdd.comm[3];
		if (index > 0) index -= 1;
		if (index >= toc.last) index = 0;
		SeekToLBA(toc.tracks[index].start, 1);
		cdd.isData = 1;
		cdd.status = CD_STAT_PLAY;
		memset(cdd.stat, 0, 9);
		cdd.stat[0] = CD_STAT_SEEK;
		cdd.stat[1] = 0xf;
		}
		break;

	case CD_COMM_TRAY_CLOSE:
		cdd.isData = 1;
		cdd.status = cdd.loaded ? CD_STAT_TOC : CD_STAT_NO_DISC;
		cdd.stat[0] = CD_STAT_STOP;
		break;

	case CD_COMM_TRAY_OPEN:
		cdd.isData = 1;
		cdd.status = CD_STAT_OPEN;
		cdd.stat[0] = CD_STAT_OPEN;
		break;

	default:
		cdd.stat[0] = cdd.status;
		break;
	}

	megacd_debugf("cmd %x%x%x%x%x%x%x%x%x%x -> status %x",
		cdd.comm[0], cdd.comm[1], cdd.comm[2], cdd.comm[3], cdd.comm[4],
		cdd.comm[5], cdd.comm[6], cdd.comm[7], cdd.comm[8], cdd.comm[9], cdd.status);
}

static void megacd_sendstatus(void)
{
	uint64_t s = GetStatus();
	EnableFpga();
	SPI(CD_STAT_SEND);
	spi16le((s >> 0) & 0xFFFF);
	spi16le((s >> 16) & 0xFFFF);
	spi16le(((s >> 32) & 0x00FF) | ((cdd.loaded && cdd.isData) ? 0x0100 : 0x0000));   // empty drive: DM=0 like the stub
	DisableFpga();
}

static void megacd_check_mount(void)
{
	int mounted = user_io_is_cue_mounted() ? 1 : 0;
	if (mounted == cdd.loaded) return;

	cdd.loaded = mounted;
	cdd.index = 0;
	cdd.lba = 0;
	cdd.isData = 1;
	cdd.latency = 10;
	cdd.status = mounted ? CD_STAT_STOP : CD_STAT_NO_DISC;
	if (mounted) SeekFile();
	megacd_debugf("disc %s, %d tracks", mounted ? "inserted" : "removed", toc.last);

	user_io_8bit_set_status(1, 1);
	user_io_8bit_set_status(0, 1);
}

static unsigned long tick_base = 0;
static unsigned long tick_n = 0;
static unsigned long audio_timer = 0;
static char tick_started = 0;

static int megacd_tick_due(void)
{
	unsigned long now = GetTimer(0);

	if (!cdd.isData && cdd.status == CD_STAT_PLAY && cdd.latency == 0) {
		tick_started = 0;
		if (audio_timer && !CheckTimer(audio_timer)) return 0;
		audio_timer = GetTimer(10);
		return 1;
	}

	if (!tick_started) {
		tick_started = 1;
		tick_base = now;
		tick_n = 0;
	}

	unsigned long due = tick_base + (tick_n * 40) / 3;
	if ((long)(now - due) < 0) return 0;
	if ((long)(now - due) > 100) {
		tick_base = now;
		tick_n = 0;
	}
	tick_n++;
	if (tick_n >= 75 * 60) {
		tick_base += (tick_n * 40) / 3;
		tick_n = 0;
	}
	return 1;
}

void megacd_poll()
{
	uint8_t c;

	EnableFpga();
	c = SPI(CD_STAT_GET);
	DisableFpga();

	cdda_ready = (c & STB_CDDA_READY) ? 1 : 0;

	megacd_check_mount();

	if (c & STB_RESET) {
		megacd_reset();
		return;
	}

	if (c & STB_COMMAND)
		megacd_command();

	if (megacd_tick_due()) {
		megacd_sendstatus();
		megacd_update();
	}
}


static const unsigned char bram_format_tail[64] = {
	0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x40,
	0x00, 0x7D, 0x00, 0x7D, 0x00, 0x7D, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x53, 0x45, 0x47, 0x41, 0x5F, 0x43, 0x44, 0x5F, 0x52, 0x4F, 0x4D, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x52, 0x41, 0x4D, 0x5F, 0x43, 0x41, 0x52, 0x54, 0x52, 0x49, 0x44, 0x47, 0x45, 0x5F, 0x5F, 0x5F
};

void megacd_image_selected(const char *name)
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
		// new save file: 8 KB, empty and formatted
		memset(sector_buffer, 0, 512);
		for (int blk = 0; blk < 16; blk++) {
			if (blk == 15) memcpy(sector_buffer + 512 - 64, bram_format_tail, 64);
			f_write(&f, sector_buffer, 512, &bw);
		}
		f_close(&f);
		megacd_debugf("created %s", savname);
	}

	user_io_file_mount((const unsigned char*)savname, 0);
}
