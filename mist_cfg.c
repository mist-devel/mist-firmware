// mist_cfg.c
// 2015, rok.krajnc@gmail.com


//// includes ////
#include <string.h>
#include "ini_parser.h"
#include "mist_cfg.h"
#include "user_io.h"
#include "data_io.h"
#include "usb/usb.h"
#include "usb/hid.h"
#include "usb/joymapping.h"

extern FIL ini_file;

// call data_io_rom_upload but reload sector_buffer afterwards since the io
// operations in data_io_rom_upload may have overwritten the buffer
// mode = 0: prepare for rom upload, mode = 1: rom upload, mode = 2, end rom upload
char ini_rom_upload(char *s, char action, int tag) {
  if(action == INI_SAVE) return 0;
#ifndef INI_PARSER_TEST
  data_io_rom_upload(s, 1);
  f_lseek(&ini_file, (((f_tell(&ini_file)+511)>>9)-1)<<9);
  FileReadBlock(&ini_file, sector_buffer);
#endif
  return 0;
}

//// mist_ini_parse() ////
void mist_ini_parse()
{
#ifndef INI_PARSER_TEST
  hid_joystick_button_remap_init();
  joy_key_map_init();
  data_io_rom_upload(NULL, 0);   // prepare upload
  memset(&mist_cfg, 0, sizeof(mist_cfg));
  mist_cfg.mouse_speed = 100;
  mist_cfg.joystick_analog_mult = 128;
  mist_cfg.joystick_dead_range = 4;
  minimig_cfg.kick1x_memory_detection_patch = 1;
  ini_parse(&mist_ini_cfg, user_io_get_core_name(), 0);
  data_io_rom_upload(NULL, 2);   // upload done
#endif
}

//// vars ////
// config data
mist_cfg_t mist_cfg = {
  .scandoubler_disable = 0,
  .csync_disable = 0,
  .mouse_boot_mode = 0,
  .mouse_speed = 100,
  .joystick_ignore_hat = 0,
  .joystick_ignore_osd = 0,
  .joystick_disable_shortcuts = 0,
  .joystick0_prefer_db9 = 0,
  .joystick_db9_fixed_index = 0,
  .joystick_emu_fixed_index = 0,
  .joystick_analog_mult = 128,
  .joystick_analog_offset = 0,
  .joystick_autofire_combo = 0,
  .joystick_disable_swap = 0,
  .joystick_dead_range = 4,
  .key_menu_as_rgui = 0,
  .keyrah_mode = 0,
  .reset_combo = 0,
  .ypbpr = 0,
  .keep_video_mode = 0,
  .led_animation = 0,
  .amiga_mod_keys = 0
};

minimig_cfg_t minimig_cfg = {
  .kick1x_memory_detection_patch = 0,
  .clock_freq = 0,
  .conf_name = {"Default","1","2","3","4"}
};

atarist_cfg_t atarist_cfg = {
  .conf_name = {"Default","1","2","3","4"}
};

// mist ini sections
static const ini_section_t mist_ini_sections[] = {
  {1, "MIST"},
  {2, "MINIMIG_CONFIG"},
  {3, "ATARIST_CONFIG"}
};

