/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef USBDEV_H
#define USBDEV_H

#include <inttypes.h>
#include "samv71.h"

#define USB_HS 1

#if USB_HS
#define EP0_SIZE 64
#define EP0_SIZE_CONF USBHS_DEVEPTCFG_EPSIZE_64_BYTE
#define BULK_SIZE 512
#define BULK_SIZE_CONF USBHS_DEVEPTCFG_EPSIZE_512_BYTE
#define INTERVAL 0x0B
#else
#define EP0_SIZE 8
#define EP0_SIZE_CONF USBHS_DEVEPTCFG_EPSIZE_8_BYTE
#define BULK_SIZE 64
#define BULK_SIZE_CONF USBHS_DEVEPTCFG_EPSIZE_64_BYTE
#define INTERVAL 0xFF
#endif

#define BULK_IN_SIZE  BULK_SIZE
#define BULK_OUT_SIZE BULK_SIZE

void usb_dev_open(void);
void usb_dev_reconnect(void);

uint8_t  usb_cdc_is_configured(void);
uint16_t usb_cdc_write(const char *pData, uint16_t length);
uint16_t usb_cdc_read(char *pData, uint16_t length);

uint8_t  usb_storage_is_configured(void);
uint16_t usb_storage_write(const char *pData, uint16_t length);
uint16_t usb_storage_read(char *pData, uint16_t length);

#endif // USBDEV_H
