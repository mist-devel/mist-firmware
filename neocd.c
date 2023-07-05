#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "neocd.h"
#include "cue_parser.h"
#include "user_io.h"
#include "utils.h"
#include "debug.h"

// data io commands
#define CD_STAT_GET    0x60
#define CD_STAT_SEND   0x61
#define CD_COMMAND_GET 0x62
#define CD_DATA_SEND   0x64
#define CD_AUDIO_SEND  0x65

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
//#define CD_COMM_OPEN			0x05
#define CD_COMM_PAUSE			0x06
#define CD_COMM_RESUME			0x07
#define CD_COMM_FW_SCAN			0x08
#define CD_COMM_RW_SCAN			0x09
#define CD_COMM_TRACK_MOVE		0x0A
#define CD_COMM_TRACK_PLAY		0x0B
#define CD_COMM_TRAY_CLOSE		0x0C
#define CD_COMM_TRAY_OPEN		0x0D

#define CD_SCAN_SPEED 30
#define CRC_START 5

typedef struct
{
	uint32_t latency;
	uint8_t status;
	uint8_t isData;
	int has_status;
	int has_command;
	char can_read_next;
	char cdda_fifo_halffull;
	int index;
	int lba;
	uint16_t sectorSize;
	int scanOffset;
	int audioLength;
	int audioOffset;
	int speed;
	uint8_t stat[10];
} neocd_t;

static neocd_t neocdd;


static void SendData(char *buf, uint16_t len, unsigned char dm) {
	//hexdump(buf, len, 0);
	EnableFpga();
	SPI(dm ? CD_DATA_SEND : CD_AUDIO_SEND);
	spi_write(buf, len);
	DisableFpga();
}

static void SeekToLBA(int lba, int play) {
	int index = 0;

	neocdd.latency = 0;
	if (play)
	{
		neocdd.latency = 11 / neocdd.speed;
	}

	neocdd.latency += (abs(lba - neocdd.lba) * 120) / 270000 / neocdd.speed;

	neocdd.lba = lba;

	while ((toc.tracks[index].end <= lba) && (index < toc.last)) index++;
	neocdd.index = index;

	if (lba < toc.tracks[index].start)
	{
		lba = toc.tracks[index].start;
	}

	int offset = (lba - toc.tracks[index].start) * toc.tracks[index].sector_size + toc.tracks[index].offset;
	f_lseek(&toc.file->file, offset);
	neocd_debugf("SeekToLBA lba=%lu offset=%08x", lba, offset);
	if (play)
	{
		neocdd.audioOffset = 0;
	}
}

static int SectorSend(uint8_t* header)
{
	int len = 2352;
	UINT br;
	if (header) {
		memcpy(sector_buffer + 12, header, 4);
	}
	DISKLED_ON
	if (toc.tracks[neocdd.index].sector_size == 2048)
		f_read(&toc.file->file, sector_buffer+16, 2048, &br);
	else
		f_read(&toc.file->file, sector_buffer, 2352, &br);
	DISKLED_OFF

	SendData(sector_buffer, len, toc.tracks[neocdd.index].type);
	return 0;
}

static uint64_t GetStatus(uint8_t crc_start) {
	//neocd_debugf("getstatus: %01x %01x %01x %01x %01x %01x %01x %01x %01x",
		//neocdd.stat[0], neocdd.stat[1], neocdd.stat[2], neocdd.stat[3], neocdd.stat[4], neocdd.stat[5], neocdd.stat[6], neocdd.stat[7], neocdd.stat[8]);
	uint8_t n9 = ~(crc_start + neocdd.stat[0] + neocdd.stat[1] + neocdd.stat[2] + neocdd.stat[3] + neocdd.stat[4] + neocdd.stat[5] + neocdd.stat[6] + neocdd.stat[7] + neocdd.stat[8]);
	return ((uint64_t)(n9 & 0xF) << 36) |
		((uint64_t)(neocdd.stat[8] & 0xF) << 32) |
		((uint64_t)(neocdd.stat[7] & 0xF) << 28) |
		((uint64_t)(neocdd.stat[6] & 0xF) << 24) |
		((uint64_t)(neocdd.stat[5] & 0xF) << 20) |
		((uint64_t)(neocdd.stat[4] & 0xF) << 16) |
		((uint64_t)(neocdd.stat[3] & 0xF) << 12) |
		((uint64_t)(neocdd.stat[2] & 0xF) << 8) |
		((uint64_t)(neocdd.stat[1] & 0xF) << 4) |
		((uint64_t)(neocdd.stat[0] & 0xF) << 0);
}

