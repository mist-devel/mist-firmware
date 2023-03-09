#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "debug.h"
#include "menu.h"
#include "fdd.h"
#include "hdd.h"
#include "config.h"
#include "osd.h"
#include "fpga.h"
#include "boot.h"
#include "user_io.h"
#include "misc_cfg.h"
#include "cue_parser.h"

// TODO!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0");
extern unsigned char drives;
extern adfTYPE df[4];
static hardfileTYPE t_hardfile[HARDFILES]; // temporary copy of former hardfile configuration
static unsigned char t_enable_ide[2]; // temporary copy of former IDE configuration
static unsigned char t_ide_idx;

extern configTYPE config;
extern char s[FF_LFN_BUF + 1];
extern char minimig_ver_beta;
extern char minimig_ver_major;
extern char minimig_ver_minor;
extern char minimig_ver_minion;

const char *config_filter_msg[] =  {"none", "HORIZONTAL", "VERTICAL", "H+V"};
const char *config_memory_chip_msg[] = {"0.5 MB", "1.0 MB", "1.5 MB", "2.0 MB"};
const char *config_memory_slow_msg[] = {"none  ", "0.5 MB", "1.0 MB", "1.5 MB"};
const char *config_scanlines_msg[] = {"off", "dim", "black"};
const char *config_dither_msg[] = {"off", "SPT", "RND", "S+R"};
const char *config_memory_fast_msg[] = {"none  ", "2.0 MB", "4.0 MB","8.0 MB","Maximum"};
const char *config_hdf_msg[] = {"Disabled", "Hardfile (disk img)", "MMC/SD card", "MMC/SD partition 1", "MMC/SD partition 2", "MMC/SD partition 3", "MMC/SD partition 4"};
const char *config_chipset_msg[] = {"OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---"};
const char *config_turbo_msg[] = {"none", "CHIPRAM", "KICK", "BOTH"};
const char *config_cd32pad_msg[] =  {"OFF", "ON"};
const char *config_joystick_msg[] =  {"Digital", "Analogue"};
char *config_button_turbo_msg[] = {"OFF", "FAST", "MEDIUM", "SLOW"};
char *config_button_turbo_choice_msg[] = {"A only", "B only", "A & B"};
const char *config_audio_filter_msg[] = {"switchable", "always off", "always on"};
const char *config_power_led_off_msg[] = {"dim", "off"};

const char *KickstartSelectedName;

// TODO: remove these extern hacks to private variables
extern char DiskInfo[5]; // disk number info of selected entry
extern unsigned char menusub;

////////////////////////////
/////// Minimig menu ///////
////////////////////////////
const char *config_memory_fast_txt()
{
  if (!(((config.cpu & 0x02) == 0x02) && ((config.memory >> 4 & 0x03) == 0x03)))
    return config_memory_fast_msg[config.memory >> 4 & 0x03];
  else
    return config_memory_fast_msg[(config.memory >> 4 & 0x03) + 1];
}

static void _strncpy(char* pStr1, const char* pStr2, size_t nCount)
{
// customized strncpy() function to fill remaing destination string part with spaces

	while (*pStr2 && nCount)
	{
		*pStr1++ = *pStr2++; // copy strings
		nCount--;
	}

	while (nCount--)
		*pStr1++ = ' '; // fill remaining space with spaces
}

