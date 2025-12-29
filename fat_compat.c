#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "mmc.h"
#include "fat_compat.h"
#include "fpga.h"
#include "osd.h"
#include "attrs.h"
#include "utils.h"

#include "FatFs/ff.h"
#include "FatFs/diskio.h"

unsigned char sector_buffer[SECTOR_BUFFER_SIZE]; // sector buffer for one CDDA sector (or 4 SD sector)
struct PartitionEntry partitions[4];             // lbastart and sectors will be byteswapped as necessary
int partitioncount;

FATFS fs;
char fat_device = 0;
uint32_t      iPreviousDirectory = 0;

#define PATHLEN 255
static char cwd[PATHLEN];

int8_t fat_uses_mmc(void) {
	return(fat_device == 0);
}

int8_t fat_medium_present() {
	if (fat_device == 0)
		return MMC_CheckCard();
	else
		return 1;
}

void fat_switch_to_usb() {
	fat_device = 1;
}

static const char fs_type_none[] = "NONE";
static const char fs_type_fat12[] = "FAT12";
static const char fs_type_fat16[] = "FAT16";
static const char fs_type_fat32[] = "FAT32";
static const char fs_type_exfat[] = "EXFAT";
static const char fs_type_unknown[] = "UNKNOWN";

char *fs_type_to_string(void) {
	switch (fs.fs_type) {
	case 0:
		return (char *)&fs_type_none;
		break;
	case FS_FAT12:
		return (char *)&fs_type_fat12;
		break;
	case FS_FAT16:
		return (char *)&fs_type_fat16;
		break;
	case FS_FAT32:
		return (char *)&fs_type_fat32;
		break;
	case FS_EXFAT:
		return (char *)&fs_type_exfat;
		break;
	default:
		return (char *)&fs_type_unknown;
		break;
	}
}

// Convert XXXXXXXXYYY to XXXXXXXX.YYY
void fnameconv(char dest[11+2], const char *src) {
	char *c;

	// copy and append nul
	strncpy(dest, src, 8);
	for(c=dest+7;*c==' ';c--); c++;
	*c++ = '.';
	strncpy(c, src+8, 3);
	for(c+=2;*c==' ';c--); c++;
	*c++='\0';
}

// FindDrive() checks if a card is present and contains FAT formatted primary partition
unsigned char FindDrive(void) {

	char res;
	partitioncount=0;
	if (disk_read(0, sector_buffer, 0, 1)) return(0);

	struct MasterBootRecord *mbr=(struct MasterBootRecord *)sector_buffer;
	memcpy(&partitions[0],&mbr->Partition[0],sizeof(struct PartitionEntry));
	memcpy(&partitions[1],&mbr->Partition[1],sizeof(struct PartitionEntry));
	memcpy(&partitions[2],&mbr->Partition[2],sizeof(struct PartitionEntry));
	memcpy(&partitions[3],&mbr->Partition[3],sizeof(struct PartitionEntry));

	if(mbr->Signature == 0xaa55) {
		// get start of first partition
		for(partitioncount=4;(partitions[partitioncount-1].sectors==0) && (partitioncount>1); --partitioncount);

		iprintf("Partition Count: %d\n",partitioncount);
		int i;
		for(i=0;i<partitioncount;++i) {
			iprintf("Partition: %d",i);
			iprintf("  Start: %ld",partitions[i].startlba);
			iprintf("  Size: %ld\n",partitions[i].sectors);
		}

	}
	strcpy(cwd, "/");
	if (f_mount(&fs, "", 1) != FR_OK) return (0);

	// some debug output

	iprintf("Partition type: ");
	iprintf(fs_type_to_string());
	iprintf("\n");
	iprintf("fat_size: %lu\n", fs.fsize);
	iprintf("fat_number: %u\n", fs.n_fats);
	iprintf("fat_start: %lu\n", fs.fatbase);
	iprintf("root_directory_start: %lu\n", fs.dirbase);
	iprintf("dir_entries: %u\n", fs.n_rootdir);
	iprintf("data_start: %lu\n", fs.database);
	iprintf("cluster_size: %u\n", fs.csize);
	iprintf("free_clusters: %lu\n", fs.free_clst);

	return(1);
}

