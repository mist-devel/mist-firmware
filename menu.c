/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-14   - OSD labels changed
// 2009-12-15   - added display of directory name extensions
// 2010-01-09   - support for variable number of tracks
// 2016-06-01   - improvements to 8-bit menu

#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "utils.h"
#include "fat_compat.h"
#include "osd.h"
#include "state.h"
#include "fpga.h"
#include "firmware.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "data_io.h"
#include "tos.h"
#include "debug.h"
#include "boot.h"
#include "archie.h"
#include "arc_file.h"
#include "usb/joymapping.h"
#include "mist_cfg.h"
#include "minimig-menu.h"

// test features (not used right now)
// #define ALLOW_TEST_MENU 0 //remove to disable in prod version


// other constants
#define DIRSIZE OSDNLINE // number of items in directory display window

static uint8_t menu_last, scroll_down, scroll_up;
static uint8_t page_idx, last_page[4], last_menusub[4], last_menu_first[4], page_level;
static menu_item_t menu_item;
static menu_page_t menu_page;
static uint8_t menuidx[OSDNLINE];
const char *helptext;

static const char *dialog_text;
static char dialog_options;
static menu_dialog_t dialog_callback;
static const char *dialog_helptext;
static unsigned char dialog_errorcode;
static unsigned char dialog_menusub;
static unsigned char dialog_autoclose;

static unsigned char menustate = MENU_NONE1;
static unsigned char parentstate;
unsigned char menusub = 0;
static unsigned int menumask = 0; // Used to determine which rows are selectable...
static unsigned long menu_timer = 0;
static menu_get_items_t menu_item_callback;
static menu_get_page_t menu_page_callback;
static menu_select_file_t menu_select_callback;

extern const char version[];

extern char s[FF_LFN_BUF + 1];

extern FILINFO  DirEntries[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;
extern unsigned char iSelectedEntry;
char DirEntryInfo[MAXDIRENTRIES][5]; // disk number info of dir entries
char DiskInfo[5]; // disk number info of selected entry
static char *SelectedName;

const char *config_cpu_msg[] = {"68000 ", "68010", "-----","68020"};
const char *config_autofire_msg[] = {"\n\n        AUTOFIRE OFF", "\n\n        AUTOFIRE FAST", "\n\n       AUTOFIRE MEDIUM", "\n\n        AUTOFIRE SLOW"};
const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

enum HelpText_Message {HELPTEXT_NONE,HELPTEXT_MAIN,HELPTEXT_HARDFILE,HELPTEXT_CHIPSET,HELPTEXT_MEMORY,HELPTEXT_VIDEO,HELPTEXT_FEATURES};
const char *helptexts[]={
	0,
	"                                Welcome to MiST!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus.  Joystick emulation on the numeric keypad can be toggled with the numlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600 IDE harddisk interface.  The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).  It is also possible to use either the entire SD card or an individual partition as an emulated harddisk.",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.)  If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 8 megabytes (68000/68010) / 24 megabytes (68020) of true Fast RAM.  To use the HRTmon feature you will need a file on the SD card named hrtmon.rom.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	"                                Minimig can set the audio filter to switchable with power LED (A500r5+), always off or always on (A1000, A500r3). The power LED off-state can be configured to dim (A500r6+) or off (A1000, A500r3/5).",
	0
};

// one screen width
const char* HELPTEXT_SPACER= "                                ";
char helptext_custom[450]; // spacer(32) + corename(64) + minimig version(16) + helptexts[x](335)

// file selection menu variables
char fs_pFileExt[13] = "xxx";
unsigned char fs_ShowExt = 0;
unsigned char fs_Options;
unsigned char fs_MenuSelect;

#define STD_EXIT       "            exit"
#define STD_SPACE_EXIT "        SPACE to exit"
#define STD_COMBO_EXIT " Hold ESC then SPACE to exit"

#define HELPTEXT_DELAY 10000
#define FRAME_DELAY 150

//////////////////////////
/////// 8-bit menu ///////
//////////////////////////
static unsigned char selected_drive_slot;


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
	// this assumes that further file entries only exist if the first one also exists
	if (f_open(&file, SelectedName, FA_READ) == FR_OK) {
		data_io_file_tx(&file, user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1), GetExtension(SelectedName));
		f_close(&file);
	}
	// close menu afterwards
	CloseMenu();
	return 0;
}

static char ImageFileSelected(uint8_t idx, const char *SelectedName) {
	// select image for SD card
	iprintf("Image selected: %s\n", SelectedName);
	data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1));
	user_io_file_mount(SelectedName, selected_drive_slot);
	CloseMenu();
	return 0;
}

