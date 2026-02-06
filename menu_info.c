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

#include "menu_info.h"
#include "fat_compat.h"
#include "menu.h"
#include "osd.h"
#include "debug.h"

#define CHAR_IS_EOL(c)      (((c) == '\n'))

#define LINE_SIZE 29
#define PAGE_SIZE 512

static const char* SUFFIX = ".info";

static FIL infofile;
static int16_t last_idx = -1;
static char line[LINE_SIZE];
static int curr_page = -1;
static int curr_page_pos = 0;
static int curr_file_size = -1;

static char nextchar()
{
	UINT br;
	if (curr_file_size < 0) {
		curr_file_size = (int) f_size(&infofile);
	}
	if (curr_page < 0 || curr_page_pos == PAGE_SIZE) {
		curr_page++;
		if ((curr_page * PAGE_SIZE) < curr_file_size) {
			menu_debugf("Seeking to %d. File size: %d\n", curr_page * PAGE_SIZE, curr_file_size);
			if (f_lseek(&infofile, curr_page * PAGE_SIZE) == FR_OK) {
				menu_debugf("Reading buffer\n");
				f_read(&infofile, sector_buffer, PAGE_SIZE, &br);
				curr_page_pos = 0;
				sector_buffer[br] = 0;
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	}
	return sector_buffer[curr_page_pos++];
}

static char *nextline()
{
	int pos = 0;
	while (pos < LINE_SIZE) {
		char v = nextchar();
		if (!v || CHAR_IS_EOL(v)) {
			break;
		} else {
			line[pos++] = v;
		}
	}
	line[pos] = 0;
	menu_debugf("Pos: %d, Line: %s\n", pos, line);
	return line;
}

static char* infoline(uint8_t idx)
{
	int pos = 0;
	char *p;
	int from;
	if (idx != last_idx + 1) {
		//Restart
		menu_debugf("Restart on %d != %d!!\n", idx, last_idx + 1);
		curr_page = -1;
		from = 0;
	} else {
		from = last_idx + 1;
	}
	while (from++ <= idx && (p = nextline()));
	last_idx = idx;
	return p;
}


static char getmenuitem(uint8_t idx, char action, menu_item_t *item)
{
	char *str;
	if (action == MENU_ACT_GET) {
		menu_debugf("getmenuitem %d\n", idx);
		str = infoline(idx);
		item->item = str;
		item->active = (str[0] != 0);
		return item->active;
	} else {
		return 0;
	}
}

static char getmenupage(uint8_t idx, char action, menu_page_t *page) 
{
	if (action == MENU_PAGE_EXIT) {
		f_close(&infofile);
		curr_page = -1;
		curr_file_size = -1;
		last_idx = -1;
		return 0;
	}

	page->title = "INFO";
	page->flags = 0;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

void menu_info_open(const char *core_id)
{
	menu_debugf("menu_info_open: %s\n", core_id);
	if (core_id && core_id[0]) {
		int core_namelen = strlen(core_id);
		char info_filename[core_namelen + strlen(SUFFIX) + 1];
		info_filename[0] = 0;
		strcat(info_filename, "/");
		strcat(info_filename, core_id);
		strcat(info_filename, SUFFIX);
		menu_debugf("Info file to look for: '%s'\n", info_filename);
		if (f_open(&infofile, info_filename, FA_READ) == FR_OK) {
			SetupMenu(&getmenupage, &getmenuitem, NULL);
		} else {
			iprintf("Unable to open info file %s\n", info_filename);
		}
	}
}