// ExFat doesn't have directory backlink (..), thus need book-keeping the current directory
void ChangeDirectoryName(const char *name) {
	uint32_t      iPreviousDirectoryTmp = fs.cdir;

	iprintf("ChangeDirectoryName: %s -> %s = ", cwd, name);
	if(name[0] == '/') {
		// Absolute path
		strcpy(sector_buffer, name);
	} else if (!strcmp(name, "..")) {
		// Parent directory
		strcpy(sector_buffer, cwd);
		int i = strlen(sector_buffer);
		while (i>1 && sector_buffer[i-1] != '/') i--;
		if (i) {
			if (i==1) strcpy(sector_buffer, "/");
			else sector_buffer[i-1] = 0;
		}
	} else {
		// Append to the end
		int cwdlen = strlen(cwd);
		int namelen = strlen(name);
		if ((cwdlen + namelen) < (PATHLEN - 2)) {
			strcpy(sector_buffer, cwd);
			if(cwdlen>1) strcat(sector_buffer, "/");
			strcat(sector_buffer, name);
		}
	}
	if (f_chdir(sector_buffer) == FR_OK && fs.cdir != iPreviousDirectoryTmp) {
		iPreviousDirectory = iPreviousDirectoryTmp;
		strcpy(cwd, sector_buffer);
	}

	iprintf("%s\n", cwd);
}

// Simplified function which doesn't use any library routines
// Requires an initialized cltbl
#pragma section_code_init
RAMFUNC FRESULT FileReadNextBlock (
	FIL* fp, 	/* Open file to be read */
	void* buff	/* Data buffer to store the read data */
)
{
	FRESULT res;
	DWORD clst;
	WORD csize;
	LBA_t sect;
	FSIZE_t remain;
	UINT rcnt, cc, csect;
	DWORD cl, ncl, *tbl;

	csect = (UINT)((fp->fptr >> 9) & (fs.csize - 1));	/* Sector offset in the cluster */
	if (csect == 0) {					/* On the cluster boundary? */
		if (fp->fptr == 0) {				/* On the top of the file? */
			clst = fp->obj.sclust;			/* Follow cluster chain from the origin */
		} else {					/* Middle or end of the file */
			tbl = fp->cltbl + 1; 			/* Top of CLMT */

			//cl = (DWORD)((fp->fptr >> 9) / fs.csize);	/* Cluster order from top of the file */
			/* dividing by power of 2 without library divider */
			cl = (DWORD)(fp->fptr >> 9);
			csize = fs.csize;
			if (!csize) return (FR_INT_ERR);
			for (;;) {
				if (csize & 1) break;
				cl = cl >> 1;
				csize = csize >> 1;
			}

			for (;;) {
				ncl = *tbl++;			/* Number of cluters in the fragment */
				if (ncl == 0) return (FR_INT_ERR); /* End of table? (error) */
				if (cl < ncl) break;		/* In this fragment? */
				cl -= ncl; tbl++;		/* Next fragment */
			}
			clst = cl + *tbl;			/* Return the cluster number */
		}
		if (clst < 2) return(FR_INT_ERR);
		if (clst == 0xFFFFFFFF) return(FR_DISK_ERR);
		fp->clust = clst;				/* Update current cluster */
	}
	clst = fp->clust;
	clst -= 2;						/* Cluster number is origin from 2 */
	if (clst >= fs.n_fatent - 2) return(FR_INT_ERR);	/* Is it invalid cluster number? */
	sect = fs.database + (LBA_t)fs.csize * clst;		/* Start sector number of the cluster */
	if (sect == 0) return(FR_INT_ERR);
	sect += csect;
	MMC_Read(sect, buff);
	fp->sect = sect;
	fp->fptr += 512;
	return (FR_OK);
}
#pragma section_no_code_init

