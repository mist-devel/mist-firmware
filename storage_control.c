/*
  storage_control.c

*/

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "swab.h"
#include "utils.h"
#include "storage_control.h"
#include "fat_compat.h"
#include "usbdev.h"
#include "mmc.h"
#include "debug.h"

#define SENSEKEY_NO_SENSE        0x0
#define SENSEKEY_NOT_READY       0x2
#define SENSEKEY_MEDIUM_ERROR    0x3
#define SENSEKEY_HARDWARE_ERROR  0x4
#define SENSEKEY_ILLEGAL_REQUEST 0x5
#define SENSEKEY_UNIT_ATTENTION  0x6
#define SENSEKEY_ABORTED_COMMAND 0xB

typedef struct
{
	uint8_t key;
	uint8_t asc;
	uint8_t ascq;
} sense_t;

static sense_t sense;

typedef struct {
	uint32_t dCBWSignature;
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t  bmCBWFlags;
	uint8_t  bCBWLUN;
	uint8_t  bCBWCLength;
	uint8_t  CBWCB[16];
} __attribute__ ((packed)) CBW_t;

typedef struct {
	uint32_t dCSWSignature;
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t  bCSWStatus;
} __attribute__ ((packed)) CSW_t;

typedef struct {
	uint8_t  DeviceType : 5;
	uint8_t  DeviceTypeQualifier : 3;
	uint8_t  DeviceTypeModifier : 7;
	uint8_t  RemovableMedia : 1;
	uint8_t  Versions;
	uint8_t  ResponseDataFormat : 4;
	uint8_t  HiSupport : 1;
	uint8_t  NormACA : 1;
	uint8_t  ReservedBit : 1;
	uint8_t  AERC : 1;
	uint8_t  AdditionalLength;
	uint8_t  Reserved[2];
	uint8_t  SoftReset : 1;
	uint8_t  CommandQueue : 1;
	uint8_t  Reserved2 : 1;
	uint8_t  LinkedCommands : 1;
	uint8_t  Synchronous : 1;
	uint8_t  Wide16Bit : 1;
	uint8_t  Wide32Bit : 1;
	uint8_t  RelativeAddressing : 1;
	uint8_t  VendorId[8];
	uint8_t  ProductId[16];
	uint8_t  ProductRevisionLevel[4];
	uint8_t  VendorSpecific[20];
	uint8_t  Reserved3[2];
	uint8_t  VersionDescriptors[8];
	uint8_t  Reserved4[30];
} __attribute__ ((packed)) INQUIRYDATA_t;

typedef struct {
	uint8_t  ErrorCode  :7;
	uint8_t  Valid  :1;
	uint8_t  SegmentNumber;
	uint8_t  SenseKey  :4;
	uint8_t  Reserved  :1;
	uint8_t  IncorrectLength  :1;
	uint8_t  EndOfMedia  :1;
	uint8_t  FileMark  :1;
	uint8_t  Information[4];
	uint8_t  AdditionalSenseLength;
	uint8_t  CommandSpecificInformation[4];
	uint8_t  AdditionalSenseCode;
	uint8_t  AdditionalSenseCodeQualifier;
	uint8_t  FieldReplaceableUnitCode;
	uint8_t  SenseKeySpecific[3];
} __attribute__ ((packed)) SENSEDATA_t;

typedef struct {
	uint32_t LBA;
	uint32_t blocklen;
} __attribute__ ((packed)) CAPACITYDATA_t;

typedef struct {
	uint8_t  Reserved[3];
	uint8_t  Length;
	uint32_t Blocks;
	uint8_t  DescriptorType  :1;
	uint8_t  Reserved2 : 7;
	uint8_t  Blocklen[3];
} __attribute__ ((packed)) FORMATCAPACITYDATA_t;

static void clear_sense() {
	sense.key = sense.asc = sense.ascq = 0;
}

static void make_sense(uint8_t key, uint8_t asc, uint8_t ascq) {
	sense.key = key;
	sense.asc = asc;
	sense.ascq = ascq;
}