static char CueFileSelected(uint8_t idx, const char *SelectedName) {
	char res;
	iprintf("Cue file selected: %s\n", SelectedName);
	data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1));
	res = user_io_cue_mount(SelectedName);
	if (res) ErrorMessage("\n  Error mounting CD image!\n", res);
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

		// p[1] is not used but kept for future use
		char x = p[1];

		// get version string
		strcpy(s, user_io_get_core_name());
		strcat(s," ");
		substrcpy(s+strlen(s), p, 1);
		OsdCoreNameSet(s);
		item->page = 0xff; // hide
		return 1;
	}

	// check for 'P'age
	char page = 0;
	if(p && (p[0] == 'P')) {
		if (p[2] == ',') {
		// 'P' is to open a submenu
			s[0] = ' ';
			substrcpy(s+1, p, 1);
			item->newpage = getIdx(p);
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
			selected_drive_slot = 0;
			if (p[1]>='0' && p[1]<='9') selected_drive_slot = p[1]-'0';
			substrcpy(ext, p, 1);
			while(strlen(ext) < 3) strcat(ext, " ");
			SelectFileNG(ext, SCAN_DIR | SCAN_LFN, (p[0] == 'F')?RomFileSelected:(p[1] == 'C')?CueFileSelected:ImageFileSelected, 1);
		} else if (action == MENU_ACT_BKSP) {
			if (p[0] == 'S' && p[1] && p[2] == 'U') {
				// umount image
				char slot = 0;
				if (p[1]>='0' && p[1]<='9') slot = p[1]-'0';
				if (user_io_is_mounted(slot)) {
					user_io_file_mount(0, slot);
				}
			}

			if (p[0] == 'S' && p[1] == 'C') {
				// umount cue
				if (user_io_is_cue_mounted())
				user_io_cue_mount(NULL);
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
			if (p[0] == 'S' && p[1] == 'C') {
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
			unsigned long long mask = 1<<getIdx(p);
			menu_debugf("Option %s %x\n", p, status ^ mask);
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
	return 1;
}

static void Setup8bitMenu() {
	char *p;

	// set helptext with core display on top of basic info
	strcpy(helptext_custom, HELPTEXT_SPACER);
	strcat(helptext_custom, OsdCoreName());
	strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
	helptext=helptext_custom;

	p = user_io_get_core_name();
	if(!p[0]) OsdCoreNameSet("8BIT");
	else      OsdCoreNameSet(p);

	SetupMenu(GetMenuPage_8bit, GetMenuItem_8bit);
}

///////////////////////////
/////// System menu ///////
///////////////////////////
// prints input as a string of binary (on/off) values
// assumes big endian, returns using special characters (checked box/unchecked box)
static void siprintbinary(char* buffer, uint8_t byte)
{
	for (int j=0;j<8;j++) {
		buffer[j]=(byte & 1)?'\x1a':'\x19';
		byte >>= 1;
	}
}

static void get_joystick_state( char *joy_string, char *joy_string2, uint8_t joy_num ) {
	// helper to get joystick status (both USB or DB9)
	uint32_t vjoy;
	vjoy = StateJoyGet(joy_num);
	vjoy |=  StateJoyGetExtra(joy_num) << 8;
	vjoy |=  StateJoyGetRight(joy_num) << 16;
	if (vjoy==0) {
		joy_string[0] = '\0';
		strcpy(joy_string2, "  \x14     \x14                 ");
		return;
	}
	strcpy(joy_string,  "  \x12     \x12   X Y L R L2 R2 L3");
	strcpy(joy_string2, "< \x13 > < \x13 > A B Sel Sta R3");
	if(!(vjoy & JOY_UP))    joy_string[2]  = ' ';
	if(!(vjoy & JOY_X))     joy_string[12] = ' ';
	if(!(vjoy & JOY_Y))     joy_string[14] = ' ';
	if(!(vjoy & JOY_L))     joy_string[16] = ' ';
	if(!(vjoy & JOY_R))     joy_string[18] = ' ';
	if(!(vjoy & JOY_L2))    memset(joy_string+20, ' ', 2);
	if(!(vjoy & JOY_R2))    memset(joy_string+23, ' ', 2);
	if(!(vjoy & JOY_L3))    memset(joy_string+26, ' ', 2);
	if(!(vjoy & JOY_LEFT))  joy_string2[0] = ' ';
	if(!(vjoy & JOY_DOWN))  joy_string2[2] = '\x14';
	if(!(vjoy & JOY_RIGHT)) joy_string2[4] = ' ';
	if(!(vjoy & JOY_A))     joy_string2[12] = ' ';
	if(!(vjoy & JOY_B))     joy_string2[14] = ' ';
	if(!(vjoy & JOY_SELECT))memset(joy_string2+16, ' ', 3);
	if(!(vjoy & JOY_START)) memset(joy_string2+20, ' ', 3);
	if(!(vjoy & JOY_R3))    memset(joy_string2+24, ' ', 2);

	if(!(vjoy & JOY_UP2))   joy_string[8] = ' ';
	if(!(vjoy & JOY_LEFT2)) joy_string2[6] = ' ';
	if(!(vjoy & JOY_DOWN2)) joy_string2[8] = '\x14';
	if(!(vjoy & JOY_RIGHT2))joy_string2[10] = ' ';

	return;
}

static void get_joystick_state_usb( char *s, unsigned char joy_num ) {
	/* USB specific - current "raw" state 
	  (in reverse binary format to correspont to MIST.INI mapping entries)
	*/
	char buffer[5];
	unsigned short i;
	char binary_string[9]="00000000";
	unsigned char joy = 0;
	unsigned int max_btn = 1;
	if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && StateNumJoysticks() <= joy_num))
	{
		strcpy( s, " ");
		return;
	}
	max_btn = StateUsbGetNumButtons(joy_num);
	joy = StateUsbJoyGet(joy_num);
	siprintf(s, "USB: ---- 0000 0000 0000 ----");
	siprintbinary(binary_string, joy);
	s[5]  = binary_string[0]=='\x1a'?'>':'\x1b';
	s[6]  = binary_string[1]=='\x1a'?'<':'\x1b';
	s[7]  = binary_string[2]=='\x1a'?'\x13':'\x1b';
	s[8]  = binary_string[3]=='\x1a'?'\x12':'\x1b';
	s[10] = binary_string[4];
	s[11] = max_btn>1 ? binary_string[5] : ' ';
	s[12] = max_btn>2 ? binary_string[6] : ' ';
	s[13] = max_btn>3 ? binary_string[7] : ' ';
	joy = StateUsbJoyGetExtra(joy_num);
	siprintbinary(binary_string, joy);
	s[15] = max_btn>4 ? binary_string[0] : ' ';
	s[16] = max_btn>5 ? binary_string[1] : ' ';
	s[17] = max_btn>6 ? binary_string[2] : ' ';
	s[18] = max_btn>7 ? binary_string[3] : ' ';
	s[20] = max_btn>8 ? binary_string[4] : ' ';
	s[21] = max_btn>9 ? binary_string[5] : ' ';
	s[22] = max_btn>10 ? binary_string[6] : ' ';
	s[23] = max_btn>11 ? binary_string[7] : ' ';

	joy = StateJoyGetRight(joy_num);
	s[25]  = (joy & JOY_RIGHT)?'>':'\x1b';
	s[26]  = (joy & JOY_LEFT) ?'<':'\x1b';
	s[27]  = (joy & JOY_DOWN) ?'\x13':'\x1b';
	s[28]  = (joy & JOY_UP  ) ?'\x12':'\x1b';
	return;
}

static void append_joystick_usbid ( char *usb_id, unsigned int usb_vid, unsigned int usb_pid ) {
	siprintf(usb_id, "VID:%04X PID:%04X", usb_vid, usb_pid);
}

static void get_joystick_id ( char *usb_id, unsigned char joy_num, short raw_id ) {
	/*
	Builds a string containing the USB VID/PID information of a joystick
	*/
	char buffer[32]="";
	usb_id[0] = 0;

	//hack populate from outside
	int vid = StateUsbVidGet(joy_num);
	int pid = StateUsbPidGet(joy_num);

	if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num >= StateNumJoysticks())) {
		if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num < StateNumJoysticks()+2)) {
			strcpy( buffer, "Atari DB9 Joystick");
		} else {
			strcpy( buffer, "None");
		}
	} else if (vid>0) {
		if (raw_id == 0) {
			strcpy(buffer, get_joystick_alias( vid, pid ));
		}
		if(!buffer[0]) {
			append_joystick_usbid( buffer, vid, pid );
		}
	}

	if(raw_id == 0)
		siprintf(usb_id, "%*s", (28-strlen(buffer))/2, " ");

	strcat(usb_id, buffer);
	return;
}

static void SetupMinimigMenu2() {
	SetupMinimigMenu();
	page_level=1;
	last_page[0] = 0;
	last_menu_first[0] = 0;
	last_menusub[0] = 0;
	page_idx = 2;
}

