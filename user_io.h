/*
 * user_io.h
 *
 */

#ifndef USER_IO_H
#define USER_IO_H

#define UIO_STATUS     0x00
#define UIO_BUT_SW     0x01

// codes as used by minimig (amiga)
#define UIO_JOYSTICK0  0x02
#define UIO_JOYSTICK1  0x03
#define UIO_MOUSE      0x04
#define UIO_KEYBOARD   0x05
#define UIO_KBD_OSD    0x06  // keycodes used by OSD only

// codes as used by MiST (atari)
#define UIO_IKBD_OUT   0x02
#define UIO_IKBD_IN    0x03
#define UIO_SERIAL_OUT 0x04
#define UIO_SERIAL_IN  0x05

#define JOY_RIGHT      0x01
#define JOY_LEFT       0x02
#define JOY_DOWN       0x04
#define JOY_UP         0x08
#define JOY_BTN1       0x10
#define JOY_BTN2       0x20
#define JOY_MOVE       (JOY_RIGHT|JOY_LEFT|JOY_UP|JOY_DOWN)

#define BUTTON1        0x01
#define BUTTON2        0x02
#define SWITCH1        0x04
#define SWITCH2        0x08

// core type value should be unlikely to be returned by broken cores
#define CORE_TYPE_UNKNOWN   0x55
#define CORE_TYPE_DUMB      0xa0
#define CORE_TYPE_MINIMIG   0xa1
#define CORE_TYPE_PACE      0xa2
#define CORE_TYPE_MIST      0xa3

void user_io_init();
void user_io_detect_core_type();
unsigned char user_io_core_type();
void user_io_poll();
int user_io_button_pressed();
void user_io_osd_key_enable(char);

// hooks from the usb layer
void user_io_mouse(unsigned char b, char x, char y);
void user_io_kbd(unsigned char m, unsigned char *k); 

#endif // USER_IO_H
