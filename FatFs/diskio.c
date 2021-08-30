/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "hardware.h"
#include "mmc.h"

/* Definitions of physical drive number for each drive */
#define DEV_MMC		0
#define DEV_USB		1


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	switch (pdrv) {

	case DEV_MMC :
		result = MMC_CheckCard();

		// translate the reslut code here
                stat = result ? 0 : STA_NOINIT;
		return stat;
/*
	case DEV_USB :
		result = USB_disk_status();

		// translate the reslut code here

		return stat;
*/
	}

	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	switch (pdrv) {
	case DEV_MMC :
		//result = MMC_disk_initialize();

		// translate the reslut code here
		stat = 0;
		return stat;
/*
	case DEV_USB :
		result = USB_disk_initialize();

		// translate the reslut code here

		return stat;
*/
	}

	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	int result;

	//iprintf("disk_read: %d LBA: %d count: %d\n", pdrv, sector, count);

	switch (pdrv) {
	case DEV_MMC :
		// translate the arguments here
		if (count == 1)
			result = MMC_Read(sector, buff);
		else
			result = MMC_ReadMultiple(sector, buff, count);

		// translate the reslut code here
		res = result ? RES_OK : RES_ERROR;
		return res;
/*
	case DEV_USB :
		// translate the arguments here

		result = USB_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;
*/
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res;
	int result;

	//iprintf("disk_write: %d LBA: %d count: %d\n", pdrv, sector, count);

	switch (pdrv) {
	case DEV_MMC :
		// translate the arguments here
		if (count == 1)
			result = MMC_Write(sector, buff);
		else
			result = MMC_WriteMultiple(sector, buff, count);

		// translate the reslut code here
		res = result ? RES_OK : RES_ERROR;
		return res;
/*
	case DEV_USB :
		// translate the arguments here

		result = USB_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;
*/
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;
	int result;

	switch (pdrv) {
	case DEV_MMC :

		// Process of the command for the MMC/SD card

		return RES_OK;
/*
	case DEV_USB :

		// Process of the command the USB drive

		return res;
*/
	}

	return RES_PARERR;
}

DWORD get_fattime()
{
	uint8_t date[7]; //year,month,date,hour,min,sec,day
	DWORD   fattime = ((DWORD)(FF_NORTC_YEAR - 1980) << 25 | (DWORD)FF_NORTC_MON << 21 | (DWORD)FF_NORTC_MDAY << 16);

	if (GetRTC((uint8_t*)&date)) {
		fattime = ((date[0] - 80) << 25) |
		          ((date[1] & 0x0f) << 21) |
		          ((date[2] & 0x1f) << 16) |
		          ((date[3] & 0x1f) << 11) |
		          ((date[4] & 0x3f) << 5) |
		          ((date[5] >> 1) & 0x1f);
	}
	return fattime;
}