static char FirmwareUpdateError() {
	switch (Error) {
		case ERROR_FILE_NOT_FOUND :
			DialogBox("\n       Update file\n        not found!\n", MENU_DIALOG_OK, 0);
			break;
		case ERROR_INVALID_DATA :
			DialogBox("\n       Invalid\n     update file!\n", MENU_DIALOG_OK, 0);
			break;
		case ERROR_UPDATE_FAILED :
			DialogBox("\n\n    Update failed!\n", MENU_DIALOG_OK, 0);
			break;
	}
	return 0;
}

static char FirmwareUpdatingDialog(uint8_t idx) {
	WriteFirmware("/FIRMWARE.UPG");
	Error = ERROR_UPDATE_FAILED;
	FirmwareUpdateError();
	return 0;
}

static char FirmwareUpdateDialog(uint8_t idx) {
	if (idx == 0) { // yes
		DialogBox("\n      Updating firmware\n\n         Please wait\n", 0, FirmwareUpdatingDialog);
	}
	return 0;
}

static char ResetDialog(uint8_t idx) {
	char m = 0;

	if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
		m = 1;

	if (idx == 0) { //yes
		if(m) {
			CloseMenu();
			OsdReset(RESET_NORMAL);
		} else {
			FIL file;
			UINT br;
			if (!user_io_create_config_name(s, "CFG", CONFIG_ROOT)) {
				iprintf("Saving config to %s\n", s);
				if(f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK) {
				 // finally write data
					((unsigned long long*)sector_buffer)[0] = user_io_8bit_set_status(arc_get_default(),~0);
					f_write(&file, sector_buffer, 8, &br);
					iprintf("Settings for %s written\n", s);
					f_close(&file);
				}
			}
		}
	}
	return 0;
}

static char CoreFileSelected(uint8_t idx, const char *SelectedName) {
	// close OSD now as the new core may not even have one
	OsdDisable();

	// reset minimig boot text position
	BootHome();

	//remember core name loaded
	OsdCoreNameSet(SelectedName);

	char mod = 0;
	const char *extension = GetExtension(SelectedName);
	const char *rbfname = SelectedName;
	arc_reset();

	if (extension && !strncasecmp(extension,"ARC",3)) {
		mod = arc_open(SelectedName);
		if(mod < 0 || !strlen(arc_get_rbfname())) { // error
			CloseMenu();
			return 0;
		}
		strcpy(s, arc_get_rbfname());
		strcat(s, ".RBF");
		rbfname = (char*) &s;
	}
	user_io_reset();
	user_io_set_core_mod(mod);
	// reset fpga with core
	fpga_init(rbfname);

	// De-init joysticks to allow re-ordering for new core
	StateReset();

	CloseMenu();

	return 0;
}

static char GetMenuPage_System(uint8_t idx, char action, menu_page_t *page) {
	if (action == MENU_PAGE_EXIT) return 0;

	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	page->flags = 0;
	switch (idx) {
		case 0:
			page->title = "System";
			page->flags = OSD_ARROW_LEFT;
			break;
		case 1:
			page->title = "FW & Core";
			break;
		case 2:
			page->title = "Clock";
			page->timer = 1000;
			break;
		case 3:
			page->title = "Inputs";
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			siprintf(s, "Joy%d", idx-3);
			page->title = s;
			page->timer = 10;
			page->stdexit = MENU_STD_SPACE_EXIT;
			break;
		case 8:
			page->title = "Keyboard";
			page->timer = 10;
			page->stdexit = MENU_STD_COMBO_EXIT;
			break;
		case 9:
			page->title = "USB";
			page->timer = 10;
			break;
	}
	return 0;
}

