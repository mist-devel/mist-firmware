// mist_cfg.h
// 2015, rok.krajnc@gmail.com


#ifndef __MIST_CFG_H__
#define __MIST_CFG_H__


//// includes ////
#include <inttypes.h>
#include "ini_parser.h"
#include "misc_cfg.h"


//// type definitions ////
typedef struct {
  uint32_t keyrah_mode;
  uint8_t scandoubler_disable;
  uint8_t csync_disable;
  uint8_t mouse_boot_mode;
  uint8_t mouse_speed;
  uint8_t joystick_ignore_hat;
  uint8_t joystick_ignore_osd;
  uint8_t joystick_disable_shortcuts;
  uint8_t joystick0_prefer_db9;
  uint8_t joystick_db9_fixed_index;
  uint8_t joystick_emu_fixed_index;
  uint8_t joystick_analog_mult;
  uint8_t joystick_autofire_combo;
  uint8_t joystick_disable_swap;
  uint8_t joystick_dead_range;
  int8_t joystick_analog_offset;
#ifdef JOY_DB9_MD
  uint8_t joystick_db9_md;
#endif
  uint8_t key_menu_as_rgui;
  uint8_t reset_combo;
  uint8_t ypbpr;
  uint8_t keep_video_mode;
  uint8_t led_animation;
  uint8_t sdram64;
  uint8_t amiga_mod_keys;
  uint8_t usb_storage;
} mist_cfg_t;


//// functions ////
void mist_ini_parse();


//// global variables ////
extern const ini_cfg_t mist_ini_cfg;
extern mist_cfg_t mist_cfg;


#endif // __MIST_CFG_H__
