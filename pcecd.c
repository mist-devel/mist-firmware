#include <string.h>
#include <stdio.h>
#include "pcecd.h"
#include "cue_parser.h"
#include "user_io.h"
#include "utils.h"
#include "debug.h"

// CDD command
#define PCECD_COMM_TESTUNIT			0x00
#define PCECD_COMM_REQUESTSENSE		0x03
#define PCECD_COMM_READ6			0x08
#define PCECD_COMM_MODESELECT6		0x15
#define PCECD_COMM_SAPSP			0xD8
#define PCECD_COMM_SAPEP			0xD9
#define PCECD_COMM_PAUSE			0xDA
#define PCECD_COMM_READSUBQ			0xDD
#define PCECD_COMM_GETDIRINFO		0xDE

#define PCECD_STATE_IDLE		0
#define PCECD_STATE_NODISC		1
#define PCECD_STATE_READ		2
#define PCECD_STATE_PLAY		3
#define PCECD_STATE_PAUSE		4

#define PCECD_STATUS_GOOD			0
#define PCECD_STATUS_CHECK_COND		1
#define PCECD_STATUS_CONDITION_MET	2
#define PCECD_STATUS_BUSY			4
#define PCECD_STATUS_INTERMEDIATE	8

#define SENSEKEY_NO_SENSE			0x0
#define SENSEKEY_NOT_READY			0x2
#define SENSEKEY_MEDIUM_ERROR		0x3
#define SENSEKEY_HARDWARE_ERROR		0x4
#define SENSEKEY_ILLEGAL_REQUEST	0x5
#define SENSEKEY_UNIT_ATTENTION		0x6
#define SENSEKEY_ABORTED_COMMAND	0xB

#define NSE_NO_DISC					0x0B
#define NSE_TRAY_OPEN				0x0D
#define NSE_SEEK_ERROR				0x15
#define NSE_HEADER_READ_ERROR		0x16
#define NSE_NOT_AUDIO_TRACK			0x1C
#define NSE_NOT_DATA_TRACK			0x1D
#define NSE_INVALID_COMMAND			0x20
#define NSE_INVALID_ADDRESS			0x21
#define NSE_INVALID_PARAMETER		0x22
#define NSE_END_OF_VOLUME			0x25
#define NSE_INVALID_REQUEST_IN_CDB	0x27
#define NSE_DISC_CHANGED			0x28
#define NSE_AUDIO_NOT_PLAYING		0x2C

#define PCECD_CDDAMODE_SILENT		0x00
#define PCECD_CDDAMODE_LOOP			0x01
#define PCECD_CDDAMODE_INTERRUPT	0x02
#define PCECD_CDDAMODE_NORMAL		0x03

// data io commands
#define CD_STAT_GET    0x60
#define CD_STAT_SEND   0x61
#define CD_COMMAND_GET 0x62
#define CD_DATA_GET    0x63
#define CD_DATA_SEND   0x64
#define CD_DATAOUT_REQ 0x65
#define CD_ACK         0x66

#define MAKE_STATUS(s,m) ((uint16_t)((((m)&0xFF) << 8) | ((s)&0xFF)))

typedef struct
{
	uint8_t key;
	uint8_t asc;
	uint8_t ascq;
	uint8_t fru;
} sense_t;

static sense_t sense;


typedef struct
{
	uint32_t latency;
	uint8_t state;
	uint8_t isData;
	int loaded;
	int has_status;
	char data_req;
	char can_read_next;
	char cdda_fifo_halffull;
	int index;
	int lba;
	int cnt;
	int scanOffset;
	int audioLength;
	int audioOffset;
	int CDDAStart;
	int CDDAEnd;
	int CDDAFirst;
	char CDDAMode;
	uint16_t stat;
} pcecd_t;

static pcecd_t pcecdd;


static void CommandError(uint8_t key, uint8_t asc, uint8_t ascq, uint8_t fru) {
	sense.key = key;
	sense.asc = asc;
	sense.ascq = ascq;
	sense.fru = fru;
}

static void SendStatus(uint16_t status) {

	pcecd_debugf("SendStatus: %d", status);
	EnableFpga();
	SPI(CD_STAT_SEND);
	spi16(status);
	DisableFpga();
}

static void PendStatus(uint16_t status) {
	pcecd_debugf("PendStatus: %d", status);
	pcecdd.stat = status;
	pcecdd.has_status = 1;
}

