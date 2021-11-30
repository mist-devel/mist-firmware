#ifndef MINIMIG_MENU_H
#define MINIMIG_MENU_H

// UI strings, used by boot messages
extern const char *config_filter_msg[];
extern const char *config_memory_chip_msg[];
extern const char *config_memory_slow_msg[];
extern const char *config_memory_fast_msg[];
extern const char *config_scanline_msg[];
extern const char *config_hdf_msg[];
extern const char *config_chipset_msg[];
const char *config_memory_fast_txt();

void SetupMinimigMenu();

#endif
