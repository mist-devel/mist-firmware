#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware.h"
#include "osd.h"
#include "state.h"
#include "state.h"
#include "user_io.h"
#include "data_io.h"
#include "archie.h"
#include "pcecd.h"
#include "hdd.h"
#include "cdc_control.h"
#include "usb.h"
#include "debug.h"
#include "keycodes.h"
#include "ikbd.h"
#include "idxfile.h"
#include "spi.h"
#include "mist_cfg.h"
#include "mmc.h"
#include "tos.h"
#include "errors.h"
#include "arc_file.h"
#include "cue_parser.h"
#include "utils.h"
#include "settings.h"
#include "usb/joymapping.h"
#include "usb/joystick.h"
#include "FatFs/diskio.h"
#include "menu.h"

// up to 16 key can be remapped
#define MAX_REMAP  16
unsigned char key_remap_table[MAX_REMAP][2];

#define BREAK  0x8000

static char umounted; // 1st image is file or direct SD?
static char buffer[512];
static uint8_t buffer_drive_index = 0;
static uint32_t buffer_lba = 0xffffffff;

extern char s[FF_LFN_BUF + 1];

// mouse and keyboard emulation state
typedef enum { EMU_NONE, EMU_MOUSE, EMU_JOY0, EMU_JOY1 } emu_mode_t;
static emu_mode_t emu_mode = EMU_NONE;
static unsigned char emu_state = 0;
static unsigned long emu_timer = 0;
#define EMU_MOUSE_FREQ 5

// keep state over core type and its capabilities
static unsigned char core_type = CORE_TYPE_UNKNOWN;
static char core_type_8bit_with_config_string = 0;
// core supports direct ROM upload via SS4
extern char rom_direct_upload;

// extra features in the firmware requested by the core
static uint32_t core_features = 0;

// core variant (mostly for arcades)
static char core_mod = 0;

// keep state of caps lock
static char caps_lock_toggle = 0;

// avoid multiple keyboard/controllers to interfere
static uint8_t latest_keyb_priority = 0;  // keyboard=0, joypad with key mappings=1

// mouse position storage for ps2 and minimig rate limitation
#define X 0
#define Y 1
#define Z 2
#define MOUSE_FREQ 20   // 20 ms -> 50hz
static int16_t mouse_pos[2][3] = { {0, 0, 0}, {0, 0, 0} };
static uint8_t mouse_flags[2] = { 0, 0 };
static unsigned long mouse_timer;

#define LED_FREQ 100   // 100 ms
static unsigned long led_timer;
char keyboard_leds = 0;
bool caps_status = 0;
bool num_status = 0;
bool scrl_status = 0;

#define RTC_FREQ 1000   // 1 s
static unsigned long rtc_timer;

static unsigned char modifier = 0, pressed[6] = { 0,0,0,0,0,0 };

static unsigned char ps2_typematic_rate = 0x80;
static unsigned long ps2_typematic_timer;

typedef enum { PS2_KBD_IDLE, PS2_KBD_SCAN_GETSET, PS2_KBD_TYPEMATIC_SET, PS2_KBD_LED_SET } ps2_kbd_state_t;
static ps2_kbd_state_t ps2_kbd_state;
static char ps2_kbd_scan_set = 2;

typedef enum { PS2_MOUSE_IDLE, PS2_MOUSE_SETRESOLUTION, PS2_MOUSE_SETSAMPLERATE } ps2_mouse_state_t;
static ps2_mouse_state_t ps2_mouse_state;

static unsigned char ps2_mouse_status;
static unsigned char ps2_mouse_resolution;
static unsigned char ps2_mouse_samplerate;

// set by OSD code to suppress forwarding of those keys to the core which
// may be in use by an active OSD
static char osd_is_visible = false;

static char autofire;
static unsigned long autofire_timer;
static uint32_t autofire_map;
static uint32_t autofire_mask;
static char autofire_joy;

// ATA drives
static hardfileTYPE  hardfiles[4];

char user_io_osd_is_visible() {
	return osd_is_visible;
}

void user_io_reset() {
	// no sd card image selected, SD card accesses will go directly
	// to the card (first slot, and only until the first unmount)
	umounted = 0;
	toc.valid = 0;
	sd_image[0].valid = 0;
	sd_image[1].valid = 0;
	sd_image[2].valid = 0;
	sd_image[3].valid = 0;
	for (int i=0; i<HARDFILES; i++) {
		hardfiles[i].enabled = HDF_DISABLED;
		hardfiles[i].present = 0;
	}
	core_mod = 0;
	core_features = 0;
	ps2_kbd_state = PS2_KBD_IDLE;
	ps2_kbd_scan_set = 2;
	ps2_mouse_state = PS2_MOUSE_IDLE;
	ps2_mouse_status = 0;
	ps2_mouse_resolution = 0;
	ps2_mouse_samplerate = 0;
	ps2_typematic_rate = 0x80;
	autofire = 0;
	autofire_joy = -1;
}

void user_io_init() {

	user_io_reset();

	if(VIDEO_KEEP_VAR != VIDEO_KEEP_VALUE) VIDEO_ALTERED_VAR = 0;
	VIDEO_KEEP_VAR = 0;

	// mark remap table as unused
	memset(key_remap_table, 0, sizeof(key_remap_table));

	if(MenuButton()) DEBUG_MODE_VAR = DEBUG_MODE ? 0 : DEBUG_MODE_VALUE;
	iprintf("debug_mode = %d\n", DEBUG_MODE);

	ikbd_init();
}

unsigned char user_io_core_type() {
	return core_type;
}

char minimig_v1() {
	return(core_type == CORE_TYPE_MINIMIG);
}

char minimig_v2() {
	return(core_type == CORE_TYPE_MINIMIG2);
}

char user_io_create_config_name(char *s, const char *ext, char flags) {
	char *p = 0;
	if (flags & CONFIG_VHD) p = arc_get_vhdname();
	if (!p || !*p) p = user_io_get_core_name();
	if(p[0]) {
		if (flags & CONFIG_ROOT) strcpy(s,"/"); else s[0] = 0;
		strcat(s, p);
		if (ext) {
			strcat(s,".");
			strcat(s,ext);
		}
		return 0;
	}
	return 1;
}

char user_io_is_8bit_with_config_string() {
	return core_type_8bit_with_config_string;
}

static char core_name[16+1];  // max 16 bytes for core name

char *user_io_get_core_name() {
	char *arc_core_name = arc_get_corename();
	return *arc_core_name ? arc_core_name : core_name;
}

static void user_io_read_core_name() {
	core_name[0] = 0;

	if(user_io_is_8bit_with_config_string()) {
		char *p = user_io_8bit_get_string(0);  // get core name
		if(p && p[0]) strncpy(core_name, p, sizeof(core_name));
		core_name[sizeof(core_name)-1] = 0;
	}

	iprintf("Core name from FPGA is \"%s\"\n", core_name);
}

void user_io_set_core_mod(char mod) {
	core_mod = mod;
}

static void user_io_send_core_mod() {
	iprintf("Sending core mod = %d\n", core_mod);
	spi_uio_cmd8(UIO_SET_MOD, core_mod);
}

void user_io_send_rtc(void) {
	uint8_t date[7]; //year,month,date,hour,min,sec,day
	uint8_t i;

	if (GetRTC((uint8_t*)&date)) {
		//iprintf("Sending time of day %u:%02u:%02u %u.%u.%u\n",
		//  date[3], date[4], date[5], date[2], date[1], 1900 + date[0]);
		spi_uio_cmd_cont(UIO_SET_RTC);
		spi8(bin2bcd(date[5])); // sec
		spi8(bin2bcd(date[4])); // min
		spi8(bin2bcd(date[3])); // hour
		spi8(bin2bcd(date[2])); // date
		spi8(bin2bcd(date[1])); // month
		spi8(bin2bcd(date[0]-100)); // year
		spi8(bin2bcd(date[6])-1); //day 1-7 -> 0-6
		spi8(0x40); // flag
		DisableIO();
	}
}

uint32_t user_io_get_core_features() {
	return core_features;
}

static void user_io_read_core_features() {
	core_features = 0;

	spi_uio_cmd_cont(UIO_GET_FEATS);
	if (spi_in() == 0x80) {
		core_features = spi_in();
		core_features = (core_features<<8) | spi_in();
		core_features = (core_features<<8) | spi_in();
		core_features = (core_features<<8) | spi_in();
	}
	DisableIO();
}

void user_io_detect_core_type() {
	core_name[0] = 0;

	EnableIO();
	core_type = SPI(0xff);
	DisableIO();
#ifdef SD_NO_DIRECT_MODE
	rom_direct_upload = 0;
#else
	rom_direct_upload = (core_type & 0x10) >> 4; // bit 4 - direct upload support
#endif
	core_type &= 0xef;

	switch(core_type) {
	case CORE_TYPE_DUMB:
		puts("Identified core without user interface");
		break;

	case CORE_TYPE_MINIMIG:
		strcpy(core_name, "MINIMIG");
		puts("Identified Minimig V1 core");
		break;

	case CORE_TYPE_MINIMIG2:
		strcpy(core_name, "MINIMIG");
		puts("Identified Minimig V2 core");
		break;

	case CORE_TYPE_PACE:
		puts("Identified PACE core");
		break;

	case CORE_TYPE_MIST:
	case CORE_TYPE_MIST2:
		strcpy(core_name, "ST");
		puts("Identified MiST core");
		break;

	case CORE_TYPE_ARCHIE:
		puts("Identified Archimedes core");
		strcpy(core_name, "ARCHIE");
		archie_init();
		break;

	case CORE_TYPE_8BIT:
		puts("Identified 8BIT core");

		// send core variant first to allow the FPGA choosing the config string
		user_io_send_core_mod();

		// forward SD card config to core in case it uses the local
		// SD card implementation
		user_io_sd_set_config();

		// check if core has a config string
		core_type_8bit_with_config_string = (user_io_8bit_get_string(0) != NULL);

		// set core name. This currently only sets a name for the 8 bit cores
		user_io_read_core_name();

		// get requested features
		user_io_read_core_features();
		break;

	default:
		iprintf("Unable to identify core (%x)!\n", core_type);
		core_type = CORE_TYPE_UNKNOWN;
	}
}

