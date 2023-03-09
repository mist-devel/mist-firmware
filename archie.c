#include "stdio.h"
#include "string.h"
#include "hardware.h"

#include "menu.h"
#include "osd.h"
#include "archie.h"
#include "hdd.h"
#include "user_io.h"
#include "data_io.h"
#include "debug.h"

#define CONFIG_FILENAME  "ARCHIE  CFG"
#define MAX_FLOPPY 2

typedef struct {
  unsigned long system_ctrl;     // system control word
  char rom_img[64];              // rom image file name
  char cmos_img[64];             // cmos image file name
  hardfileTYPE  hardfile[2];
} archie_config_t;

static archie_config_t config;

static char floppy_name[MAX_FLOPPY][64];

extern char s[FF_LFN_BUF + 1];

enum state { STATE_HRST, STATE_RAK1, STATE_RAK2, STATE_IDLE, 
	     STATE_WAIT4ACK1, STATE_WAIT4ACK2, STATE_HOLD_OFF } kbd_state;

// archie keyboard controller commands
#define HRST    0xff
#define RAK1    0xfe
#define RAK2    0xfd
#define RQPD    0x40         // mask 0xf0
#define PDAT    0xe0         // mask 0xf0
#define RQID    0x20
#define KBID    0x80         // mask 0xc0
#define KDDA    0xc0         // new key down data, mask 0xf0
#define KUDA    0xd0         // new key up data, mask 0xf0
#define RQMP    0x22         // request mouse data
#define MDAT    0x00         // mouse data, mask 0x80
#define BACK    0x3f
#define NACK    0x30         // disable kbd scan, disable mouse
#define SACK    0x31         // enable kbd scan, disable mouse
#define MACK    0x32         // disable kbd scan, enable mouse
#define SMAK    0x33         // enable kbd scan, enable mouse
#define LEDS    0x00         // mask 0xf8
#define PRST    0x21         // nop

#define QUEUE_LEN 8
static unsigned char tx_queue[QUEUE_LEN][2];
static unsigned char tx_queue_rptr, tx_queue_wptr;
#define QUEUE_NEXT(a)  ((a+1)&(QUEUE_LEN-1))

static unsigned long ack_timeout;
static short mouse_x, mouse_y; 

#define FLAG_SCAN_ENABLED  0x01
#define FLAG_MOUSE_ENABLED 0x02
static unsigned char flags;

// #define HOLD_OFF_TIME 2
#ifdef HOLD_OFF_TIME
static unsigned long hold_off_timer;
#endif

static char buffer[17];  // local buffer to assemble file name (8+.+3+\0)

char *archie_get_rom_name(void) {
  return config.rom_img;
}

char *archie_get_cmos_name(void) {
  return config.cmos_img;
}

char *archie_get_floppy_name(char i) {

  if(!floppy_name[i][0]) {
    strcpy(buffer, "* no disk *");
    return buffer;
  } else
    return (floppy_name[i]);
}

