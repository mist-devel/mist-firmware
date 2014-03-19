#ifndef ASIX_H
#define ASIX_H

#include <stdbool.h>
#include <inttypes.h>

typedef struct {
  ep_t ep;
  uint16_t phy_id;
  uint32_t qNextPollTime;     // next poll time
  bool bPollEnable;
} usb_asix_info_t;

// interface to usb core
extern const usb_device_class_config_t usb_asix_class;

#endif // ASIX_H
