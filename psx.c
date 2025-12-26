#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx.h"
#include "cue_parser.h"
#include "user_io.h"
#include "data_io.h"
#include "utils.h"
#include "debug.h"

typedef enum
{
	UNKNOWN = 0,
	JP,
	US,
	EU
} region_t;

static const char *region_str[] = {"Unknown", "JP", "US", "EU"};

typedef struct
{
	uint32_t track_count;
	uint32_t total_lba;
	uint32_t total_bcd;
	uint16_t libcrypt_mask;
	uint16_t metadata; // lower 2 bits encode the region, 3rd bit is reset request, the other bits are reseved
} __attribute__ ((packed)) disk_header_t;

typedef struct
{
	uint32_t start_lba;
	uint32_t end_lba;
	uint32_t bcd;
	uint32_t reserved;
} __attribute__ ((packed)) track_t;

#define SBI_HEADER_SIZE 4
#define SBI_BLOCK_SIZE  14

static const uint32_t libCryptSectors[16] =
{
	14105,
	14231,
	14485,
	14579,
	14649,
	14899,
	15056,
	15130,
	15242,
	15312,
	15378,
	15628,
	15919,
	16031,
	16101,
	16167,
};

static uint16_t psx_libCryptMask(FIL* sbi_file)
{
	UINT br;
	uint16_t mask = 0;
	if ((f_read(sbi_file, sector_buffer, SECTOR_BUFFER_SIZE, &br)) == FR_OK)
	{
		for (int i = 0;; i++)
		{
			int pos = SBI_HEADER_SIZE + i * SBI_BLOCK_SIZE;
			if (pos >= br) break;
			uint32_t lba = 150 + MSF2LBA(bcd2bin(sector_buffer[pos]), bcd2bin(sector_buffer[pos + 1]), bcd2bin(sector_buffer[pos + 2]));
			psx_debugf("Testing lba from SBI: %d", lba);
			for (int m = 0; m < 16; m++) if (libCryptSectors[m] == lba) mask |= (1 << (15 - m));
		}
	}

	return mask;
}

static void psx_read_sector(char* buffer, unsigned int lba)
{
	UINT br;
	if (!toc.valid) {
		memset(buffer, 0, 2352);
		return;
	}

	int index = cue_gettrackbylba(lba);
	int offset = (lba - toc.tracks[index].start) * toc.tracks[index].sector_size + toc.tracks[index].offset;
	//psx_debugf("read CD lba=%d, track=%d offset=%d (trackstart=%d tracoffset=%d tracksectorsize=%d)", lba, index, offset, toc.tracks[index].start, toc.tracks[index].offset, toc.tracks[index].sector_size);
	if (toc.tracks[index].sector_size != 2352) {
		// unsupported sector size by the core
		memset(buffer, 0, 2352);
	} else {
		DISKLED_ON
		f_lseek(&toc.file->file, offset);
		f_read(&toc.file->file, buffer, 2352, &br);
		DISKLED_OFF
	}
	return;
}

static void psx_send_cue_and_metadata(uint16_t libcrypt_mask, region_t region, int reset)
{
	disk_header_t header;
	track_t track;
	msf_t msf;

	header.track_count = (bin2bcd(toc.last) << 8) | toc.last;
	header.total_lba = toc.end;
	LBA2MSF(toc.end, &msf);
	header.total_bcd = (bin2bcd(msf.m) << 8) | bin2bcd(msf.s);
	header.libcrypt_mask = libcrypt_mask;
	header.metadata = region; // the lower 2 bits of metadata contain the region
	if (reset) header.metadata |= 4; // 3rd bit is reset request

	data_io_set_index(251);
	data_io_file_tx_start();

	EnableFpga();
	SPI(DIO_FILE_TX_DAT);
	spi_write((const char *)&header, sizeof(disk_header_t));
	track.reserved = 0;
	for (int i = 0; i < toc.last; i++) {
		track.start_lba = toc.tracks[i].start;
		track.end_lba = toc.tracks[i].end;
		LBA2MSF(toc.tracks[i].start + 150, &msf);
		track.bcd = ((bin2bcd(msf.m) << 8) | bin2bcd(msf.s)) | ((toc.tracks[i].type ? 0 : 1) << 16);
		psx_debugf("%d start_lba=%d end_lba=%d bcd=%04x", i, track.start_lba, track.end_lba, track.bcd);
		spi_write((const char *)&track, sizeof(track_t));
	}
	DisableFpga();

	data_io_file_tx_done();
}

static region_t psx_get_region()
{
	int license_sector = 4;
	psx_read_sector(sector_buffer, license_sector);
	uint8_t* license_start = (uint8_t*)memmem(sector_buffer, 2352, "          Licensed  by          Sony Computer Entertainment ", 60);
	if (license_start) {
		const uint8_t* region_start = license_start + 60;
		if (memcmp(region_start, "Amer  ica ", 10) == 0)
			return US;
		if (memcmp(region_start, "Inc.", 4) == 0)
			return JP;
		if (memcmp(region_start, "Euro pe", 7) == 0)
			return EU;
	}

	return UNKNOWN;
}

void psx_mount_cd(const unsigned char *name)
{
	if (!toc.valid) return;
	region_t region = psx_get_region();
	uint16_t libcrypt_mask = 0;
	const char *fileExt = 0;
	int len = strlen(name);

	while(len > 2) {
		if (name[len-2] == '.') {
			fileExt = &name[len-1];
			break;
		}
		len--;
	}
	if (fileExt) {
		char sbi[len+3];
		memcpy(sbi, name, len-1);
		strcpy(&sbi[len-1], "SBI");
		FIL sbi_f;
		iprintf("PSX: trying SBI file (%s)\n", sbi);
		if(f_open(&sbi_f, sbi, FA_READ) == FR_OK) {
			libcrypt_mask = psx_libCryptMask(&sbi_f);
			f_close(&sbi_f);
		}
	}

	iprintf("PSX: CD region: %s crypt_mask: %04x\n", region_str[region], libcrypt_mask);
	psx_send_cue_and_metadata(libcrypt_mask, region, 0);
}

void psx_read_cd(uint8_t drive_index, unsigned int lba)
{
	user_io_sd_ack(drive_index);
	if (lba>=150) lba-=150;
	psx_read_sector(sector_buffer, lba);
	spi_uio_cmd_cont(UIO_SECTOR_RD);
	spi_write(sector_buffer, 2352);
	DisableIO();
}
