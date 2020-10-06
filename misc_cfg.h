// misc_cfg.h


#ifndef __MISC_CFG_H__
#define __MISC_CFG_H__


//// type definitions ////
typedef struct {
  uint8_t kick1x_memory_detection_patch;
  uint8_t clock_freq;
  char conf_name[5][11];
} minimig_cfg_t;

typedef struct {
  char conf_name[5][11];
} atarist_cfg_t;


//// global variables ////
extern minimig_cfg_t minimig_cfg;
extern atarist_cfg_t atarist_cfg;


#endif // __MISC_CFG_H__