static void scsi_inquiry(uint8_t *cmd) {
	uint16_t len = cmd[3]<<8 | cmd[4];
	INQUIRYDATA_t *data = (INQUIRYDATA_t*)sector_buffer;
	memset(data, 0, sizeof(INQUIRYDATA_t));
	data->Versions = 0x04;
	data->RemovableMedia = 1;
	data->ResponseDataFormat = 2;
	data->AdditionalLength = 0x1f;
	memcpy(data->VendorId, "Lotharek", 8);
	memcpy(data->ProductId, "MiST Board      ", 16);
	memcpy(data->ProductRevisionLevel, "1.3 ", 4);
	usb_storage_write(sector_buffer, MIN(len, sizeof(INQUIRYDATA_t)));
}

static void scsi_readcapacity(uint8_t *cmd) {
	CAPACITYDATA_t cap;
	cap.LBA = swab32(MMC_GetCapacity()-1);
	cap.blocklen = swab32(512);
	usb_storage_write((const char*) &cap, sizeof(CAPACITYDATA_t));
}

static void scsi_request_sense(uint8_t *cmd) {
	uint8_t len = cmd[4];
	SENSEDATA_t dat;

	memset(&dat, 0, sizeof(SENSEDATA_t));
	dat.ErrorCode = 0x70;
	dat.Valid = 1;
	dat.SenseKey = sense.key;
	dat.AdditionalSenseLength = sizeof(SENSEDATA_t)-7;
	dat.AdditionalSenseCode = sense.asc;
	dat.AdditionalSenseCodeQualifier = sense.ascq;
	usb_storage_write((const char*) &dat, MIN(len, sizeof(SENSEDATA_t)));
}

static void scsi_mode_sense(uint8_t *cmd) {
	uint16_t len, datalen;
	if (cmd[0] == 0x5A) { // MODE_SENSE10
		len = cmd[7]<<8 | cmd[8];
		datalen = 8;
		sector_buffer[0] = 0x00;
		sector_buffer[1] = datalen-2;
		sector_buffer[2] = 0;
		sector_buffer[3] = mmc_write_protected() ? 0x80 : 0x00;
		sector_buffer[4] = sector_buffer[5] = sector_buffer[6] = sector_buffer[7] = 0;
	} else {
		len = cmd[4]; // MODE SENSE6
		datalen = 4;
		sector_buffer[0] = datalen-1;
		sector_buffer[1] = 0;
		sector_buffer[2] = mmc_write_protected() ? 0x80 : 0x00;
		sector_buffer[3] = 0;
	}
	usb_storage_write(sector_buffer, MIN(len, datalen));
}

static void scsi_read_format_capacities(uint8_t *cmd) {
	uint16_t len = cmd[7]<<8 | cmd[8];
	FORMATCAPACITYDATA_t dat;

	memset(&dat, 0, sizeof(FORMATCAPACITYDATA_t));
	dat.Length = 8;
	dat.Blocks = MMC_GetCapacity();
	dat.Blocklen[1] = 0x02; // 512 bytes
	usb_storage_write((const char*) &dat, MIN(len, sizeof(FORMATCAPACITYDATA_t)));
}

static uint8_t scsi_read(uint8_t *cmd) {
	uint32_t lba = cmd[2]<<24 | cmd[3]<<16 | cmd[4]<<8 | cmd[5];
	uint16_t len = cmd[7]<<8 | cmd[8];
	storage_debugf("Read lba=%d len=%d", lba, len);
	while (len) {
		uint8_t ret;
		uint16_t read = MIN(len, SECTOR_BUFFER_SIZE/512);
		DISKLED_ON
		if (len == 1) ret=MMC_Read(lba, sector_buffer);
		else ret=MMC_ReadMultiple(lba, sector_buffer, read);
		DISKLED_OFF
		if (!ret) {
			iprintf("STORAGE: Error reading from MMC (lba=%d, len=%d)\n", lba, len);
			return 0;
		}
		lba+=read;
		len-=read;
		usb_storage_write(sector_buffer, read*512);
	}
	return 1;
}