void user_io_init_core() {
	if(core_type == CORE_TYPE_8BIT) {

		// send a reset
		user_io_8bit_set_status(UIO_STATUS_RESET, ~0);

		FIL file;
		UINT br;
		// try to load config

		if(!user_io_create_config_name(s, "CFG", CONFIG_ROOT)) {
			iprintf("Loading config %s\n", s);

			if (f_open(&file, s, FA_READ) == FR_OK)  {
				iprintf("Found config\n");
				if(f_size(&file) <= 8) {
					((unsigned long long*)sector_buffer)[0] = 0;
					f_read(&file, sector_buffer, f_size(&file), &br);
					user_io_8bit_set_status(((unsigned long long*)sector_buffer)[0], ~1);
				} else {
					settings_load(false);
				}
				f_close(&file);
			} else {
				user_io_8bit_set_status(arc_get_default(), ~1);
			}
		}

		// check if there's a <core>.rom or <core>.r0[1-6] present, send it via index 0-6
		for (int i = 0; i < 7; i++) {
			char ext[4];
			if (!i) {
				strcpy(ext, "ROM");
			} else {
				strcpy(ext, "R01");
				ext[2] = '0'+i;
			}
			for (char root = 0; root <= 1; root++) {
				if (!user_io_create_config_name(s, ext, root)) {
					iprintf("Looking for %s\n", s);
					if (f_open(&file, s, FA_READ) == FR_OK) {
						data_io_file_tx(&file, i, ext);
						f_close(&file);
						break;
					}
				}
			}
		}

		if(!user_io_create_config_name(s, "RAM", CONFIG_ROOT)) {
			iprintf("Looking for %s\n", s);
			// check if there's a <core>.ram present, send it via index -1
			if (f_open(&file, s, FA_READ) == FR_OK) {
				data_io_file_tx(&file, -1, "RAM");
				f_close(&file);
			}
		}
		for (int i = 0; i < SD_IMAGES; i++) {
			hardfile[i] = &hardfiles[i];
			if ((core_features & (FEAT_IDE0 << (2*i))) == (FEAT_IDE0_CDROM << (2*i))) {
				iprintf("IDE %d: ATAPI CDROM\n", i);
				hardfiles[i].enabled = HDF_CDROM;
				OpenHardfile(i, false);
			}
		}

		// check if there's a <core>.vhd present
		if(!user_io_create_config_name(s, "VHD", CONFIG_ROOT | CONFIG_VHD)) {
			iprintf("Looking for %s\n", s);
			if (!(core_features & FEAT_IDE0))
				 user_io_file_mount(s, 0);

			if (!user_io_is_mounted(0)) {
				// check for <core>.HD0/1 files
				if(!user_io_create_config_name(s, "HD0", CONFIG_ROOT | CONFIG_VHD)) {
					for (int i = 0; i < SD_IMAGES; i++) {
						s[strlen(s)-1] = '0'+i;
						iprintf("Looking for %s\n", s);
						if ((core_features & (FEAT_IDE0 << (2*i))) == (FEAT_IDE0_ATA << (2*i))) {
							iprintf("IDE %d: ATA Hard Disk\n", i);
							hardfiles[i].enabled = HDF_FILE;
							strncpy(hardfiles[i].name, s, sizeof(hardfiles[0].name));
							hardfiles[i].name[sizeof(hardfiles[0].name)-1] = 0;
							OpenHardfile(i, false);
						} else {
							user_io_file_mount(s, i);
						}
					}
				}
			}
		}
		if (core_features & FEAT_IDE_MASK)
			SendHDFCfg();

		// release reset
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
	}
}

static unsigned short usb2amiga( unsigned  char k ) {
	//  replace MENU key by RGUI to allow using Right Amiga on reduced keyboards
	// (it also disables the use of Menu for OSD)
	if (mist_cfg.key_menu_as_rgui && k==0x65) {
		return 0x67;
	}
	return usb2ami[k];
}

static unsigned short usb2ps2code( unsigned char k) {
	//  replace MENU key by RGUI e.g. to allow using RGUI on reduced keyboards without physical key
	// (it also disables the use of Menu for OSD)
	if (mist_cfg.key_menu_as_rgui && k==0x65) {
		return EXT | 0x27;
	}
	return (ps2_kbd_scan_set == 1) ? usb2ps2_set1[k] : usb2ps2[k];
}

void user_io_analog_joystick(unsigned char joystick, char valueX, char valueY, char valueX2, char valueY2) {
	if(osd_is_visible) return;

	if(core_type == CORE_TYPE_8BIT || core_type == CORE_TYPE_MINIMIG2) {
		int16_t valueXX = valueX*mist_cfg.joystick_analog_mult/128 + mist_cfg.joystick_analog_offset;
		int16_t valueYY = valueY*mist_cfg.joystick_analog_mult/128 + mist_cfg.joystick_analog_offset;
		int16_t valueXX2 = valueX2*mist_cfg.joystick_analog_mult/128 + mist_cfg.joystick_analog_offset;
		int16_t valueYY2 = valueY2*mist_cfg.joystick_analog_mult/128 + mist_cfg.joystick_analog_offset;
		//iprintf("analog: x=%d, y=%d, xx=%d, yy=%d, mult=%d, offs=%d\n", valueX, valueY, valueXX, valueYY, mist_cfg.joystick_analog_mult, mist_cfg.joystick_analog_offset);
		spi_uio_cmd8_cont(UIO_ASTICK, joystick);
		spi8(valueXX);
		spi8(valueYY);
		spi8(valueXX2);
		spi8(valueYY2);
		DisableIO();
	}
}

void user_io_digital_joystick(unsigned char joystick, unsigned char map) {
	uint8_t state = map;
	// "only" 6 joysticks are supported
	if(joystick > 5)
		return;
	// if osd is open, control it via joystick
	if(osd_is_visible)
		return;

	//iprintf("j%d: %x\n", joystick, map);

	// atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
	// but only for joystick 1 and 2
	if((core_type == CORE_TYPE_MIST) && (joystick < 2)) {
		ikbd_joystick(joystick, map);
		return;
	}

	// every other core else uses this
	// (even MIST, joystick 3 and 4 were introduced later)
	spi_uio_cmd8((joystick < 2)?(UIO_JOYSTICK0 + joystick):((UIO_JOYSTICK2 + joystick - 2)), map);
}

void user_io_digital_joystick_ext(unsigned char joystick, uint32_t map) {
	// "only" 6 joysticks are supported
	if(joystick > 5) return;
	if(osd_is_visible) return;
	//iprintf("ext j%d: %x\n", joystick, map);
	spi_uio_cmd32(UIO_JOYSTICK0_EXT + joystick, 0x000fffff & map);
	if (autofire && (map & 0x30)) {
		autofire_mask = map & 0x30;
		autofire_map = (autofire_map & autofire_mask) | (map & ~autofire_mask);
		if (autofire_joy != joystick) {
			autofire_joy = joystick;
			autofire_timer = GetTimer(autofire*50);
		}
	} else {
		autofire_joy = -1;
	}
}

static char dig2ana(char min, char max) {
	if(min && !max) return -128;
	if(max && !min) return  127;
	return 0;
}

void user_io_joystick(unsigned char joystick, unsigned char map) {
  // digital joysticks also send analog signals
	user_io_digital_joystick(joystick, map);
	user_io_digital_joystick_ext(joystick, map);
	user_io_analog_joystick(joystick, 
		       dig2ana(map&JOY_LEFT, map&JOY_RIGHT),
		       dig2ana(map&JOY_UP, map&JOY_DOWN),
		       0 ,0);
}

// transmit serial/rs232 data into core
void user_io_serial_tx(char *chr, uint16_t cnt) {
	if (core_type == CORE_TYPE_MIST)
		spi_uio_cmd_cont(UIO_SERIAL_OUT);
	else
		spi_uio_cmd_cont(UIO_SIO_OUT);
	while(cnt--) spi8(*chr++);
	DisableIO();
}

char user_io_serial_status(serial_status_t *status_in, uint8_t status_out) {
	uint8_t i, *p = (uint8_t*)status_in;

	spi_uio_cmd_cont(UIO_SERIAL_STAT);

	// first byte returned by core must be "magic". otherwise the
	// core doesn't support this request
	if(SPI(status_out) != 0xa5) {
		DisableIO();
		return 0;
	}

	// read the whole structure
	for(i=0;i<sizeof(serial_status_t);i++)
		*p++ = spi_in();

	DisableIO();
	return 1;
}

// transmit midi data into core
void user_io_midi_tx(char chr) {
	spi_uio_cmd8(UIO_MIDI_OUT, chr);
}

// send ethernet mac address into FPGA
void user_io_eth_send_mac(uint8_t *mac) {
	uint8_t i;

	spi_uio_cmd_cont(UIO_ETH_MAC);
	for(i=0;i<6;i++) spi8(*mac++);
	DisableIO();
}

// set SD card info in FPGA (CSD, CID)
void user_io_sd_set_config(void) {
	unsigned char data[33];

	// get CSD and CID from SD card
	if (fat_uses_mmc()) {
		MMC_GetCID(data);
		MMC_GetCSD(data+16);
		// byte 32 is a generic config byte
		data[32] = MMC_IsSDHC()?1:0;
	} else {
		// synthetic CSD for non-MMC storage
		uint32_t capacity;
		disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &capacity);
		memset(data, sizeof(data), 0);
		data[16+0] = 0x40;
		data[16+1] = 0x0e;
		data[16+3] = 0x32;
		data[16+4] = 0x5b;
		data[16+5] = 0x59;
		data[16+6] = 0x90;
		data[16+7] = (capacity >> 26) & 0xff;
		data[16+8] = (capacity >> 18) & 0xff;
		data[16+9] = (capacity >> 10) & 0xff;
		data[16+10] = 0x5f;
		data[16+11] = 0xc0;
		data[32] = 1; // SDHC
	}

	// and forward it to the FPGA
	spi_uio_cmd_cont(UIO_SET_SDCONF);
	spi_write(data, sizeof(data));
	DisableIO();

	//  hexdump(data, sizeof(data), 0);
}

static void user_io_sd_ack(char drive_index) {
	spi_uio_cmd_cont(UIO_SD_ACK);
	spi8(drive_index);
	DisableIO();
}

// read 8+32 bit sd card status word from FPGA
uint8_t user_io_sd_get_status(uint32_t *lba, uint8_t *drive_index) {
	uint32_t s;
	uint8_t c; 

	*drive_index = 0;
	spi_uio_cmd_cont(UIO_GET_SDSTAT);
	c = spi_in();
	if ((c & 0xf0) == 0x60) *drive_index = spi_in() & 0x03;
	s = spi_in();
	s = (s<<8) | spi_in();
	s = (s<<8) | spi_in();
	s = (s<<8) | spi_in();
	DisableIO();

	if(lba) *lba = s;

	return c;
}