static char GetMenuItem_System(uint8_t idx, char action, menu_item_t *item) {
	static uint8_t date[7];
	/* check joystick status */
	static char joy_string[32];
	static char joy_string2[32];

	item->stipple = 0;
	item->active = 1;
	item->page = 0;
	item->newpage = 0;
	item->newsub = 0;
	item->item = "";
	if(idx<=5) item->page = 0;
	else if (idx<=12) item->page = 1;
	else if (idx<=19) item->page = 2;
	else if (idx<=25) item->page = 3;
	else if (idx<=32) {item->page = (page_idx>=4 && page_idx<=7) ? page_idx : 4; item->active = 0;}
	else if (idx<=36) {item->page = 8; item->active = 0;}
	else if (idx<=42) {item->page = 9; item->active = 0;}
	else return 0;
	if (item->page != page_idx) return 1; // shortcut

	char m  = (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2);
	char a  = (user_io_core_type()==CORE_TYPE_ARCHIE);
	char st = (user_io_core_type()==CORE_TYPE_MIST || user_io_core_type()==CORE_TYPE_MIST2);

	switch (action) {
		case MENU_ACT_GET:
			switch(idx) {
				// page 0
				case 0:
					item->item = " Firmware & Core";
					item->newpage = 1;
					break;
				case 1:
					item->item = " Date & Time";
					item->active = GetRTC((uint8_t*)&date);
					item->stipple = !item->active;
					item->newpage = 2;
					break;
				case 2:
					item->item = " Input Devices";
					item->newpage = 3;
					break;
				case 3:
					if(a || st) {
						item->active = 0;
					} else
						item->item = m ? " Reset" : " Reset settings";
					break;
				case 4:
					if (m || a || st) {
						item->active = 0;
					} else
						item->item = " Save settings";
					break;
				case 5:
					item->item = " About";
					break;

				// page 1 - firmware & core
				case 6:
					siprintf(s, "   ARM  s/w ver. %s", version + 5);
					item->item = s;
					item->active = 0;
					break;
				case 7: {
					char *v = GetFirmwareVersion("/FIRMWARE.UPG");
					if(v)
						siprintf(s, "   FILE s/w ver. %s", v);
					else
						s[0] = 0;
					item->item = s;
					item->active = 0;
					}
					break;
				case 8:
					item->item = "           Update";
					item->active = fat_uses_mmc();
					item->stipple = !item->active;
					break;
				case 9:
				case 11:
					item->active = 0;
					break;
				case 10:
					if(strlen(OsdCoreName())<26) {
						siprintf(s, "%*s%s", (29-strlen(OsdCoreName()))/2, " ", OsdCoreName());
					}
					else strcpy(s, OsdCoreName());
					s[28] = 0;
					item->item = s;
					item->active = 0;
					break;
				case 12:
					item->item = "      Change FPGA core";
					break;

				// page 2 - RTC
				case 13:
					GetRTC((uint8_t*)&date);
					siprintf(s, "       Year      %4d", 1900+date[0]);
					item->item = s;
					break;
				case 14:
					siprintf(s, "       Month       %2d", date[1]);
					item->item = s;
					break;
				case 15:
					siprintf(s, "       Date        %2d", date[2]);
					item->item = s;
					break;
				case 16:
					siprintf(s, "       Hour        %2d", date[3]);
					item->item = s;
					break;
				case 17:
					siprintf(s, "       Minute      %2d", date[4]);
					item->item = s;
					break;
				case 18:
					siprintf(s, "       Second      %2d", date[5]);
					item->item = s;
					break;
				case 19:
					siprintf(s, "       Day  %9s", date[6] <= 7 ? days[date[6]-1] : "--------");
					item->item = s;
					break;

				// page 3 - Inputs
				case 20:
				case 21:
				case 22:
				case 23:
					siprintf(s, " Joystick %d Test", idx-19);
					item->item = s;
					item->newpage = 4+idx-20; // page 4-7
					break;
				case 24:
					item->item = " Keyboard Test";
					item->newpage = 8;
					break;
				case 25:
					item->item = " USB status";
					item->newpage = 9;
					break;

				// page 4-7 - joy test
				case 26:
					get_joystick_state(joy_string, joy_string2, page_idx-4); //grab state of joy
					siprintf(s, "       Test Joystick %d", page_idx-4+1);
					item->item = s;
					break;
				case 27:
					get_joystick_id(s, page_idx-4, 0);
					item->item = s;
					break;
				case 29:
					item->item = joy_string;
					break;
				case 30:
					item->item = joy_string2;
					break;
				case 32:
					get_joystick_state_usb(s, page_idx-4);
					item->item = s;
					break;

				// page 8 - keyboard test
				case 33:
					item->item = "       USB scancodes";
					break;
				case 34: {
					uint8_t keys[6]={0,0,0,0,0,0};
					StateKeyboardPressed(keys);
					siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]);
					item->item = s;
					};
					break;
				case 35: {
					uint8_t mod = StateKeyboardModifiers();
					char usb_id[32];
					strcpy(usb_id, "                      ");
					siprintbinary(usb_id, mod);
					siprintf(s, "    mod keys - %s ", usb_id);
					item->item = s;
					};
					break;
				case 36: {
					uint8_t mod = StateKeyboardModifiers();
					uint16_t keys_ps2[6]={0,0,0,0,0,0};
					StateKeyboardPressedPS2(keys_ps2);
					add_modifiers(mod, keys_ps2);
					siprintf(s, "   %4x %4x %4x %4x ", keys_ps2[0], keys_ps2[1], keys_ps2[2], keys_ps2[3]);// keys_ps2[4], keys_ps2[5]);
					item->item = s;
					};
					break;

				// page 9 - USB status
				case 37:
				case 38:
				case 39:
				case 40:
				case 41:
				case 42: {
					char usb_id[32];
					get_joystick_id( usb_id, idx-37, 1);
					siprintf(s, " Joy%d - %s", idx-36, usb_id);
					item->item = s;
					break;
					}
				default:
					item->active = 0;
			}
			break;

		case MENU_ACT_SEL:
			switch(idx) {
				case 0:
					item->newpage = 1;
					item->newsub = 6;
					break;
				case 1:
					if(GetRTC((uint8_t*)&date)) item->newpage = 2;
					break;
				case 2:
					item->newpage = 3;
					break;
				case 3: {
					char m = 0;
					if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
						m = 1;
					DialogBox(m ? "\n         Reset MiST?" : "\n       Reset settings?", MENU_DIALOG_YESNO, ResetDialog);
					break;
				}
				case 4: {
					// Save settings
					FIL file;
					UINT bw = 0;
					if (!user_io_create_config_name(s, "CFG", CONFIG_ROOT)) {
						iprintf("Saving config to %s\n", s);
						FRESULT res;
						if((res = f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS)) == FR_OK) {
							// finally write data
							((unsigned long long*)sector_buffer)[0] = user_io_8bit_set_status(0,0);
							res = f_write(&file, sector_buffer, 8, &bw);
							f_close(&file);
						}
						if (res == FR_OK && bw == 8) {
							iprintf("Settings for %s written\n", s);
							Setup8bitMenu();
							menusub = 0;
						} else
							ErrorMessage("\n   Error writing settings!\n", 0);
					}
					break;
				}
				case 5:
					parentstate = MENU_8BIT_ABOUT1;
					menusub = 0;
					break;

				// page 1 - firmware & core
				case 8:
					if (fat_uses_mmc()) {
						if (CheckFirmware("/FIRMWARE.UPG"))
							DialogBox("\n     Update the firmware\n        Are you sure?", MENU_DIALOG_YESNO, FirmwareUpdateDialog);
						else
							FirmwareUpdateError();
					}
					break;
				case 12:
					SelectFileNG("RBFARC", SCAN_LFN | SCAN_SYSDIR, CoreFileSelected, 0);
					break;
				case 20:
				case 21:
				case 22:
				case 23:
					item->newpage = 4+idx-20; // page 4-7
					break;
				case 24:
					item->newpage = 8;
					break;
				case 25:
					item->newpage = 9;
					break;
			}
			break;
		case MENU_ACT_LEFT:
		case MENU_ACT_RIGHT:
		case MENU_ACT_PLUS:
		case MENU_ACT_MINUS:
			if (page_idx == 0 && action == MENU_ACT_LEFT) {
				// go back to core requesting this menu
				switch(user_io_core_type()) {
					case CORE_TYPE_MINIMIG:
					case CORE_TYPE_MINIMIG2:
						SetupMinimigMenu2();
						menusub = 0;
						break;
					case CORE_TYPE_MIST:
					case CORE_TYPE_MIST2:
						tos_setup_menu();
						menusub = 0;
						break;
					case CORE_TYPE_ARCHIE:
						archie_setup_menu();
						menusub = 0;
						break;
					case CORE_TYPE_8BIT:
						Setup8bitMenu();
						menusub = 0;
						break;
				}
			}
			if (page_idx == 2) {
				if (GetRTC((uint8_t*)&date)) {
					static const char mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
					int year;
					uint8_t is_leap, month, maxday;
					char left = action == MENU_ACT_LEFT || action == MENU_ACT_MINUS;

					year = 1900+date[0];
					month = date[1];
					if (month > 12) month = 12;
					is_leap = (!(year % 4) && (year % 100)) || !(year % 400);
					maxday = mdays[month-1] + (month == 2 && is_leap);

					switch(idx) {
						case 13: if (left) date[0]--; else date[0]++; break;
						case 14: if (left) date[1] = decval(date[1], 1, 12); else date[1] = incval(date[1], 1, 12); break;
						case 15: if (left) date[2] = decval(date[2], 1, maxday); else date[2] = incval(date[2], 1, maxday); break;
						case 16: if (left) date[3] = decval(date[3], 0, 23); else date[3] = incval(date[3], 0, 23); break;
						case 17: if (left) date[4] = decval(date[4], 0, 59); else date[4] = incval(date[4], 0, 59); break;
						case 18: if (left) date[5] = decval(date[5], 0, 59); else date[5] = incval(date[5], 0, 59); break;
						case 19: if (left) date[6] = decval(date[6], 1, 7); else date[6] = incval(date[6], 1, 7); break;
					}
					if (idx>=13 && idx<=19) SetRTC((uint8_t*)&date);
				}
			}
			break;
		default:
			return 0;
	}
	return 1;
}