static void SendData(char *buf, uint16_t len, unsigned char dm) {
	EnableFpga();
	SPI(CD_DATA_SEND);
	SPI(len & 0xFF);
	SPI(((len >> 8) & 0xFF) | (dm << 7));
	spi_write(buf, len);
	DisableFpga();
}

static void SendSector(uint16_t len, unsigned char dm) {
	UINT br;
	DISKLED_ON;
	if (toc.tracks[pcecdd.index].type && (pcecdd.lba >= 0)) {
		// data sector

		if (toc.tracks[pcecdd.index].sector_size != 2048)
			f_lseek(&toc.file->file, f_tell(&toc.file->file) + 16);

		pcecd_debugf("Send data sector, lba: %d pos: %llu", pcecdd.lba, f_tell(&toc.file->file));
		f_read(&toc.file->file, sector_buffer, 2048, &br);

		if (toc.tracks[pcecdd.index].sector_size != 2048)
			f_lseek(&toc.file->file, f_tell(&toc.file->file) + (toc.tracks[pcecdd.index].sector_size - 2048 - 16));
		//pcecd_debugf("Send data sector, post pos: %llu", f_tell(&toc.file->file));

		SendData(sector_buffer, 2048, dm);
		//hexdump(buffer, 2048, 0);
	} else {
		f_read(&toc.file->file, sector_buffer, 2352, &br);
		SendData(sector_buffer, 2352, dm);
	}
	DISKLED_OFF;
}

static char CheckDisk() {
	if (!user_io_is_cue_mounted()) {
		pcecdd.state = PCECD_STATE_NODISC;
		CommandError(SENSEKEY_NOT_READY, NSE_NO_DISC, 0, 0);
		SendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));
		return 0;
	}
	return 1;
}

static unsigned long pcecd_read_timer = 0;

static void pcecd_run() {


	if (pcecdd.latency > 0) {
		if(!CheckTimer(pcecd_read_timer)) return;
		pcecd_read_timer = GetTimer(13);
		pcecdd.latency--;
		return;
	}

	if (pcecdd.state == PCECD_STATE_READ) {

		if (pcecdd.index >= toc.last) {
			pcecdd.state = PCECD_STATE_IDLE;
			return;
		}

		if (!pcecdd.can_read_next)
			return;

		if (!user_io_is_cue_mounted()) {
			pcecdd.state = PCECD_STATE_NODISC;
			CommandError(SENSEKEY_NOT_READY, NSE_NO_DISC, 0, 0);
			PendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));
			return;
		}

		if(!CheckTimer(pcecd_read_timer)) return;
		pcecd_read_timer = GetTimer(13);

		pcecdd.can_read_next = 0;

		if (toc.tracks[pcecdd.index].type) {
			// CD-ROM (Mode 1)
			SendSector(2048, 1);
		} else {
			if (pcecdd.lba >= toc.tracks[pcecdd.index].start) {
				pcecdd.isData = 0x00;
			}
			//SectorSend(0);
		}

		pcecdd.cnt--;

		if (!pcecdd.cnt) {
			PendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
			pcecdd.state = PCECD_STATE_IDLE;
		}

		pcecdd.lba++;
		if (pcecdd.lba >=toc.tracks[pcecdd.index].end) {
			pcecdd.index++;
			pcecdd.isData = 0x01;
			f_lseek(&toc.file->file, toc.tracks[pcecdd.index].offset);
		}
	} else if (pcecdd.state == PCECD_STATE_PLAY) {

		if (!user_io_is_cue_mounted()) {
			pcecdd.state = PCECD_STATE_NODISC;
			CommandError(SENSEKEY_NOT_READY, NSE_NO_DISC, 0, 0);
			PendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));
			return;
		}

		pcecdd.index = cue_gettrackbylba(pcecdd.lba);

		if ((pcecdd.lba >= pcecdd.CDDAEnd) || toc.tracks[pcecdd.index].type || pcecdd.index >= toc.last)
		{
			if (pcecdd.CDDAMode == PCECD_CDDAMODE_LOOP) {
				pcecdd.lba = pcecdd.CDDAStart;
				pcecdd.latency = 2; // some time to seek back
			}
			else {
				pcecdd.state = PCECD_STATE_IDLE;
			}

			if (pcecdd.CDDAMode == PCECD_CDDAMODE_INTERRUPT) {
				SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
			}

			pcecd_debugf("playback reached the end %d\n", pcecdd.lba);
		} else if (!pcecdd.cdda_fifo_halffull) {
			for (int i = 0; i <= pcecdd.CDDAFirst; i++) {
				if (!toc.tracks[pcecdd.index].type) {
					f_lseek(&toc.file->file, toc.tracks[pcecdd.index].offset + (pcecdd.lba - toc.tracks[pcecdd.index].start) * 2352);
					//pcecd_debugf("Audio sector send = %i, track = %i, offset = %llu", pcecdd.lba, pcecdd.index, f_tell(&toc.file->file));
					SendSector(2352, 0);
				}
				pcecdd.lba++;
			}
			pcecdd.CDDAFirst = 0;
		}
	}
}