// read 8 bit keyboard LEDs status from FPGA
static uint8_t user_io_kbdled_get_status(void) {
	uint8_t c;

	spi_uio_cmd_cont(UIO_GET_KBD_LED);
	c = spi_in();
	DisableIO();

	return c;
}

// read 32 bit ethernet status word from FPGA
uint32_t user_io_eth_get_status(void) {
	uint32_t s;

	spi_uio_cmd_cont(UIO_ETH_STATUS);
	s = spi_in();
	s = (s<<8) | spi_in();
	s = (s<<8) | spi_in();
	s = (s<<8) | spi_in();
	DisableIO();

	return s;
}

// read ethernet frame from FPGAs ethernet tx buffer
void user_io_eth_receive_tx_frame(uint8_t *d, uint16_t len) {
	spi_uio_cmd_cont(UIO_ETH_FRM_IN);
	while(len--) *d++=spi_in();
	DisableIO();
}

// write ethernet frame to FPGAs rx buffer
void user_io_eth_send_rx_frame(uint8_t *s, uint16_t len) {
	spi_uio_cmd_cont(UIO_ETH_FRM_OUT);
	spi_write(s, len);
	spi8(0);     // one additional byte to allow fpga to store the previous one
	DisableIO();
}

// the physical joysticks (db9 ports at the right device side)
// as well as the joystick emulation are renumbered if usb joysticks
// are present in the system. The USB joystick(s) replace joystick 1
// and 0 and the physical joysticks are "shifted up". 
//
// Since the primary joystick is in port 1 the first usb joystick 
// becomes joystick 1 and only the second one becomes joystick 0
// (mouse port)

static uint8_t joystick_renumber(uint8_t j) {
	uint8_t usb_sticks = joystick_count();

	// no usb sticks present: no changes are being made
	if(!usb_sticks) return j;

	// Keep DB9 joysticks as joystick 0 and joystick 1
	// USB joysticks will be 2,3,...
	if(mist_cfg.joystick_db9_fixed_index) return j;

	if(j == 0) {
		// if usb joysticks are present, then physical joystick 0 (mouse port)
		// becomes becomes 2,3,...
		j = mist_cfg.joystick0_prefer_db9 ? 0 : usb_sticks + 1;
	} else {
		// if one usb joystick is present, then physical joystick 1 (joystick port)
		// becomes physical joystick 0 (mouse) port. If more than 1 usb joystick
		// is present it becomes 2,3,...
		if(usb_sticks == 1) j = 0;
		else                j = usb_sticks;
	}

	return j;
}

static void user_io_joystick_emu() {
	// iprintf("joystick_emu_fixed_index: %d\n", mist_cfg.joystick_emu_fixed_index);
	// joystick emulation also follows renumbering if requested (default)
	if(emu_mode == EMU_JOY0) user_io_joystick(mist_cfg.joystick_emu_fixed_index ? 0 : joystick_renumber(0), emu_state);
	if(emu_mode == EMU_JOY1) user_io_joystick(mist_cfg.joystick_emu_fixed_index ? 1 : joystick_renumber(1), emu_state);
}

// 16 byte fifo for amiga key codes to limit max key rate sent into the core
#define KBD_FIFO_SIZE  16   // must be power of 2
static unsigned short kbd_fifo[KBD_FIFO_SIZE];
static unsigned char kbd_fifo_r=0, kbd_fifo_w=0;
static long kbd_timer = 0;

static void kbd_fifo_minimig_send(unsigned short code) {
	spi_uio_cmd8((code&OSD)?UIO_KBD_OSD:UIO_KEYBOARD, code & 0xff);
	kbd_timer = GetTimer(10);  // next key after 10ms earliest
}

static void kbd_fifo_enqueue(unsigned short code) {
	// if fifo full just drop the value. This should never happen
	if(((kbd_fifo_w+1)&(KBD_FIFO_SIZE-1)) == kbd_fifo_r)
		return;

	// store in queue
	kbd_fifo[kbd_fifo_w] = code;
	kbd_fifo_w = (kbd_fifo_w + 1)&(KBD_FIFO_SIZE-1);
}

// send pending bytes if timer has run up
static void kbd_fifo_poll() {
	// timer enabled and running?
	if(kbd_timer && !CheckTimer(kbd_timer))
		return;

	kbd_timer = 0;  // timer == 0 means timer is not running anymore

	if(kbd_fifo_w == kbd_fifo_r)
		return;

	kbd_fifo_minimig_send(kbd_fifo[kbd_fifo_r]);
	kbd_fifo_r = (kbd_fifo_r + 1)&(KBD_FIFO_SIZE-1);
}


char user_io_is_cue_mounted() {
	return toc.valid;
}

char user_io_cue_mount(const unsigned char *name, unsigned char index) {
	char res = CUE_RES_OK;
	toc.valid = 0;
	if (name) {
		res = cue_parse(name, &sd_image[index]);
	}

	// send mounted image size first then notify about mounting
	EnableIO();
	SPI(UIO_SET_SDINFO);
	// use LE version, so following BYTE(s) may be used for size extension in the future.
	spi32le(toc.valid ? f_size(&toc.file->file) : 0);
	spi32le(toc.valid ? f_size(&toc.file->file) >> 32 : 0);
	spi32le(0); // reserved for future expansion
	spi32le(0); // reserved for future expansion
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, 1);
	return res;
}

static inline char sd_index(unsigned char index) {
	unsigned char retval = index;
	if (core_type == CORE_TYPE_ARCHIE)
		return (index + 2);
	else
		return index;
}

char user_io_is_mounted(unsigned char index) {
	return sd_image[sd_index(index)].valid;
}

void user_io_file_mount(const unsigned char *name, unsigned char index) {
	FRESULT res;

	buffer_lba = 0xffffffff; // invalidate cache
	if (name) {
		if (sd_image[sd_index(index)].valid)
			f_close(&sd_image[sd_index(index)].file);

		res = IDXOpen(&sd_image[sd_index(index)], name, FA_READ | FA_WRITE);
		if (res != FR_OK) res = IDXOpen(&sd_image[sd_index(index)], name, FA_READ);
		if (res == FR_OK) {
			iprintf("selected %llu bytes to slot %d\n", f_size(&sd_image[sd_index(index)].file), index);

			sd_image[sd_index(index)].valid = 1;
			// build index for fast random access
			IDXIndex(&sd_image[sd_index(index)]);
		} else {
			iprintf("error mounting %s (%d)\n", name, res);
			return;
		}
	} else {
		iprintf("unmounting file in slot %d\n", index);
		if (sd_image[sd_index(index)].valid) f_close(&sd_image[sd_index(index)].file);
		sd_image[sd_index(index)].valid = 0;
		if (!index) umounted = 1;
	}

	// send mounted image size first then notify about mounting
	EnableIO();
	SPI(UIO_SET_SDINFO);
	// use LE version, so following BYTE(s) may be used for size extension in the future.
	spi32le(sd_image[sd_index(index)].valid ? f_size(&sd_image[sd_index(index)].file) : 0);
	spi32le(sd_image[sd_index(index)].valid ? f_size(&sd_image[sd_index(index)].file) >> 32: 0);
	spi32le(0); // reserved for future expansion
	spi32le(0); // reserved for future expansion
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, index);
}

// 8 bit cores have a config string telling the firmware how
// to treat it
char *user_io_8bit_get_string(char index) {
	unsigned char i, lidx = 0, j = 0, d = 0, arc = 0;
	int arc_ptr = 0;
	char dip[3];
	static char buffer[128+1];  // max 128 bytes per config item

	// clear buffer
	buffer[0] = 0;

	spi_uio_cmd_cont(UIO_GET_STRING);
	i = spi_in();
	// the first char returned will be 0xff if the core doesn't support
	// config strings. atari 800 returns 0xa4 which is the status byte
	if((i == 0xff) || (i == 0xa4)) {
		DisableIO();
		return NULL;
	}

	//  iprintf("String: ");

	while ((i != 0) && (i!=0xff) && (j<sizeof(buffer))) {
		if(i == ';') {
			if(!arc && d==3 && !strncmp(dip, "DIP", 3)) {
				// found "DIP", continue with config snippet from ARC
				if(lidx == index) {
					// skip the DIP line
					j = 0;
					buffer[0] = 0;
				}
				arc = 1;
			} else {
				if(lidx == index) buffer[j++] = 0;
				lidx++;
			}
			d = 0;
		} else {
			if(lidx == index)
				buffer[j++] = i;
			if (d<3)
				dip[d++] = i;
		}

		//iprintf("%c", i);
		if (arc) {
			i = arc_get_conf()[arc_ptr++];
			if (!i) arc = 0;
		}
		if (!arc)
			i = spi_in();
	}

	DisableIO();
	//  iprintf("\n");

	// if this was the last string in the config string list, then it still
	// needs to be terminated
	if(lidx == index)
		buffer[j] = 0;

	// also return NULL for empty strings
	if(!buffer[0])
		return NULL;

	return buffer;
}

unsigned long long user_io_8bit_set_status(unsigned long long new_status, unsigned long long mask) {
	static unsigned long long status = 0;

	// if mask is 0 just return the current status 
	if(mask) {
		// keep everything not masked
		status &= ~mask;
		// updated masked bits
		status |= new_status & mask;

		spi_uio_cmd8(UIO_SET_STATUS, status);
		spi_uio_cmd64(UIO_SET_STATUS2, status);
	}

	return status;
}

char kbd_reset = 0;

void user_io_send_buttons(char force) {
	static unsigned char key_map = 0;

	// frequently poll the adc the switches 
	// and buttons are connected to
	PollADC();

	unsigned char map = 0;
	if(Buttons() & 1) map |= SWITCH2;
	if(Buttons() & 2) map |= SWITCH1;

	if(Buttons() & 4) map |= BUTTON1;
	else if(Buttons() & 8) map |= BUTTON2;
	if(kbd_reset)     map |= BUTTON2;

	if(!mist_cfg.keep_video_mode) VIDEO_ALTERED_VAR = 0;

	if(VIDEO_ALTERED_VAR & 1)
	{
		if(VIDEO_SD_DISABLE_VAR) map |= CONF_SCANDOUBLER_DISABLE;
	}
	else
	{
		if(mist_cfg.scandoubler_disable) map |= CONF_SCANDOUBLER_DISABLE;
	}

	if(VIDEO_ALTERED_VAR & 2)
	{
		if(VIDEO_YPBPR_VAR) map |= CONF_YPBPR;
	}
	else
	{
		if(mist_cfg.ypbpr) map |= CONF_YPBPR;
	}
	if(mist_cfg.csync_disable) map |= CONF_CSYNC_DISABLE;

	if(mist_cfg.sdram64) map |= CONF_SDRAM64;

	if((map != key_map) || force) {
		key_map = map;
		spi_uio_cmd8(UIO_BUT_SW, map);
		iprintf("sending keymap\n");
	}
}

