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
#include <inttypes.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "menu.h"
#include "osd.h"
#include "menu-8bit.h"
#include "user_io.h"
#include "data_io.h"
#include "hdd.h"
#include "fat_compat.h"
#include "cue_parser.h"

extern char s[FF_LFN_BUF + 1];

// TODO: remove these extern hacks to private variables
extern unsigned char menusub;
extern char fs_pFileExt[13];
extern hardfileTYPE  hardfiles[4];


//////////////////////////
/////// 8-bit menu ///////
//////////////////////////
typedef enum _RomType {ROM_NORMAL, ROM_PROCESSED} RomType;

static unsigned char selected_drive_slot;
static RomType romtype;
static char data_processor_id[4]; //Max 3 chars, plus null at end

static void substrcpy(char *d, char *s, char idx) {
	char p = 0;

	while(*s) {
		if((p == idx) && *s && (*s != ','))
			*d++ = *s;

		if(*s == ',')
			p++;

		s++;
	}
	*d = 0;
}

static char* GetExt(char *ext) {
	static char extlist[32];
	char *p = extlist;

	while(*ext) {
		strcpy(p, ",");
		strncat(p, ext, 3);
		if(strlen(ext)<=3) break;
		ext +=3;
		p += strlen(p);
	}

	return extlist+1;
}

static unsigned char getIdx(char *opt) {
	if((opt[1]>='0') && (opt[1]<='9')) return opt[1]-'0';    // bits 0-9
	if((opt[1]>='A') && (opt[1]<='Z')) return opt[1]-'A'+10; // bits 10-35
	if((opt[1]>='a') && (opt[1]<='z')) return opt[1]-'a'+36; // bits 36-61
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

static unsigned char getStatus(char *opt, unsigned long long status) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt+1);
	unsigned char x = (status & ((unsigned long long)1<<idx1)) ? 1 : 0;

	if(idx2>idx1) {
		x = status >> idx1;
		x = x & ~(~0 << (idx2 - idx1 + 1));
	}

	return x;
}

static unsigned long long setStatus(char *opt, unsigned long long status, unsigned char value) {
	unsigned char idx1 = getIdx(opt);
	unsigned char idx2 = getIdx(opt+1);
	unsigned long long x = 1;

	if(idx2>idx1) x = ~(~0 << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | (((unsigned long long)value << idx1) & x);
}

static unsigned long long getStatusMask(char *opt) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt+1);
	unsigned long long x = 1;

	if(idx2>idx1) x = ~(~0 << (idx2 - idx1 + 1));

	//iprintf("grtStatusMask %d %d %x\n", idx1, idx2, x);

	return x << idx1;
}

static char RomFileSelected(uint8_t idx, const char *SelectedName) {
	FIL file;
	char ext_idx = user_io_ext_idx(SelectedName, fs_pFileExt);

	iprintf("RomFileSelected romType=%d\n", romtype);
	// this assumes that further file entries only exist if the first one also exists
	if (f_open(&file, SelectedName, FA_READ) == FR_OK) {
		if (romtype == ROM_PROCESSED) {
			data_io_file_tx_processor(&file, ext_idx << 6 | selected_drive_slot, GetExtension(SelectedName), SelectedName, data_processor_id);
		} else {
			data_io_file_tx(&file, ext_idx << 6 | selected_drive_slot, GetExtension(SelectedName));
			f_close(&file);
		}
	}
	// close menu afterwards (but allow custom processor to have its own menu)
	if (romtype != ROM_PROCESSED) CloseMenu();
	return 0;
}