// insert floppy image pointed to to by global <file> into <drive>
static void InsertFloppy(adfTYPE *drive, const unsigned char *name)
{
	unsigned char i, j, readonly = false;
	unsigned long tracks;
	FRESULT res;

	if ((res = f_open(&drive->file, name, FA_READ | FA_WRITE)) != FR_OK) {
		iprintf("Disk open failed (%d), trying read only mode\n", res);
		readonly = true;
		if (f_open(&drive->file, name, FA_READ) != FR_OK)
		return;
	}
	// calculate number of tracks in the ADF image file
	tracks = f_size(&drive->file) / (512*11);
	if (tracks > MAX_TRACKS) {
		iprintf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\r", tracks);
		tracks = MAX_TRACKS;
	}
	drive->tracks = (unsigned char)tracks;

	// copy image file name into drive struct
	_strncpy(drive->name, name, sizeof(drive->name));

	if (DiskInfo[0]) {// if selected file has valid disk number info then copy it to its name in drive struct
		drive->name[16] = ' '; // precede disk number info with space character
		strncpy(&drive->name[17], DiskInfo, sizeof(DiskInfo)); // copy disk number info
	}

	// initialize the rest of drive struct
	drive->status = DSK_INSERTED;
	if (!readonly) // read-only attribute
		drive->status |= DSK_WRITABLE;

	drive->sector_offset = 0;
	drive->track = 0;
	drive->track_prev = -1;

	// some debug info
	iprintf("Inserting floppy: \"%s\"\r", name);
	iprintf("file readonly: 0x%u\r", readonly);
	iprintf("file size: %llu (%llu KB)\r", f_size(&drive->file), f_size(&drive->file) >> 10);
	iprintf("drive tracks: %u\r", drive->tracks);
	iprintf("drive status: 0x%02X\r", drive->status);
}

static void inserttestfloppy() {
	char name[] = "/AUTOX.ADF";
	int i;

	for(i=0;i<4;i++) {
		name[5] = '0'+i;
		InsertFloppy(&df[i], name);
	}
}

static char FloppyFileSelected(uint8_t idx, const char *SelectedName) {
	InsertFloppy(&df[idx], SelectedName);
	menusub++;
	return 0;
}