static void SetError() {
	neocdd.stat[0] = CD_STAT_ERROR;
	neocdd.stat[1] = neocdd.stat[2] = neocdd.stat[3] = neocdd.stat[4] = neocdd.stat[5] = neocdd.stat[6] = neocdd.stat[7] = neocdd.stat[8] = neocdd.stat[9] = 0;
}

static void neocd_reset() {
	neocdd.latency = 10;
	neocdd.index = 0;
	neocdd.lba = 0;
	neocdd.scanOffset = 0;
	neocdd.isData = 1;
	neocdd.status = CD_STAT_NO_DISC;
	neocdd.audioLength = 0;
	neocdd.audioOffset = 0;
	neocdd.has_command = 0;
	neocdd.speed = 1;

	neocdd.stat[0] = 0x0;
	neocdd.stat[1] = 0x0;
	neocdd.stat[2] = 0x0;
	neocdd.stat[3] = 0x0;
	neocdd.stat[4] = 0x0;
	neocdd.stat[5] = 0x0;
	neocdd.stat[6] = 0x0;
	neocdd.stat[7] = 0x0;
	neocdd.stat[8] = 0x0;
	neocdd.stat[9] = 0xF;
}

static unsigned long neocd_read_timer = 0;

static void neocd_run() {

	if (neocdd.latency > 0) {
		if(!CheckTimer(neocd_read_timer)) return;
		neocd_read_timer = GetTimer(10);
		neocdd.latency--;
		return;
	}

	if (!user_io_is_cue_mounted()) {
		if (neocdd.status != CD_STAT_OPEN)
			neocdd.status = CD_STAT_NO_DISC;
	}
	else if (neocdd.status == CD_STAT_NO_DISC) {
		if (user_io_is_cue_mounted())
			neocd_reset();
			neocdd.status = CD_STAT_STOP;
	}
	else if (neocdd.status == CD_STAT_STOP || neocdd.status == CD_STAT_TRAY || neocdd.status == CD_STAT_OPEN) {
	}
	else if (neocdd.status == CD_STAT_SEEK) {
		neocdd.status = CD_STAT_PAUSE;
	}
	else if (neocdd.status == CD_STAT_PLAY) {
		if (neocdd.index >= toc.last)
		{
			neocdd.status = CD_STAT_END;
			return;
		}

		if (!((!toc.tracks[neocdd.index].type && neocdd.cdda_fifo_halffull) ||
		      ( toc.tracks[neocdd.index].type && neocdd.can_read_next))) {
			return; // not enough space in FPGA FIFO yet
		}
		if (toc.tracks[neocdd.index].type)
		{
			// CD-ROM (Mode 1)
			uint8_t header[4];
			msf_t msf;
			LBA2MSF(neocdd.lba + 150, &msf);
			header[0] = bin2bcd(msf.m);
			header[1] = bin2bcd(msf.s);
			header[2] = bin2bcd(msf.f);
			header[3] = 0x01;
			SectorSend(header);
		}
		else
		{
			if (neocdd.lba >= toc.tracks[neocdd.index].start)
			{
				neocdd.isData = 0x00;
			}
			SectorSend(0);
		}

		neocdd.lba++;

		if (neocdd.lba >= toc.tracks[neocdd.index].end)
		{
			neocdd.index++;
			neocdd.isData = 0x01;
			f_lseek(&toc.file->file, toc.tracks[neocdd.index].offset);
		}
	}
	else if (neocdd.status == CD_STAT_SCAN) {
		neocdd.lba += neocdd.scanOffset;

		if (neocdd.lba >= toc.tracks[neocdd.index].end)
		{
			neocdd.index++;
			if (neocdd.index < toc.last)
			{
				neocdd.lba = toc.tracks[neocdd.index].start;
			}
			else
			{
				neocdd.lba = toc.end;
				neocdd.status = CD_STAT_END;
				neocdd.isData = 0x01;
				return;
			}
		}
		else if (neocdd.lba < toc.tracks[neocdd.index].start)
		{
			if (neocdd.index > 0)
			{
				neocdd.index--;
				neocdd.lba = toc.tracks[neocdd.index].end;
			}
			else
			{
				neocdd.lba = 0;
			}
		}

		neocdd.isData = toc.tracks[neocdd.index].type;
		int offset = (neocdd.lba - toc.tracks[neocdd.index].start) * toc.tracks[neocdd.index].sector_size + toc.tracks[neocdd.index].offset;
		f_lseek(&toc.file->file, offset);
	}
}