static char ImageFileSelected(uint8_t idx, const char *SelectedName) {
	// select image for SD card
	iprintf("Image selected: %s\n", SelectedName);
	if ((user_io_get_core_features() & (FEAT_IDE0 << (2*selected_drive_slot))) == (FEAT_IDE0_ATA << (2*selected_drive_slot))) {
		iprintf("IDE %d: ATA Hard Disk\n", selected_drive_slot);
		hardfiles[selected_drive_slot].enabled = HDF_FILE;
		strncpy(hardfiles[selected_drive_slot].name, SelectedName, sizeof(hardfiles[0].name));
		hardfiles[selected_drive_slot].name[sizeof(hardfiles[0].name)-1] = 0;
		OpenHardfile(selected_drive_slot, false);
		SendHDFCfg();
	} else {
		data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | selected_drive_slot);
		user_io_file_mount(SelectedName, selected_drive_slot);
	}
	CloseMenu();
	return 0;
}

static char CueFileSelected(uint8_t idx, const char *SelectedName) {
	char res;
	iprintf("Cue file selected: %s\n", SelectedName);
	data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | selected_drive_slot);
	res = user_io_cue_mount(SelectedName, selected_drive_slot);
	if (res) ErrorMessage(cue_error_msg[res-1], res);
	else     CloseMenu();
	return 0;
}