static void pcecd_command() {
	int new_lba = 0;
	msf_t msf;
	int track;
	uint8_t command[12];
	uint8_t buf[32];
	memset(buf, 0, 32);

	int i;

	EnableFpga();
	SPI(CD_COMMAND_GET);

	for (int i = 0; i < 12; i++)
		command[i] = SPI(0);
	DisableFpga();
	//pcecd_debugf("command: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
	//	command[0], command[1], command[2], command[3], command[4], command[5],
	//	command[6], command[7], command[8], command[9], command[10], command[11]);

	switch (command[0]) {
	case PCECD_COMM_TESTUNIT:
		if (CheckDisk()) {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}
		pcecd_debugf("Command TESTUNIT, state = %u", pcecdd.state);
		break;

	case PCECD_COMM_REQUESTSENSE:
		buf[0] = 18;
		buf[1] = 0 | 0x80;

		buf[2] = 0x70;
		buf[4] = sense.key;
		buf[9] = 0x0A;
		buf[14] = sense.asc;
		buf[15] = sense.ascq;
		buf[16] = sense.fru;

		sense.key = sense.asc = sense.ascq = sense.fru = 0;

		SendData(buf + 2, 18, 1);

		pcecd_debugf("Command REQUESTSENSE, key = %02X, asc = %02X, ascq = %02X, fru = %02X", sense.key, sense.asc, sense.ascq, sense.fru);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		break;

	case PCECD_COMM_GETDIRINFO: {
		if (!CheckDisk()) break;

		int len = 0;
		switch (command[1]) {
		case 0:
		default:
			buf[0] = 2;
			buf[1] = 0 | 0x80;
			buf[2] = 1;
			buf[3] = bin2bcd(toc.last);
			len = 2;
			break;

		case 1:
			new_lba = toc.end + 150;
			LBA2MSF(new_lba, &msf);

			buf[0] = 4;
			buf[1] = 0 | 0x80;
			buf[2] = bin2bcd(msf.m);
			buf[3] = bin2bcd(msf.s);
			buf[4] = bin2bcd(msf.f);
			buf[5] = 0;
			len = 4;
			break;

		case 2:
			track = bcd2bin(command[2]);
			new_lba = toc.tracks[track - 1].start + 150;
			LBA2MSF(new_lba, &msf);

			buf[0] = 4;
			buf[1] = 0 | 0x80;
			buf[2] = bin2bcd(msf.m);
			buf[3] = bin2bcd(msf.s);
			buf[4] = bin2bcd(msf.f);
			buf[5] = toc.tracks[track - 1].type << 2;
			len = 4;
			break;
		}

		SendData(buf + 2, len, 1);
		pcecd_debugf("Command GETDIRINFO, [1] = %02X, [2] = %02X(%d)", command[1], command[2], command[2]);
		pcecd_debugf("Send data, len = %u, [2] = %02X, [3] = %02X, [4] = %02X, [5] = %02X\n\x1b[0m", len, buf[2], buf[3], buf[4], buf[5]);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}
		break;

	case PCECD_COMM_READ6: {
		if (!CheckDisk()) break;

		new_lba = ((command[1] << 16) | (command[2] << 8) | command[3]) & 0x1FFFFF;
		int cnt_ = command[4] ? command[4] : 256;

		pcecdd.index = cue_gettrackbylba(new_lba);

		/* HuVideo streams by fetching 120 sectors at a time, taking advantage of the geometry
		 * of the disc to reduce/eliminate seek time */
		if ((pcecdd.lba == new_lba) && (cnt_ == 120))
		{
			pcecdd.latency = 0;
		}
		/*
		else if (command[13] & 0x80) // fast seek (OSD setting)
		{
			pcecdd.latency = 0;
		} */
		else
		{
			pcecdd.latency = 0;//(int)(get_cd_seek_ms(pcecdd.lba, new_lba)/13.33);
		}
		pcecd_debugf("seek time ticks: %d", pcecdd.latency);

		pcecdd.lba = new_lba;
		pcecdd.cnt = cnt_;

		int offset = (new_lba - toc.tracks[pcecdd.index].start) * toc.tracks[pcecdd.index].sector_size + toc.tracks[pcecdd.index].offset;
		f_lseek(&toc.file->file, offset);

		pcecd_debugf("lba: %d index: %d, offset: %d", new_lba, pcecdd.index, offset);

		pcecdd.audioOffset = 0;

		pcecdd.can_read_next = 1;
		pcecdd.state = PCECD_STATE_READ;

		pcecd_debugf("Command READ6, lba = %u, cnt = %u", pcecdd.lba, pcecdd.cnt);
		}
		break;

	case PCECD_COMM_MODESELECT6:
		pcecd_debugf("Command MODESELECT6, cnt = %u", command[4]);

		if (command[4]) {
			pcecdd.data_req = 1;
		}
		else {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}

		break;

	case PCECD_COMM_SAPSP: {
		if (!CheckDisk()) break;

		switch (command[9] & 0xc0)
		{
		default:
		case 0x00:
			new_lba = (command[3] << 16) | (command[4] << 8) | command[5];
			break;

		case 0x40:
			new_lba = MSF2LBA(bcd2bin(command[2]), bcd2bin(command[3]), bcd2bin(command[4]));
			break;

		case 0x80:
		{
			int track = bcd2bin(command[2]);

			if (!track)
				track = 1;
			else if (track > toc.last)
				track = toc.last;
			new_lba = toc.tracks[track - 1].start;
		}
		break;
		}
		/*
		if (command[13] & 0x80) // fast seek (OSD setting)
		{
			pcecdd.latency = 0;
		} 
		else*/
		{
			pcecdd.latency = 0;//(int)(get_cd_seek_ms(this->lba, new_lba) / 13.33);
		}

		pcecd_debugf("seek time ticks: %d", pcecdd.latency);

		pcecdd.lba = new_lba;
		int index = cue_gettrackbylba(new_lba);

		pcecdd.index = index;

		pcecdd.CDDAStart = new_lba;
		pcecdd.CDDAEnd = toc.end;
		pcecdd.CDDAMode = command[1];
		pcecdd.CDDAFirst = 1;

		if (pcecdd.CDDAMode == PCECD_CDDAMODE_SILENT) {
			pcecdd.state = PCECD_STATE_PAUSE;
		} else {
			pcecdd.state = PCECD_STATE_PLAY;
		}

		PendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
	}
		pcecd_debugf("Command SAPSP, start = %d, end = %d, [1] = %02X, [2] = %02X, [9] = %02X\n", pcecdd.CDDAStart, pcecdd.CDDAEnd, command[1], command[2], command[9]);
		break;

	case PCECD_COMM_SAPEP: {
		if (!CheckDisk()) break;

		switch (command[9] & 0xc0)
		{
		default:
		case 0x00:
			new_lba = (command[3] << 16) | (command[4] << 8) | command[5];
			break;

		case 0x40:
			new_lba = MSF2LBA(bcd2bin(command[2]), bcd2bin(command[3]), bcd2bin(command[4]));
			break;

		case 0x80:
		{
			int track = bcd2bin(command[2]);

			if (!track) track = 1;
			new_lba = (track >= toc.last) ? toc.end : (toc.tracks[track - 1].start);
		}
		break;
		}

		pcecdd.CDDAMode = command[1];
		pcecdd.CDDAEnd = new_lba;

		if (pcecdd.CDDAMode == PCECD_CDDAMODE_SILENT) {
			pcecdd.state = PCECD_STATE_IDLE;
		} else {
			pcecdd.state = PCECD_STATE_PLAY;
		}

		pcecd_debugf("Command SAPEP, end = %i, [1] = %02X, [2] = %02X, [9] = %02X", pcecdd.CDDAEnd, command[1], command[2], command[9]);

		if (pcecdd.CDDAMode != PCECD_CDDAMODE_INTERRUPT) {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}
	}
		break;

	case PCECD_COMM_PAUSE:
		if (!CheckDisk()) break;

		pcecdd.state = PCECD_STATE_PAUSE;

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));

		pcecd_debugf("Command PAUSE, current lba = %i\n", pcecdd.lba);
		break;

	case PCECD_COMM_READSUBQ: {
		if (!CheckDisk()) break;

		int lba_rel = pcecdd.lba - toc.tracks[pcecdd.index].start;

		buf[0] = 0x0A;
		buf[1] = 0 | 0x80;
		buf[2] = pcecdd.state == PCECD_STATE_PAUSE ? 2 : (pcecdd.state == PCECD_STATE_PLAY ? 0 : 3);
		buf[3] = 0;
		buf[4] = bin2bcd(pcecdd.index + 1);
		buf[5] = bin2bcd(pcecdd.index);

		LBA2MSF(lba_rel, &msf);
		buf[6] = bin2bcd(msf.m);
		buf[7] = bin2bcd(msf.s);
		buf[8] = bin2bcd(msf.f);

		LBA2MSF(pcecdd.lba, &msf);
		buf[9] = bin2bcd(msf.m);
		buf[10] = bin2bcd(msf.s);
		buf[11] = bin2bcd(msf.f);

		SendData(buf + 2, 10, 1);

		pcecd_debugf("Command READSUBQ, [1] = %02X, track = %i, index = %i, lba_rel = %i, lba_abs = %i\n\x1b[0m", command[1], pcecdd.index + 1, pcecdd.index, lba_rel, pcecdd.lba);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}
		break;

	default:
		CommandError(SENSEKEY_ILLEGAL_REQUEST, NSE_INVALID_COMMAND, 0, 0);

		pcecd_debugf("Command undefined, [0] = %02X, [1] = %02X, [2] = %02X, [3] = %02X, [4] = %02X, [5] = %02X",
			command[0], command[1], command[2], command[3], command[4], command[5]);
		pcecdd.has_status = 0;
		SendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));
		break;
	}
}