static void neocd_command() {
	int new_lba = 0;
	msf_t msf;
	int track;
	uint8_t command[10];

	int i;

	EnableFpga();
	SPI(CD_COMMAND_GET);
	for (int i = 0; i < 5; i++) {
		uint8_t c = SPI(0);
		command[i*2]   = c & 0x0f;
		command[i*2+1] = c >> 4;
	}
	DisableFpga();
	//neocd_debugf("command: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
	//	command[0], command[1], command[2], command[3], command[4], command[5], command[6], command[7], command[8], command[9]);

	uint8_t crc = (~(5 + command[0] + command[1] + command[2] + command[3] + command[4] + command[5] + command[6] + command[7] + command[8])) & 0xF;
	if (command[9] != crc) {
		neocd_debugf("Command CRC error");
		SetError();
		return;
	}

	if ((neocdd.status == CD_STAT_OPEN || neocdd.status == CD_STAT_NO_DISC) &&
	    (command[0] != CD_COMM_IDLE &&
	     command[0] != CD_COMM_TRAY_OPEN &&
	     command[0] != CD_COMM_TRAY_CLOSE)) {
		SetError();
		return;
	}

	switch (command[0]) {

	case CD_COMM_IDLE:
		if (neocdd.latency <= 3)
		{
			neocdd.stat[0] = neocdd.status;
			if (neocdd.stat[1] == 0x0f)
			{
				int lba = neocdd.lba + 150;
				LBA2MSF(lba, &msf);
				neocdd.stat[1] = 0x0;
				neocdd.stat[2] = bin2bcd(msf.m) >> 4;
				neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
				neocdd.stat[4] = bin2bcd(msf.s) >> 4;
				neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
				neocdd.stat[6] = bin2bcd(msf.f) >> 4;
				neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
				neocdd.stat[8] = toc.tracks[neocdd.index].type ? 0x04 : 0x00;
			} else if (neocdd.stat[1] == 0x00) {
				int lba = neocdd.lba + 150;
				LBA2MSF(lba, &msf);
				neocdd.stat[2] = bin2bcd(msf.m) >> 4;
				neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
				neocdd.stat[4] = bin2bcd(msf.s) >> 4;
				neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
				neocdd.stat[6] = bin2bcd(msf.f) >> 4;
				neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
				neocdd.stat[8] = toc.tracks[neocdd.index].type ? 0x04 : 0x00;
			} else if (neocdd.stat[1] == 0x01) {
				int lba = abs(neocdd.lba - toc.tracks[neocdd.index].start);
				LBA2MSF(lba,&msf);
				neocdd.stat[2] = bin2bcd(msf.m) >> 4;
				neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
				neocdd.stat[4] = bin2bcd(msf.s) >> 4;
				neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
				neocdd.stat[6] = bin2bcd(msf.f) >> 4;
				neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
				neocdd.stat[8] = toc.tracks[neocdd.index].type ? 0x04 : 0x00;
			} else if (neocdd.stat[1] == 0x02) {
				neocdd.stat[2] = (neocdd.index < toc.last) ? bin2bcd(neocdd.index + 1) >> 4 : 0xA;
				neocdd.stat[3] = (neocdd.index < toc.last) ? bin2bcd(neocdd.index + 1) & 0xF : 0xA;
			}
			//neocd_debugf("Command IDLE status=%02x", neocdd.status);
		}
		break;

	case CD_COMM_STOP:
		neocdd.status = CD_STAT_STOP;
		neocdd.isData = 1;

		neocdd.stat[0] = neocdd.status;
		neocdd.stat[1] = 0;
		neocdd.stat[2] = 0;
		neocdd.stat[3] = 0;
		neocdd.stat[4] = 0;
		neocdd.stat[5] = 0;
		neocdd.stat[6] = 0;
		neocdd.stat[7] = 0;
		neocdd.stat[8] = 0;

		neocd_debugf("Command STOP status=%02x", neocdd.status);
		break;

	case CD_COMM_TOC:
		if (neocdd.status == CD_STAT_STOP)
			neocdd.status = CD_STAT_TOC;

		switch (command[3]) {
		case 0: {
			int lba_ = neocdd.lba + 150;
			LBA2MSF(lba_, &msf);

			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x0;
			neocdd.stat[2] = bin2bcd(msf.m) >> 4;
			neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
			neocdd.stat[4] = bin2bcd(msf.s) >> 4;
			neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
			neocdd.stat[6] = bin2bcd(msf.f) >> 4;
			neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
			neocdd.stat[8] = toc.tracks[neocdd.index].type << 2;
			neocd_debugf("Command TOC 0, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X", lba_, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0], (uint32_t)(GetStatus(CRC_START) >> 32), (uint32_t)GetStatus(CRC_START));
			}
			break;

		case 1: {
			int lba_ = abs(neocdd.lba - toc.tracks[neocdd.index].start);
			LBA2MSF(lba_, &msf);

			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x1;
			neocdd.stat[2] = bin2bcd(msf.m) >> 4;
			neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
			neocdd.stat[4] = bin2bcd(msf.s) >> 4;
			neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
			neocdd.stat[6] = bin2bcd(msf.f) >> 4;
			neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
			neocdd.stat[8] = toc.tracks[neocdd.index].type << 2;
			neocd_debugf("Command TOC 1, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X", lba_, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0], (uint32_t)(GetStatus(CRC_START) >> 32), (uint32_t)GetStatus(CRC_START));
			}
			break;

		case 2: {
			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x2;
			neocdd.stat[2] = ((neocdd.index < toc.last) ? bin2bcd(neocdd.index + 1) >> 4 : 0xA);
			neocdd.stat[3] = ((neocdd.index < toc.last) ? bin2bcd(neocdd.index + 1) & 0xF : 0xA);
			neocdd.stat[4] = 0;
			neocdd.stat[5] = 0;
			neocdd.stat[6] = 0;
			neocdd.stat[7] = 0;
			neocdd.stat[8] = 0;
			neocd_debugf("Command TOC 2, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X", neocdd.index, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0], (uint32_t)(GetStatus(CRC_START) >> 32), (uint32_t)GetStatus(CRC_START));
			}
			break;

		case 3: {
			int lba_ = toc.end + 150;
			LBA2MSF(lba_, &msf);

			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x3;
			neocdd.stat[2] = bin2bcd(msf.m) >> 4;
			neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
			neocdd.stat[4] = bin2bcd(msf.s) >> 4;
			neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
			neocdd.stat[6] = bin2bcd(msf.f) >> 4;
			neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
			neocdd.stat[8] = 0;
			neocd_debugf("Command TOC 3, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", lba_, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0]);
			}
			break;

		case 4: {
			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x4;
			neocdd.stat[2] = 0;
			neocdd.stat[3] = 1;
			neocdd.stat[4] = bin2bcd(toc.last) >> 4;
			neocdd.stat[5] = bin2bcd(toc.last) & 0xF;
			neocdd.stat[6] = 0;
			neocdd.stat[7] = 0;
			neocdd.stat[8] = 0;
			neocd_debugf("Command TOC 4, last = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", toc.last, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0]);
			}
			break;

		case 5: {
			int track = command[4] * 10 + command[5];
			if (track > toc.last) {
				SetError();
				return;
			}
			int lba_ = toc.tracks[track - 1].start + 150;
			LBA2MSF(lba_, &msf);

			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x5;
			neocdd.stat[2] = bin2bcd(msf.m) >> 4;
			neocdd.stat[3] = bin2bcd(msf.m) & 0xF;
			neocdd.stat[4] = bin2bcd(msf.s) >> 4;
			neocdd.stat[5] = bin2bcd(msf.s) & 0xF;
			neocdd.stat[6] = (bin2bcd(msf.f) >> 4) | (toc.tracks[track - 1].type << 3);
			neocdd.stat[7] = bin2bcd(msf.f) & 0xF;
			neocdd.stat[8] = bin2bcd(track) & 0xF;
			neocd_debugf("Command TOC 5, lba = %i, track = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", lba_, track, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0]);
			}
			break;

		case 6:
			neocdd.stat[0] = neocdd.status;
			neocdd.stat[1] = 0x6;
			neocdd.stat[2] = 0;
			neocdd.stat[3] = 0;
			neocdd.stat[4] = 0;
			neocdd.stat[5] = 0;
			neocdd.stat[6] = 0;
			neocdd.stat[7] = 0;
			neocdd.stat[8] = 0;
			neocd_debugf("Command TOC 6");
			break;

		default:

			break;
		}
		break;
	case CD_COMM_PLAY: {
		int lba_;
		lba_ = MSF2LBA(command[2] * 10 + command[3], command[4] * 10 + command[5], command[6] * 10 + command[7]);

		SeekToLBA(lba_, 1);

		neocdd.isData = 1;

		neocdd.status = CD_STAT_PLAY;

		neocdd.stat[0] = CD_STAT_SEEK;
		neocdd.stat[1] = 0xf;
		neocdd.stat[2] = 0;
		neocdd.stat[3] = 0;
		neocdd.stat[4] = 0;
		neocdd.stat[5] = 0;
		neocdd.stat[6] = 0;
		neocdd.stat[7] = 0;
		neocdd.stat[8] = 0;

		neocd_debugf("Command PLAY, lba = %i, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", lba_, neocdd.index, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0]);
		}
		break;

	case CD_COMM_SEEK: {
		int lba_;
		lba_ = MSF2LBA(command[2] * 10 + command[3], command[4] * 10 + command[5], command[6] * 10 + command[7]);

		SeekToLBA(lba_, 0);

		neocdd.isData = 1;

		neocdd.status = CD_STAT_SEEK;

		neocdd.stat[0] = neocdd.status;
		neocdd.stat[1] = 0xf;
		neocdd.stat[2] = 0;
		neocdd.stat[3] = 0;
		neocdd.stat[4] = 0;
		neocdd.stat[5] = 0;
		neocdd.stat[6] = 0;
		neocdd.stat[7] = 0;
		neocdd.stat[8] = 0;

		neocd_debugf("Command SEEK, lba = %i, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", lba_, neocdd.index, command[9], command[8], command[7], command[6], command[5], command[4], command[3], command[2], command[1], command[0]);
	}
		break;

	case CD_COMM_PAUSE:
		neocdd.isData = 0x01;

		neocdd.status = CD_STAT_PAUSE;

		neocdd.stat[0] = neocdd.status;
		neocd_debugf("Command PAUSE, status = %X", neocdd.status);
		break;

	case CD_COMM_RESUME:
		neocdd.status = CD_STAT_PLAY;
		neocdd.stat[0] = neocdd.status;
		neocdd.audioOffset = 0;
		neocd_debugf("Command RESUME, status = %X", neocdd.status);
		break;

	case CD_COMM_FW_SCAN:
		neocdd.scanOffset = CD_SCAN_SPEED;
		neocdd.status = CD_STAT_SCAN;
		neocdd.stat[0] = neocdd.status;
		break;

	case CD_COMM_RW_SCAN:
		neocdd.scanOffset = -CD_SCAN_SPEED;
		neocdd.status = CD_STAT_SCAN;
		neocdd.stat[0] = neocdd.status;
		break;

	case CD_COMM_TRACK_MOVE:
		neocdd.isData = 1;
		neocdd.status = CD_STAT_PAUSE;
		neocdd.stat[0] = neocdd.status;
		break;

	case CD_COMM_TRACK_PLAY: {
		int index = command[2] * 10 + command[3];
		if (index > 0)
		{
			index -= 1;
		}
		if (index >= toc.last) {
			SetError();
			return;
		}
		int lba = toc.tracks[index].start;

		SeekToLBA(lba, 1);

		neocdd.isData = 1;

		neocdd.status = CD_STAT_PLAY;

		neocdd.stat[0] = CD_STAT_SEEK;
		neocdd.stat[1] = 0xf;
		neocdd.stat[2] = 0;
		neocdd.stat[3] = 0;
		neocdd.stat[4] = 0;
		neocdd.stat[5] = 0;
		neocdd.stat[6] = 0;
		neocdd.stat[7] = 0;
		neocdd.stat[8] = 0;

		neocd_debugf("Command CD_COMM_TRACK_PLAY, index: %u, status = %u", index, neocdd.status);
	}
		break;

	case CD_COMM_TRAY_CLOSE:
		neocdd.isData = 1;
		neocdd.status = user_io_is_cue_mounted() ? CD_STAT_TOC : CD_STAT_NO_DISC;
		neocdd.stat[0] = CD_STAT_STOP;

		neocd_debugf("Command TRAY_CLOSE, status = %u", neocdd.status);
		break;

	case CD_COMM_TRAY_OPEN:
		neocdd.isData = 1;
		neocdd.status = CD_STAT_OPEN;
		neocdd.stat[0] = CD_STAT_OPEN;

		neocd_debugf("Command TRAY_OPEN, status = %u", neocdd.status);
		break;

	default:
		neocdd.stat[0] = neocdd.status;
		neocd_debugf("command undefined: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			command[0], command[1], command[2], command[3], command[4], command[5], command[6], command[7], command[8], command[9]);
		break;
	}
}


static void neocd_sendstatus() {

	uint64_t status = GetStatus(CRC_START);
	//neocd_debugf("SendStatus %llx", status);
	EnableFpga();
	SPI(CD_STAT_SEND);
	spi16le((status >> 0) & 0xFFFF);
	spi16le((status >> 16) & 0xFFFF);
	spi16le((status >> 32) & 0x00FF);
	DisableFpga();
}


static unsigned long neocd_timer = 0;

void neocd_poll() {
	char c;
	EnableFpga();
	c = SPI(CD_STAT_GET); // cmd request
	DisableFpga();
	neocdd.cdda_fifo_halffull = (c & 0x02);
	neocdd.can_read_next = (c & 0x01);
	neocdd.speed = ((c & 0x18) >> 3) + 1;

	if (c & 0x20) {
		neocd_reset();
		return;
	}

	if (c&0x04) {
		neocd_command();
		neocdd.has_command = 1;
	}
	neocd_run();

	if(CheckTimer(neocd_timer)) {
		neocd_timer = GetTimer(10);
		if (neocdd.has_command) {
			neocdd.has_command = 0;
			neocd_sendstatus();
		}
	}
}
