// http://wiki.amigaos.net/index.php/Keymap_Library
// http://www.win.tue.nl/~aeb/linux/kbd/scancodes-14.html

#include "osd.h"

#define MISS  0xff
#define KEYCODE_MAX (0x6f)

// The original minimig had the keyboard connected to the FPGA. Thus all key events (even for OSD)
// came from the FPGA core. The MIST has the keyboard attached to the arm controller. To be compatible
// with the minimig core all keys (incl. OSD!) are forwarded to the FPGA and the OSD keys are returned.
// These keys are tagged with the "OSD" flag
// The atari/mist core does not forwards keys through the FPGA but queues them inside the arm controller.
// These keys are tagged with the "OSD_LOC" flag. The keycodes itself used are identical between OSD
// and OSD_LOC

#define OSD               0x0100     // to be used by OSD, not the core itself
#define OSD_LOC           0x0200     // OSD key not forwarded to core, but queued in arm controller
#define CAPS_LOCK_TOGGLE  0x0400     // caps lock toggle behaviour
#define NUM_LOCK_TOGGLE   0x0800


// amiga unmapped: 
// 0x0d |
// 0x5a KP-(
// 0x5b KP-)
// codes >= 0x69 are for OSD only and are not sent to the amiga itself

// keycode translation table
const unsigned short usb2ami[] = {
  MISS,  // 00: NoEvent
  MISS,  // 01: Overrun Error
  MISS,  // 02: POST fail
  MISS,  // 03: ErrorUndefined
  0x20,  // 04: a
  0x35,  // 05: b
  0x33,  // 06: c
  0x22,  // 07: d
  0x12,  // 08: e
  0x23,  // 09: f
  0x24,  // 0a: g
  0x25,  // 0b: h
  0x17,  // 0c: i
  0x26,  // 0d: j
  0x27,  // 0e: k
  0x28,  // 0f: l
  0x37,  // 10: m
  0x36,  // 11: n
  0x18,  // 12: o
  0x19,  // 13: p
  0x10,  // 14: q
  0x13,  // 15: r
  0x21,  // 16: s
  0x14,  // 17: t
  0x16,  // 18: u
  0x34,  // 19: v
  0x11,  // 1a: w
  0x32,  // 1b: x
  0x15,  // 1c: y
  0x31,  // 1d: z
  0x01,  // 1e: 1
  0x02,  // 1f: 2
  0x03,  // 20: 3
  0x04,  // 21: 4
  0x05,  // 22: 5
  0x06,  // 23: 6
  0x07,  // 24: 7
  0x08,  // 25: 8
  0x09,  // 26: 9
  0x0a,  // 27: 0
  0x44,  // 28: Return
  0x45,  // 29: Escape
  0x41,  // 2a: Backspace
  0x42,  // 2b: Tab
  0x40,  // 2c: Space
  0x0b,  // 2d: -
  0x0c,  // 2e: =
  0x1a,  // 2f: [
  0x1b,  // 30: ]
  MISS,  // 31: backslash
  0x2b,  // 32: Europe 1
  0x29,  // 33: ; 
  0x2a,  // 34: '
  0x00,  // 35: `
  0x38,  // 36: ,
  0x39,  // 37: .
  0x3a,  // 38: /
  0x62 | CAPS_LOCK_TOGGLE,  // 39: Caps Lock
  0x50,  // 3a: F1
  0x51,  // 3b: F2
  0x52,  // 3c: F3
  0x53,  // 3d: F4
  0x54,  // 3e: F5
  0x55,  // 3f: F6
  0x56,  // 40: F7
  0x57,  // 41: F8
  0x58,  // 42: F9
  0x59,  // 43: F10
  0x5f,  // 44: F11
  0x69 | OSD,  // 45: F12 (OSD)
  0x6e | OSD,  // 46: Print Screen (OSD)
  NUM_LOCK_TOGGLE,  // 47: Scroll Lock (OSD)
  MISS,  // 48: Pause
  MISS,  // 49: Insert
  MISS,  // 4a: Home
  0x6c | OSD,  // 4b: Page Up (OSD)
  0x46,  // 4c: Delete
  MISS,  // 4d: End
  0x6d | OSD,  // 4e: Page Down (OSD)
  0x4e,  // 4f: Right Arrow
  0x4f,  // 50: Left Arrow
  0x4d,  // 51: Down Arrow
  0x4c,  // 52: Up Arrow
  NUM_LOCK_TOGGLE,  // 53: Num Lock
  0x5c,  // 54: KP /
  0x5d,  // 55: KP *
  0x4a,  // 56: KP -
  0x5e,  // 57: KP +
  0x43,  // 58: KP Enter
  0x1d,  // 59: KP 1
  0x1e,  // 5a: KP 2
  0x1f,  // 5b: KP 3
  0x2d,  // 5c: KP 4
  0x2e,  // 5d: KP 5
  0x2f,  // 5e: KP 6
  0x3d,  // 5f: KP 7
  0x3e,  // 60: KP 8
  0x3f,  // 61: KP 9
  0x0f,  // 62: KP 0
  0x3c,  // 63: KP .
  0x30,  // 64: Europe 2
  0x69 | OSD,  // 65: App
  MISS,  // 66: Power
  MISS,  // 67: KP =
  MISS,  // 68: F13
  MISS,  // 69: F14
  MISS,  // 6a: F15
  MISS,  // 6b: F16
  MISS,  // 6c: F17
  MISS,  // 6d: F18
  MISS,  // 6e: F19
  MISS   // 6f: F20
};

