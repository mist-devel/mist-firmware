/*
  scsi.h

*/

#ifndef SCSI_H
#define SCSI_H


#define SENSEKEY_NO_SENSE        0x0
#define SENSEKEY_SOFT_ERROR      0x1
#define SENSEKEY_NOT_READY       0x2
#define SENSEKEY_MEDIUM_ERROR    0x3
#define SENSEKEY_HARDWARE_ERROR  0x4
#define SENSEKEY_ILLEGAL_REQUEST 0x5
#define SENSEKEY_UNIT_ATTENTION  0x6
#define SENSEKEY_ABORTED_COMMAND 0xB


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

#endif
