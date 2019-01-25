// mist_cfg.c
// 2015, rok.krajnc@gmail.com


//// includes ////
#include <string.h>
#include "ini_parser.h"
#include "mist_cfg.h"
#include "user_io.h"
#include "usb/usb.h"
#include "usb/hid.h"
#include "usb/joymapping.h"

//// mist_ini_parse() ////
void mist_ini_parse()
{
  hid_joystick_button_remap_init();
  virtual_joystick_remap_init();
  joy_key_map_init();
  memset(&mist_cfg, 0, sizeof(mist_cfg));
  ini_parse(&mist_ini_cfg);
}


//// vars ////
// config data
mist_cfg_t mist_cfg = { 
  .scandoubler_disable = 0,
  .mouse_boot_mode = 0, 
  .joystick_ignore_hat = 0,
  .joystick_ignore_osd = 0,
  .joystick_disable_shortcuts = 0,
  .joystick0_prefer_db9 = 0,
  .key_menu_as_rgui = 0,
  .keyrah_mode = 0,
  .reset_combo = 0,
  .ypbpr = 0,
  .keep_video_mode = 0,
  .led_animation = 0
};

// mist ini sections
const ini_section_t mist_ini_sections[] = {
  {1, "MIST"}
};

// mist ini vars
const ini_var_t mist_ini_vars[] = {
  {"LED_ANIMATION", (void*)(&(mist_cfg.led_animation)), UINT8, 0, 1, 1},
  {"YPBPR", (void*)(&(mist_cfg.ypbpr)), UINT8, 0, 1, 1},
  {"KEEP_VIDEO_MODE", (void*)(&(mist_cfg.keep_video_mode)), UINT8, 0, 1, 1},
  {"KEYRAH_MODE", (void*)(&(mist_cfg.keyrah_mode)), UINT32, 0, 0xFFFFFFFF, 1},
  {"RESET_COMBO", (void*)(&(mist_cfg.reset_combo)), UINT8, 0, 2, 1},
  {"SCANDOUBLER_DISABLE", (void*)(&(mist_cfg.scandoubler_disable)), UINT8, 0, 1, 1},
  {"MOUSE_BOOT_MODE", (void*)(&(mist_cfg.mouse_boot_mode)), UINT8, 0, 1, 1},
  {"JOYSTICK_IGNORE_HAT", (void*)(&(mist_cfg.joystick_ignore_hat)), UINT8, 0, 1, 1},
  {"JOYSTICK_DISABLE_SHORTCUTS", (void*)(&(mist_cfg.joystick_disable_shortcuts)), UINT8, 0, 1, 1},
  {"JOYSTICK_IGNORE_OSD", (void*)(&(mist_cfg.joystick_ignore_osd)), UINT8, 0, 1, 1},
  {"JOYSTICK0_PREFER_DB9", (void*)(&(mist_cfg.joystick0_prefer_db9)), UINT8, 0, 1, 1},
  {"KEY_MENU_AS_RGUI", (void*)(&(mist_cfg.key_menu_as_rgui)), UINT8, 0, 1, 1},
  {"KEY_REMAP", (void*)user_io_key_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"HID_BUTTON_REMAP", (void*)hid_joystick_button_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"JOYSTICK_REMAP", (void*)virtual_joystick_remap, CUSTOM_HANDLER, 0, 0, 1},
  {"JOY_KEY_MAP", (void*)joystick_key_map, CUSTOM_HANDLER, 0, 0, 1},
  {"ROM", (void*)ini_rom_upload, CUSTOM_HANDLER, 0, 0, 1}
}; 

// mist ini config
const ini_cfg_t mist_ini_cfg = {
  "MIST    INI",
  mist_ini_sections,
  mist_ini_vars,
  (int)(sizeof(mist_ini_sections) / sizeof(ini_section_t)),
  (int)(sizeof(mist_ini_vars)     / sizeof(ini_var_t))
};