// unmapped atari keys:
// 0x63   KP (
// 0x64   KP )

// keycode translation table for atari
const unsigned short usb2atari[] = {
  MISS,  // 00: NoEvent
  MISS,  // 01: Overrun Error
  MISS,  // 02: POST fail
  MISS,  // 03: ErrorUndefined
  0x1e,  // 04: a
  0x30,  // 05: b
  0x2e,  // 06: c
  0x20,  // 07: d
  0x12,  // 08: e
  0x21,  // 09: f
  0x22,  // 0a: g
  0x23,  // 0b: h
  0x17,  // 0c: i
  0x24,  // 0d: j
  0x25,  // 0e: k
  0x26,  // 0f: l
  0x32,  // 10: m
  0x31,  // 11: n
  0x18,  // 12: o
  0x19,  // 13: p
  0x10,  // 14: q
  0x13,  // 15: r
  0x1f,  // 16: s
  0x14,  // 17: t
  0x16,  // 18: u
  0x2f,  // 19: v
  0x11,  // 1a: w
  0x2d,  // 1b: x
  0x15,  // 1c: y
  0x2c,  // 1d: z
  0x02,  // 1e: 1
  0x03,  // 1f: 2
  0x04,  // 20: 3
  0x05,  // 21: 4
  0x06,  // 22: 5
  0x07,  // 23: 6
  0x08,  // 24: 7
  0x09,  // 25: 8
  0x0a,  // 26: 9
  0x0b,  // 27: 0
  0x1c,  // 28: Return
  0x01,  // 29: Escape
  0x0e,  // 2a: Backspace
  0x0f,  // 2b: Tab
  0x39,  // 2c: Space
  0x0c,  // 2d: -
  0x0d,  // 2e: =
  0x1a,  // 2f: [
  0x1b,  // 30: ]
  0x2b,  // 31: backslash
  MISS,  // 32: Europe 1
  0x27,  // 33: ; 
  0x28,  // 34: '
  0x29,  // 35: `
  0x33,  // 36: ,
  0x34,  // 37: .
  0x35,  // 38: /
  0x3a | CAPS_LOCK_TOGGLE,  // 39: Caps Lock
  0x3b,  // 3a: F1
  0x3c,  // 3b: F2
  0x3d,  // 3c: F3
  0x3e,  // 3d: F4
  0x3f,  // 3e: F5
  0x40,  // 3f: F6
  0x41,  // 40: F7
  0x42,  // 41: F8
  0x43,  // 42: F9
  0x44,  // 43: F10
  MISS,  // 44: F11
  OSD_LOC,  // 45: F12
  MISS,  // 46: Print Screen
  NUM_LOCK_TOGGLE,  // 47: Scroll Lock
  MISS,  // 48: Pause
  0x52,  // 49: Insert
  0x47,  // 4a: Home
  0x62,  // 4b: Page Up
  0x53,  // 4c: Delete
  MISS,  // 4d: End
  0x61,  // 4e: Page Down
  0x4d,  // 4f: Right Arrow
  0x4b,  // 50: Left Arrow
  0x50,  // 51: Down Arrow
  0x48,  // 52: Up Arrow
  NUM_LOCK_TOGGLE,  // 53: Num Lock
  0x65,  // 54: KP /
  0x66,  // 55: KP *
  0x4a,  // 56: KP -
  0x4e,  // 57: KP +
  0x72,  // 58: KP Enter
  0x6d,  // 59: KP 1
  0x6e,  // 5a: KP 2
  0x6f,  // 5b: KP 3
  0x6a,  // 5c: KP 4
  0x6b,  // 5d: KP 5
  0x6c,  // 5e: KP 6
  0x67,  // 5f: KP 7
  0x68,  // 60: KP 8
  0x69,  // 61: KP 9
  0x70,  // 62: KP 0
  0x71,  // 63: KP .
  0x60,  // 64: Europe 2
  OSD_LOC, // 65: App
  MISS,  // 66: Power
  MISS,  // 67: KP =
  MISS,  // 68: F13
  MISS,  // 69: F14
  MISS,  // 6a: F15
  MISS,  // 6b: F16
  MISS,  // 6c: F17
  MISS,  // 6d: F18
  MISS,  // 6e: F19
  MISS   // 6f: F20
};

#if 0
  // #define KEY_UPSTROKE     0x80
#define KEY_MENU         0x69
#define KEY_PGUP         0x6C
#define KEY_PGDN         0x6D
#define KEY_HOME         0x6A
#define KEY_ESC          0x45
#define KEY_ENTER        0x44
#define KEY_BACK         0x41
#define KEY_SPACE        0x40
#define KEY_UP           0x4C
#define KEY_DOWN         0x4D
#define KEY_LEFT         0x4F
#define KEY_RIGHT        0x4E
#define KEY_F1           0x50
#define KEY_F2           0x51
#define KEY_F3           0x52
#define KEY_F4           0x53
#define KEY_F5           0x54
#define KEY_F6           0x55
#define KEY_F7           0x56
#define KEY_F8           0x57
#define KEY_F9           0x58
#define KEY_F10          0x59
#define KEY_CTRL         0x63
#define KEY_LALT         0x64
#define KEY_KPPLUS       0x5E
#define KEY_KPMINUS      0x4A
#define KEY_KP0          0x0F
#endif