static void set_kbd_led(unsigned char led, bool on)
{
	if(led & HID_LED_CAPS_LOCK)
	{
		if(!(keyboard_leds & KBD_LED_CAPS_CONTROL)) hid_set_kbd_led(led, on);
		caps_status = on;
	}

	if(led & HID_LED_NUM_LOCK)
	{
		if(!(keyboard_leds & KBD_LED_NUM_CONTROL)) hid_set_kbd_led(led, on);
		num_status = on;
	}

	if(led & HID_LED_SCROLL_LOCK)
	{
		if(!(keyboard_leds & KBD_LED_SCRL_CONTROL)) hid_set_kbd_led(led, on);
		scrl_status = on;
	}
}

static void handle_ps2_kbd_commands()
{
	unsigned char c, cmd;
	spi_uio_cmd_cont(UIO_KEYBOARD_IN);
	c = spi_in();
	cmd = spi_in();
	DisableIO();
	if (c == UIO_KEYBOARD_IN) { // receiving echo of the command code shows the core supports this message
		iprintf("PS2 keyboard cmd: %02x\n", cmd);
		switch (ps2_kbd_state) {
			case PS2_KBD_IDLE:
				switch (cmd) {
					case 0xFF: // reset
						ps2_kbd_scan_set = 2;
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						spi_uio_cmd8(UIO_KEYBOARD, 0xAA); // BAT successful
						break;
					case 0xF2: // read ID
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						spi_uio_cmd8(UIO_KEYBOARD, 0xAB); // ID1
						spi_uio_cmd8(UIO_KEYBOARD, 0x83); // ID2
						break;
					case 0xF0: // scan get/set
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						ps2_kbd_state = PS2_KBD_SCAN_GETSET;
						break;
					case 0xF3: // typematic set
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						ps2_kbd_state = PS2_KBD_TYPEMATIC_SET;
						break;
					case 0xED: // set LEDs
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						ps2_kbd_state = PS2_KBD_LED_SET;
						break;
					case 0xEE: // echo
						spi_uio_cmd8(UIO_KEYBOARD, 0xEE); // ACK
						break;
					case 0xF4: // enable scanning
						// TODO: handle the message
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						break;
					case 0xF5: // disable scanning
						// TODO: handle the message
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						break;
					case 0xF6: // set default parameters
						ps2_kbd_scan_set = 2;
						spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
						break;
				}
				break;
			case PS2_KBD_SCAN_GETSET:
				if (cmd<=3) {
					spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
					if (!cmd) // get
						spi_uio_cmd8(UIO_KEYBOARD, ps2_kbd_scan_set);
					else // set
						ps2_kbd_scan_set = cmd;
					ps2_kbd_state = PS2_KBD_IDLE;
				} else {
					spi_uio_cmd8(UIO_KEYBOARD, 0xFE); // RESEND
				}
				break;
			case PS2_KBD_TYPEMATIC_SET:
				ps2_typematic_rate = cmd;
				spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
				ps2_kbd_state = PS2_KBD_IDLE;
				break;
			case PS2_KBD_LED_SET:
				// TODO: handle the message
				spi_uio_cmd8(UIO_KEYBOARD, 0xFA); // ACK
				ps2_kbd_state = PS2_KBD_IDLE;
				break;
		}
	}
}

static void send_keycode(unsigned short code);
static unsigned short keycode(unsigned char in);

// 1000/(2^(39-rate)^(1/8))
static int ps2_typematic_rates[] =
	{34, 37, 40, 44, 48, 52, 57, 62, 68, 74, 81, 88, 96, 105, 114, 125, 136,
	 148, 162, 176, 192, 210, 229, 250, 272, 297, 324, 353, 385, 420, 458, 500};

static void handle_ps2_typematic_repeat()
{
	if (ps2_typematic_rate & 0x80) return;
	if (ps2_kbd_state != PS2_KBD_IDLE) return;
	if (CheckTimer(ps2_typematic_timer)) {
		ps2_typematic_timer = GetTimer(ps2_typematic_rates[ps2_typematic_rate & 0x1f]);
		for (char i=5; i>=0; i--) {
			if (pressed[i]) {
				unsigned short code = keycode(pressed[i]);

				if (!osd_is_visible && !(code & CAPS_LOCK_TOGGLE)&& !(code & NUM_LOCK_TOGGLE)) {
					send_keycode(code);
				}
				break;
			}
		}
	}
}

static void handle_ps2_mouse_commands()
{
	unsigned char c, cmd;
	spi_uio_cmd_cont(UIO_MOUSE_IN);
	c = spi_in();
	cmd = spi_in();
	DisableIO();
	if (c == UIO_MOUSE_IN) { // receiving echo of the command code shows the core supports this message
		iprintf("PS2 mouse cmd: %02x\n", cmd);
		switch (ps2_mouse_state) {
			case PS2_MOUSE_IDLE:
				switch (cmd) {
					case 0xFF: // reset
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xAA); // BAT successful
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0);
						break;
					case 0xF6: // set defaults;
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						break;
					case 0xE6: // set mouse scaling to 1:1
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_status &= ~0x10;
						break;
					case 0xE7: // set mouse scaling to 1:2
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_status |= 0x10;
						break;
					case 0xE8: // set resolution
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_state = PS2_MOUSE_SETRESOLUTION;
						break;
					case 0xE9: // status request
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_status);
						spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_resolution);
						spi_uio_cmd8(UIO_MOUSE0_EXT, ps2_mouse_samplerate);
						break;
					case 0xF2: // get device ID
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0x00); // Normal PS2 mouse
						break;
					case 0xF4: // enable data reporting
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_status |= 0x20;
						break;
					case 0xF5: // disable data reporting
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_status &= ~0x20;
						break;
					case 0xF3: // set sample rate
						spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
						ps2_mouse_state = PS2_MOUSE_SETSAMPLERATE;
						break;
				}
				break;
			case PS2_MOUSE_SETRESOLUTION:
					spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
					ps2_mouse_resolution = cmd;
					ps2_mouse_state = PS2_MOUSE_IDLE;
					break;
			case PS2_MOUSE_SETSAMPLERATE:
					spi_uio_cmd8(UIO_MOUSE0_EXT, 0xFA); // ACK
					ps2_mouse_samplerate = cmd;
					ps2_mouse_state = PS2_MOUSE_IDLE;
					break;
		}
	}
}