void SetupSystemMenu() {
	helptext = helptexts[HELPTEXT_NONE];
	SetupMenu(GetMenuPage_System, GetMenuItem_System);
	menusub = 0;
}

///////////////////////////
/////// Menu engine ///////
///////////////////////////
void ResetMenu()
{
	strcpy(fs_pFileExt, "xxx");
}

void CloseMenu()
{
	menustate = parentstate = MENU_NONE1;
}

// return to previous page
void ClosePage() {
	parentstate = MENU_NG;
	page_level--;
	menusub = last_menusub[page_level];
	menu_page_callback(page_idx, MENU_PAGE_EXIT, &menu_page);
	page_idx = last_page[page_level];
	menuidx[0] = last_menu_first[page_level];
	menustate = parentstate;
}

// change current page without nesting
void ChangePage(char idx) {
	page_idx = idx;
	menuidx[0] = 0;
	menustate = parentstate = MENU_NG;
}

static void PrintDirectory(void);
static void ScrollLongName(void);


void SelectFile(char* pFileExt, unsigned char Options, unsigned char MenuSelect, char chdir)
{
	// this function displays file selection menu

	menu_debugf("%s - %s\n", pFileExt, fs_pFileExt);

	if (strncmp(pFileExt, fs_pFileExt, 12) != 0) // check desired file extension
	{ // if different from the current one go to the root directory and init entry buffer
		ChangeDirectoryName("/");

		// for 8 bit cores try to 
		if(((user_io_core_type() == CORE_TYPE_8BIT) || (user_io_core_type() == CORE_TYPE_ARCHIE)) && chdir)
			user_io_change_into_core_dir();
		ScanDirectory(SCAN_INIT, pFileExt, Options);
	}

	menu_debugf("pFileExt = %3s\n", pFileExt);
	strcpy(fs_pFileExt, pFileExt);
	fs_ShowExt = ((strlen(fs_pFileExt)>3 && strncmp(fs_pFileExt, "RBFARC", 6)) || strchr(fs_pFileExt, '*') || strchr(fs_pFileExt, '?'));
	fs_Options = Options;
	fs_MenuSelect = MenuSelect;

	menustate = parentstate = MENU_FILE_SELECT;
}

void SelectFileNG(char *pFileExt, unsigned char Options, menu_select_file_t callback, char chdir) {
	SelectFile(pFileExt, Options, MENU_FILE_SELECT_EXIT, chdir);
	menu_select_callback = callback;
}

void SetupMenu(menu_get_page_t menu_page_cb, menu_get_items_t menu_item_cb)
{
	menuidx[0] = menu_last = page_idx = page_level = scroll_down = scroll_up = 0;
	menu_item_callback = menu_item_cb;
	menu_page_callback = menu_page_cb;
	menustate = parentstate = MENU_NG;
}