static uint8_t scsi_write(uint8_t *cmd) {
	uint32_t lba = cmd[2]<<24 | cmd[3]<<16 | cmd[4]<<8 | cmd[5];
	uint16_t len = cmd[7]<<8 | cmd[8];
	storage_debugf("Write lba=%d len=%d", lba, len);
	while (len) {
		uint8_t ret;
		uint16_t write = MIN(len, SECTOR_BUFFER_SIZE/512);
		uint16_t read, total_read = write*512;
		uint8_t *buf = sector_buffer;
		long to = GetTimer(100);  // wait max 100ms for host
		while (total_read) {
			if (CheckTimer(to)) {
				iprintf("STORAGE: Timeout while waiting for USB host during write (lba=%d, len=%d)\n", lba, len);
				return 0;
			}
			read = usb_storage_read(buf, total_read);
			total_read -= read;
			buf += read;
		}
		//hexdump(sector_buffer, write*512, 0);
		DISKLED_ON
		if (write == 1) ret = MMC_Write(lba, sector_buffer);
		else ret = MMC_WriteMultiple(lba, sector_buffer, write);
		DISKLED_OFF
		if (!ret) return 0;
		lba+=write;
		len-=write;
	}
	return 1;
}

static void storage_control_send_csw(uint32_t tag, uint8_t status) {
	CSW_t* csw = (CSW_t*)sector_buffer;
	csw->dCSWSignature = 0x53425355;
	csw->dCSWTag = tag;
	csw->dCSWDataResidue = 0;
	csw->bCSWStatus = status;
	usb_storage_write(sector_buffer, sizeof(CSW_t));
}

void storage_control_poll(void) {
	uint16_t read;
	uint32_t tag;
	uint8_t ret;

	if (!usb_storage_is_configured()) return;

	// read CSW
	if((read = usb_storage_read(sector_buffer, BULK_OUT_SIZE)) != 0) {
		CBW_t *cbw = (CBW_t*)sector_buffer;
		if (read != 31 || cbw->dCBWSignature != 0x43425355) {
			return;
		}
		tag = cbw->dCBWTag;
		//hexdump(sector_buffer, read, 0);
		//iprintf("\n");
		switch (cbw->CBWCB[0]) {
			case 0x00:
				storage_debugf("Test Unit Ready");
				clear_sense();
				storage_control_send_csw(tag, 0);
				break;
			case 0x03:
				storage_debugf("Request sense");
				scsi_request_sense(cbw->CBWCB);
				storage_control_send_csw(tag, 0);
				break;
			case 0x12:
				storage_debugf("Inquiry");
				clear_sense();
				scsi_inquiry(cbw->CBWCB);
				storage_control_send_csw(tag, 0);
				break;
			case 0x1A:
			case 0x5A:
				storage_debugf("Mode Sense");
				clear_sense();
				scsi_mode_sense(cbw->CBWCB);
				storage_control_send_csw(tag, 0);
				break;
			case 0x1E:
				storage_debugf("Prevent Removal");
				clear_sense();
				storage_control_send_csw(tag, 0);
				break;
			case 0x23:
				storage_debugf("Read format capacities");
				clear_sense();
				scsi_read_format_capacities(cbw->CBWCB);
				storage_control_send_csw(tag, 0);
				break;
			case 0x25:
				storage_debugf("Read Capacity");
				clear_sense();
				scsi_readcapacity(cbw->CBWCB);
				storage_control_send_csw(tag, 0);
				break;
			case 0x28:
				ret = scsi_read(cbw->CBWCB);
				if (ret)
					clear_sense();
				else
					make_sense(SENSEKEY_MEDIUM_ERROR, 0x11, 0x00);
				storage_control_send_csw(tag, !ret);
				break;
			case 0x2A:
				ret = scsi_write(cbw->CBWCB);
				if (ret)
					clear_sense();
				else
					make_sense(SENSEKEY_MEDIUM_ERROR, 0x03, 0x00);
				storage_control_send_csw(tag, !ret);
				break;
			case 0x1B:
				storage_debugf("Start stop unit");
				clear_sense();
				storage_control_send_csw(tag, 0);
				break;
			default:
				iprintf("STORAGE: Unhandled cmd: %02x", cbw->CBWCB[0]);
				make_sense(SENSEKEY_ILLEGAL_REQUEST, 0x20, 0x00);
				storage_control_send_csw(tag, 1);
				break;
		}
	}
}