static char GetMenuPage_8bit(uint8_t idx, char action, menu_page_t *page) {
	if (action == MENU_PAGE_EXIT) return 0;

	char *p = user_io_get_core_name();
	if(!p[0]) page->title = "8BIT";
	else      page->title = p;
	page->flags = OSD_ARROW_RIGHT;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

static char GetMenuItem_8bit(uint8_t idx, char action, menu_item_t *item) {

	char *p;
	char *pos;
	unsigned long long status = user_io_8bit_set_status(0,0);  // 0,0 gets status

	if (action == MENU_ACT_RIGHT) {
		SetupSystemMenu();
		return 0;
	}

	item->item = "";
	item->active = 0;
	item->stipple = 0;
	item->page = 0;
	item->newpage = 0;
	item->newsub = 0;

	if(idx == 0) {
		item->page = 0xff; // hide
		return 1;
	}
	p = user_io_8bit_get_string(idx);
	menu_debugf("Option %d: %s\n", idx, p);
	if (idx > 1 && !p) return 0;

	// check if there's a file type supported
	if(idx == 1) {
		if (p && strlen(p)) {
			if (action == MENU_ACT_SEL) {
				// use a local copy of "p" since SelectFile will destroy the buffer behind it
				static char ext[13];
				strncpy(ext, p, 13);
				while(strlen(ext) < 3) strcat(ext, " ");
				selected_drive_slot = 1;
				romtype = ROM_NORMAL;
				SelectFileNG(ext, SCAN_DIR | SCAN_LFN, RomFileSelected, 1);
			} else if (action == MENU_ACT_GET) {
				//menumask = 1;
				strcpy(s, " Load *.");
				strcat(s, GetExt(p));
				item->item = s;
				item->active = 1;
				return 1;
			} else {
				return 0;
			}
		} else {
			item->page = 0xff; // hide
			return 1;
		}
	}

	// check for 'V'ersion strings
	if(p && (p[0] == 'V')) {
		item->page = 0xff; // hide
		return 1;
	}

	// check for 'P'age
	char page = 0;
	if(p && (p[0] == 'P')) {
		if (p[2] == ',') {
		// 'P' is to open a submenu
			if(action == MENU_ACT_GET || action == MENU_ACT_SEL) {
				s[0] = ' ';
				substrcpy(s+1, p, 1);
				item->newpage = getIdx(p);
			} else
				return 0;
		} else {
			// 'P' is a prefix fo F,S,O,T,R
			page = getIdx(p);
			p+=2;
			menu_debugf("P is prefix for: %s\n", p);
		}
	}

	// check for 'F'ile or 'S'D image strings
	if(p && ((p[0] == 'F') || (p[0] == 'S'))) {
		if(action == MENU_ACT_SEL) {
			static char ext[13];
			char iscue = 0;
			unsigned char firstline = OsdLines() <= 8 ? 0 : 2;
			selected_drive_slot = (p[0] == 'F') ? (menusub - firstline + 1) : 0;
			if (p[0]=='S' && (p[1]=='C' || (p[1] && p[1] != ',' && p[2] == 'C'))) {
				// S[0-9]C - select CUE/ISO file
				selected_drive_slot = 3;
				iscue = 1;
			}
			if (p[1]>='0' && p[1]<='9') selected_drive_slot = p[1]-'0';
			romtype = ROM_NORMAL;
			pos = p + 1;
			while (*pos && *pos != ',') {
				if (*pos == 'p') {
					substrcpy(data_processor_id, pos + 1, 0);
					romtype = ROM_PROCESSED;
					menu_debugf("Found data_io processor %s\n", data_processor_id);
					break;
				}
				pos++;
			}
			if (p[1] && p[1] != ',' && p[2] && p[2] != ',' && !strncmp(&p[2], "SNES", 4)) {
				romtype = ROM_PROCESSED; // handle legacy F1SNES notation as a custom data processor
				strcpy(data_processor_id, "SFC");
			}
			if (p[1] && p[1] != ',' && p[2] && p[2] != ',' && !strncmp(&p[2], "ZXCOL", 5)) {
				romtype = ROM_PROCESSED; // F2ZXCOL
				strcpy(data_processor_id, "COL");
			}
			if (p[1] && p[1] != ',' && p[2] && p[2] != ',' && !strncmp(&p[2], "ZXCHR", 5)) {
				romtype = ROM_PROCESSED; // F3ZXCHR
				strcpy(data_processor_id, "CHR");
			}
			substrcpy(ext, p, 1);
			while(strlen(ext) < 3) strcat(ext, " ");
			SelectFileNG(ext, SCAN_DIR | SCAN_LFN, (p[0] == 'F')?RomFileSelected:iscue?CueFileSelected:ImageFileSelected, 1);
		} else if (action == MENU_ACT_BKSP) {
			if (p[0] == 'S' && p[1] && p[2] == 'U') {
				// umount image
				char slot = 0;
				if (p[1]>='0' && p[1]<='9') slot = p[1]-'0';
				if (user_io_is_mounted(slot)) {
					user_io_file_mount(0, slot);
				}
			}

			if (p[0] == 'S' && (p[1]=='C' || (p[1] && p[1] != ',' && p[2] == 'C'))) {
				// umount cue
				if (user_io_is_cue_mounted())
					user_io_cue_mount(NULL, 0);
			}
		} else if (action == MENU_ACT_GET) {
			substrcpy(s, p, 2);
			if(strlen(s)) {
				strcpy(s, " ");
				substrcpy(s+1, p, 2);
				strcat(s, " *.");
			} else {
				if(p[0] == 'F') strcpy(s, " Load *.");
				else            strcpy(s, " Mount *.");
			}

			pos = s+strlen(s);
			substrcpy(pos, p, 1);
			strcpy(pos, GetExt(pos));
			if (p[0] == 'S' && p[1] && p[2] == 'U') {
				char slot = 0;
				if (p[1]>='0' && p[1]<='9') slot = p[1]-'0';
				if (user_io_is_mounted(slot)) {
					s[0] = '\x1e';
				}
			}
			if (p[0] == 'S' && (p[1]=='C' || (p[1] && p[1] != ',' && p[2] == 'C'))) {
				if (user_io_is_cue_mounted())
					s[0] = '\x1f';
			}
		} else {
			return 0;
		}
	}

	// check for 'T'oggle strings
	if(p && (p[0] == 'T')) {
		if (action == MENU_ACT_SEL || action == MENU_ACT_PLUS || action == MENU_ACT_MINUS) {
			unsigned long long mask = (unsigned long long)1<<getIdx(p);
			menu_debugf("Option %s %llx\n", p, status ^ mask);
			// change bit
			user_io_8bit_set_status(status ^ mask, mask);
			// ... and change it again in case of a toggle bit
			user_io_8bit_set_status(status, mask);
		} else if (action == MENU_ACT_GET) {
			s[0] = ' ';
			substrcpy(s+1, p, 1);
		} else {
			return 0;
		}
	}

	// check for Prof'I'le strings
	if(p && (p[0] == 'I')) {
		if (action == MENU_ACT_SEL || action == MENU_ACT_PLUS || action == MENU_ACT_MINUS) {
			unsigned long long mask = 0, preset = 0;
			substrcpy(s, p, 2);
			preset = strtoll(s, NULL, 0);
			substrcpy(s, p, 3);
			mask = strtoll(s, NULL, 0);
			menu_debugf("Option %s %llx %llx\n", p, preset, mask);
			// change bit with reset
			user_io_8bit_set_status(preset | UIO_STATUS_RESET, mask | UIO_STATUS_RESET);
			// release reset
			user_io_8bit_set_status(preset & ~UIO_STATUS_RESET, mask | UIO_STATUS_RESET);
		} else if (action == MENU_ACT_GET) {
			s[0] = ' ';
			substrcpy(s+1, p, 1);
		} else {
			return 0;
		}
	}

	// check for 'O'ption strings
	if(p && (p[0] == 'O')) {
		if(action == MENU_ACT_SEL) {
			unsigned char x = getStatus(p, status) + 1;
			// check if next value available
			substrcpy(s, p, 2+x);
			if(!strlen(s)) x = 0;
			//menu_debugf("Option %s %llx %llx %x %x\n", p, status, mask, x2, x);
			user_io_8bit_set_status(setStatus(p, status, x), ~0);
		} else if (action == MENU_ACT_GET) {
			unsigned char x = getStatus(p, status);

			menu_debugf("Option %s %llx %llx\n", p, x, status);

			// get currently active option
			substrcpy(s, p, 2+x);
			char l = strlen(s);
			if(!l) {
				// option's index is outside of available values.
				// reset to 0.
				x = 0;
				user_io_8bit_set_status(setStatus(p, status, x), ~0);
				substrcpy(s, p, 2+x);
				l = strlen(s);
			}

			s[0] = ' ';
			substrcpy(s+1, p, 1);
			strcat(s, ":");
			l = 26-l-strlen(s); 
			while(l-- >= 0) strcat(s, " ");
			substrcpy(s+strlen(s), p, 2+x);
		} else {
			return 0;
		}
	}

	// check for 'R'AM strings
	if(p && (p[0] == 'R')) {
		if (action == MENU_ACT_SEL) {
			int len = strtol(p+1,0,0);
			menu_debugf("Option %s %d\n", p, len);
			if (len) {
				FIL file;

				if (!user_io_create_config_name(s, "RAM", CONFIG_ROOT)) {
					menu_debugf("Saving RAM file");
					if (f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK) {
						data_io_file_rx(&file, -1, len);
						f_close(&file);
						CloseMenu();
					} else {
						ErrorMessage("\n   Error saving RAM file!\n", 0);
					}
				}
			}
		} else if (action == MENU_ACT_GET) {
			s[0] = ' ';
			substrcpy(s+1, p, 1);
		} else {
			return 0;
		}
	}

	item->item = s;
	item->active = 1;
	item->page = page;

	// Check for separator ("-")
	if(p && (p[0] == '-')) {
		item->item = "";
		item->active = 0;
	}
	return 1;
}

void Setup8bitMenu() {
	char *c, *p;
	int i;

	c = user_io_get_core_name();
	if(!c[0]) OsdCoreNameSet("8BIT");
	else      OsdCoreNameSet(c);

	i = 2;
	// search for 'V'ersion string
	while ((p = user_io_8bit_get_string(i++))) {
		if(p[0] == 'V') {
			// p[1] is not used but kept for future use
			char x = p[1];
			// get version string
			strcpy(s, user_io_get_core_name());
			strcat(s," ");
			substrcpy(s+strlen(s), p, 1);
			OsdCoreNameSet(s);
		}
	}

	// set helptext with core display on top of basic info
	strcpy(helptext_custom, HELPTEXT_SPACER);
	strcat(helptext_custom, OsdCoreName());
	strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
	helptext=helptext_custom;

	SetupMenu(GetMenuPage_8bit, GetMenuItem_8bit, NULL);
}