// mist ini vars
static const ini_var_t mist_ini_vars[] = {
  // [MIST] or [<core name>]
  {"LED_ANIMATION", (void*)(&(mist_cfg.led_animation)), UINT8, 0, 1, 1},
  {"YPBPR", (void*)(&(mist_cfg.ypbpr)), UINT8, 0, 1, 1},
  {"KEEP_VIDEO_MODE", (void*)(&(mist_cfg.keep_video_mode)), UINT8, 0, 1, 1},
  {"KEYRAH_MODE", (void*)(&(mist_cfg.keyrah_mode)), UINT32, 0, 0xFFFFFFFF, 1},
  {"RESET_COMBO", (void*)(&(mist_cfg.reset_combo)), UINT8, 0, 2, 1},
  {"SCANDOUBLER_DISABLE", (void*)(&(mist_cfg.scandoubler_disable)), UINT8, 0, 1, 1},
  {"CSYNC_DISABLE", (void*)(&(mist_cfg.csync_disable)), UINT8, 0, 1, 1},
  {"MOUSE_BOOT_MODE", (void*)(&(mist_cfg.mouse_boot_mode)), UINT8, 0, 1, 1},
  {"MOUSE_SPEED", (void*)(&(mist_cfg.mouse_speed)), UINT8, 10, 200, 1},
  {"JOYSTICK_IGNORE_HAT", (void*)(&(mist_cfg.joystick_ignore_hat)), UINT8, 0, 1, 1},
  {"JOYSTICK_DISABLE_SHORTCUTS", (void*)(&(mist_cfg.joystick_disable_shortcuts)), UINT8, 0, 1, 1},
  {"JOYSTICK_IGNORE_OSD", (void*)(&(mist_cfg.joystick_ignore_osd)), UINT8, 0, 1, 1},
  {"JOYSTICK0_PREFER_DB9", (void*)(&(mist_cfg.joystick0_prefer_db9)), UINT8, 0, 1, 1},
  {"JOYSTICK_DB9_FIXED_INDEX", (void*)(&(mist_cfg.joystick_db9_fixed_index)), UINT8, 0, 1, 1},
  {"JOYSTICK_EMU_FIXED_INDEX", (void*)(&(mist_cfg.joystick_emu_fixed_index)), UINT8, 0, 1, 1},
  {"JOYSTICK_ANALOG_MULTIPLIER", (void*)(&(mist_cfg.joystick_analog_mult)), UINT8, 1, 128, 1},
  {"JOYSTICK_ANALOG_OFFSET", (void*)(&(mist_cfg.joystick_analog_offset)), INT8, -127, 127, 1},
  {"JOYSTICK_AUTOFIRE_COMBO", (void*)(&(mist_cfg.joystick_autofire_combo)), INT8, 0, 2, 1},
  {"JOYSTICK_DISABLE_SWAP", (void*)(&(mist_cfg.joystick_disable_swap)), INT8, 0, 1, 1},
  {"JOYSTICK_DEAD_RANGE", (void*)(&(mist_cfg.joystick_dead_range)), UINT8, 0, 255, 1},
  {"KEY_MENU_AS_RGUI", (void*)(&(mist_cfg.key_menu_as_rgui)), UINT8, 0, 1, 1},
  {"SDRAM64", (void*)(&(mist_cfg.sdram64)), UINT8, 0, 1, 1},
#ifdef JOY_DB9_MD
  {"JOYSTICK_DB9_MD", (void*)(&(mist_cfg.joystick_db9_md)), UINT8, 0, 2, 1},
#endif
#ifndef INI_PARSER_TEST
  {"KEY_REMAP", (void*)user_io_key_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"HID_BUTTON_REMAP", (void*)hid_joystick_button_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"JOYSTICK_REMAP", (void*)virtual_joystick_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"JOY_KEY_MAP", (void*)joystick_key_map, CUSTOM_HANDLER, 0, 0, 1},
#endif
  {"ROM", (void*)ini_rom_upload, CUSTOM_HANDLER, 0, 0, 1},
  {"AMIGA_MOD_KEYS", (void*)(&(mist_cfg.amiga_mod_keys)), UINT8, 0, 3, 1},
  {"USB_STORAGE", (void*)(&(mist_cfg.usb_storage)), UINT8, 0, 1, 1},
  // [MINIMIG_CONFIG]
  {"KICK1X_MEMORY_DETECTION_PATCH", (void*)(&(minimig_cfg.kick1x_memory_detection_patch)), UINT8, 0, 1, 2},
  {"CLOCK_FREQ", (void*)(&(minimig_cfg.clock_freq)), UINT8, 0, 2, 2},
  {"CONF_DEFAULT", (void*)(&(minimig_cfg.conf_name[0])), STRING, 1, 10, 2},
  {"CONF_1", (void*)(&(minimig_cfg.conf_name[1])), STRING, 1, 10, 2},
  {"CONF_2", (void*)(&(minimig_cfg.conf_name[2])), STRING, 1, 10, 2},
  {"CONF_3", (void*)(&(minimig_cfg.conf_name[3])), STRING, 1, 10, 2},
  {"CONF_4", (void*)(&(minimig_cfg.conf_name[4])), STRING, 1, 10, 2},
  // [ATARIST_CONFIG]
  {"CONF_DEFAULT", (void*)(&(atarist_cfg.conf_name[0])), STRING, 1, 10, 3},
  {"CONF_1", (void*)(&(atarist_cfg.conf_name[1])), STRING, 1, 10, 3},
  {"CONF_2", (void*)(&(atarist_cfg.conf_name[2])), STRING, 1, 10, 3},
  {"CONF_3", (void*)(&(atarist_cfg.conf_name[3])), STRING, 1, 10, 3},
  {"CONF_4", (void*)(&(atarist_cfg.conf_name[4])), STRING, 1, 10, 3}
};

// mist ini config
const ini_cfg_t mist_ini_cfg = {
#ifdef INI_PARSER_TEST
  "test.ini",
#else
  "/MIST.INI",
#endif
  mist_ini_sections,
  mist_ini_vars,
  (int)(sizeof(mist_ini_sections) / sizeof(ini_section_t)),
  (int)(sizeof(mist_ini_vars)     / sizeof(ini_var_t))
};