void user_io_poll() {

	// check of core has changed from a good one to a not supported on
	// as this likely means that the user is reloading the core via jtag
	unsigned char ct;
	static unsigned char ct_cnt = 0;

	EnableIO();
	ct = SPI(0xff);
	DisableIO();
	SPI(0xff);      // needed for old minimig core

	if((ct&0xef) == core_type)
		ct_cnt = 0;        // same core type, everything is fine
	else {
		// core type has changed
		if(++ct_cnt == 255) {
			USB_LOAD_VAR = USB_LOAD_VALUE;
			// wait for a new valid core id to appear
			while((ct &  0xe0) != 0xa0) {
				EnableIO();
				ct = SPI(0xff);
				DisableIO();
				SPI(0xff);      // needed for old minimig core
			}

			// reset io controller to cope with new core
			MCUReset(); // restart
			for(;;);
		}
	}

	if((core_type != CORE_TYPE_MINIMIG) &&
	   (core_type != CORE_TYPE_MINIMIG2) &&
	   (core_type != CORE_TYPE_PACE) &&
	   (core_type != CORE_TYPE_MIST) &&
	   (core_type != CORE_TYPE_MIST2) &&
	   (core_type != CORE_TYPE_ARCHIE) &&
	   (core_type != CORE_TYPE_8BIT)) {
		return;  // no user io for the installed core
	}

	if((core_type == CORE_TYPE_MIST) ||
	   (core_type == CORE_TYPE_MIST2)) {
		char redirect = tos_get_cdc_control_redirect();

		if (core_type == CORE_TYPE_MIST) ikbd_poll();

		// check for input data on usart
		USART_Poll();

		unsigned char c = 0;

		// check for incoming serial data. this is directly forwarded to the
		// arm rs232 and mixes with debug output. Useful for debugging only of
		// e.g. the diagnostic cartridge    
		if(!pl2303_is_blocked()) {
			if (core_type == CORE_TYPE_MIST)
				spi_uio_cmd_cont(UIO_SERIAL_IN);
			else
				spi_uio_cmd_cont(UIO_SIO_IN);

			while(spi_in() && !pl2303_is_blocked()) {
				c = spi_in();

				// if a serial/usb adapter is connected it has precesence over
				// any other sink
				if(pl2303_present()) 
					pl2303_tx_byte(c);
				else {
					if(c != 0xff)
						putchar(c);

					// forward to USB if redirection via USB/CDC enabled
					if(redirect == CDC_REDIRECT_RS232)
						cdc_control_tx(c);
				}
			}
			DisableIO();
		}

		// check for incoming parallel/midi data
		if((redirect == CDC_REDIRECT_PARALLEL) || (redirect == CDC_REDIRECT_MIDI)) {
			spi_uio_cmd_cont((redirect == CDC_REDIRECT_PARALLEL)?UIO_PARALLEL_IN:UIO_MIDI_IN);
			// character 0xff is returned if FPGA isn't configured
			c = 0;
			while(spi_in() && (c!= 0xff)) {
				c = spi_in();
				cdc_control_tx(c);
			}
			DisableIO();
			// always flush when doing midi to reduce latencies
			if(redirect == CDC_REDIRECT_MIDI)
				cdc_control_flush();
		}
	}

	// poll db9 joysticks
	unsigned char joy_map = 0;

	if(GetDB9(0, &joy_map)) {

		joy_map = virtual_joystick_mapping(0x00db, 0x0000, joy_map);

		uint8_t idx = joystick_renumber(0);
		if (!user_io_osd_is_visible()) user_io_joystick(idx, joy_map);
		StateJoySet(joy_map, mist_cfg.joystick_db9_fixed_index ? idx : joystick_count()); // send to OSD
		virtual_joystick_keyboard(joy_map);
	}
	if(GetDB9(1, &joy_map)) {

		joy_map = virtual_joystick_mapping(0x00db, 0x0001, joy_map);

		uint8_t idx = joystick_renumber(1);
		if (!user_io_osd_is_visible()) user_io_joystick(idx, joy_map);
		StateJoySet(joy_map, mist_cfg.joystick_db9_fixed_index ? idx : joystick_count() + 1); // send to OSD
		virtual_joystick_keyboard(joy_map);
	}

	if (autofire && autofire_joy >= 0 && autofire_joy <= 5 && CheckTimer(autofire_timer)) {
		autofire_map ^= autofire_mask;
		//iprintf("06x\n", autofire_map);
		spi_uio_cmd32(UIO_JOYSTICK0_EXT + autofire_joy, 0x000fffff & autofire_map);
		autofire_timer = GetTimer(autofire*50);
	}

	user_io_send_buttons(0);

	// mouse movement emulation is continous 
	if(emu_mode == EMU_MOUSE) {
		if(CheckTimer(emu_timer)) {
			emu_timer = GetTimer(EMU_MOUSE_FREQ);

			if(emu_state & JOY_MOVE) {
				unsigned char b = 0;
				char x = 0, y = 0;
				if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_LEFT)  x = -1; 
				if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_RIGHT) x = +1; 
				if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_UP)    y = -1; 
				if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_DOWN)  y = +1; 

				if(emu_state & JOY_BTN1) b |= 1;
				if(emu_state & JOY_BTN2) b |= 2;

				user_io_mouse(0, b, x, y, 0);
			}
		}
	}

	if((core_type == CORE_TYPE_MINIMIG) ||
	   (core_type == CORE_TYPE_MINIMIG2)) {
		kbd_fifo_poll();

		// frequently check mouse for events
		if(CheckTimer(mouse_timer)) {
			mouse_timer = GetTimer(MOUSE_FREQ);

			// has ps2 mouse data been updated in the meantime
			for (char idx = 0; idx < 2; idx ++) {
				if(mouse_flags[idx] & 0x80) {
					char x, y, z;
					// ----- X axis -------
					if(mouse_pos[idx][X] < -128) {
						x = -128;
						mouse_pos[idx][X] += 128;
					} else if(mouse_pos[idx][X] > 127) {
						x = 127;
						mouse_pos[idx][X] -= 127;
					} else {
						x = mouse_pos[idx][X];
						mouse_pos[idx][X] = 0;
					}

					// ----- Y axis -------
					if(mouse_pos[idx][Y] < -128) {
						y = (-128);
						mouse_pos[idx][Y] += 128;
					} else if(mouse_pos[idx][Y] > 127) {
						y = 127;
						mouse_pos[idx][Y] -= 127;
					} else {
						y = mouse_pos[idx][Y];
						mouse_pos[idx][Y] = 0;
					}

					// ----- Z axis -------
					if(mouse_pos[idx][Z] < -128) {
						z = (-128);
						mouse_pos[idx][Z] += 128;
					} else if(mouse_pos[idx][Z] > 127) {
						z = 127;
						mouse_pos[idx][Z] -= 127;
					} else {
						z = mouse_pos[idx][Z];
						mouse_pos[idx][Z] = 0;
					}

					if (!idx) {
						// send the first mouse only with the old message
						spi_uio_cmd_cont(UIO_MOUSE);
						spi8(x);
						spi8(y);
						spi8(mouse_flags[idx] & 0x03);
						DisableIO();
					}

					spi_uio_cmd_cont(UIO_MOUSE0_EXT + idx);
					spi8(x);
					spi8(y);
					spi8(mouse_flags[idx] & 0x03);
					spi8(z);
					DisableIO();

					// reset flags
					mouse_flags[idx] = 0;
				}
			}
		}
	}

	if((core_type == CORE_TYPE_MIST) ||
	   (core_type == CORE_TYPE_MIST2)) {
		// do some tos specific monitoring here
		tos_poll();
	}

	// serial IO - TODO: merge with MiST2
	if(core_type == CORE_TYPE_8BIT) {
		unsigned char c = 1, f, p=0;

		// check for input data on usart
		USART_Poll(); // TODO: currently doesn't send anything for 8BIT

		// check for serial data to be sent

		// check for incoming serial data. this is directly forwarded to the
		// arm rs232 and mixes with debug output.
		spi_uio_cmd_cont(UIO_SIO_IN);
		// status byte is 1000000A with A=1 if data is available
		if((f = spi_in(0)) == 0x81) {
			iprintf("\033[1;36m");

			// character 0xff is returned if FPGA isn't configured
			while((f == 0x81) && (c!= 0xff) && (c != 0x00) && (p < 8)) {
				c = spi_in();
				if(c != 0xff && c != 0x00) 
					iprintf("%c", c);

				f = spi_in();
				p++;
			}
			iprintf("\033[0m");
		}
		DisableIO();
	}

	if((core_type == CORE_TYPE_8BIT) && (!strcmp(user_io_get_core_name(), "TGFX16") || (core_features & FEAT_PCECD)))
		pcecd_poll();

	// sd card emulation
	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2) ||
	   (core_type == CORE_TYPE_ARCHIE))
	{
		uint32_t lba;
		uint8_t drive_index;
		uint8_t c = user_io_sd_get_status(&lba, &drive_index);

		// valid sd commands start with "5x" (old API), or "6x" (new API)
		// to avoid problems with cores that don't implement this command
		if((c & 0xf0) == 0x50 || (c & 0xf0) == 0x60) {

#if 0
			// debug: If the io controller reports and non-sdhc card, then
			// the core should never set the sdhc flag
			if((c & 3) && !MMC_IsSDHC() && (c & 0x04))
				iprintf("WARNING: SDHC access to non-sdhc card\n");
#endif

			// check if core requests configuration
			if(c & 0x08) {
				iprintf("core requests SD config\n");
				user_io_sd_set_config();
			}

			// check if system is trying to access a sdhc card from 
			// a sd/mmc setup

			// check if an SDHC card is inserted
			if(MMC_IsSDHC()) {
				static char using_sdhc = 1;

				// SD request and 
				if(c & 0x03){
					if (!(c & 0x04)) {
						if(using_sdhc) {
							// we have not been using sdhc so far? 
							// -> complain!
							ErrorMessage(" This core does not support\n"
								" SDHC cards. Using them may\n"
								" lead to data corruption.\n\n"
								" Please use an SD card <2GB!", 0);
							using_sdhc = 0;
						}
					} else
						// SDHC request from core is always ok
						using_sdhc = 1;
				}
			}

			// Write to file/SD Card
			if((c & 0x03) == 0x02) {
				// only write if the inserted card is not sdhc or
				// if the core uses sdhc
				if((!MMC_IsSDHC()) || (c & 0x04)) {
					uint8_t wr_buf[512];

					if(user_io_dip_switch1())
						iprintf("SD WR (%d) %d\n", drive_index, lba);

					// if we write the sector stored in the read buffer, then
					// invalidate the cache
					if(buffer_lba == lba && buffer_drive_index == drive_index) {
						buffer_lba = 0xffffffff;
					}
					user_io_sd_ack(drive_index);
					// Fetch sector data from FPGA ...
					spi_uio_cmd_cont(UIO_SECTOR_WR);
					spi_block_read(wr_buf);
					DisableIO();

					// ... and write it to disk
					DISKLED_ON;

#if 1
					if(sd_image[sd_index(drive_index)].valid) {
						if(((f_size(&sd_image[sd_index(drive_index)].file)-1) >> 9) >= lba) {
							IDXSeek(&sd_image[sd_index(drive_index)], lba);
							IDXWrite(&sd_image[sd_index(drive_index)], wr_buf);
						}
					} else if (!drive_index && !umounted)
						disk_write(fs.pdrv, wr_buf, lba, 1);
#else
					hexdump(wr_buf, 512, 0);
#endif

					DISKLED_OFF;
				}
			}

			// Read from file/SD Card
			if((c & 0x03) == 0x01) {

				if(user_io_dip_switch1())
					iprintf("SD RD (%d) %d\n", drive_index, lba);

				// invalidate cache if it stores data from another drive
				if (drive_index != buffer_drive_index)
					buffer_lba = 0xffffffff;

				// are we using a file as the sd card image?
				// (C64 floppy does that ...)
				if(buffer_lba != lba) {
					DISKLED_ON;
					if(sd_image[sd_index(drive_index)].valid) {
						if(((f_size(&sd_image[sd_index(drive_index)].file)-1) >> 9) >= lba) {
							IDXSeek(&sd_image[sd_index(drive_index)], lba);
							IDXRead(&sd_image[sd_index(drive_index)], buffer);
						}
					} else if (!drive_index && !umounted) {
						// sector read
						// read sector from sd card if it is not already present in
						// the buffer
						disk_read(fs.pdrv, buffer, lba, 1);
					}
					buffer_lba = lba;
					DISKLED_OFF;
				}

				if(buffer_lba == lba) {
					// hexdump(buffer, 32, 0);
					user_io_sd_ack(drive_index);
					// data is now stored in buffer. send it to fpga
					spi_uio_cmd_cont(UIO_SECTOR_RD);
					spi_block_write(buffer);
					DisableIO();

					// the end of this transfer acknowledges the FPGA internal
					// sd card emulation
				}

				// just load the next sector now, so it may be prefetched
				// for the next request already
				DISKLED_ON;
				if(sd_image[sd_index(drive_index)].valid) {
					// but check if it would overrun on the file
					if(((f_size(&sd_image[sd_index(drive_index)].file)-1) >> 9) > lba) {
						IDXSeek(&sd_image[sd_index(drive_index)], lba+1);
						IDXRead(&sd_image[sd_index(drive_index)], buffer);
						buffer_lba = lba + 1;
					}
				} else {
					// sector read
					// read sector from sd card if it is not already present in
					// the buffer
					disk_read(fs.pdrv, buffer, lba+1, 1);
					buffer_lba = lba+1;
				}
				buffer_drive_index = drive_index;
				DISKLED_OFF;
			}
		}
	}

	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2)) {

		// frequently check ps2 mouse for events
		if(CheckTimer(mouse_timer)) {
			mouse_timer = GetTimer(MOUSE_FREQ);

			for (char idx=0; idx<2; idx++) {
				// has ps2 mouse data been updated in the meantime
				if(mouse_flags[idx] & 0x08) {
					unsigned char ps2_mouse[4];

					// PS2 format:
					// YOvfl, XOvfl, dy8, dx8, 1, mbtn, rbtn, lbtn
					// dx[7:0]
					// dy[7:0]
					// 0,0,btn5,btn,dz[3:0]
					ps2_mouse[0] = mouse_flags[idx];

					// ------ X axis -----------
					// store sign bit in first byte
					ps2_mouse[0] |= (mouse_pos[idx][X] < 0)?0x10:0x00;
					if(mouse_pos[idx][X] < -255) {
						// min possible value + overflow flag
						ps2_mouse[0] |= 0x40;
						ps2_mouse[1] = -128;
					} else if(mouse_pos[idx][X] > 255) {
						// max possible value + overflow flag
						ps2_mouse[0] |= 0x40;
						ps2_mouse[1] = 255;
					} else
						ps2_mouse[1] = mouse_pos[idx][X];

					// ------ Y axis -----------
					// store sign bit in first byte
					ps2_mouse[0] |= (mouse_pos[idx][Y] < 0)?0x20:0x00;
					if(mouse_pos[idx][Y] < -255) {
						// min possible value + overflow flag
						ps2_mouse[0] |= 0x80;
						ps2_mouse[2] = -128;
					} else if(mouse_pos[idx][Y] > 255) {
						// max possible value + overflow flag
						ps2_mouse[0] |= 0x80;
						ps2_mouse[2] = 255;
					} else
						ps2_mouse[2] = mouse_pos[idx][Y];

					// ------ Z axis -----------
					ps2_mouse[3] = 0;
					if(mouse_pos[idx][Z] < -8) {
						// min possible value
						ps2_mouse[3] = -8;
					} else if(mouse_pos[idx][Z] > 7) {
						// max possible value
						ps2_mouse[3] = 7;
					} else
						ps2_mouse[3] = mouse_pos[idx][Z];

					// collect movement info and send at predefined rate
					if(!(ps2_mouse[0]==0x08 && ps2_mouse[1]==0 && ps2_mouse[2]==0 && ps2_mouse[3]==0) && user_io_dip_switch1())
						iprintf("PS2 MOUSE(%d): %x %d %d %d\n", idx, ps2_mouse[0], ps2_mouse[1], ps2_mouse[2], ps2_mouse[3]);

					// old message sends the movements for all mice
					spi_uio_cmd_cont(UIO_MOUSE);
					spi8(ps2_mouse[0]);
					spi8(ps2_mouse[1]);
					spi8(ps2_mouse[2]);
					DisableIO();

					// new message with Intellimouse PS2 message
					spi_uio_cmd_cont(UIO_MOUSE0_EXT+idx);
					spi8(ps2_mouse[0]);
					spi8(ps2_mouse[1]);
					spi8(ps2_mouse[2]);
					spi8(ps2_mouse[3]);
					DisableIO();

					// reset counters
					mouse_flags[idx] = 0;
					mouse_pos[idx][X] = mouse_pos[idx][Y] = mouse_pos[idx][Z] = 0;
				}
			}
		}
	}

	if(core_type == CORE_TYPE_8BIT)
	{
		handle_ps2_typematic_repeat();
		handle_ps2_kbd_commands();
		handle_ps2_mouse_commands();
	}

	if(core_type == CORE_TYPE_ARCHIE) 
		archie_poll();

	if(core_features & FEAT_IDE_MASK)
	{
		unsigned char  c1;

		EnableFpga();
		c1 = SPI(0); // cmd request
		SPI(0);
		SPI(0);
		SPI(0);
		SPI(0);
		SPI(0);
		DisableFpga();
		HandleHDD(c1, 0, 1);
	}

	if((core_type == CORE_TYPE_MINIMIG2) ||
	   (core_type == CORE_TYPE_MIST2) ||
	   (core_type == CORE_TYPE_ARCHIE) ||
	   (core_type == CORE_TYPE_8BIT))
	{
		if(CheckTimer(rtc_timer))
		{
			rtc_timer = GetTimer(RTC_FREQ);
			user_io_send_rtc();
		}
	}

	if(CheckTimer(led_timer))
	{
		led_timer = GetTimer(LED_FREQ);
		uint8_t leds = user_io_kbdled_get_status();
		if((leds & KBD_LED_FLAG_MASK) != KBD_LED_FLAG_STATUS) leds = 0;

		if((keyboard_leds & KBD_LED_CAPS_MASK) != (leds & KBD_LED_CAPS_MASK))
			hid_set_kbd_led(HID_LED_CAPS_LOCK, (leds & KBD_LED_CAPS_CONTROL) ? leds & KBD_LED_CAPS_STATUS : caps_status);

		if((keyboard_leds & KBD_LED_NUM_MASK) != (leds & KBD_LED_NUM_MASK))
			hid_set_kbd_led(HID_LED_NUM_LOCK, (leds & KBD_LED_NUM_CONTROL) ? leds & KBD_LED_NUM_STATUS : num_status);

		if((keyboard_leds & KBD_LED_SCRL_MASK) != (leds & KBD_LED_SCRL_MASK))
			hid_set_kbd_led(HID_LED_SCROLL_LOCK, (leds & KBD_LED_SCRL_CONTROL) ? leds & KBD_LED_SCRL_STATUS : scrl_status);

		keyboard_leds = leds;
	}

	// check for long press > 1 sec on menu button
	// and toggle scandoubler on/off then
	static unsigned long timer = 1;
	static unsigned char ypbpr_toggle = 0;
	if(MenuButton())
	{
		if(timer == 1) 
			timer = GetTimer(1000);
		else if(timer != 2)
		{
			if(CheckTimer(timer))
			{
				// toggle video mode bit
				mist_cfg.scandoubler_disable = !mist_cfg.scandoubler_disable;
				timer = 2;
	
				user_io_send_buttons(1);
				OsdDisableMenuButton(1);
				VIDEO_ALTERED_VAR |= 1;
				VIDEO_SD_DISABLE_VAR = mist_cfg.scandoubler_disable;
			}
		}

		if(UserButton())
		{
			if(!ypbpr_toggle)
			{
				// toggle video mode bit
				mist_cfg.ypbpr = !mist_cfg.ypbpr;
				timer = 2;
				ypbpr_toggle = 1;

				user_io_send_buttons(1);
				OsdDisableMenuButton(1);
				VIDEO_ALTERED_VAR |= 2;
				VIDEO_YPBPR_VAR = mist_cfg.ypbpr;
			}
		}
		else
		{
			ypbpr_toggle = 0;
		}

	}
	else
	{
		timer = 1;
		OsdDisableMenuButton(0);
		ypbpr_toggle = 0;
	}
}

