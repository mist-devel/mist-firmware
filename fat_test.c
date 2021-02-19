#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "fat.h"

#define FAT_IMG "/dev/sdd1"
//#define FAT_IMG "test.img"

extern DIRENTRY DirEntry[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;
extern unsigned char iSelectedEntry;
extern unsigned long iCurrentDirectory;
extern char DirEntryLFN[MAXDIRENTRIES][261];

FILE * fp;

void iprintf(const char *format, ...) {
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);
}

unsigned char MMC_CheckCard() {
	return 1;
}

unsigned char MMC_Read(unsigned long lba, unsigned char *pReadBuffer) {
//	printf("MMC_Read lba: %d\n", lba);
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, 1, fp);
#if 0
	for (int i = 0; i < 512; i++) {
		if (i%16 == 0) printf("\n");
		printf("%02x ", pReadBuffer[i]);
	}
	printf("\n");
#endif
}

unsigned char MMC_Write(unsigned long lba, unsigned char *pWriteBuffer) {
}

unsigned char MMC_ReadMultiple(unsigned long lba, unsigned char *pReadBuffer, unsigned long nBlockCount) {
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, nBlockCount, fp);
}

void ErrorMessage(const char *message, unsigned char code) {
	printf(message);
}

void BootPrint(const char *message) {
	printf(message);
}

int main () {
	unsigned char i;
	unsigned char k;
	char *lfn;
	int page = 0;

	fp = fopen(FAT_IMG, "r+");
	if (!fp) {
		perror(0);
		return(-1);
	}
	FindDrive();
	ChangeDirectory(DIRECTORY_ROOT);
	printf("sizeof(DirEntry) = %d %d\n", sizeof(DIRENTRY), sizeof(unsigned long));

	ScanDirectory(SCAN_INIT, "RBF", SCAN_DIR | SCAN_LFN);
	printf("nDirEntries = %d\n", nDirEntries);
	while (1) {
		for (i = 0; i < nDirEntries; i++)
		{
			k = sort_table[i];
			lfn = DirEntryLFN[k];
			if (lfn) printf("%c %s\n",i == iSelectedEntry ? '*' : ' ', lfn); 
			else printf("%c %s %d %d\n", i == iSelectedEntry ? '*' : ' ', DirEntry[k].Name, DirEntry[k].StartCluster, DirEntry[k].FileSize);
		}
		if (page == 2) break;
		if (nDirEntries == 8) {
			printf("Next Page\n");
			ScanDirectory(SCAN_NEXT_PAGE, "RBF", SCAN_DIR | SCAN_LFN);
			page++;
		} else {
			break;
		}
	}
	fclose(fp);
	return(0);
}