void archie_save_config(void) {
  FIL file;
  UINT bw;

  // save configuration data
  if (FileOpenCompat(&file, CONFIG_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)  {
    f_write(&file, &config, sizeof(archie_config_t), &bw);
    f_close(&file);
  }
}

void archie_set_floppy(char i, const unsigned char *name) {

  user_io_file_mount(name, i);
  if (user_io_is_mounted(i)) {
    strncpy(floppy_name[i], name, sizeof(floppy_name[i]));
    floppy_name[i][sizeof(floppy_name[i])-1] = 0;
  } else {
    floppy_name[i][0] = 0;
  }
}

void archie_save_cmos() {
  FIL file;

  archie_debugf("Saving CMOS file");
  strcpy(s, "/");
  strcat(s, config.cmos_img);
  if (f_open(&file, s, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
    data_io_file_rx(&file, 0x03, 256);
    f_close(&file);
  }
}

void archie_set_cmos(const unsigned char *name) {
  FIL file;

  if (!name) return;
  if(f_open(&file, name, FA_READ) == FR_OK) {
    archie_debugf("CMOS file %s with %llu bytes to send", name, f_size(&file));
    // save file name
    strncpy(config.cmos_img, name, sizeof(config.cmos_img));
    config.cmos_img[sizeof(config.cmos_img)-1] = 0;
    data_io_file_tx(&file, 0x03, 0);
    f_close(&file);
  } else
    archie_debugf("CMOS %.s no found", name);
}

void archie_set_rom(const unsigned char *name) {
  FIL file;

  if (!name) return;
  if(f_open(&file, name, FA_READ) == FR_OK) {
    archie_debugf("ROM file %s with %llu bytes to send", name, f_size(&file));
    // save file name
    strncpy(config.rom_img, name, sizeof(config.rom_img));
    config.rom_img[sizeof(config.rom_img)-1] = 0;
    data_io_file_tx(&file, 0x01, 0);
    f_close(&file);
  } else
    archie_debugf("ROM %.11s no found", name);
}

static void archie_kbd_enqueue(unsigned char state, unsigned char byte) {
  if(QUEUE_NEXT(tx_queue_wptr) == tx_queue_rptr) {
    archie_debugf("KBD tx queue overflow");
    return;
  }

  archie_debugf("KBD ENQUEUE %x (%x)", byte, state);
  tx_queue[tx_queue_wptr][0] = state;
  tx_queue[tx_queue_wptr][1] = byte;
  tx_queue_wptr = QUEUE_NEXT(tx_queue_wptr);
} 

static void archie_kbd_tx(unsigned char state, unsigned char byte) {
  archie_debugf("KBD TX %x (%x)", byte, state);
  spi_uio_cmd_cont(0x05);
  spi8(byte);
  DisableIO();

  kbd_state = state;
  ack_timeout = GetTimer(10);  // 10ms timeout
}

static void archie_kbd_send(unsigned char state, unsigned char byte) {
  // don't send if we are waiting for an ack
  if((kbd_state != STATE_WAIT4ACK1)&&(kbd_state != STATE_WAIT4ACK2)) 
    archie_kbd_tx(state, byte);
  else
    archie_kbd_enqueue(state, byte);
}

static void archie_kbd_reset(void) {
  archie_debugf("KBD reset");
  tx_queue_rptr = tx_queue_wptr = 0;
  kbd_state = STATE_HRST;
  mouse_x = mouse_y = 0;
  flags = 0;
}

void archie_init(void) {
  FIL file;
  UINT br;
  char i;

  archie_debugf("init");

  ResetMenu();
  ChangeDirectoryName("/");

  // set config defaults
  config.system_ctrl = 0;
  strcpy(config.rom_img, "RISCOS.ROM");
  strcpy(config.cmos_img, "CMOS.RAM");

  config.hardfile[0].enabled = HDF_FILE;
  strcpy(config.hardfile[0].name, "ARCHIE1.HDF");
  config.hardfile[1].enabled = HDF_FILE;
  strcpy(config.hardfile[1].name, "ARCHIE2.HDF");

  // try to load config from card
  if(FileOpenCompat(&file, CONFIG_FILENAME, FA_READ) == FR_OK) {
    if(f_size(&file) == sizeof(archie_config_t))
      f_read(&file, &config, sizeof(archie_config_t), &br);
    else
      archie_debugf("Unexpected config size %d != %d", f_size(&file), sizeof(archie_config_t));
    f_close(&file);
  } else
    archie_debugf("No %.11s config found", CONFIG_FILENAME);

  // upload rom file
  archie_set_rom(config.rom_img);

  // upload ext file
  if(FileOpenCompat(&file, "RISCOS  EXT", FA_READ) == FR_OK) {
    archie_debugf("Found RISCOS.EXT, uploading it");
    data_io_file_tx(&file, 0x02, 0);
    f_close(&file);
  } else 
    archie_debugf("RISCOS.EXT no found");

  // upload cmos file
  archie_set_cmos(config.cmos_img);

  // try to open default floppies
  for(i=0;i<MAX_FLOPPY;i++) {
    char fdc_name[] = "FLOPPY0.ADF";
    fdc_name[6] = '0'+i;
    user_io_file_mount(fdc_name, i);
    if (user_io_is_mounted(i)) {
      strcpy(floppy_name[i], fdc_name);
      archie_debugf("Inserted floppy %d", i);
    } else
      floppy_name[i][0] = 0;
  }

  // open hdd image(s)
  hardfile[0] = &config.hardfile[0];
  hardfile[1] = &config.hardfile[1];

  OpenHardfile(0, true);
  OpenHardfile(1, true);

  archie_kbd_send(STATE_RAK1, HRST);
  ack_timeout = GetTimer(20);  // give archie 20ms to reply
}

void archie_kbd(unsigned short code) {
  archie_debugf("KBD key code %x", code);

  // don't send anything yet if we are still in reset state
  if(kbd_state <= STATE_RAK2) {
    archie_debugf("KBD still in reset");
    return;
  }

  // ignore any key event if key scanning is disabled
  if(!(flags & FLAG_SCAN_ENABLED)) {
    archie_debugf("KBD keyboard scan is disabled!");
    return;
  }

  // select prefix for up or down event
  unsigned char prefix = (code&0x8000)?KUDA:KDDA;

  archie_kbd_send(STATE_WAIT4ACK1, prefix | (code>>4)); 
  archie_kbd_send(STATE_WAIT4ACK2, prefix | (code&0x0f));
}

void archie_mouse(unsigned char b, char x, char y) {
  archie_debugf("KBD MOUSE X:%d Y:%d B:%d", x, y, b);

  // max values -64 .. 63
  mouse_x += x;
  if(mouse_x >  63) mouse_x =  63;
  if(mouse_x < -64) mouse_x = -64;

  mouse_y -= y;
  if(mouse_y >  63) mouse_y =  63;
  if(mouse_y < -64) mouse_y = -64;

  // don't send anything yet if we are still in reset state
  if(kbd_state <= STATE_RAK2) {
    archie_debugf("KBD still in reset");
    return;
  }

  // ignore any mouse movement if mouse is disabled or if nothing to report
  if((flags & FLAG_MOUSE_ENABLED) && (mouse_x || mouse_y)) {
    // send asap if no pending byte
    if(kbd_state == STATE_IDLE) {
      archie_kbd_send(STATE_WAIT4ACK1, mouse_x & 0x7f); 
      archie_kbd_send(STATE_WAIT4ACK2, mouse_y & 0x7f);
      mouse_x = mouse_y = 0;
    }
  }

  // ignore mouse buttons if key scanning is disabled
  if(flags & FLAG_SCAN_ENABLED) {
    static const uint8_t remap[] = { 0, 2, 1 };
    static unsigned char buts = 0;
    uint8_t s;

    // map all three buttons
    for(s=0;s<3;s++) {
      uint8_t mask = (1<<s);
      if((b&mask) != (buts&mask)) {
	unsigned char prefix = (b&mask)?KDDA:KUDA;
	archie_kbd_send(STATE_WAIT4ACK1, prefix | 0x07); 
	archie_kbd_send(STATE_WAIT4ACK2, prefix | remap[s]);
      }
    }
    buts = b;
  }
}

static void archie_check_queue(void) {
  if(tx_queue_rptr == tx_queue_wptr)
    return;

  archie_kbd_tx(tx_queue[tx_queue_rptr][0], tx_queue[tx_queue_rptr][1]); 
  tx_queue_rptr = QUEUE_NEXT(tx_queue_rptr);
}

void archie_handle_kbd(void) {

#ifdef HOLD_OFF_TIME
  if((kbd_state == STATE_HOLD_OFF) && CheckTimer(hold_off_timer)) {
    archie_debugf("KBD resume after hold off");
    kbd_state = STATE_IDLE;
    archie_check_queue();
  }
#endif

  // timeout waiting for ack?
  if((kbd_state == STATE_WAIT4ACK1) || (kbd_state == STATE_WAIT4ACK2)) {
    if(CheckTimer(ack_timeout)) {
      if(kbd_state == STATE_WAIT4ACK1)
	archie_debugf(">>>> KBD ACK TIMEOUT 1ST BYTE <<<<");
      if(kbd_state == STATE_WAIT4ACK2)
	archie_debugf(">>>> KBD ACK TIMEOUT 2ND BYTE <<<<");

      kbd_state = STATE_IDLE;
    }
  }

  // timeout in reset sequence?
  if(kbd_state <= STATE_RAK2) {
    if(CheckTimer(ack_timeout)) {
      archie_debugf("KBD timeout in reset state");
      
      archie_kbd_send(STATE_RAK1, HRST);
      ack_timeout = GetTimer(20);  // 20ms timeout
    }
  }

  spi_uio_cmd_cont(0x04);
  if(spi_in() == 0xa1) {
    unsigned char data = spi_in();
    DisableIO();
    
    archie_debugf("KBD RX %x", data);

    switch(data) {
      // arm requests reset
    case HRST:
      archie_kbd_reset();
      archie_kbd_send(STATE_RAK1, HRST);
      ack_timeout = GetTimer(20);  // 20ms timeout
      break;

      // arm sends reset ack 1
    case RAK1:
      if(kbd_state == STATE_RAK1) {
	archie_kbd_send(STATE_RAK2, RAK1);
	ack_timeout = GetTimer(20);  // 20ms timeout
      } else 
	kbd_state = STATE_HRST;
      break;

      // arm sends reset ack 2
    case RAK2:
      if(kbd_state == STATE_RAK2) { 
	archie_kbd_send(STATE_IDLE, RAK2);
	ack_timeout = GetTimer(20);  // 20ms timeout
      } else 
	kbd_state = STATE_HRST;
      break;

      // arm request keyboard id
    case RQID:
      archie_kbd_send(STATE_IDLE, KBID | 1);
      break;

      // arm acks first byte
    case BACK:
      if(kbd_state != STATE_WAIT4ACK1) {
          archie_debugf("KBD unexpected BACK, resetting KBD");
          kbd_state = STATE_HRST;
      } else {
#ifdef HOLD_OFF_TIME
          // wait some time before sending next byte
          archie_debugf("KBD starting hold off");
          kbd_state = STATE_HOLD_OFF;
          hold_off_timer = GetTimer(10);
#else
          kbd_state = STATE_IDLE;
          archie_check_queue();
#endif
      }
      break;

      // arm acks second byte
    case NACK:
    case SACK:
    case MACK:
    case SMAK:

      if(((data == SACK) || (data == SMAK)) && !(flags & FLAG_SCAN_ENABLED)) {
	archie_debugf("KBD Enabling key scanning");
	flags |= FLAG_SCAN_ENABLED;
      }

      if(((data == NACK) || (data == MACK)) && (flags & FLAG_SCAN_ENABLED)) {
	archie_debugf("KBD Disabling key scanning");
	flags &= ~FLAG_SCAN_ENABLED;
      }

      if(((data == MACK) || (data == SMAK)) && !(flags & FLAG_MOUSE_ENABLED)) {
	archie_debugf("KBD Enabling mouse");
	flags |= FLAG_MOUSE_ENABLED;
      }

      if(((data == NACK) || (data == SACK)) && (flags & FLAG_MOUSE_ENABLED)) {
	archie_debugf("KBD Disabling mouse");
	flags &= ~FLAG_MOUSE_ENABLED;
      }
      
      // wait another 10ms before sending next byte
#ifdef HOLD_OFF_TIME
      archie_debugf("KBD starting hold off");
      kbd_state = STATE_HOLD_OFF;
      hold_off_timer = GetTimer(10);
#else
      kbd_state = STATE_IDLE;
      archie_check_queue();
#endif
      break;
    }
  } else
    DisableIO();
}

void archie_handle_hdd(void) {
  unsigned char  c1;

  EnableFpga();
  c1 = SPI(0); // cmd request
  SPI(0);
  SPI(0);
  SPI(0);
  SPI(0);
  SPI(0);
  DisableFpga();

  HandleHDD(c1, 0, 0);
}

void archie_poll(void) {
  archie_handle_kbd();
  archie_handle_hdd();
}

//////////////////////////
////// Archie menu ///////
//////////////////////////
static char archie_file_selected(uint8_t idx, const char *SelectedName) {
	switch(idx) {
		case 0:
		case 1:
			archie_set_floppy(idx, SelectedName);
			break;
		case 2:
			archie_set_rom(SelectedName);
			break;
		case 3:
			archie_set_cmos(SelectedName);
			break;
	}
	return 0;
}

static char archie_getmenupage(uint8_t idx, char action, menu_page_t *page) {
	if (action == MENU_PAGE_EXIT) return 0;
	page->title = "Archie";
	page->flags = OSD_ARROW_RIGHT;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

static char archie_getmenuitem(uint8_t idx, char action, menu_item_t *item) {
	item->stipple = 0;
	item->active = 1;
	item->page = 0;
	item->newpage = 0;
	item->newsub = 0;
	if (action == MENU_ACT_RIGHT) {
		SetupSystemMenu();
		return 0;
	}

	switch (action) {
		case MENU_ACT_GET:
			switch(idx) {
				case 0:
					strcpy(s, " Floppy 0: ");
					strcat(s, archie_get_floppy_name(0));
					item->item = s;
					break;
				case 1:
					strcpy(s, " Floppy 1: ");
					strcat(s, archie_get_floppy_name(1));
					item->item = s;
					break;
				case 2:
					strcpy(s, "   OS ROM: ");
					strcat(s, archie_get_rom_name());
					item->item = s;
					break;
				case 3:
					strcpy(s, " CMOS RAM: ");
					strcat(s, archie_get_cmos_name());
					item->item = s;
					break;
				case 4:
					item->item = " Save CMOS RAM";
					break;
				case 5:
					item->item = " Save config";
					break;
				default:
					item->active = 0;
					return 0;
			}
			break;
		case MENU_ACT_SEL:
			switch(idx) {
				case 0:  // Floppy 0
				case 1:  // Floppy 1
					if(user_io_is_mounted(idx)) {
						archie_set_floppy(idx, NULL);
					} else
						SelectFileNG("ADF", SCAN_DIR | SCAN_LFN, archie_file_selected, 1);
					break;
				case 2:  // Load ROM
					SelectFileNG("ROM", SCAN_LFN, archie_file_selected, 0);
					break;

				case 3:  // Load CMOS
					SelectFileNG("RAM", SCAN_LFN, archie_file_selected, 0);
					break;

				case 4:  // Save CMOS
					archie_save_cmos();
					break;

				case 5:  // Save config
					archie_save_config();
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

void archie_setup_menu()
{
	SetupMenu(archie_getmenupage, archie_getmenuitem, NULL);
}