char user_io_dip_switch1() {
	return(((Buttons() & 2)?1:0) || DEBUG_MODE);
}

static void send_keycode(unsigned short code) {
	if((core_type == CORE_TYPE_MINIMIG) ||
	   (core_type == CORE_TYPE_MINIMIG2)) {
		// amiga has "break" marker in msb
		if(code & BREAK) code = (code & 0xff) | 0x80;

		// send immediately if possible
		if(CheckTimer(kbd_timer) &&(kbd_fifo_w == kbd_fifo_r) )
			kbd_fifo_minimig_send(code);
		else
			kbd_fifo_enqueue(code);
	}

	if(core_type == CORE_TYPE_MIST) {
		// atari has "break" marker in msb
		ikbd_keyboard((code & BREAK) ? ((code & 0xff) | 0x80) : code);
	}

	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2)) {
		// send ps2 keycodes for those cores that prefer ps2
		spi_uio_cmd_cont(UIO_KEYBOARD);

		// "pause" has a complex code 
		if((code&0xff) == 0x77) {

			// pause does not have a break code
			if(!(code & BREAK)) {
				// Pause key sends E11477E1F014E077
				static const unsigned char c[] = { 
					0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77, 0x00 };
				const unsigned char *p = c;
				
				iprintf("PS2 KBD ");
				while(*p) {
					iprintf("%x ", *p);
					spi8(*p++);
				}
				iprintf("\n");
			}
		} else {
			if (user_io_dip_switch1()) {
				iprintf("PS2 KBD ");
				if(code & EXT)   iprintf("e0 ");
				if(code & BREAK) iprintf("f0 ");
				iprintf("%x\n", code & 0xff);
			}

			if(code & EXT)    // prepend extended code flag if required
				spi8(0xe0);
 
			if(code & BREAK)  // prepend break code if required
				if (ps2_kbd_scan_set == 1)
					code |= 0x80;
				else
					spi8(0xf0);

			spi8(code & 0xff);  // send code itself
		}

		DisableIO();
	}

	if(core_type == CORE_TYPE_ARCHIE) 
		archie_kbd(code);
}

void user_io_mouse(unsigned char idx, unsigned char b, char x, char y, char z) {

	// send mouse data as minimig expects it
	if((core_type == CORE_TYPE_MINIMIG) || 
	   (core_type == CORE_TYPE_MINIMIG2)) {
		mouse_pos[idx][X] += x;
		mouse_pos[idx][Y] += y;
		mouse_pos[idx][Z] += z;
		mouse_flags[idx] |= 0x80 | (b&3);
	}

	// 8 bit core expects ps2 like data
	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2)) {
		mouse_pos[idx][X] += x;
		mouse_pos[idx][Y] -= y;  // ps2 y axis is reversed over usb
		mouse_pos[idx][Z] += z;
		mouse_flags[idx] |= 0x08 | (b&7);
	}

	// send mouse data as mist expects it
	if(core_type == CORE_TYPE_MIST)
		ikbd_mouse(b, x, y);

	if(core_type == CORE_TYPE_ARCHIE) 
		archie_mouse(b, x, y);
}

// check if this is a key that's supposed to be suppressed
// when emulation is active
static unsigned char is_emu_key(unsigned char c, unsigned alt) {
	static const unsigned char m[] = { JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP };
	static const unsigned char m2[] = 
	{
		0x5A, JOY_DOWN,
		0x5C, JOY_LEFT,
		0x5D, JOY_DOWN,
		0x5E, JOY_RIGHT,
		0x60, JOY_UP,
		0x5F, JOY_BTN1,
		0x61, JOY_BTN2
	};

	if(emu_mode == EMU_NONE) return 0;

	if(alt)
	{
		for(int i=0; i<(sizeof(m2)/sizeof(m2[0])); i +=2) if(c == m2[i]) return m2[i+1];
	}
	else
	{
		// direction keys R/L/D/U
		if(c >= 0x4f && c <= 0x52) return m[c-0x4f];
	}

	return 0;
}  

/* usb modifer bits: 
      0     1     2    3    4     5     6    7
   LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
*/
#define EMU_BTN1  (0+(keyrah*4))  // left control
#define EMU_BTN2  (1+(keyrah*4))  // left shift
#define EMU_BTN3  (2+(keyrah*4))  // left alt
#define EMU_BTN4  (3+(keyrah*4))  // left gui (usually windows key)