void HandleUI(void)
{
	unsigned char i, c, up, down, select, backsp, menu, right, left, plus, minus;
	static unsigned char ctrl = false;
	static unsigned char lalt = false;
	static long helptext_timer;
	static long page_timer;
	static char helpstate=0;
	uint8_t keys[6] = {0,0,0,0,0,0};

	// get user control codes
	c = OsdGetCtrl();

	// decode and set events
	menu = false;
	select = false;
	up = false;
	down = false;
	left = false;
	right = false;
	plus=false;
	minus=false;
	backsp=false;

	switch (c)
	{
		case KEY_CTRL :
			ctrl = true;
			break;
		case KEY_CTRL | KEY_UPSTROKE :
			ctrl = false;
			break;
		case KEY_LALT :
			lalt = true;
			break;
		case KEY_LALT | KEY_UPSTROKE :
			lalt = false;
			break;
		case KEY_KP0 :
			// Only sent by Minimig
			if (ctrl && lalt)
			{
				if (menustate == MENU_NONE2 || menustate == MENU_DIALOG2)
				{
					char autofire_tmp = config.autofire & 3;
					autofire_tmp++;
					config.autofire=(config.autofire & 0x0c) | (autofire_tmp & 3);
					ConfigAutofire(config.autofire);
					if (menustate == MENU_NONE2 || menustate == MENU_DIALOG2)
						InfoMessage(config_autofire_msg[config.autofire & 3]);
				}
			}
			break;

		case KEY_MENU:
			menu = true;
			OsdKeySet(KEY_MENU | KEY_UPSTROKE);
			break;

		// Within the menu the esc key acts as the menu key. problem:
		// if the menu is left with a press of ESC, then the follwing
		// break code for the ESC key when the key is released will 
		// reach the core which never saw the make code. Simple solution:
		// react on break code instead of make code
		case KEY_ESC | KEY_UPSTROKE :
			if (menustate != MENU_NONE2)
				menu = true;
			break;
		case KEY_ENTER :
		case KEY_SPACE :
			select = true;
			break;
		case KEY_BACK :
			backsp = true;
			break;
		case KEY_UP:
			up = true;
			break;
		case KEY_DOWN:
			down = true;
			break;
		case KEY_LEFT :
			left = true;
			break;
		case KEY_RIGHT :
			right = true;
			break;
		case KEY_KPPLUS :
			plus=true;
			break;
		case KEY_KPMINUS :
			minus=true;
			break;
	}

	if(menu || select || up || down || left || right )
	{
		if(helpstate)
			OsdWrite(7,STD_EXIT,(menumask-((1<<(menusub+1))-1))<=0,0); // Redraw the Exit line...
		helpstate=0;
		helptext_timer=GetTimer(HELPTEXT_DELAY);
	}

	if(helptext)
	{
		if(helpstate<9)
		{
			if(CheckTimer(helptext_timer))
			{
				helptext_timer=GetTimer(FRAME_DELAY);
				OsdWriteOffset(7,STD_EXIT,0,0,helpstate);
				++helpstate;
			}
		}
		else if(helpstate==9)
		{
			ScrollReset();
			++helpstate;
		}
		else
			ScrollText(7,helptext,0,0,0,0);
	}

	// Standardised menu up/down.
	// The screen should set menumask, bit 0 to make the top line selectable, bit 1 for the 2nd line, etc.
	// (Lines in this context don't have to correspond to rows on the OSD.)
	// Also set parentstate to the appropriate menustate to redraw the highlighted line.
	if(menumask)
	{
		if (down) {
			if (menumask>=(1<<(menusub+1)))	// Any active entries left?
			{
				do
					menusub++;
				while((menumask & (1<<menusub)) == 0);
				menustate = parentstate;
			}
			else if (menustate == MENU_NG2)
			{
				scroll_down = 1;
			}
		}

		if (up) {
			if (menusub > 0 && (menumask & ((1<<menusub)-1)))
			{
				do
					--menusub;
				while((menumask & (1<<menusub)) == 0);
				menustate = parentstate;
			}
			else if (menustate == MENU_NG2)
			{
				scroll_up = 1;
			}
		}
	}


	// Switch to current menu screen
	switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
		case MENU_NONE1 :
			helptext=helptexts[HELPTEXT_NONE];
			menumask=0;
			OsdDisable();
			menustate = MENU_NONE2;
			break;

		case MENU_NONE2 :
			if (menu)
			{
				if((user_io_core_type() == CORE_TYPE_MINIMIG) ||
				   (user_io_core_type() == CORE_TYPE_MINIMIG2))
					SetupMinimigMenu();
				else if((user_io_core_type() == CORE_TYPE_MIST) ||
				        (user_io_core_type() == CORE_TYPE_MIST2))
					tos_setup_menu();
				else if(user_io_core_type() == CORE_TYPE_ARCHIE)
					archie_setup_menu();
				else {
					// the "menu" core is special in jumps directly to the core selection menu
					if(!strcmp(user_io_get_core_name(), "MENU") || (user_io_get_core_features() & FEAT_MENU)) {
						SetupSystemMenu();
						page_idx = 1; // Firmware & Core page
						SelectFileNG("RBFARC", SCAN_LFN | SCAN_SYSDIR, CoreFileSelected, 0);
					} else {
						Setup8bitMenu();
					}
				}

				menusub = 0;
				OsdClear();
				OsdEnable(DISABLE_KEYBOARD);
			}
			break;

		/******************************************************************/
		/* common menu engine                                             */
		/******************************************************************/

		case MENU_NG:
			menu_page_callback(page_idx, MENU_PAGE_ENTER, &menu_page);
			if (menu_page.title) OsdSetTitle(menu_page.title, menu_page.flags);
			page_timer = 0;
			menustate = parentstate = MENU_NG1;

		case MENU_NG1: {
			char idx, itemidx, valid, *item;
			menumask=0;
			itemidx = menuidx[0];
			for(idx=0; idx<OSDNLINE; idx++) {
				valid = 0;
				item = "";
				menu_item.page = page_idx;
				while(menu_item_callback(itemidx++, 0, &menu_item)) {
					menu_debugf("menu_ng: idx: %d, item: %d, '%s', stipple %d page %d\n",idx, itemidx-1, menu_item.item, menu_item.stipple, menu_item.page);
					if (menu_item.page == page_idx) {
						valid = 1;
						if (menu_page.stdexit && idx == OSDNLINE-1)
							break;
						menuidx[idx] = itemidx-1;
						menu_last = idx;
						if(menu_item.newpage) {
							char l = 25-strlen(menu_item.item);
							strcpy(s, menu_item.item);
							while(l--) strcat(s, " ");
							strcat(s,"\x16"); // right arrow
							item = s;
						} else {
							item = menu_item.item;
						}
						if (menu_item.active) menumask |= 1<<idx;
						break;
					}
					menu_item.page = page_idx;
				}
				if (menu_page.stdexit && idx == OSDNLINE-1) {
					switch(menu_page.stdexit) {
						case 1:
							item = STD_EXIT;
							if (!valid) menumask |= 1<<(OSDNLINE-1);
							break;
						case 2:
							item = STD_SPACE_EXIT;
							break;
						case 3:
							item = STD_COMBO_EXIT;
							break;
					}
					menu_item.stipple = 0;
				}
				if (!valid) {
					menu_item.stipple = 0;
				}
				if (!(menumask & 1<<idx) && menusub == idx) menusub++;
				OsdWrite(idx, item, menusub == idx, menu_item.stipple);
			}
			if (menu_page.timer) page_timer = GetTimer(menu_page.timer);
			menustate = MENU_NG2;
			parentstate=MENU_NG1;
			menu_debugf("menu_first: %d menu_last: %d menusub: %d menumask: %02x\n", menuidx[0], menuidx[menu_last], menusub, menumask);
		}
		break;

		case MENU_NG2: {
			char idx, items = 0, stdexit = 0, action;

			if (menu_page.stdexit == MENU_STD_COMBO_EXIT) {
				StateKeyboardPressed(keys);
				for(i=0; i<6; i++) {
					if(keys[i]==0x29) { //ESC
						if (c==KEY_SPACE) stdexit = 1;
					}
				}
			} else if (menu_page.stdexit == MENU_STD_SPACE_EXIT) {
				if (c==KEY_SPACE) stdexit = 1;
			} else if (menu || (menu_page.stdexit && select && menusub == OSDNLINE - 1)) {
				stdexit = 1;
			}
			if (scroll_down) {
				menu_debugf("menu_ng: scroll down\n");
				scroll_down = 0;
				idx = menuidx[menu_last]+1;
				while(menu_item_callback(idx, 0, &menu_item)) {      // are more items there?
					if (menu_item.page == page_idx) {            // same page?
						items++;
						if (menu_item.active) {              // any selectable?
							menuidx[0] = menuidx[items]; // then scroll down
							menusub = OSDNLINE - 1 - (menu_page.stdexit?1:0);
							break;
						}
					}
					idx++;
				}
				menustate = parentstate;
			}
			if (scroll_up) {
				menu_debugf("menu_ng: scroll up\n");
				scroll_up = 0;
				if (menuidx[0] > 0) {
					idx = menuidx[0] - 1;
					while(menu_item_callback(idx, 0, &menu_item)) {// are more items there?
						if (menu_item.page == page_idx && menu_item.active) {     // any selectable?
							menuidx[0] = idx; // then scroll up
							menusub = 0;
							break;
						}
						if (!idx) break;
						idx--;
					}
					menustate = parentstate;
				}
			}
			if ((backsp || stdexit) && page_level) {
				ClosePage();
				break;
			}
			if (stdexit) {
				parentstate = MENU_NONE1;
				menu_page_callback(page_idx, MENU_PAGE_EXIT, &menu_page);
				menustate = parentstate;
				break;
			}
			action = MENU_ACT_NONE;
			if (select)      action = MENU_ACT_SEL;
			else if (backsp) action = MENU_ACT_BKSP;
			else if (right)  action = MENU_ACT_RIGHT;
			else if (left)   action = MENU_ACT_LEFT;
			else if (plus)   action = MENU_ACT_PLUS;
			else if (minus)  action = MENU_ACT_MINUS;

			if (action != MENU_ACT_NONE) {
				menu_item.page = page_idx;
				if (menu_item_callback(menuidx[menusub], action, &menu_item)) {
					if (menu_item.newpage) {
						parentstate = MENU_NG;
						last_menu_first[page_level] = menuidx[0];
						last_menusub[page_level] = menusub;
						last_page[page_level] = page_idx;
						page_level++;
						page_idx = menu_item.newpage;
						menuidx[0] = 0;
						menusub = menu_item.newsub;
					}
					menustate = parentstate;
				}
			} else if (menu_page.timer && CheckTimer(page_timer)) {
				menustate = MENU_NG1;
			}
		}
		break;

		/******************************************************************/
		/* About box                                                      */
		/******************************************************************/

		case MENU_8BIT_ABOUT1:
			menumask=0;
			helptext = helptexts[HELPTEXT_NONE];
			OsdSetTitle("About", 0); 
			menustate = MENU_8BIT_ABOUT2;
			parentstate=MENU_8BIT_ABOUT1;
			OsdDrawLogo(0,0,1);
			OsdDrawLogo(1,1,1);
			OsdDrawLogo(2,2,1);
			OsdDrawLogo(3,3,1);
			OsdDrawLogo(4,4,1);
			OsdDrawLogo(6,6,1);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			StarsInit();
			ScrollReset();
			break;

		case MENU_8BIT_ABOUT2:
			StarsUpdate();
			OsdDrawLogo(0,0,1);
			OsdDrawLogo(1,1,1);
			OsdDrawLogo(2,2,1);
			OsdDrawLogo(3,3,1);
			OsdDrawLogo(4,4,1);
			OsdDrawLogo(6,6,1);
			ScrollText(5,"                                 MiST by Till Harbaum, based on Minimig by Dennis van Weeren and other projects. MiST hardware and software is distributed under the terms of the GNU General Public License version 3. MiST FPGA cores are the work of their respective authors under individual licensing.", 0, 0, 0, 0);
			// menu key closes menu
			if (menu || select || left) {
				menustate = MENU_NG;
				menusub = 5;
			}
			break;
/*
		case MENU_8BIT_CHRTEST1:
			char usb_id[32];
			helptext = helptexts[HELPTEXT_NONE];
			menumask=0;
			OsdSetTitle("CHR", 0);
			menustate = MENU_8BIT_CHRTEST2;
			parentstate=MENU_8BIT_CHRTEST1;
			strcpy(usb_id, "                          ");
			for(i=1; i<24; i++) {
				if(i<4 || i>13)
					usb_id[i] = i;
				else
					usb_id[i] = ' ';
			}
			OsdWrite(0, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+24;
			OsdWrite(1, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+(24*2);
			OsdWrite(2, usb_id, 0, 0);	
			for(i=0; i<24; i++) usb_id[i] = i+(24*3);
			OsdWrite(3, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+(24*4);
			OsdWrite(4, usb_id, 0, 0);
			strcpy(usb_id, "                          ");
			for(i=0; i<8; i++) usb_id[i] = i+(24*5);
			OsdWrite(5, usb_id, 0, 0);
			//for(i=0; i<24; i++) usb_id[i] = i+(24*6);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_SPACE_EXIT, menusub==0, 0);
			break;

		case MENU_8BIT_CHRTEST2:

			if(c==KEY_SPACE) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = 1;
			}
			break;
*/
		/******************************************************************/
		/* file selection menu                                            */
		/******************************************************************/
		case MENU_FILE_SELECT:
			OsdSetTitle("Select",0);
			helptext=helptexts[HELPTEXT_NONE];
			menustate = parentstate = MENU_FILE_SELECT1;
			//break; // fall through

		case MENU_FILE_SELECT1 :
			PrintDirectory();
			menustate = MENU_FILE_SELECT2;
			break;

		case MENU_FILE_SELECT2 :
			menumask=0;

			ScrollLongName(); // scrolls file name if longer than display line

			if (c == KEY_HOME)
			{
				ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (c == KEY_BACK)
			{
				if (iCurrentDirectory) // if not root directory
				{
					ChangeDirectoryName("..");
					if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
						ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);
					else
						ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);

					menustate = MENU_FILE_SELECT1;
				}
			}

			if ((c == KEY_PGUP) || (c == KEY_LEFT))
			{
				ScanDirectory(SCAN_PREV_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if ((c == KEY_PGDN) || (c == KEY_RIGHT))
			{
				ScanDirectory(SCAN_NEXT_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (down) // scroll down one entry
			{
				ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (up) // scroll up one entry
			{
				ScanDirectory(SCAN_PREV, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if ((i = GetASCIIKey(c)))
			{ // find an entry beginning with given character
				if (nDirEntries)
				{
					if (DirEntries[sort_table[iSelectedEntry]].fattrib & AM_DIR)
					{ // it's a directory
						if (tolower(i) < tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
								ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
						}
						else if (tolower(i) > tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
							ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
						}
						else
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options)) // find nexr
								if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
									ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
						}
					}
					else
					{ // it's a file
						if (tolower(i) < tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
								ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
						}
						else if (tolower(i) > tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
								ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
						}
						else
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options)) // find next
								if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
									ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
						}
					}
				}
				menustate = MENU_FILE_SELECT1;
			}

			if (select)
			{
				if (DirEntries[sort_table[iSelectedEntry]].fattrib & AM_DIR)
				{
					ChangeDirectoryName(DirEntries[sort_table[iSelectedEntry]].fname);
					{
						if (strncmp((char*)DirEntries[sort_table[iSelectedEntry]].fname, "..", 2) == 0)
						{ // parent dir selected
							if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
								ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);
							else
								ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
						}
						else
							ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);

						menustate = MENU_FILE_SELECT1;
					}
				}
				else
				{
					if (nDirEntries)
					{
						SelectedName = (char*) &DirEntries[sort_table[iSelectedEntry]].fname;
						strncpy(DiskInfo, DirEntryInfo[iSelectedEntry], sizeof(DiskInfo));
						parentstate = MENU_NG;
						menustate = fs_MenuSelect;
					}
				}
			}

			if (menu)
			{
				menustate = MENU_NG;
			}

			break;

		case MENU_FILE_SELECT_EXIT:
			menu_select_callback(menuidx[menusub], SelectedName);
			menustate = parentstate;
			break;

		/******************************************************************/
		/* dialog box                                                     */
		/******************************************************************/
		case MENU_DIALOG1: {
			char i=0, l=0;
			const char *message = dialog_text;
			menumask = 0;
			parentstate = menustate;
			do {
 
				// line full or line break
				if((i == 29) || (*message == '\n') || !*message) {
					s[i] = 0;
					OsdWrite(l++, s, 0,0);
					i=0;  // start next line
				} else {
					s[i++] = *message;
				}
			} while(*message++);

			if(dialog_errorcode && (l < OSDNLINE)) {
				siprintf(s, " Code: #%d", dialog_errorcode);
				OsdWrite(l++, s, 0,0);
			}

			if (dialog_options & 0x03) {
				OsdWrite(l++, "", 0,0);
				if (dialog_options & MENU_DIALOG_OK) {
					menumask = 0x01;
					OsdWrite(l++, "             OK", menusub == 0,0);
				}
				if (dialog_options & MENU_DIALOG_YESNO) {
					menumask = 0x03;
					OsdWrite(l++, "             yes", menusub == 0,0);
					OsdWrite(l++, "             no", menusub == 1,0);
				}
			}
			while(l < OSDNLINE) OsdWrite(l++, "", 0,0);
			menustate = MENU_DIALOG2;
		}
		break;

		case MENU_DIALOG2:
			if (select ||
			  !(dialog_options & (MENU_DIALOG_OK | MENU_DIALOG_YESNO | MENU_DIALOG_TIMER)) ||
			  ((dialog_options & MENU_DIALOG_TIMER) && CheckTimer(menu_timer))) {
				menustate = parentstate = dialog_autoclose ? MENU_NONE1 : MENU_NG;
				helptext = dialog_helptext; // restore helptext
				if(dialog_callback) dialog_callback(menusub);
				menusub = dialog_menusub;
			}
			break;

		/******************************************************************/
		/* we should never come here                                      */
		/******************************************************************/
		default :
			break;
	}
}