// Open a file with name of XXXXXXXXYYY
FRESULT FileOpenCompat(FIL *file, const char *name, BYTE mode) {
	char n[11+3];
	fnameconv(&n[1], name);
	FRESULT res;

	n[0] = '/';
	res = f_open(file, n, mode);
	if (res != FR_OK)
		iprintf("Error opening file \"%s\" (%d)\n", n, res);
	return (res);
}

// Read an 512-byte block
FRESULT FileReadBlock(FIL *file, unsigned char *pBuffer) {
	UINT br;
	return (f_read(file, pBuffer, 512, &br));
}

FRESULT FileReadBlockEx(FIL *file, unsigned char *pBuffer, unsigned int len) {
	UINT br;
	return (f_read(file, pBuffer, 512*len, &br));
}

// Write an 512-byte block
FRESULT FileWriteBlock(FIL *file, unsigned char *pBuffer) {
	UINT bw;
	return (f_write(file, pBuffer, 512, &bw));
}


FILINFO       DirEntries[MAXDIRENTRIES];
unsigned char sort_table[MAXDIRENTRIES];
unsigned char nDirEntries = 0;          // entries in DirEntry table
unsigned char iSelectedEntry = 0;       // selected entry index
unsigned char maxDirEntries = 0;

static FILINFO       t_DirEntries[MAXDIRENTRIES];
static unsigned char t_sort_table[MAXDIRENTRIES];

static DIR           dir;
static FILINFO       fil;
static unsigned char nNewEntries = 0;      // indicates if a new entry has been found (used in scroll mode)

FAST static int CompareDirEntries(FILINFO *pDirEntry1, FILINFO *pDirEntry2)
{
	int rc;

	if (((pDirEntry2->fattrib & AM_DIR) && !(pDirEntry1->fattrib & AM_DIR)) // directories first
	|| ((pDirEntry2->fname[0] == '.' && pDirEntry2->fname[1] == '.'))) // parent directory entry at top
		return 1;

	if (((pDirEntry1->fattrib & AM_DIR) && !(pDirEntry2->fattrib & AM_DIR)) // directories first
	|| ((pDirEntry1->fname[0] == '.' && pDirEntry1->fname[1] == '.'))) // parent directory entry at top
		return -1;

	rc = _strnicmp(pDirEntry1->fname, pDirEntry2->fname, FF_LFN_BUF+1);
	return(rc);
}

FAST static char CompareExt(const char *fileName, const char *extension)
{
	char found = 0;
	const char *fileExt = GetExtension(fileName);
	if (!fileExt) return 0;

	while(!found && *extension)
	{
		found = 1;
		for (int i = 0; i < 3; i++)
		{
			if (!fileExt[i]) break;
			if (extension[i] == '?') continue;
			if (tolower(extension[i]) != tolower(fileExt[i])) found = 0;
		}

		if (strlen(extension) < 3) break;
		extension += 3;
	}

	return found;
}

FAST const char *GetExtension(const char *fileName) {
	const char *fileExt = 0;
	int len = strlen(fileName);

	while(len > 2) {
		if (fileName[len-2] == '.') {
			fileExt = &fileName[len-1];
			break;
		}
		len--;
	}
	return fileExt;
}

FAST static void SortTempTable(char prev) {
	unsigned char x;
	for (int i = nNewEntries - 1; i > 0; i--) {// one pass bubble-sorting (table is already sorted, only the new item must be placed in order)
		if ((prev * CompareDirEntries(&t_DirEntries[t_sort_table[i]], &t_DirEntries[t_sort_table[i-1]])) > 0) {
			x = t_sort_table[i];
			t_sort_table[i] = t_sort_table[i-1];
			t_sort_table[i-1] = x;
		}
		else
			break; // don't check further entries as they are already sorted
	}
}