static unsigned short keycode(unsigned char in) {
	if((core_type == CORE_TYPE_MINIMIG) ||
	   (core_type == CORE_TYPE_MINIMIG2)) 
	return usb2amiga(in);

	if(core_type == CORE_TYPE_MIST)
		return usb2atari[in];

	if(core_type == CORE_TYPE_ARCHIE)
		return usb2archie[in];

	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2))
		return usb2ps2code(in);

	return MISS;
}

static void check_reset(unsigned short modifiers, char useKeys)
{
	unsigned short combo[] =
	{
		0x45,  // lctrl+lalt+ralt
		0x89,  // lctrl+lgui+rgui
		0x105, // lctrl+lalt+del
	};

	if((modifiers & ~2)==combo[useKeys])
	{
		if(modifiers & 2) // with lshift - MiST reset
		{
			if(mist_cfg.keep_video_mode) VIDEO_KEEP_VAR = VIDEO_KEEP_VALUE;
			MCUReset(); // HW reset
			for(;;);
		}

		switch(core_type)
		{
			case CORE_TYPE_MINIMIG:
			case CORE_TYPE_MINIMIG2:
				OsdReset(RESET_NORMAL);
				break;

			case CORE_TYPE_8BIT:
				kbd_reset = 1;
				break;
		}
	}
	else
	{
		kbd_reset = 0;
	}
}

unsigned short modifier_keycode(unsigned char index) {
	/* usb modifer bits:
	        0     1     2    3    4     5     6    7
	      LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
	*/

	if((core_type == CORE_TYPE_MINIMIG) ||
	   (core_type == CORE_TYPE_MINIMIG2)) {
		static const unsigned short amiga_modifier[] = 
			{ 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };
		return amiga_modifier[index];
	}

	if(core_type == CORE_TYPE_MIST) {
		static const unsigned short atari_modifier[] = 
			{ 0x1d, 0x2a, 0x38, MISS, 0x1d, 0x36, 0x38, MISS };
		return atari_modifier[index];
	}

	if((core_type == CORE_TYPE_8BIT) ||
	   (core_type == CORE_TYPE_MIST2)) {
		static const unsigned short ps2_modifier[] = 
			{ 0x14, 0x12, 0x11, EXT|0x1f, EXT|0x14, 0x59, EXT|0x11, EXT|0x27 };
		static const unsigned short ps2_modifier_set1[] = 
			{ 0x1d, 0x2a, 0x38, MISS, EXT|0x1d, 0x36, EXT|0x38, MISS };
		return (ps2_kbd_scan_set == 1) ? ps2_modifier_set1[index] : ps2_modifier[index];
	}

	if(core_type == CORE_TYPE_ARCHIE) {
		static const unsigned short archie_modifier[] = 
			{ 0x36, 0x4c, 0x5e, MISS, 0x61, 0x58, 0x60, MISS };
		return archie_modifier[index];
	}

	return MISS;
}

void user_io_osd_key_enable(char on) {
	iprintf("OSD is now %s\n", on?"visible":"invisible");
	osd_is_visible = on;
}

static char key_used_by_osd(unsigned short s) {
	// this key is only used to open the OSD and has no keycode
	if((s & OSD_OPEN) && !(s & 0xff))  return true; 

	// no keys are suppressed if the OSD is inactive
	if(!osd_is_visible) return false;

	// in atari mode eat all keys if the OSD is online,
	// else none as it's up to the core to forward keys
	// to the OSD
	return((core_type == CORE_TYPE_MIST) ||
	       (core_type == CORE_TYPE_MIST2) ||
	       (core_type == CORE_TYPE_ARCHIE) ||
	       (core_type == CORE_TYPE_8BIT));
}

static char kr_fn_table[] =
{
	0x54, 0x48, // pause/break
	0x55, 0x46, // prnscr
	0x50, 0x4a, // home
	0x4f, 0x4d, // end
	0x52, 0x4b, // pgup
	0x51, 0x4e, // pgdown
	0x3a, 0x44, // f11
	0x3b, 0x45, // f12

	0x3c, 0x6c, // EMU_MOUSE
	0x3d, 0x6d, // EMU_JOY0
	0x3e, 0x6e, // EMU_JOY1
	0x3f, 0x6f, // EMU_NONE

	//Emulate keypad for A600 
	0x1E, 0x59, //KP1
	0x1F, 0x5A, //KP2
	0x20, 0x5B, //KP3
	0x21, 0x5C, //KP4
	0x22, 0x5D, //KP5
	0x23, 0x5E, //KP6
	0x24, 0x5F, //KP7
	0x25, 0x60, //KP8
	0x26, 0x61, //KP9
	0x27, 0x62, //KP0
	0x2D, 0x56, //KP-
	0x2E, 0x57, //KP+
	0x31, 0x55, //KP*
	0x2F, 0x68, //KP(
	0x30, 0x69, //KP)
	0x37, 0x63, //KP.
	0x28, 0x58  //KP Enter
};

static void keyrah_trans(unsigned char *m, unsigned char *k)
{
	static char keyrah_fn_state = 0;
	char fn = 0;
	char empty = 1;
	char rctrl = 0;
	int i = 0;
	while(i<6)
	{
		if((k[i] == 0x64) || (k[i] == 0x32))
		{
			if(k[i] == 0x64) fn = 1;
			if(k[i] == 0x32) rctrl = 1;
			for(int n = i; n<5; n++) k[n] = k[n+1];
			k[5] = 0;
		}
		else
		{
			if(k[i]) empty = 0;
			i++;
		}
	}
	
	if(fn)
	{
		for(i=0; i<6; i++)
		{
			for(int n = 0; n<(sizeof(kr_fn_table)/(2*sizeof(kr_fn_table[0]))); n++)
			{
				if(k[i] == kr_fn_table[n*2]) k[i] = kr_fn_table[(n*2)+1];
			}
		}
	}
	else
	{
		// free these keys for core usage
		for(i=0; i<6; i++)
		{
			if(k[i] == 0x53) k[i] = 0x68;
			if(k[i] == 0x47) k[i] = 0x69;
			if(k[i] == 0x49) k[i] = 0x6b; // workaround!
		}
	}

	*m = rctrl ? (*m) | 0x10 : (*m) & ~0x10;
	if(fn)
	{
		keyrah_fn_state |= 1;
		if(*m || !empty) keyrah_fn_state |= 2;
	}
	else
	{
		if(keyrah_fn_state == 1)
		{
			if((core_type == CORE_TYPE_MINIMIG) ||
				(core_type == CORE_TYPE_MINIMIG2))
			{
				send_keycode(KEY_MENU);
				send_keycode(BREAK | KEY_MENU);
			}
			else
			{
				OsdKeySet(KEY_MENU);
			}
		}
		keyrah_fn_state = 0;
	}
}

//Keyrah v2: USB\VID_18D8&PID_0002\A600/A1200_MULTIMEDIA_EXTENSION_VERSION
#define KEYRAH_ID (mist_cfg.keyrah_mode && (((((uint32_t)vid)<<16) | pid) == mist_cfg.keyrah_mode))