static void ScrollLongName(void)
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary

	char k = sort_table[iSelectedEntry];
	static int len;
	int max_len;

	if (DirEntries[k].fname[0]) // && CheckTimer(scroll_timer)) // scroll if long name and timer delay elapsed
	{
		// FIXME - yuk, we don't want to do this every frame!
		len = strlen(DirEntries[k].fname); // get name length

		if((len > 4) && !fs_ShowExt)
			if (DirEntries[k].fname[len - 4] == '.')
				len -= 4; // remove extension

		max_len = 30; // number of file name characters to display (one more required for scrolling)
		if (DirEntries[k].fattrib & AM_DIR)
			max_len = 23; // number of directory name characters to display

		ScrollText(iSelectedEntry,DirEntries[k].fname,len,max_len,1,2);
	}
}


static char* GetDiskInfo(char* lfn, long len)
{
// extracts disk number substring form file name
// if file name contains "X of Y" substring where X and Y are one or two digit number
// then the number substrings are extracted and put into the temporary buffer for further processing
// comparision is case sensitive

    short i, k;
    static char info[] = "XX/XX"; // temporary buffer
    static char template[4] = " of "; // template substring to search for
    char *ptr1, *ptr2, c;
    unsigned char cmp;

    if (len > 20) // scan only names which can't be fully displayed
    {
        for (i = (unsigned short)len - 1 - sizeof(template); i > 0; i--) // scan through the file name starting from its end
        {
            ptr1 = &lfn[i]; // current start position
            ptr2 = template;
            cmp = 0;
            for (k = 0; k < sizeof(template); k++) // scan through template
            {
                cmp |= *ptr1++ ^ *ptr2++; // compare substrings' characters one by one
                if (cmp)
                   break; // stop further comparing if difference already found
            }

            if (!cmp) // match found
            {
                k = i - 1; // no need to check if k is valid since i is greater than zero

                c = lfn[k]; // get the first character to the left of the matched template substring
                if (c >= '0' && c <= '9') // check if a digit
                {
                    info[1] = c; // copy to buffer
                    info[0] = ' '; // clear previous character
                    k--; // go to the preceding character
                    if (k >= 0) // check if index is valid
                    {
                        c = lfn[k];
                        if (c >= '0' && c <= '9') // check if a digit
                            info[0] = c; // copy to buffer
                    }

                    k = i + sizeof(template); // get first character to the right of the mached template substring
                    c = lfn[k]; // no need to check if index is valid
                    if (c >= '0' && c <= '9') // check if a digit
                    {
                        info[3] = c; // copy to buffer
                        info[4] = ' '; // clear next char
                        k++; // go to the followwing character
                        if (k < len) // check if index is valid
                        {
                            c = lfn[k];
                            if (c >= '0' && c <= '9') // check if a digit
                                info[4] = c; // copy to buffer
                        }
                        return info;
                    }
                }
            }
        }
    }
    return NULL;
}

