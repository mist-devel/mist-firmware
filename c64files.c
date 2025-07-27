/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>

#include "c64files.h"
#include "fat_compat.h"
#include "data_io.h"
#include "menu.h"
#include "osd.h"

#define IDX_EOT                 4 // End-Of-Transmission

#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))

#define IDX_LINE_SIZE 128

static FIL idxfile;
static FIL tapfile;
static int idx_pt = 0;
static int lastidx = -1;
static char idxline[IDX_LINE_SIZE];
static int f_index;

static char c64_idx_getch()
{
	UINT br;

	if (!(idx_pt&0x1ff)) {
		// reload buffer
		f_read(&idxfile, sector_buffer, 512, &br);
		//hexdump(sector_buffer, 512, 0);
	}

	if (idx_pt >= f_size(&idxfile)) return 0;
	else return sector_buffer[(idx_pt++)&0x1ff];
}

static int c64_idx_getline(char* line, int *offset)
{
	char c;
	char ignore=0;
	char literal=0;
	char leadingspace = 0;
	int i=0;
	*offset = 0;

	while(1) {
		c = c64_idx_getch();
		if ((!c) || CHAR_IS_LINEEND(c)) break;
		if (!CHAR_IS_SPACE(c) && *offset) leadingspace = 0;
		if (CHAR_IS_QUOTE(c) && !ignore) literal ^= 1;
		else if (CHAR_IS_COMMENT(c) && !ignore && !literal) ignore++;
		else if ((literal || !ignore ) && i<(IDX_LINE_SIZE-1) && !leadingspace) line[i++] = c;
		if (*offset == 0 && CHAR_IS_SPACE(c)) {
			line[i] = 0;
			*offset = strtoll(line, NULL, 0);
			i = 0;
			if (*offset) leadingspace = 1;
		}
	}
	line[i] = '\0';
	return c==0 ? IDX_EOT : literal ? 1 : 0;
}

static char *c64_idxitem(int idx, int *offset)
{
	if (idx <= lastidx) {
		idx_pt = 0;
		f_rewind(&idxfile);
		lastidx = -1;
	}
	while (1) {
		int r = c64_idx_getline(idxline, offset);
		if (idxline[0]) lastidx++;
		if (r == IDX_EOT || idx == lastidx) break;
	}
	return idxline;
}

static char c64_idx_getmenupage(uint8_t idx, char action, menu_page_t *page) 
{
	if (action == MENU_PAGE_EXIT) {
		f_close(&idxfile);
		f_close(&tapfile);
		return 0;
	}

	page->title = "C64IDX";
	page->flags = 0;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

static char c64_idx_getmenuitem(uint8_t idx, char action, menu_item_t *item)
{
	int offset;
	char *str;
	if (action == MENU_ACT_GET) {
		str = c64_idxitem(idx, &offset);
		item->item = str;
		item->active = (str[0] != 0);
		return (str[0] != 0);
	} else if (action == MENU_ACT_SEL) {
		str = c64_idxitem(idx, &offset);
		iprintf("C64: load TAP segment \"%s\" at offset %08x\n", str, offset);
		f_close(&idxfile);

		UINT br;
		UINT c;
		FRESULT res;
		char *p;
		// Send header
		DISKLED_ON
		res = f_read(&tapfile, sector_buffer, 20, &br);
		DISKLED_OFF
		if (res != FR_OK) {
			f_close(&tapfile);
			CloseMenu();
			return 1;
		}
		data_io_file_tx_prepare(&tapfile, f_index, "TAP");
		EnableFpga();
		SPI(DIO_FILE_TX_DAT);
		spi_write(sector_buffer, 20);
		DisableFpga();
		// Send data
		if (f_lseek(&tapfile, offset) == FR_OK) {
			while(1) {
				DISKLED_ON
				res = f_read(&tapfile, sector_buffer, SECTOR_BUFFER_SIZE, &br);
				DISKLED_OFF
				if (res == FR_OK) {
					EnableFpga();
					SPI(DIO_FILE_TX_DAT);
					spi_write(sector_buffer, br);
					DisableFpga();
				}
				if (res != FR_OK || br != SECTOR_BUFFER_SIZE) break;
			}
		}
		data_io_file_tx_done();
		f_close(&tapfile);
		CloseMenu();
		return 1;
	} else
		return 0;
}

static void c64_handleidx(FIL *file, int index, const char *name, const char *ext)
{
	iprintf("C64: open IDX %s\n", name);

	f_rewind(file);
	idxfile = *file;
	idx_pt = 0;
	lastidx = -1;
	f_index = index;

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
		char tap[len+3];
		memcpy(tap, name, len-1);
		strcpy(&tap[len-1], "TAP");
		if(f_open(&tapfile, tap, FA_READ) != FR_OK) {
			f_close(&idxfile);
			ErrorMessage("Unable to open the\ncorresponding TAP file!", 0);
			return;
		}
		SetupMenu(&c64_idx_getmenupage, &c64_idx_getmenuitem, NULL);
	} else {
		f_close(&idxfile);
		CloseMenu();
	}
}

static data_io_processor_t c64_idxfile = {"IDX", &c64_handleidx};


void c64files_init()
{
	data_io_add_processor(&c64_idxfile);
}