static char HardFileChanged(uint8_t idx) {

	if (idx == 0) {// yes
		// FIXME - waiting for user-confirmation increases the window of opportunity for file corruption!
		for (int i = 0; i < HARDFILES; i++) {
			if ((config.hardfile[i].enabled != t_hardfile[i].enabled)
			    || (strncmp(config.hardfile[i].name, t_hardfile[i].name, sizeof(t_hardfile[0].name)) != 0))
			{
				OpenHardfile(i, true);
				//if((config.hardfile[0].enabled == HDF_FILE) && !FindRDB(0))
				//	menustate = MENU_SYNTHRDB1;
			}
		}

		ConfigIDE(config.enable_ide[0],        config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
		ConfigIDE(config.enable_ide[1] | 0x02, config.hardfile[2].present && config.hardfile[2].enabled, config.hardfile[3].present && config.hardfile[3].enabled);
		CloseMenu();
		OsdReset(RESET_NORMAL);
	} else { // no
		memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
		config.enable_ide[t_ide_idx] = t_enable_ide[t_ide_idx];
	}
	return 0;
}

static char HardFileSelected(uint8_t idx, const char *SelectedName) {
	int hdf_idx;
	if (idx == 10) // master drive selected
		hdf_idx = t_ide_idx << 1;
	else if (idx == 12) // slave drive selected
		hdf_idx = (t_ide_idx << 1) + 1;
	else // invalid
		return 0;

	// Read RDB from selected drive and determine type...
	strncpy(config.hardfile[hdf_idx].name, SelectedName, sizeof(config.hardfile[hdf_idx].name));
	config.hardfile[hdf_idx].name[sizeof(config.hardfile[hdf_idx].name)-1] = 0;
	switch(GetHDFFileType(SelectedName)) {
		case HDF_FILETYPE_RDB:
			config.hardfile[hdf_idx].enabled=HDF_FILE;
			config.hardfile[hdf_idx].present = 1;
			break;
		case HDF_FILETYPE_DOS:
			config.hardfile[hdf_idx].enabled=HDF_FILE|HDF_SYNTHRDB;
			config.hardfile[hdf_idx].present = 1;
			break;
		case HDF_FILETYPE_UNKNOWN:
			config.hardfile[hdf_idx].present = 1;
			if(config.hardfile[hdf_idx].enabled==HDF_FILE) // Warn if we can't detect the type
				DialogBox("\n No partition table found -\n Hardfile image may need\n to be prepped with\n HDToolbox, then formatted.", MENU_DIALOG_OK, 0);
			else
				DialogBox("\n No filesystem recognised.\n Hardfile may need formatting\n (or may simply be an\n unrecognised filesystem)", MENU_DIALOG_OK, 0);
			break;
		case HDF_FILETYPE_NOTFOUND:
		default:
			config.hardfile[hdf_idx].present = 0;
	}
	return 0;
}

static char CueISOFileSelected(uint8_t idx, const char *SelectedName) {
	int hdf_idx;
	if (idx == 10) // master drive selected
		hdf_idx = t_ide_idx << 1;
	else if (idx == 12) // slave drive selected
		hdf_idx = (t_ide_idx << 1) + 1;
	else // invalid
		return 0;

	char res;
	res = cue_parse(SelectedName, &sd_image[hdf_idx]);
	if (res) ErrorMessage(cue_error_msg[res-1], res);
}

static char KickstartReload(uint8_t idx) {
	if (idx == 0) {// yes
		CloseMenu();
		strncpy(config.kickstart, KickstartSelectedName, sizeof(config.kickstart));
		config.kickstart[sizeof(config.kickstart) - 1] = 0;
		if(minimig_v1()) {
			OsdDisable();
			OsdReset(RESET_BOOTLOADER);
			ConfigChipset(config.chipset | CONFIG_TURBO);
			ConfigFloppy(config.floppy.drives, CONFIG_FLOPPY2X);
			if (UploadKickstart(config.kickstart)) {
				BootExit();
			}
			ConfigChipset(config.chipset); // restore CPU speed mode
			ConfigFloppy(config.floppy.drives, config.floppy.speed); // restore floppy speed mode
		} else {
			// reset bootscreen cursor position
			BootHome();
			OsdDisable();
			EnableOsd();
			SPI(OSD_CMD_RST);
			rstval = (SPI_RST_CPU | SPI_CPU_HLT);
			SPI(rstval);
			DisableOsd();
			SPIN(); SPIN(); SPIN(); SPIN();
			UploadKickstart(config.kickstart);
			EnableOsd();
			SPI(OSD_CMD_RST);
			rstval = (SPI_RST_USR | SPI_RST_CPU);
			SPI(rstval);
			DisableOsd();
			SPIN(); SPIN(); SPIN(); SPIN();
			EnableOsd();
			SPI(OSD_CMD_RST);
			rstval = 0;
			SPI(rstval);
			DisableOsd();
			SPIN(); SPIN(); SPIN(); SPIN();
		}
	}
	return 0;
}

static char KickstartSelected(uint8_t idx, const char *SelectedName) {
	KickstartSelectedName = SelectedName;
	DialogBox("\n       Reload Kickstart?\n", MENU_DIALOG_YESNO, KickstartReload);
	return 0;
}

static char GetMenuPage_Minimig(uint8_t idx, char action, menu_page_t *page) {
	if (action == MENU_PAGE_EXIT) {
		if(idx == 1) {
			if ((memcmp(config.hardfile, t_hardfile, sizeof(t_hardfile)) != 0) ||
			    (config.enable_ide[0] != t_enable_ide[0]) ||
			    (config.enable_ide[1] != t_enable_ide[1]))
			{
				DialogBox("\n    Changing configuration\n      requires reset.\n\n       Reset Minimig?", MENU_DIALOG_YESNO, HardFileChanged);
			}

		};
	} else {
		page->timer = 0;
		page->stdexit = MENU_STD_EXIT;
		page->flags = OSD_ARROW_RIGHT;

		switch (idx) {
			case 0:
				// set helptext with core display on top of basic info
				page->title = "Minimig";
				strcpy(helptext_custom, HELPTEXT_SPACER);
				strcat(helptext_custom, OsdCoreName());
				siprintf(s, "%s v%d.%d.%d", minimig_ver_beta ? " BETA" : "", minimig_ver_major, minimig_ver_minor, minimig_ver_minion);
				strcat(helptext_custom, s);
				strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
				helptext=helptext_custom;
				break;
			case 1:
				page->title = "Harddisks";
				page->flags = 0;
				helptext=helptexts[HELPTEXT_HARDFILE];
				break;
			case 2:
				page->title = "Settings";
				page->flags = OSD_ARROW_LEFT|OSD_ARROW_RIGHT;
				helptext=helptexts[HELPTEXT_MAIN];
				break;
			case 3:
				page->title = "Load";
				page->flags = 0;
				helptext=helptexts[HELPTEXT_NONE];
				break;
			case 4:
				page->title = "Save";
				page->flags = 0;
				helptext=helptexts[HELPTEXT_NONE];
				break;
			case 5:
				page->title = "Chipset";
				page->flags = OSD_ARROW_LEFT|OSD_ARROW_RIGHT;
				helptext=helptexts[HELPTEXT_CHIPSET];
				break;
			case 6:
				page->title = "Memory";
				page->flags = OSD_ARROW_LEFT|OSD_ARROW_RIGHT;
				helptext=helptexts[HELPTEXT_MEMORY];
				break;
			case 7:
				page->title = "Video";
				page->flags = OSD_ARROW_LEFT|OSD_ARROW_RIGHT;
				helptext=helptexts[HELPTEXT_VIDEO];
				break;
			case 8:
				page->title = "Features";
				page->flags = OSD_ARROW_LEFT|OSD_ARROW_RIGHT;
				helptext=helptexts[HELPTEXT_FEATURES];
				break;
		}
	}
	return 0;
}

static char GetMenuItem_Minimig(uint8_t idx, char action, menu_item_t *item) {
	char page_idx = item->page; // save current page number
	item->stipple = 0;
	item->active = 1;
	item->page = 0;
	item->newpage = 0;
	item->newsub = 0;
	item->item = "";
	if(idx<=6) item->page = 0;
	else if(idx<=12) item->page = 1;
	else if(idx<=19) item->page = 2;
	else if(idx<=25) item->page = 3;
	else if(idx<=31) item->page = 4;
	else if(idx<=37) item->page = 5;
	else if(idx<=44) item->page = 6;
	else if(idx<=49) item->page = 7;
	else if(idx<=52) item->page = 8;
	else return 0;

	if (item->page != page_idx) return 1; // shortcut

	switch (action) {
		case MENU_ACT_GET:
			switch(idx) {
				case 0:
				case 1:
				case 2:
				case 3:
					// floppy drive info
					// We display a line for each drive that's active
					// in the config file, but grey out any that the FPGA doesn't think are active.
					// We also print a help text in place of the last drive if it's inactive.
					if(idx==config.floppy.drives+1) {
						item->item = " KP +/- to add/remove drives";
						item->active = 0;
						item->stipple = 1;
					} else {
						strcpy(s, " dfx: ");
						s[3] = idx + '0';
						if (idx <= drives) {
							if (df[idx].status & DSK_INSERTED) {// floppy disk is inserted
								strncpy(&s[6], df[idx].name, sizeof(df[0].name));
								if(!(df[idx].status & DSK_WRITABLE))
									strcpy(&s[6 + sizeof(df[idx].name)-1], " \x17"); // padlock icon for write-protected disks
								else
									strcpy(&s[6 + sizeof(df[idx].name)-1], "  "); // clear padlock icon for write-enabled disks
							} else {// no floppy disk
								strcat(s, "* no disk *");
							}
						} else if(idx<=config.floppy.drives) {
							strcat(s,"* active after reset *");
						} else
							strcpy(s, "");
						item->item = s;
						if ((idx>drives)||(idx>config.floppy.drives)) item->active = 0;
						item->stipple = !item->active;
					}
					break;
				case 4:
					siprintf(s," Floppy disk turbo : %s",config.floppy.speed ? "on" : "off");
					item->item = s;
					break;
				case 5:
					item->item = " Primary hard disks";
					item->newpage = 1;
					break;
				case 6:
					item->item = " Secondary hard disks";
					item->newpage = 1;
					break;

				// Page 1 - Hard disks
				case 7:
					siprintf(s, "   A600 %s IDE : %s",
						t_ide_idx ? "Secondary" : "Primary",
						config.enable_ide[t_ide_idx] ? "on " : "off");
					item->item = s;
					break;
				case 8:
					item->active = 0;
					break;
				case 9:
				case 11: {
					uint8_t slave = idx == 11;
					strcpy(s, slave ? "  Slave : " : " Master : ");
					if(config.hardfile[(t_ide_idx << 1)+slave].enabled==(HDF_FILE|HDF_SYNTHRDB))
						strcat(s,"Hardfile (filesys)");
					else if(config.hardfile[(t_ide_idx << 1)+slave].enabled==HDF_CDROM)
						strcat(s,"CDROM");
					else
						strcat(s, config_hdf_msg[config.hardfile[(t_ide_idx << 1)+slave].enabled & HDF_TYPEMASK]);
					item->item = s;
					item->active = config.enable_ide[t_ide_idx];
					item->stipple = !item->active;
					}
					break;
				case 10:
				case 12: {
					uint8_t slave = idx == 12;
					uint8_t enabled = config.hardfile[(t_ide_idx << 1)+slave].enabled;
					if (config.hardfile[(t_ide_idx << 1)+slave].present) {
						strcpy(s, "                                ");
						if(enabled == HDF_CDROM)
							strcpy(&s[14], toc.valid ? "* Inserted *" : "* Empty *");
						else
							strncpy(&s[14], config.hardfile[(t_ide_idx << 1)+slave].name, sizeof(config.hardfile[0].name));
					} else
						strcpy(s, "       ** file not found **");
					item->item = s;
					item->active = config.enable_ide[t_ide_idx] &&
					   (((enabled&HDF_TYPEMASK) == HDF_FILE) || ((enabled&HDF_TYPEMASK) == HDF_CDROM));
					item->stipple = !item->active;
					}
					break;

				// Page 2 - Settings
				case 13:
					item->item = "    load configuration";
					item->newpage = 3;
					break;
				case 14:
					item->item = "    save configuration";
					item->newpage = 4;
					break;
				case 16:
					item->item = "    chipset settings";
					item->newpage = 5;
					break;
				case 17:
					item->item = "     memory settings";
					item->newpage = 6;
					break;
				case 18:
					item->item = "      video settings";
					item->newpage = 7;
					break;
				case 19:
					item->item = "   features settings";
					item->newpage = 8;
					break;

				// Page 3 - Load configuration
				case 21:
				case 22:
				case 23:
				case 24:
				case 25:
					SetConfigurationFilename(idx-21);
					if(!ConfigurationExists(0)) item->active = 0;
					item->stipple = !item->active;
					strcpy(s,"          ");
					strcat(s, minimig_cfg.conf_name[idx-21]);
					item->item = s;
					break;

				// Page 4 - Save configuration
				case 27:
				case 28:
				case 29:
				case 30:
				case 31:
					strcpy(s,"          ");
					strcat(s, minimig_cfg.conf_name[idx-27]);
					item->item = s;
					break;

				// Page 5 - Chipset
				case 32:
					strcpy(s, "         CPU : ");
					strcat(s, config_cpu_msg[config.cpu & 0x03]);
					item->item = s;
					break;
				case 33:
					strcpy(s, "       Turbo : ");
					strcat(s, config_turbo_msg[(config.cpu >> 2) & 0x03]);
					item->item = s;
					break;
				case 34:
					strcpy(s, "       Video : ");
					strcat(s, config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
					item->item = s;
					break;
				case 35:
					strcpy(s, "     Chipset : ");
					strcat(s, config_chipset_msg[(config.chipset >> 2) & (minimig_v1()?3:7)]);
					item->item = s;
					break;
				case 36:
					strcpy(s, "     CD32Pad : ");
					strcat(s, config_cd32pad_msg[(config.autofire >> 2) & 1]);
					item->item = s;
					break;
				case 37:
					strcpy(s, "    Joystick : ");
					strcat(s, config_joystick_msg[(config.autofire >> 3) & 1]);
					item->item = s;
					break;

				// Page 6 - Memory
				case 39:
					strcpy(s, "      CHIP  : ");
					strcat(s, config_memory_chip_msg[config.memory & 0x03]);
					item->item = s;
					break;
				case 40:
					strcpy(s, "      SLOW  : ");
					strcat(s, config_memory_slow_msg[config.memory >> 2 & 0x03]);
					item->item = s;
					break;
				case 41:
					strcpy(s, "      FAST  : ");
					strcat(s, config_memory_fast_txt());
					item->item = s;
					break;
				case 43:
					strcpy(s, "      ROM   : ");
					strncat(s, config.kickstart, sizeof(config.kickstart));
					item->item = s;
					break;
				case 44:
					strcpy(s, "      HRTmon: ");
					strcat(s, (config.memory&0x40) ? "enabled " : "disabled");
					item->item = s;
					break;

				// Page 7 - Video
				case 46:
					strcpy(s, "   Lores Filter : ");
					strcat(s, config_filter_msg[config.filter.lores & 0x03]);
					item->item = s;
					break;
				case 47:
					strcpy(s, "   Hires Filter : ");
					strcat(s, config_filter_msg[config.filter.hires & 0x03]);
					item->item = s;
					break;
				case 48:
					strcpy(s, "   Scanlines    : ");
					strcat(s, config_scanlines_msg[(config.scanlines&0x3) % 3]);
					item->item = s;
					break;
				case 49:
					if(minimig_v1()) {
						item->active = 0;
					} else {
						strcpy(s, "   Dither       : ");
						strcat(s, config_dither_msg[(config.scanlines>>2) & 0x03]);
						item->item = s;
					}
					break;

				// Page 8 = Features
				case 51:
					strcpy(s, "  Audio Filter  : ");
					strcat(s, config_audio_filter_msg[(config.features.audiofiltermode & 0x03) % 3]);
					item->item = s;
					break;
				case 52:
					strcpy(s, "  Power LED off : ");
					strcat(s, config_power_led_off_msg[config.features.powerledoffstate & 0x01]);
					item->item = s;
					break;

				default:
					item->active = 0;
			}
			break;
		case MENU_ACT_SEL:
			switch(idx) {
				case 0:
				case 1:
				case 2:
				case 3:
					if (df[idx].status & DSK_INSERTED) {// eject selected floppy
						df[idx].status = 0;
					} else {
						df[idx].status = 0;
						SelectFileNG("ADF", SCAN_DIR | SCAN_LFN, FloppyFileSelected, 0);
					}
					break;
				case 4:
					config.floppy.speed^=1;
					ConfigFloppy(config.floppy.drives,config.floppy.speed);
					break;
				case 5:
				case 6:
					memcpy(t_hardfile, config.hardfile, sizeof(config.hardfile));
					t_enable_ide[0] = config.enable_ide[0];
					t_enable_ide[1] = config.enable_ide[1];
					t_ide_idx = idx-5;
					item->newpage = 1;
					break;

				// FIXME!  Nasty race condition here.  Changing HDF type has immediate effect
				// which could be disastrous if the user's writing to the drive at the time!
				// Make the menu work on the copy, not the original, and copy on acceptance,
				// not on rejection.
				case 7:
					config.enable_ide[t_ide_idx]=(config.enable_ide[t_ide_idx]==0);
					break;
				case 9:
				case 11: {
					uint8_t hdf_idx = (t_ide_idx << 1) + (idx == 11);
					if(config.hardfile[hdf_idx].enabled==HDF_FILE) {
						config.hardfile[hdf_idx].enabled|=HDF_SYNTHRDB;
					} else if(config.hardfile[hdf_idx].enabled==(HDF_FILE|HDF_SYNTHRDB)) {
						config.hardfile[hdf_idx].enabled&=~HDF_SYNTHRDB;
						config.hardfile[hdf_idx].enabled +=1;
					} else if(config.hardfile[hdf_idx].enabled==(HDF_CARDPART0+partitioncount)) {
						// only one CDROM is supported, so check if already choosen
						if (config.hardfile[0].enabled != HDF_CDROM &&
						    config.hardfile[1].enabled != HDF_CDROM &&
						    config.hardfile[2].enabled != HDF_CDROM &&
						    config.hardfile[3].enabled != HDF_CDROM) {
							config.hardfile[hdf_idx].enabled = HDF_CDROM;
						} else {
							config.hardfile[hdf_idx].enabled = 0;
						}
					} else if(config.hardfile[hdf_idx].enabled==HDF_CDROM) {
						config.hardfile[hdf_idx].enabled = 0;
					} else {
						config.hardfile[hdf_idx].enabled +=1;
					}
					}
					break;
				case 10:
				case 12: {
					uint8_t hdf_idx = (t_ide_idx << 1) + (idx == 12);
					if(config.hardfile[hdf_idx].enabled==HDF_CDROM) {
						if(toc.valid)
							toc.valid = 0;
						else
							SelectFileNG("CUEISO", SCAN_DIR | SCAN_LFN, CueISOFileSelected, 0);
					} else {
						SelectFileNG("HDF", SCAN_LFN, HardFileSelected, 0);
					}
					}
					break;

				// Page 2 - Settings
				case 13:
					item->newpage = 3;
					break;
				case 14:
					item->newpage = 4;
					break;
				case 16:
					item->newpage = 5;
					break;
				case 17:
					item->newpage = 6;
					break;
				case 18:
					item->newpage = 7;
					break;
				case 19:
					item->newpage = 8;
					break;

				// Page 3 - Load configuration
				case 21:
				case 22:
				case 23:
				case 24:
				case 25:
					CloseMenu();
					ResetMenu();
					OsdDisable();
					SetConfigurationFilename(idx-21);
					LoadConfiguration(NULL, 0);
					break;

				// Page 4 - Save configuration
				case 27:
				case 28:
				case 29:
				case 30:
				case 31:
					SetConfigurationFilename(idx-27);
					SaveConfiguration(NULL);
					CloseMenu();
					break;

				// Page 5 - Chipset
				case 32: {
					int _config_cpu = config.cpu & 0x3;
					_config_cpu += 1;
					if (_config_cpu==0x02) _config_cpu += 1;
					config.cpu = (config.cpu & 0xfc) | (_config_cpu & 0x3);
					ConfigCPU(config.cpu);
					}
					break;
				case 33: {
					int _config_turbo = (config.cpu >> 2) & 0x3;
					_config_turbo += 1;
					config.cpu = (config.cpu & 0x3) | ((_config_turbo & 0x3) << 2);
					ConfigCPU(config.cpu);
					}
					break;
				case 34:
					config.chipset ^= CONFIG_NTSC;
					ConfigChipset(config.chipset);
					break;
				case 35:
					if(minimig_v1()) {
						if (config.chipset & CONFIG_ECS)
							config.chipset &= ~(CONFIG_ECS|CONFIG_A1000);
						else
							config.chipset += CONFIG_A1000;
					} else {
						switch(config.chipset&0x1c) {
							case 0:
								config.chipset = (config.chipset&3) | CONFIG_A1000;
								break;
							case CONFIG_A1000:
								config.chipset = (config.chipset&3) | CONFIG_ECS;
								break;
							case CONFIG_ECS:
								config.chipset = (config.chipset&3) | CONFIG_AGA | CONFIG_ECS;
								break;
							case (CONFIG_AGA|CONFIG_ECS):
								config.chipset = (config.chipset&3) | 0;
								break;
						}
					}
					ConfigChipset(config.chipset);
					break;
				case 36:
					//config.autofire = ((((config.autofire >> 2) + 1) & 1) << 2) || (config.autofire & 3);
					config.autofire  = (config.autofire ^ 0x04);
					ConfigAutofire(config.autofire);
					break;
				case 37:
					config.autofire  = (config.autofire ^ 0x08);
					ConfigAutofire(config.autofire);
					break;

				// Page 6 - Memory
				case 39:
					config.memory = ((config.memory + 1) & 0x03) | (config.memory & ~0x03);
					ConfigMemory(config.memory);
					break;
				case 40:
					config.memory = ((config.memory + 4) & 0x0C) | (config.memory & ~0x0C);
					ConfigMemory(config.memory);
					break;
				case 41:
					config.memory = ((config.memory + 0x10) & 0x30) | (config.memory & ~0x30);
					//if ((config.memory & 0x30) == 0x30)
					//config.memory -= 0x30;
					//if (!(config.disable_ar3 & 0x01)&&(config.memory & 0x20))
					//config.memory &= ~0x30;
					ConfigMemory(config.memory);
					break;
				case 43:
					SelectFileNG("ROM", SCAN_LFN, KickstartSelected, 0);
					break;
				case 44:
					config.memory ^= 0x40;
					ConfigMemory(config.memory);
					break;

				// Page 7 - Video
				case 46:
					config.filter.lores++;
					config.filter.lores &= 0x03;
					if(minimig_v1())
						MM1_ConfigFilter(config.filter.lores, config.filter.hires);
					else
						ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					break;
				case 47:
					config.filter.hires++;
					config.filter.hires &= 0x03;
					if(minimig_v1())
						MM1_ConfigFilter(config.filter.lores, config.filter.hires);
					else
						ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					break;
				case 48:
					if(minimig_v1()) {
						config.scanlines++;
						if (config.scanlines > 2)
							config.scanlines = 0;
						MM1_ConfigScanlines(config.scanlines);
					} else {
						config.scanlines = ((config.scanlines + 1)&0x03) | (config.scanlines&0xfc);
						if ((config.scanlines&0x03) > 2)
							config.scanlines = config.scanlines&0xfc;
						ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					}
					break;
				case 49:
					if (!minimig_v1()) {
						config.scanlines = (config.scanlines + 4)&0x0f;
						ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					}
					break;

				// Page 8 = Features
				case 51:
					config.features.audiofiltermode++;
					if (config.features.audiofiltermode > 2)
						config.features.audiofiltermode = 0;
					ConfigFeatures(config.features.audiofiltermode, config.features.powerledoffstate);
					break;
				case 52:
					config.features.powerledoffstate ^= 1;
					ConfigFeatures(config.features.audiofiltermode, config.features.powerledoffstate);
					break;

				default:
					return 0;
			}
			break;
		case MENU_ACT_PLUS:
		case MENU_ACT_MINUS:
			if (page_idx == 0) { // add/remove floppy drive
				if(action == MENU_ACT_PLUS && (config.floppy.drives<3)) {
					config.floppy.drives++;
					ConfigFloppy(config.floppy.drives,config.floppy.speed);
				} else if(action == MENU_ACT_MINUS && (config.floppy.drives>0)) {
					config.floppy.drives--;
					ConfigFloppy(config.floppy.drives,config.floppy.speed);
				}
				//menustate = MENU_MAIN1;
			} else
				return 0;
			break;
		case MENU_ACT_BKSP:
			if (page_idx == 0) { // eject all floppies
				for (int i = 0; i <= drives; i++)
					df[i].status = 0;
			}
			break;
		case MENU_ACT_RIGHT:
			switch(page_idx) {
				case 0:
					item->newpage = 2;
					break;
				case 2:
					SetupSystemMenu();
					break;
				case 5:
				case 6:
				case 7:
					ChangePage(page_idx+1);
					break;
				case 8:
					ChangePage(5);
					break;
				default:
					return 0;
			}
			break;
		case MENU_ACT_LEFT:
			switch(page_idx) {
				case 2: // return to main
					ClosePage();
					break;
				case 5:
					ChangePage(8);
					break;
				case 6:
				case 7:
				case 8:
					ChangePage(page_idx-1);
					break;
				default:
					return 0;
			}
			break;
		default:
			return 0;
	}
	return 1;
}

void SetupMinimigMenu() {
	SetupMenu(GetMenuPage_Minimig, GetMenuItem_Minimig, NULL);
}