// print directory contents
static void PrintDirectory(void)
{
    unsigned char i;
    unsigned char k;
    unsigned long len;
    char *lfn;
    char *info;
    char *p;
    unsigned char j;

    s[32] = 0; // set temporary string length to OSD line length

    ScrollReset();

    for (i = 0; i < 8; i++)
    {
        memset(s, ' ', 32); // clear line buffer
        if (i < nDirEntries)
        {
            k = sort_table[i]; // ordered index in storage buffer
            lfn = DirEntries[k].fname; // long file name pointer
            DirEntryInfo[i][0] = 0; // clear disk number info buffer

            len = strlen(lfn); // get name length
            info = NULL; // no disk info

            if (!(DirEntries[k].fattrib & AM_DIR)) // if a file
            {
                if((len > 4) && !fs_ShowExt)
                    if (lfn[len-4] == '.')
                        len -= 4; // remove extension

                info = GetDiskInfo(lfn, len); // extract disk number info

                if (info != NULL)
                   memcpy(DirEntryInfo[i], info, 5); // copy disk number info if present
            }

            if (len > 30)
                len = 30; // trim display length if longer than 30 characters

            if (i != iSelectedEntry && info != NULL)
            { // display disk number info for not selected items
                strncpy(s + 1, lfn, 30-6); // trimmed name
                strncpy(s + 1+30-5, info, 5); // disk number
            }
            else
                strncpy(s + 1, lfn, len); // display only name

            if (DirEntries[k].fattrib & AM_DIR) // mark directory with suffix
                strcpy(&s[22], " <DIR>");
        }
        else
        {
            if (i == 0 && nDirEntries == 0) // selected directory is empty
                strcpy(s, "          No files!");
        }

        OsdWrite(i, s, i == iSelectedEntry,0); // display formatted line text
    }
}

void DialogBox(const char *message, char options, menu_dialog_t callback) {
	dialog_text = message;
	dialog_options = options;
	dialog_callback = callback;
	dialog_helptext = helptext;
	dialog_errorcode = 0;
	dialog_menusub = menusub;
	dialog_autoclose = !user_io_osd_is_visible();
	helptext = helptexts[HELPTEXT_NONE];
	menusub = 0;
	if ((options & 0x03) == MENU_DIALOG_YESNO) menusub = 1;
	menustate = parentstate = MENU_DIALOG1;
}

/*  Error Message */
void ErrorMessage(const char *message, unsigned char code) {
	DialogBox(message, MENU_DIALOG_OK, 0);
	OsdSetTitle("Error",0);
	dialog_errorcode = code;
	OsdEnable(DISABLE_KEYBOARD);
}

void InfoMessage(const char *message) {
	if (menustate != MENU_DIALOG2) {
		OsdSetTitle("Message",0);
		OsdEnable(0); // do not disable keyboard
	}
	DialogBox(message, MENU_DIALOG_TIMER, 0);
	menu_timer = GetTimer(2000);
}