void user_io_kbd(unsigned char m, unsigned char *k, uint8_t priority, unsigned short vid, unsigned short pid)
{
	// ignore lower priority clears if higher priority key was pressed
	if(m==0 && (k[0] + k[1] + k[2] + k[3] + k[4] + k[5])==0)
	{
		if (priority > latest_keyb_priority) return;  // lower number = higher priority
	}
	latest_keyb_priority = priority; // set for next call

	char keyrah = KEYRAH_ID ? 1 : 0;
	if(emu_mode == EMU_MOUSE) keyrah <<= 1;

	if(keyrah) keyrah_trans(&m, k);

	if(mist_cfg.amiga_mod_keys) {
		//  bit  0     1      2    3    4     5      6    7
		//  key  LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
		//       1     2      4    8    10    20     40   80
		unsigned char m_in = m;
		// swap RALT/RGUI & LALT/LGUI
		m = ((m & 0x40) << 1) | ((m & 0x80) >> 1) | m & 0x20 | m & 0x10 | ((m & 0x8) >> 1) | ((m & 0x4) << 1) | m & 0x2;
		// swap CAPSLOCK/LCTRL
		for(char i=0;i<6;i++) {
			if(k[i] == 0x39) {
				m |= 0x1;
				k[i] = 0;
			}
			if(m_in & 0x1) {
				if(i<5) {
					if(k[i] == 0) {
						k[i] = 0x39;
						m_in &= ~0x1;
					}
				} else {
					k[i] = 0x39;
				}
			}
		}
	}

	unsigned short reset_m = m;
	for(char i=0;i<6;i++) if(k[i] == 0x4c) reset_m |= 0x100;
	check_reset(reset_m, KEYRAH_ID ? 1 : mist_cfg.reset_combo);

	if( (core_type == CORE_TYPE_MINIMIG) ||
		(core_type == CORE_TYPE_MINIMIG2) ||
		(core_type == CORE_TYPE_MIST) ||
		(core_type == CORE_TYPE_MIST2) ||
		(core_type == CORE_TYPE_ARCHIE) ||
		(core_type == CORE_TYPE_8BIT))
	{
		//iprintf("KBD: %d\n", m);
		//hexdump(k, 6, 0);

		char keycodes[6] = { 0,0,0,0,0,0 };
		uint16_t keycodes_ps2[6] = { 0,0,0,0,0,0 };
		char i, j;

		// remap keycodes if requested
		for(i=0;(i<6) && k[i];i++)
		{
			for(j=0;j<MAX_REMAP;j++)
			{
				if(key_remap_table[j][0] == k[i])
				{
					k[i] = key_remap_table[j][1];
					break;
				}
			}
		}

		// remap modifiers to each other if requested
		//  bit  0     1      2    3    4     5      6    7
		//  key  LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
		if(false)
		{ // (disabled until we configure it via INI)
			uint8_t default_mod_mapping [8] =
			{
				0x1,
				0x2,
				0x4,
				0x8,
				0x10,
				0x20,
				0x40,
				0x80
			};
			uint8_t modifiers = 0;
			for(i=0; i<8; i++) if (m & (0x01<<i))  modifiers |= default_mod_mapping[i];
			m = modifiers;
		}

		// modifier keys are used as buttons in emu mode
		if(emu_mode != EMU_NONE && !osd_is_visible)
		{
			char last_btn = emu_state & (JOY_BTN1 | JOY_BTN2 | JOY_BTN3 | JOY_BTN4);
			if(keyrah!=2)
			{
				if(m & (1<<EMU_BTN1)) emu_state |=  JOY_BTN1;
				else                  emu_state &= ~JOY_BTN1;
				if(m & (1<<EMU_BTN2)) emu_state |=  JOY_BTN2;
				else                  emu_state &= ~JOY_BTN2;
			}
			if(m & (1<<EMU_BTN3)) emu_state |=  JOY_BTN3;
			else                  emu_state &= ~JOY_BTN3;
			if(m & (1<<EMU_BTN4)) emu_state |=  JOY_BTN4;
			else                  emu_state &= ~JOY_BTN4;

			// check if state of mouse buttons has changed
			// (on a mouse only two buttons are supported)
			if((last_btn  & (JOY_BTN1 | JOY_BTN2)) != (emu_state & (JOY_BTN1 | JOY_BTN2)))
			{
				if(emu_mode == EMU_MOUSE)
				{
					unsigned char b;
					if(emu_state & JOY_BTN1) b |= 1;
					if(emu_state & JOY_BTN2) b |= 2;
					user_io_mouse(0, b, 0, 0, 0);
				}
			}

			// check if state of joystick buttons has changed
			if(last_btn != (emu_state & (JOY_BTN1|JOY_BTN2|JOY_BTN3|JOY_BTN4))) {
				user_io_joystick_emu();
			}
		}

		// handle modifier keys
		if(m != modifier && !osd_is_visible)
		{
			for(i=0;i<8;i++)
			{
				// Do we have a downstroke on a modifier key?
				if((m & (1<<i)) && !(modifier & (1<<i)))
				{
					// shift keys are used for mouse joystick emulation in emu mode
					if(((i != EMU_BTN1) && (i != EMU_BTN2) && (i != EMU_BTN3) && (i != EMU_BTN4)) || (emu_mode == EMU_NONE))
					{
						if(modifier_keycode(i) != MISS) send_keycode(modifier_keycode(i));
					}
				}

				if(!(m & (1<<i)) && (modifier & (1<<i)))
				{
					if(((i != EMU_BTN1) && (i != EMU_BTN2) && (i != EMU_BTN3) && (i != EMU_BTN4)) || (emu_mode == EMU_NONE))
					{
						if(modifier_keycode(i) != MISS) send_keycode(BREAK | modifier_keycode(i));
					}
				}
			}

			modifier = m;
		}

		// check if there are keys in the pressed list which aren't 
		// reported anymore
		for(i=0;i<6;i++)
		{
			unsigned short code = keycode(pressed[i]);

			if(pressed[i] && code != MISS)
			{
				if (user_io_dip_switch1())
					iprintf("key 0x%X break: 0x%X\n", pressed[i], code);

				for(j=0;j<6 && pressed[i] != k[j];j++);

				// don't send break for caps lock
				if(j == 6)
				{
					// If OSD is visible, then all keys are sent into the OSD
					// using Amiga key codes since the OSD itself uses Amiga key codes
					// for historical reasons. If the OSD is invisble then only
					// those keys marked for OSD in the core specific table are
					// sent for OSD handling.
					if(code & OSD_OPEN)
					{
						OsdKeySet(0x80 | KEY_MENU);
					}
					else
					{
						// special OSD key handled internally 
						if(osd_is_visible) OsdKeySet(0x80 | usb2amiga(pressed[i]));
					}

					if(!key_used_by_osd(code))
					{
						// iprintf("Key is not used by OSD\n");
						if(is_emu_key(pressed[i], keyrah) && !osd_is_visible)
						{
							emu_state &= ~is_emu_key(pressed[i], keyrah);
							user_io_joystick_emu();
							if(keyrah == 2) 
							{
								unsigned char b;
								if(emu_state & JOY_BTN1) b |= 1;
								if(emu_state & JOY_BTN2) b |= 2;
								user_io_mouse(0, b, 0, 0, 0);
							}
						}
						else if(!(code & CAPS_LOCK_TOGGLE) && !(code & NUM_LOCK_TOGGLE))
						{
							send_keycode(BREAK | code);	
						}
					}
				}
			}  
		}

		for(i=0;i<6;i++)
		{
			unsigned short code = keycode(k[i]);

			if(k[i] && (k[i] <= KEYCODE_MAX) && code != MISS)
			{
				// check if this key is already in the list of pressed keys
				for(j=0;j<6 && k[i] != pressed[j];j++);

				if(j == 6)
				{
					if (user_io_dip_switch1())
						iprintf("key 0x%X make: 0x%X\n", k[i], code);

					// If OSD is visible, then all keys are sent into the OSD
					// using Amiga key codes since the OSD itself uses Amiga key codes
					// for historical reasons. If the OSD is invisble then only
					// those keys marked for OSD in the core specific table are
					// sent for OSD handling.
					if(code & OSD_OPEN) 
					{
						OsdKeySet(KEY_MENU);
					}
					else
					{
						// special OSD key handled internally 
						if(osd_is_visible)
							OsdKeySet(usb2amiga(k[i]));
						else if (((mist_cfg.joystick_autofire_combo == 0 && k[i] == 0x62) ||  // KP0
						          (mist_cfg.joystick_autofire_combo == 1 && k[i] == 0x2B)) && // TAB
						          (m & 0x05) == 0x05 && // LCTR+LALT
						          (core_type == CORE_TYPE_8BIT ||
						          core_type == CORE_TYPE_ARCHIE ||
						          core_type == CORE_TYPE_MIST2))
						{
							autofire = ((autofire + 1) & 0x03);
							InfoMessage(config_autofire_msg[autofire]);
						}

					}

					// no further processing of any key that is currently 
					// redirected to the OSD
					if(!key_used_by_osd(code))
					{
						// iprintf("Key is not used by OSD\n");
						if(is_emu_key(k[i], keyrah) && !osd_is_visible)
						{
							emu_state |= is_emu_key(k[i], keyrah);
							user_io_joystick_emu();
							if(keyrah == 2) 
							{
								unsigned char b;
								if(emu_state & JOY_BTN1) b |= 1;
								if(emu_state & JOY_BTN2) b |= 2;
								user_io_mouse(0, b, 0, 0, 0);
							}
						}
						else if(!(code & CAPS_LOCK_TOGGLE)&& !(code & NUM_LOCK_TOGGLE))
						{
							send_keycode(code);
						}
						else
						{
							if(code & CAPS_LOCK_TOGGLE)
							{
								// send alternating make and break codes for caps lock
								send_keycode((code & 0xff) | (caps_lock_toggle?BREAK:0));
								caps_lock_toggle = !caps_lock_toggle;

								set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
							}

							if(code & NUM_LOCK_TOGGLE)
							{
								// num lock has four states indicated by leds:
								// all off: normal
								// num lock on, scroll lock on: mouse emu
								// num lock on, scroll lock off: joy0 emu
								// num lock off, scroll lock on: joy1 emu

								if(emu_mode == EMU_MOUSE) emu_timer = GetTimer(EMU_MOUSE_FREQ);

								switch(code ^ NUM_LOCK_TOGGLE)
								{
									case 1:
										emu_mode = EMU_MOUSE;
										break;

									case 2:
										emu_mode = EMU_JOY0;
										break;

									case 3:
										emu_mode = EMU_JOY1;
										break;

									case 4:
										emu_mode = EMU_NONE;
										break;

									default:
										emu_mode = (emu_mode+1)&3;
										break;
								}

								if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY0) set_kbd_led(HID_LED_NUM_LOCK, true);
									else set_kbd_led(HID_LED_NUM_LOCK, false);

								if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY1) set_kbd_led(HID_LED_SCROLL_LOCK, true);
									else set_kbd_led(HID_LED_SCROLL_LOCK, false);
							}
						}
					}
				}
			}
		}
		
		for(i=0;i<6;i++)
		{
			pressed[i] = k[i];
			keycodes[i] = pressed[i]; // send raw USB code, not amiga - keycode(pressed[i]);
			keycodes_ps2[i] = keycode(pressed[i]);
		}
		StateKeyboardSet(m, keycodes, keycodes_ps2);

		// set the typematic timer to the first delay
		if (core_type == CORE_TYPE_8BIT)
			ps2_typematic_timer = GetTimer((((ps2_typematic_rate & 0x60)>>5)+1)*250);

	}
}

/* translates a USB modifiers into scancodes */
void add_modifiers(uint8_t mod, uint16_t* keys_ps2)
{
	uint8_t i;
	uint8_t offset = 1;
	uint8_t index = 0;
	while(offset)
	{
		if(mod&offset)
		{
			uint16_t ps2_value = modifier_keycode(index);
			if(ps2_value != MISS)
			{
				if(ps2_value & EXT) ps2_value = (0xE000 | (ps2_value & 0xFF));
				for(i=0; i<4; i++)
				{
					if(keys_ps2[i]==0)
					{
						keys_ps2[i] = ps2_value;
						break;
					}
				}
			}
		}
		offset <<= 1;
		index++;
	}
}

char user_io_key_remap(char *s, char action, int tag) {
	if (action == INI_SAVE) return 0;
	// s is a string containing two comma separated hex numbers
	if((strlen(s) != 5) && (s[2]!=',')) {
		ini_parser_debugf("malformed entry %s", s);
		return 0;
	}

	char i;
	for(i=0;i<MAX_REMAP;i++) {
		if(!key_remap_table[i][0]) {
			key_remap_table[i][0] = strtol(s, NULL, 16);
			key_remap_table[i][1] = strtol(s+3, NULL, 16);

			ini_parser_debugf("key remap entry %d = %02x,%02x", 
			  i, key_remap_table[i][0], key_remap_table[i][1]);
			return 0;
		}
	}
	return 0;
}

unsigned char user_io_ext_idx(const char *name, const char* ext) {
	unsigned char idx = 0;
	int len = strlen(ext);
	int extlen;

	const char *nameext = GetExtension(name);
	if (!nameext) return 0;
	extlen = strlen(nameext);
	while((len>3) && *ext) {
		if(!_strnicmp(nameext,ext,extlen > 3 ? 3 : extlen)) return idx;
		if(strlen(ext)<=3) break;
		idx++;
		ext +=3;
	}

	return 0;
}

void user_io_change_into_core_dir(void) {

	if (arc_get_dirname()[0]) {
		strcpy(s, "/");
		strcat(s, arc_get_dirname());
	} else {
		user_io_create_config_name(s, 0, CONFIG_ROOT);
	}
	// try to change into subdir named after the core
	iprintf("Trying to open work dir \"%s\"\n", s);
	ChangeDirectoryName(s);

}
