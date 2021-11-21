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

/*
 Based on the work of

 Copyright (C) 2012 Kristian Lauszus, TKJ Electronics. All rights reserved.
 */

#ifndef _xboxusb_h_
#define _xboxusb_h_

/* Data Xbox 360 taken from descriptors */
#define XBOX_EP_MAXPKTSIZE       32 // max size for data via USB

/* Names we give to the 3 Xbox360 pipes */
#define XBOX_CONTROL_PIPE    0
#define XBOX_INPUT_PIPE      1
#define XBOX_OUTPUT_PIPE     2

// PID and VID of the different devices
#define XBOX_VID                                0x045E // Microsoft Corporation
#define MADCATZ_VID                             0x1BAD // For unofficial Mad Catz controllers
#define JOYTECH_VID                             0x162E // For unofficial Joytech controllers
#define GAMESTOP_VID                            0x0E6F // Gamestop controller

#define XBOX_WIRED_PID                          0x028E // Microsoft 360 Wired controller
#define XBOX_WIRELESS_PID                       0x028F // Wireless controller only support charging
#define XBOX_WIRELESS_RECEIVER_PID              0x0719 // Microsoft Wireless Gaming Receiver
#define XBOX_WIRELESS_RECEIVER_THIRD_PARTY_PID  0x0291 // Third party Wireless Gaming Receiver
#define MADCATZ_WIRED_PID                       0xF016 // Mad Catz wired controller
#define JOYTECH_WIRED_PID                       0xBEEF // For Joytech wired controller
#define GAMESTOP_WIRED_PID                      0x0401 // Gamestop wired controller
#define AFTERGLOW_WIRED_PID                     0x0213 // Afterglow wired controller - it uses the same VID as a Gamestop controller

#define XBOX_REPORT_BUFFER_SIZE 14 // Size of the input report buffer

#define XBOX_MAX_ENDPOINTS   3

typedef struct {
	bool     bPollEnable;    // poll enable flag
	uint8_t  interval;
	uint32_t oldButtons;
	uint32_t qNextPollTime;  // next poll time
	ep_t     inEp;
	uint16_t jindex;
} usb_xbox_info_t;

// interface to usb core
extern const usb_device_class_config_t usb_xbox_class;

#endif
