#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "fat_compat.h"

//#define FAT_IMG "/dev/sdd"
//#define TESTDIR "/c64/games/d64/s"
#define FAT_IMG "test-arcade.img"
#define TESTDIR "/"

extern FILINFO  DirEntries[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;
extern unsigned char iSelectedEntry;

FILE * fp;

void dump(unsigned char *buf) {
	for (int i = 0; i < 512; i++) {
		if (i%16 == 0) {
			printf(" ");
			for (int j = 0; j < 16; j++) {
				if (buf[i-16+j] > 32 && buf[i-16+j] < 128)
					printf("%c",buf[i-16+j]);
				else
					printf(" ");
			}
			printf("\n");
		}
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

void iprintf(const char *format, ...) {
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);
}

void FatalError(const char *msg, int errno) {
	printf("Fatal error: %s (%d)\n", msg, errno);
	exit(1);
}

unsigned char MMC_CheckCard() {
	return 1;
}

unsigned char MMC_Read(unsigned long lba, unsigned char *pReadBuffer) {
//	printf("MMC_Read lba: %d\n", lba);
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, 1, fp);
	return(1);
}

unsigned char MMC_Write(unsigned long lba, unsigned char *pWriteBuffer) {
	fseek(fp, lba << 9, SEEK_SET);
	fwrite(pWriteBuffer, 512, 1, fp);
	return(1);
}

unsigned char MMC_ReadMultiple(unsigned long lba, unsigned char *pReadBuffer, unsigned long nBlockCount) {
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, nBlockCount, fp);
	return(1);
}

void ErrorMessage(const char *message, unsigned char code) {
	printf(message);
}

void BootPrint(const char *message) {
	printf(message);
}

////////////////////////////////////////////////////////////////////

void FileReadTest() {
	char *fname = "ZAXXON  ARC";
	FIL file;
	char buf[512];

	if (FileOpenCompat(&file, fname, FA_READ) == FR_OK) {
		FileReadBlock(&file, buf);
		dump(buf);
		f_close(&file);
	} else {
		printf("Error opening %s\n", fname);
	}

}

void FileNextBlockTest() {
	char *fname = "POOYAN  ROM";
	FIL file;
	FILE *fp;
	char buf[512];
	FSIZE_t fsize;
	DWORD clmt[99];

	clmt[0] = 99;

	if (FileOpenCompat(&file, fname, FA_READ) == FR_OK) {
		file.cltbl = clmt;
		if (f_lseek(&file, CREATE_LINKMAP) != FR_OK) {
			f_close(&file);
			return;
		}
		fp = fopen("dump.bin", "w");
		fsize = f_size(&file);
		printf("File: %s, size: %llu\n", fname, fsize);
		while (fsize) {
			FileReadNextBlock(&file, buf);
			//dump(buf);
			fwrite(buf, 512, 1, fp);
			if (fsize > 512) fsize -= 512; else fsize = 0;
		}
		f_close(&file);
		fclose(fp);
	} else {
		printf("Error opening %s\n", fname);
	}

}

void ScanDirectoryTest() {
	unsigned char i;
	unsigned char k;
	unsigned long lastStartCluster;
	int page = 0;

	ChangeDirectoryName(TESTDIR);

	ScanDirectory(SCAN_INIT, "*", SCAN_DIR | SCAN_LFN);
	printf("nDirEntries = %d\n", nDirEntries);
	while (1) {
		for (i = 0; i < nDirEntries; i++)
		{
			k = sort_table[i];
			printf("%c %s %llu", i == iSelectedEntry ? '*' : ' ', DirEntries[k].fname, DirEntries[k].fsize);
			printf("\n");
		}
		lastStartCluster = DirEntries[0].fclust;
		if (nDirEntries == 8) {
			iSelectedEntry = MAXDIRENTRIES -1;
			printf("Next Page\n");
			ScanDirectory(SCAN_NEXT_PAGE, "*", SCAN_DIR | SCAN_LFN);
			if (DirEntries[0].fclust == lastStartCluster) break;
			page++;
		} else {
			break;
		}
	}
}

int main () {

	fp = fopen(FAT_IMG, "r");
	if (!fp) {
		perror(0);
		return(-1);
	}
	FindDrive();
	FileReadTest();
	FileNextBlockTest();
	ScanDirectoryTest();

	fclose(fp);
	return(0);
}