static void pcecd_data() {
	uint8_t data[10];

	EnableFpga();
	SPI(CD_DATA_GET);

	for (int i = 0; i < 10; i++)
		data[i] = SPI(0);
	DisableFpga();
	pcecd_debugf("data: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		data[0], data[1], data[2], data[3], data[4],
		data[5], data[6], data[7], data[8], data[9]);
}

static void pcecd_clear_busy() {
	//pcecd_debugf("Clear busy");
	pcecdd.can_read_next = 1;
}

static void pcecd_reset() {
	pcecd_debugf("Reset request");
	pcecdd.latency = 0;
	pcecdd.index = 0;
	pcecdd.lba = 0;
	pcecdd.scanOffset = 0;
	pcecdd.isData = 1;
	pcecdd.state = user_io_is_cue_mounted() ? PCECD_STATE_IDLE : PCECD_STATE_NODISC;
	pcecdd.audioLength = 0;
	pcecdd.audioOffset = 0;
	pcecdd.has_status = 0;
	pcecdd.data_req = 0;
	pcecdd.can_read_next = 0;
	pcecdd.CDDAStart = 0;
	pcecdd.CDDAEnd = 0;
	pcecdd.CDDAMode = PCECD_CDDAMODE_SILENT;
	pcecdd.stat = 0;
	pcecdd.cdda_fifo_halffull = 1;
}

static unsigned long pcecd_timer = 0;

void pcecd_poll() {
	char c;
	EnableFpga();
	c = SPI(CD_STAT_GET); // cmd request
	DisableFpga();
	if (c&0x01) pcecd_command();
	if (c&0x02) pcecd_data();
	if (c&0x04) pcecd_clear_busy();
	if (c&0x08) {
		pcecd_reset();
		EnableFpga();
		SPI(CD_ACK);
		SPI(0);
		DisableFpga();
	}
	pcecdd.cdda_fifo_halffull = (c & 0x10);

	pcecd_run();

	if(CheckTimer(pcecd_timer)) {
		pcecd_timer = GetTimer(13);

		if (pcecdd.has_status && !pcecdd.latency) {
			SendStatus(pcecdd.stat);
			pcecdd.has_status = 0;
		}
		if (pcecdd.data_req) {
			EnableFpga();
			SPI(CD_DATAOUT_REQ);
			SPI(0);
			DisableFpga();
			pcecdd.data_req = 0;
		}
	}
}