//mode: SCAN_INIT, SCAN_PREV, SCAN_NEXT, SCAN_PREV_PAGE, SCAN_NEXT_PAGE
char ScanDirectory(unsigned long mode, char *extension, unsigned char options) {

	char rc = 0; //return code
	char find_file = 0;
	char find_dir = 0;
	char is_file = 0;
	char initial = 1;
	int i;
	unsigned char x;

	maxDirEntries = OsdLines();

	if (mode == SCAN_INIT || mode == SCAN_INIT_FIRST)
	{
		nDirEntries = 0;
		iSelectedEntry = 0;
		for (i = 0; i < maxDirEntries; i++)
			sort_table[i] = i;
		if (f_opendir(&dir, ".") != FR_OK) return 0;
	}
	else
	{
		if (nDirEntries == 0) // directory is empty so there is no point in searching for any entry
			return 0;

		if (mode == SCAN_NEXT)
		{
			if (iSelectedEntry + 1 < nDirEntries) // scroll within visible items
			{
				iSelectedEntry++;
				return 0;
			}
			if (nDirEntries < maxDirEntries)
				return 0;
		}
		else if (mode == SCAN_PREV)
		{
			if (iSelectedEntry > 0) // scroll within visible items
			{
				iSelectedEntry--;
				return 0;
			}
		}
		else if (mode ==SCAN_NEXT_PAGE)
		{
			if (iSelectedEntry + 1 < nDirEntries)
			{
				iSelectedEntry = nDirEntries - 1;
				return 0;
			}
			if (nDirEntries < maxDirEntries)
				return 0;
		}
		else if (mode == SCAN_PREV_PAGE)
		{
			if (iSelectedEntry)
			{
				iSelectedEntry = 0;
				return 0;
			}
		}

		find_file = options & FIND_FILE;
		find_dir = options & FIND_DIR;
	}

	//enable caching in the sector buffer while traversing the directory,
	//because FatFs is inefficiently using single sector reads
	disk_cache_set(true, fs.database);
	f_rewinddir(&dir);
	nNewEntries = 0;
	while (1) {
		if (initial && fs.cdir && options & (SCAN_DIR | SCAN_SYSDIR)) {
			fil.fattrib = AM_DIR;
			strcpy(fil.fname, "..");
			fil.altname[0] = 0;
			initial = 0;
		} else {
			if (f_readdir(&dir, &fil) != FR_OK) break;
		}
		if (fil.fname[0] == 0) break;

		is_file = ~fil.fattrib & AM_DIR;

		if (!(fil.fattrib & AM_HID) &&
		   ((extension[0] == '*')
                    || CompareExt(fil.fname, extension)
                    || (options & SCAN_DIR && fil.fattrib & AM_DIR)
                    || (options & SCAN_SYSDIR && fil.fattrib & AM_DIR && (fil.fattrib & AM_SYS || (fil.fname[0] == '.' && fil.fname[1] == '.')))))
		{
			if (mode == SCAN_INIT) { // initial directory scan (first 8 entries)
				if (nDirEntries < maxDirEntries) {
					//iprintf("fname=%s, altname=%s\n", fil.fname, fil.altname);
					DirEntries[nDirEntries] = fil;
					nDirEntries++;
				} else if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-1]]) < 0) {// compare new entry with the l
					// replace the last entry with the new one if appropriate
					DirEntries[sort_table[maxDirEntries-1]] = fil;
				}
				for (i = nDirEntries - 1; i > 0; i--) {// one pass bubble-sorting (table is already sorted, only the new item must be placed in order)
					if (CompareDirEntries(&DirEntries[sort_table[i]], &DirEntries[sort_table[i-1]])<0) // compare items
					{
						x = sort_table[i];
						sort_table[i] = sort_table[i-1];
						sort_table[i-1] = x;
					}
					else
						break; // don't check further entries as they are already sorted
				}
			} else if (mode == SCAN_INIT_FIRST) {
				// find a dir entry with given cluster number and store it in the buffer
				if (fil.fclust == iPreviousDirectory) { // directory entry found
					for (i = 0; i< maxDirEntries; i++)
						sort_table[i] = i; // init sorting table

					nDirEntries = 1;
					iSelectedEntry = 0;

					DirEntries[0] = fil; // add the entry at the top of the buffer
					rc = 1; // indicate to the caller that the directory entry has been found
					break;
				}
			} else if (mode == SCAN_INIT_NEXT) {
				// scan the directory table and return next maxDirEntries-1 alphabetically sorted entries (first entry is in the buffer)
				if (CompareDirEntries(&fil, &DirEntries[sort_table[0]]) > 0) {// compare new entry with the first one
					if (nDirEntries < maxDirEntries) {// initial directory scan (first 8 entries)
						DirEntries[nDirEntries] = fil; // add new entry at first empty slot in storage buffer
						nDirEntries++;
					} else {
						if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-1]]) < 0) {// compare new entry with the last already found
							DirEntries[sort_table[maxDirEntries-1]] = fil; // replace the last entry with the new one if appropriate
						}
					}

					for (i = nDirEntries - 1; i > 0; i--) {// one pass bubble-sorting (table is already sorted, only the new item must be placed in order)
						if (CompareDirEntries(&DirEntries[sort_table[i]], &DirEntries[sort_table[i-1]]) < 0) {// compare items and swap if necessary
							x = sort_table[i];
							sort_table[i] = sort_table[i-1];
							sort_table[i-1] = x;
						} else
							break; // don't check further entries as they are already sorted
					}
				}
			} else if (mode == SCAN_NEXT) {
				if (nNewEntries == 0) {// no entry higher than the last one has been found yet
					if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-1]]) > 0) { // found entry higher than the
						nNewEntries++;
						DirEntries[sort_table[0]] = fil;
						// scroll entries' indices
						x = sort_table[0];
						for (i = 0; i < maxDirEntries-1; i++)
							sort_table[i] = sort_table[i+1];
						sort_table[maxDirEntries-1] = x; // last entry is the found one
					}
				} else {// higher entry already found but we need to check the remaining ones if any of them is lower then the already found one
					// check if the found entry is lower than the last one and higher than the last but one, if so then replace the last one with it
					if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-1]]) < 0)
						if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-2]]) > 0) {
							DirEntries[sort_table[maxDirEntries-1]] = fil;
						}
				}
			} else if (mode == SCAN_PREV) {
				if (nNewEntries == 0) {// no entry lower than the first one has been found yet
					if (CompareDirEntries(&fil, &DirEntries[sort_table[0]]) < 0) {// found entry lower than the first one
						nNewEntries++;
						if (nDirEntries < maxDirEntries) nDirEntries++;
						DirEntries[sort_table[maxDirEntries-1]] = fil;
						// scroll entries' indices
						x = sort_table[maxDirEntries-1];
						for (i = maxDirEntries - 1; i > 0; i--)
							sort_table[i] = sort_table[i-1];
						sort_table[0] = x; // the first entry is the found one
					}
				} else {// lower entry already found but we need to check the remaining ones if any of them is higher then the already found one
					// check if the found entry is higher than the first one and lower than the second one, if so then replace the first one with it
					if (CompareDirEntries(&fil, &DirEntries[sort_table[0]]) > 0)
						if (CompareDirEntries(&fil, &DirEntries[sort_table[1]]) < 0) {
							DirEntries[sort_table[0]] = fil;
						}
				}
			} else if (mode == SCAN_NEXT_PAGE) {
				if (CompareDirEntries(&fil, &DirEntries[sort_table[maxDirEntries-1]]) > 0) { // compare with the last visible en
					if (nNewEntries < maxDirEntries) {// initial directory scan (first 8 entries)
						//iprintf("fname=%s, altname=%s\n", fil.fname, fil.altname);
						t_DirEntries[nNewEntries] = fil;
						t_sort_table[nNewEntries] = nNewEntries; // init sorting table
						nNewEntries++;
						SortTempTable(-1);
					} else if (CompareDirEntries(&fil, &t_DirEntries[t_sort_table[maxDirEntries-1]]) < 0) {// compare new entr
						t_DirEntries[t_sort_table[maxDirEntries-1]] = fil;
						SortTempTable(-1);
					}
				}
			} else if (mode == SCAN_PREV_PAGE) {
				if (CompareDirEntries(&fil, &DirEntries[sort_table[0]]) < 0) { // compare with the last visible en
					if (nNewEntries < maxDirEntries) {// initial directory scan (first 8 entries)
						//iprintf("fname=%s, altname=%s\n", fil.fname, fil.altname);
						t_DirEntries[nNewEntries] = fil;
						t_sort_table[nNewEntries] = nNewEntries; // init sorting table
						nNewEntries++;
						SortTempTable(1);
					} else if (CompareDirEntries(&fil, &t_DirEntries[t_sort_table[maxDirEntries-1]]) > 0) {// compare new entry
						t_DirEntries[t_sort_table[maxDirEntries-1]] = fil;
						SortTempTable(1);
					}
				}
			} else if ((mode >= '0' && mode <= '9') || (mode >= 'A' && mode <= 'Z')) {// find first entry beginning with given character
				if (find_file)
					x = tolower(fil.fname[0]) >= tolower(mode) && is_file;
				else if (find_dir)
					x = tolower(fil.fname[0]) >= tolower(mode) || is_file;
				else
					x = (CompareDirEntries(&fil, &DirEntries[sort_table[iSelectedEntry]]) > 0); // compare with the last visible entry

				if (x) {
					if (nNewEntries < maxDirEntries) {// initial directory scan (first 8 entries)
						t_DirEntries[nNewEntries] = fil;
						t_sort_table[nNewEntries] = nNewEntries; // init sorting table
						nNewEntries++;
						SortTempTable(-1);
					} else if (CompareDirEntries(&fil, &t_DirEntries[t_sort_table[maxDirEntries-1]]) < 0) { // compare new entry with the last already found
						t_DirEntries[t_sort_table[maxDirEntries-1]] = fil;
						SortTempTable(-1);
					}
				}
			}
		}
	}
	disk_cache_set(false, 0);

	if (nNewEntries) {
		if (mode == SCAN_NEXT_PAGE) {
			unsigned char j = maxDirEntries - nNewEntries; // number of remaining old entries to scroll
			for (i = 0; i < j; i++) {
				x = sort_table[i];
				sort_table[i] = sort_table[i + nNewEntries];
				sort_table[i + nNewEntries] = x;
			}
			// copy temporary buffer to display
			for (i = 0; i < nNewEntries; i++) {
				DirEntries[sort_table[i+j]] = t_DirEntries[t_sort_table[i]];
			}
		} else if (mode == SCAN_PREV_PAGE) { // note: temporary buffer entries are in reverse order
			unsigned char j = nNewEntries - 1;
			for (i = maxDirEntries - 1; i > j; i--) {
				x = sort_table[i];
				sort_table[i] = sort_table[i - nNewEntries];
				sort_table[i - nNewEntries] = x;
			}
			// copy temporary buffer to display
			for (i = 0; i < nNewEntries; i++) {
				DirEntries[sort_table[j-i]] = t_DirEntries[t_sort_table[i]];
			}
			nDirEntries += nNewEntries;
			if (nDirEntries > maxDirEntries)
				nDirEntries = maxDirEntries;
		} else if ((mode >= '0' && mode <= '9') || (mode >= 'A' && mode <= 'Z')) {
			if (tolower(t_DirEntries[t_sort_table[0]].fname[0]) == tolower(mode)) {
				x = 1; // if we were looking for a file we couldn't find anything other
				if (find_dir) { // when looking for a directory we could find a file beginning with the same character as given one
					x = t_DirEntries[t_sort_table[0]].fattrib & AM_DIR;
				} else if (!find_file) {// find_next
					// when looking for a directory we could find a file beginning with the same character as given one
					x = (t_DirEntries[t_sort_table[0]].fattrib & AM_DIR) == (DirEntries[sort_table[iSelectedEntry]].fattrib & AM_DIR);
				}
				if (x) { // first entry is what we were searching for
					for (i = 0; i < nNewEntries; i++) {
						DirEntries[sort_table[i]] = t_DirEntries[t_sort_table[i]];
					}
					nDirEntries = nNewEntries;
					iSelectedEntry = 0;
					rc = 1; // inform the caller that the search succeeded
				}
			}
		}
	}
	return rc;
}
