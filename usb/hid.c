#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "usb.h"
#include "max3421e.h"
#include "timer.h"
#include "hidparser.h"
#include "debug.h"
#include "joymapping.h"
#include "../user_io.h"
#include "../hardware.h"
#include "../mist_cfg.h"
#include "../osd.h"
#include "../state.h"


static unsigned char kbd_led_state = 0;  // default: all leds off
static unsigned char joysticks = 0;      // number of detected usb joysticks

// up to 8 buttons can be remapped
#define MAX_JOYSTICK_BUTTON_REMAP 8

/*****************************************************************************/
//NOTE: the below mapping is hardware buttons to USB,
//      not to be confused with USB HID -> Virtual Keyboard
//      The purpose of the below is to overcome hardware problems e.g. :
//			- some controllers have buttons that are always on, so this allows to ignore them
//			- the layout of physical buttons might be random
//      In general it's easier to use virtual joystick mapping, but this gives a lower-level of control if needed.
//
static struct {
  uint16_t vid;   // vendor id
  uint16_t pid;   // product id
  uint8_t offset; // bit index within report
  uint8_t button; // joystick button to be reported
} joystick_button_remap[MAX_JOYSTICK_BUTTON_REMAP];
  
void hid_joystick_button_remap_init(void) {
  memset(joystick_button_remap, 0, sizeof(joystick_button_remap));
}

void hid_joystick_button_remap(char *s) {
  uint8_t i;

  hid_debugf("%s(%s)", __FUNCTION__, s);

  if(strlen(s) < 13) {
    hid_debugf("malformed entry");
    return;
  }

  // parse remap request
  for(i=0;i<MAX_JOYSTICK_BUTTON_REMAP;i++) {
    if(!joystick_button_remap[i].vid) {
      // first two entries are comma seperated 
      joystick_button_remap[i].vid = strtol(s, NULL, 16);
      joystick_button_remap[i].pid = strtol(s+5, NULL, 16);
      joystick_button_remap[i].offset = strtol(s+10, NULL, 10);
      // search for next comma
      s+=10; while(*s && (*s != ',')) s++; s++;
      joystick_button_remap[i].button = strtol(s, NULL, 10);
      
      hid_debugf("parsed: %x/%x %d -> %d", 
		 joystick_button_remap[i].vid, joystick_button_remap[i].pid,
		 joystick_button_remap[i].offset, joystick_button_remap[i].button);
      
      return;
    }
  }
}

/*****************************************************************************/


uint8_t hid_get_joysticks(void) {
  return joysticks;
}

//get HID report descriptor 
static uint8_t hid_get_report_descr(usb_device_t *dev, uint8_t i, uint16_t size)  {
  //  hid_debugf("%s(%x, if=%d, size=%d)", __FUNCTION__, dev->bAddress, iface, size);

  uint8_t buf[size];
  usb_hid_info_t *info = &(dev->hid_info);
  uint8_t rcode = usb_ctrl_req( dev, HID_REQ_HIDREPORT, USB_REQUEST_GET_DESCRIPTOR, 0x00, 
			      HID_DESCRIPTOR_REPORT, info->iface[i].iface_idx, size, buf);
 
  if(!rcode) {
    hid_debugf("HID report descriptor:");
    hexdump(buf, size, 0);

    // we got a report descriptor. Try to parse it
    if(parse_report_descriptor(buf, size, &(info->iface[i].conf))) {
      if(info->iface[i].conf.type == REPORT_TYPE_JOYSTICK) {
				hid_debugf("Detected USB joystick #%d", joysticks);
				info->iface[i].device_type = HID_DEVICE_JOYSTICK;
				info->iface[i].jindex = joysticks++;
				StateNumJoysticksSet(joysticks);			
      }
    } else {
      // parsing failed. Fall back to boot mode for mice
      if(info->iface[i].conf.type == REPORT_TYPE_MOUSE) {
				hid_debugf("Failed to parse mouse, try using boot mode");
				info->iface[i].ignore_boot_mode = false;
      }
    }
  }
    
  return rcode;
}

static uint8_t hid_set_idle(usb_device_t *dev, uint8_t iface, uint8_t reportID, uint8_t duration ) {
  //  hid_debugf("%s(%x, if=%d id=%d, dur=%d)", __FUNCTION__, dev->bAddress, iface, reportID, duration);

  return( usb_ctrl_req( dev, HID_REQ_HIDOUT, HID_REQUEST_SET_IDLE, reportID, 
		       duration, iface, 0x0000, NULL));
}

static uint8_t hid_set_protocol(usb_device_t *dev, uint8_t iface, uint8_t protocol) {
  //  hid_debugf("%s(%x, if=%d proto=%d)", __FUNCTION__, dev->bAddress, iface, protocol);

  return( usb_ctrl_req( dev, HID_REQ_HIDOUT, HID_REQUEST_SET_PROTOCOL, protocol, 
		       0x00, iface, 0x0000, NULL));
}

static uint8_t hid_set_report(usb_device_t *dev, uint8_t iface, uint8_t report_type, uint8_t report_id, 
			      uint16_t nbytes, uint8_t* dataptr ) {
  //  hid_debugf("%s(%x, if=%d data=%x)", __FUNCTION__, dev->bAddress, iface, dataptr[0]);

  return( usb_ctrl_req(dev, HID_REQ_HIDOUT, HID_REQUEST_SET_REPORT, report_id, 
		       report_type, iface, nbytes, dataptr));
}

/* todo: handle parsing in chunks */
static uint8_t usb_hid_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
  usb_hid_info_t *info = &(dev->hid_info);
  uint8_t rcode;
  bool isGoodInterface = false;

  union buf_u {
    usb_configuration_descriptor_t conf_desc;
    usb_interface_descriptor_t iface_desc;
    usb_endpoint_descriptor_t ep_desc;
    usb_hid_descriptor_t hid_desc;
    uint8_t raw[len];
  } buf, *p;

  // usb_interface_descriptor

  if(rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)) 
    return rcode;

  /* scan through all descriptors */
  p = &buf;
  while(len > 0) {
    switch(p->conf_desc.bDescriptorType) {
    case USB_DESCRIPTOR_CONFIGURATION:
      // hid_debugf("conf descriptor size %d", p->conf_desc.bLength);
      // we already had this, so we simply ignore it
      break;

    case USB_DESCRIPTOR_INTERFACE:
      isGoodInterface = false;
      // hid_debugf("iface descriptor size %d", p->iface_desc.bLength);

      /* check the interface descriptors for supported class */

      // only HID interfaces are supported
      if(p->iface_desc.bInterfaceClass == USB_CLASS_HID) {
	//	puts("iface is HID");

	if(info->bNumIfaces < MAX_IFACES) {
	  // ok, let's use this interface
	  isGoodInterface = true;

	  info->iface[info->bNumIfaces].iface_idx = p->iface_desc.bInterfaceNumber;
	  info->iface[info->bNumIfaces].ignore_boot_mode = false;
	  info->iface[info->bNumIfaces].has_boot_mode = false;
	  info->iface[info->bNumIfaces].is_5200daptor = false;
	  info->iface[info->bNumIfaces].key_state = 0;
	  info->iface[info->bNumIfaces].device_type = HID_DEVICE_UNKNOWN;
	  info->iface[info->bNumIfaces].conf.type = REPORT_TYPE_NONE;

	  if(p->iface_desc.bInterfaceSubClass == HID_BOOT_INTF_SUBCLASS) {
	    // hid_debugf("Iface %d is Boot sub class", info->bNumIfaces);
	    info->iface[info->bNumIfaces].has_boot_mode = true;
	  }
	  
	  switch(p->iface_desc.bInterfaceProtocol) {
	  case HID_PROTOCOL_NONE:
	    hid_debugf("HID protocol is NONE");
	    break;
	    
	  case HID_PROTOCOL_KEYBOARD:
	    hid_debugf("HID protocol is KEYBOARD");
	    info->iface[info->bNumIfaces].device_type = HID_DEVICE_KEYBOARD;
	    break;
	    
	  case HID_PROTOCOL_MOUSE:
	    hid_debugf("HID protocol is MOUSE");
	    // don't use boot mode for mice unless it's explicitey requested in mist.ini
	    if(!mist_cfg.mouse_boot_mode)
	      info->iface[info->bNumIfaces].ignore_boot_mode = true;

	    info->iface[info->bNumIfaces].device_type = HID_DEVICE_MOUSE;
	    break;
	    
	  default:
	    hid_debugf("HID protocol is %d", p->iface_desc.bInterfaceProtocol);
	    break;
	  }
	}
      }
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      //      hid_debugf("endpoint descriptor size %d", p->ep_desc.bLength);

      if(isGoodInterface) {

	// only interrupt in endpoints are supported
	if ((p->ep_desc.bmAttributes & 0x03) == 3 && (p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
	  hid_debugf("endpoint %d, interval = %dms", 
		  p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.bInterval);

	  // Fill in the endpoint info structure
	  uint8_t epidx = info->bNumIfaces;
	  info->iface[epidx].interval = p->ep_desc.bInterval;
	  info->iface[epidx].ep.epAddr	 = (p->ep_desc.bEndpointAddress & 0x0F);
	  info->iface[epidx].ep.maxPktSize = p->ep_desc.wMaxPacketSize[0];
	  info->iface[epidx].ep.epAttribs	 = 0;
	  info->iface[epidx].ep.bmNakPower = USB_NAK_NOWAIT;
	  info->bNumIfaces++;
	}
      }
      break;

    case HID_DESCRIPTOR_HID:
      hid_debugf("hid descriptor size %d", p->ep_desc.bLength);

      if(isGoodInterface) {
	// we need a report descriptor
	if(p->hid_desc.bDescrType == HID_DESCRIPTOR_REPORT) {
	  uint16_t len = p->hid_desc.wDescriptorLength[0] + 
	    256 * p->hid_desc.wDescriptorLength[1];
	  hid_debugf(" -> report descriptor size = %d", len);
	  
	  info->iface[info->bNumIfaces].report_desc_size = len;
	}
      }
      break;

    default:
      hid_debugf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
    }

    // advance to next descriptor
    len -= p->conf_desc.bLength;
    p = (union buf_u*)(p->raw + p->conf_desc.bLength);
  }
  
  if(len != 0) {
    hid_debugf("Config underrun: %d", len);
    return USB_ERROR_CONFIGURAION_SIZE_MISMATCH;
  }

  return 0;
}

static uint8_t usb_hid_init(usb_device_t *dev) {
  hid_debugf("%s(%x)", __FUNCTION__, dev->bAddress);

  uint8_t rcode;
  uint8_t i;
  uint16_t vid, pid;

  usb_hid_info_t *info = &(dev->hid_info);
  
  union {
    usb_device_descriptor_t dev_desc;
    usb_configuration_descriptor_t conf_desc;
  } buf;

  // reset status
  info->bPollEnable = false;
  info->bNumIfaces = 0;

  for(i=0;i<MAX_IFACES;i++) {
    info->iface[i].qNextPollTime = 0;
    info->iface[i].ep.epAddr	 = i;
    info->iface[i].ep.maxPktSize = 8;
    info->iface[i].ep.epAttribs	 = 0;
    info->iface[i].ep.bmNakPower = USB_NAK_MAX_POWER;
  }

  // try to re-read full device descriptor from newly assigned address
  if(rcode = usb_get_dev_descr( dev, sizeof(usb_device_descriptor_t), &buf.dev_desc ))
    return rcode;

  // save vid/pid for automatic hack later
  vid = buf.dev_desc.idVendor;
  pid = buf.dev_desc.idProduct;

  uint8_t num_of_conf = buf.dev_desc.bNumConfigurations;
  //  hid_debugf("number of configurations: %d", num_of_conf);

  for(i=0; i<num_of_conf; i++) {
    if(rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &buf.conf_desc)) 
      return rcode;
    
    //    hid_debugf("conf descriptor %d has total size %d", i, buf.conf_desc.wTotalLength);

    // parse directly if it already fitted completely into the buffer
    usb_hid_parse_conf(dev, i, buf.conf_desc.wTotalLength);
  }

  // check if we found valid hid interfaces
  if(!info->bNumIfaces) {
    hid_debugf("no hid interfaces found");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  // Set Configuration Value
  rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);

  // process all supported interfaces
  for(i=0; i<info->bNumIfaces; i++) {

    info->iface[i].conf.pid = pid;
    info->iface[i].conf.vid = vid;

    // no boot mode, try to parse HID report descriptor
    // when running archie core force the usage of the HID descriptor as 
    // boot mode only supports two buttons and the archie wants three
    if(!info->iface[i].has_boot_mode || info->iface[i].ignore_boot_mode) {
      rcode = hid_get_report_descr(dev, i, info->iface[i].report_desc_size);
      if(rcode) return rcode;

      if(info->iface[i].device_type == REPORT_TYPE_NONE) {
	// bInterfaceProtocol was 0 ("none") -> try to parse anyway
	iprintf("HID NONE: report type = %d, size = %d\n",
		info->iface[i].conf.type, info->iface[i].conf.report_size);

	// currently we only use this to support "report type "REPORT_TYPE_KEYBOARD" which in turn
	// allows arduinos to be used as keyboards
	if((info->iface[i].conf.type == REPORT_TYPE_KEYBOARD) &&
	   (info->iface[i].conf.report_size == 8)) {
	  iprintf("HID NONE: is keyboard (arduino?)\n");
	  info->iface[i].device_type = REPORT_TYPE_KEYBOARD;
	  info->iface[i].has_boot_mode = true;   // assume that the report is boot mode style as it's 8 bytes in size
	}
      }

      if(info->iface[i].device_type == REPORT_TYPE_MOUSE) {
	iprintf("MOUSE: report type = %d, id = %d, size = %d\n", 
		info->iface[i].conf.type,
		info->iface[i].conf.report_id,
		info->iface[i].conf.report_size);
      }

      if(info->iface[i].device_type == REPORT_TYPE_JOYSTICK) {
	char k;
        
	iprintf("JOYSTICK: report type = %d, id = %d, size = %d\n", 
		info->iface[i].conf.type,
		info->iface[i].conf.report_id,
		info->iface[i].conf.report_size);
	
	for(k=0;k<2;k++)
	  iprintf("Axis%d: %d@%d %d->%d\n", k, 
		  info->iface[i].conf.joystick_mouse.axis[k].size,
		  info->iface[i].conf.joystick_mouse.axis[k].offset/8,
		  info->iface[i].conf.joystick_mouse.axis[k].logical.min,
		  info->iface[i].conf.joystick_mouse.axis[k].logical.max);
	
	for(k=0;k<4;k++)
	  iprintf("Button%d: @%d/%d\n", k,
		  info->iface[i].conf.joystick_mouse.button[k].byte_offset,
		  info->iface[i].conf.joystick_mouse.button[k].bitmask);
      }
      
      if((vid == 0x04d8) && (pid == 0xf6ec) && (i==0)) {
        iprintf("hacking 5200daptor\n");
            
        info->iface[0].conf.joystick_mouse.button[2].byte_offset = 4;
        info->iface[0].conf.joystick_mouse.button[2].bitmask = 0x40;    // "Reset"
        info->iface[0].conf.joystick_mouse.button[3].byte_offset = 4;
        info->iface[0].conf.joystick_mouse.button[3].bitmask = 0x10;    // "Start"

        info->iface[0].is_5200daptor = true;
      }
      
      // apply remap information from mist.ini if present
      uint8_t j;
      for(j=0;j<MAX_JOYSTICK_BUTTON_REMAP;j++) {
	if((joystick_button_remap[j].vid == vid) && (joystick_button_remap[j].pid == pid)) {
	  uint8_t but = joystick_button_remap[j].button;
	  info->iface[0].conf.joystick_mouse.button[but].byte_offset = joystick_button_remap[j].offset >> 3;
	  info->iface[0].conf.joystick_mouse.button[but].bitmask = 0x80 >> (joystick_button_remap[j].offset & 7);
	  iprintf("hacking from ini file %d %d -> %d\n", 
		  info->iface[0].conf.joystick_mouse.button[but].byte_offset, 
		  info->iface[0].conf.joystick_mouse.button[but].bitmask, but);
	}
      }
    }

    rcode = hid_set_idle(dev, info->iface[i].iface_idx, 0, 0);
    if (rcode && rcode != hrSTALL)
      return rcode;

    // enable boot mode if its not diabled
    if(info->iface[i].has_boot_mode && !info->iface[i].ignore_boot_mode) {
      hid_debugf("enabling boot mode");
      hid_set_protocol(dev, info->iface[i].iface_idx, HID_BOOT_PROTOCOL);
    } else
      hid_set_protocol(dev, info->iface[i].iface_idx, HID_RPT_PROTOCOL);
  }
  
  puts("HID configured");

  // update leds
  for(i=0;i<MAX_IFACES;i++)
    if(dev->hid_info.iface[i].device_type == HID_DEVICE_KEYBOARD)
      hid_set_report(dev, dev->hid_info.iface[i].iface_idx, 2, 0, 1, &kbd_led_state);

  //Logitech K400r : set F1-F12 as primary functions
  if((vid == 0x046d) && (pid == 0xc52b)) {
	  hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x03\x15\x00\x00\x00"); timer_delay_msec(100);
	  hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x0F\x15\x01\x00\x00"); timer_delay_msec(100);
	  hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x10\x15\x00\x00\x00"); timer_delay_msec(100);
  }

  info->bPollEnable = true;
  return 0;
}

static uint8_t usb_hid_release(usb_device_t *dev) {
  usb_hid_info_t *info = &(dev->hid_info);

  puts(__FUNCTION__);

  uint8_t i;
  // check if a joystick is released
  for(i=0;i<info->bNumIfaces;i++) {
    if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {
      uint8_t c_jindex = info->iface[i].jindex;
      hid_debugf("releasing joystick #%d, renumbering", c_jindex);

      // walk through all devices and search for sticks with a higher id

      // search for all joystick interfaces on all hid devices
      usb_device_t *dev = usb_get_devices();
      uint8_t j;
      for(j=0;j<USB_NUMDEVICES;j++) {
				if(dev[j].bAddress && (dev[j].class == &usb_hid_class)) {
					// search for joystick interfaces
					uint8_t k;
					for(k=0;k<MAX_IFACES;k++) {
						if(dev[j].hid_info.iface[k].device_type == HID_DEVICE_JOYSTICK) {
							if(dev[j].hid_info.iface[k].jindex > c_jindex) {
								hid_debugf("decreasing jindex of dev #%d from %d to %d", j, 
									dev[j].hid_info.iface[k].jindex, dev[j].hid_info.iface[k].jindex-1);
								dev[j].hid_info.iface[k].jindex--;
								StateUsbIdSet( dev[j].hid_info.iface[k].conf.vid, dev[j].hid_info.iface[k].conf.pid, dev[j].hid_info.iface[k].conf.joystick_mouse.button_count, dev[j].hid_info.iface[k].jindex);

							}
						}
					}
				}
      }
      // one less joystick in the system ...
      joysticks--;
      StateNumJoysticksSet(joysticks);
      if (joysticks < 6)
        StateUsbIdSet(0, 0, 0, joysticks);
    }
  }

  return 0;
}

// special 5200daptor button processing
static void handle_5200daptor(usb_hid_iface_info_t *iface, uint8_t *buf) {

  // list of buttons that are reported as keys
  static const struct {
    uint8_t byte_offset;   // offset of the byte within the report which the button bit is in
    uint8_t mask;          // bitmask of the button bit
    uint8_t key_code[2];   // usb keycodes to be sent for joystick 0 and joystick 1
  } button_map[] = {
    { 4, 0x10, 0x3a, 0x3d }, /* START -> f1/f4 */
    { 4, 0x20, 0x3b, 0x3e }, /* PAUSE -> f2/f5 */
    { 4, 0x40, 0x3c, 0x3f }, /* RESET -> f3/f6 */
    { 5, 0x01, 0x1e, 0x21 }, /*     1 ->  1/4  */
    { 5, 0x02, 0x1f, 0x22 }, /*     2 ->  2/5  */
    { 5, 0x04, 0x20, 0x23 }, /*     3 ->  3/6  */
    { 5, 0x08, 0x14, 0x15 }, /*     4 ->  q/r  */
    { 5, 0x10, 0x1a, 0x17 }, /*     5 ->  w/t  */
    { 5, 0x20, 0x08, 0x1c }, /*     6 ->  e/y  */
    { 5, 0x40, 0x04, 0x09 }, /*     7 ->  a/f  */
    { 5, 0x80, 0x16, 0x0a }, /*     8 ->  s/g  */
    { 6, 0x01, 0x07, 0x0b }, /*     9 ->  d/h  */
    { 6, 0x02, 0x1d, 0x19 }, /*     * ->  z/v  */
    { 6, 0x04, 0x1b, 0x05 }, /*     0 ->  x/b  */
    { 6, 0x08, 0x06, 0x11 }, /*     # ->  c/n  */
    { 0, 0x00, 0x00, 0x00 }  /* ----  end ---- */
  };

  // keyboard events are only generated for the first
  // two joysticks in the system
  if(iface->jindex > 1) return;

  // build map of pressed keys
  uint8_t i;
  uint16_t keys = 0;
  for(i=0;button_map[i].mask;i++) 
    if(buf[button_map[i].byte_offset] & button_map[i].mask)
      keys |= (1<<i);

  // check if keys have changed
  if(iface->key_state != keys) {
    uint8_t buf[6] = { 0,0,0,0,0,0 };
    uint8_t p = 0;

    // report up to 6 pressed keys
    for(i=0;(i<16)&&(p<6);i++) 
      if(keys & (1<<i))
	buf[p++] = button_map[i].key_code[iface->jindex];

    //    iprintf("5200: %d %d %d %d %d %d\n", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

    // generate key events
    user_io_kbd(0x00, buf, UIO_PRIORITY_GAMEPAD, iface->conf.vid, iface->conf.pid); 

    // save current state of keys
    iface->key_state = keys;
  }
}

// collect bits from byte stream and assemble them into a signed word
static uint16_t collect_bits(uint8_t *p, uint16_t offset, uint8_t size, bool is_signed) {
  // mask unused bits of first byte
  uint8_t mask = 0xff << (offset&7);
  uint8_t byte = offset/8;
  uint8_t bits = size;
  uint8_t shift = offset&7;
  
  //  iprintf("0 m:%x by:%d bi=%d sh=%d ->", mask, byte, bits, shift);
  uint16_t rval = (p[byte++] & mask) >> shift;
  //  iprintf("%d\n", (int16_t)rval);
  mask = 0xff;
  shift = 8-shift;
  bits -= shift;

  // first byte already contained more bits than we need
  if(shift > size) {
    //    iprintf("  too many bits, masked %x ->", (1<<size)-1);
    // mask unused bits
    rval &= (1<<size)-1;
    //    iprintf("%d\n", (int16_t)rval);
  } else {
    // further bytes if required
    while(bits) {
      mask = (bits<8)?(0xff>>(8-bits)):0xff;
      //      iprintf("+ m:%x by:%d bi=%d sh=%d ->", mask, byte, bits, shift);
      rval += (p[byte++] & mask) << shift;
      //      iprintf("%d\n", (int16_t)rval);
      shift += 8;
      bits -= (bits>8)?8:bits;
    }
  }

  if(is_signed) {
    // do sign expansion
    uint16_t sign_bit = 1<<(size-1);
    if(rval & sign_bit) {
      while(sign_bit) {
	rval |= sign_bit;
	sign_bit <<= 1;
      }
      //      iprintf(" is negative -> sign expand to %d\n", (int16_t)rval);
    }
  }

  return rval;
}

static usb_hid_iface_info_t *virt_joy_kbd_iface = NULL;

/* processes a single USB interface */
static void usb_process_iface (usb_hid_iface_info_t *iface, 
							   uint16_t read, 
							   uint8_t *buf) {

  // successfully received some bytes
  if(iface->has_boot_mode && !iface->ignore_boot_mode) {
		if(iface->device_type == HID_DEVICE_MOUSE) {
			// boot mouse needs at least three bytes
			if(read >= 3)
				// forward all three bytes to the user_io layer
				user_io_mouse(buf[0], buf[1], buf[2]);
		}
		
		if(iface->device_type == HID_DEVICE_KEYBOARD) {
			// boot kbd needs at least eight bytes
			if(read >= 8) {
				user_io_kbd(buf[0], buf+2, UIO_PRIORITY_KEYBOARD, iface->conf.vid, iface->conf.pid);
			}
		}
  }

  // use more complex parser for all joysticks. Use it for mice only if 
  // it's explicitely stated not to use boot mode
  if((iface->device_type == HID_DEVICE_JOYSTICK)
	   || ((iface->device_type == HID_DEVICE_MOUSE) 
	   && iface->ignore_boot_mode)) {
	  
		hid_report_t *conf = &iface->conf;

	// check size of report. If a report id was given then one
	// additional byte is present with a matching report id
	if((read == conf->report_size+(conf->report_id?1:0)) && 
	  (!conf->report_id || (buf[0] == conf->report_id))) {
	 
			uint8_t btn = 0, jmap = 0;
			uint8_t btn_extra = 0;
			int16_t a[2];
			uint8_t idx, i;

			// skip report id if present
			uint8_t *p = buf+(conf->report_id?1:0);

			// hid_debugf("data:"); hexdump(buf, read, 0);
		
			// two axes ...
			for(i=0;i<2;i++) {
				// if logical minimum is > logical maximum then logical minimum 
				// is signed. This means that the value itself is also signed
				bool is_signed = conf->joystick_mouse.axis[i].logical.min > 
				conf->joystick_mouse.axis[i].logical.max;
				a[i] = collect_bits(p, conf->joystick_mouse.axis[i].offset, 
							conf->joystick_mouse.axis[i].size, is_signed);
			}
			
			// ... and four  first buttons
			for(i=0;i<4;i++)
				if(p[conf->joystick_mouse.button[i].byte_offset] & 
				 conf->joystick_mouse.button[i].bitmask) btn |= (1<<i);
			
			// ... and the eight extra buttons
			for(i=4;i<12;i++)
				if(p[conf->joystick_mouse.button[i].byte_offset] & 
				 conf->joystick_mouse.button[i].bitmask) btn_extra |= (1<<(i-4));

			//if (btn_extra != 0)
			//  iprintf("EXTRA BTNS:%d\n", btn_extra);
			 
				 
			// ---------- process mouse -------------
			if(iface->device_type == HID_DEVICE_MOUSE) {
				// iprintf("mouse %d %d %x\n", (int16_t)a[0], (int16_t)a[1], btn);
				// limit mouse movement to +/- 128
				for(i=0;i<2;i++) {
				if((int16_t)a[i] >  127) a[i] =  127;
				if((int16_t)a[i] < -128) a[i] = -128;
				}
				user_io_mouse(btn, a[0], a[1]);
			}

			// ---------- process joystick -------------
			if(iface->device_type == HID_DEVICE_JOYSTICK) {

				for(i=0;i<2;i++) {
				        int hrange = (conf->joystick_mouse.axis[i].logical.max - abs(conf->joystick_mouse.axis[i].logical.min)) / 2;
                                        int dead = hrange/63;
                                        
                                        if (a[i] < conf->joystick_mouse.axis[i].logical.min) a[i] = conf->joystick_mouse.axis[i].logical.min;
                                          else if (a[i] > conf->joystick_mouse.axis[i].logical.max) a[i] = conf->joystick_mouse.axis[i].logical.max;
					
					a[i] = a[i] - (abs(conf->joystick_mouse.axis[i].logical.min) + conf->joystick_mouse.axis[i].logical.max) / 2;
					
					hrange -= dead;
                                        if (a[i] < -dead) a[i] += dead;
                                        else if (a[i] > dead) a[i] -= dead;
                                        else a[i] = 0;
                                        
                                        a[i] = (a[i] * 127) / hrange;
                                        
                                        if (a[i] < -127) a[i] = -127;
                                         else if (a[i] > 127) a[i] = 127;
                                         
                                        a[i]=a[i]+127; // mist wants a value in the range [0..255]


				}

				// handle hat if present and overwrite any axis value
				if(conf->joystick_mouse.hat.size && !mist_cfg.joystick_ignore_hat) {
					uint8_t hat = collect_bits(p, conf->joystick_mouse.hat.offset, 
								 conf->joystick_mouse.hat.size, 0);

					 // we don't want more than 4 bits
					 uint8_t size = conf->joystick_mouse.hat.size;
					 while(size-- > 4) 
						 hat >>= 1;

					 //		  iprintf("HAT = %d\n", hat);

					 // TODO: Deal with 3 bit (4 direction/no diagonal) hats 
					 static const uint8_t hat2x[] = { 127,255,255,255,127,  0,  0,  0 };
					 static const uint8_t hat2y[] = {   0,  0,127,255,255,255,127,  0 };

					 if(hat&8) {
						 // hat is idle - don't override analog 
						 /*
						 if (a[0] > JOYSTICK_AXIS_TRIGGER_MIN) || a[0] < JOYSTICK_AXIS_TRIGGER_MAX) a[0] = JOYSTICK_AXIS_MID; 
						 if (a[1] > JOYSTICK_AXIS_TRIGGER_MIN) || a[1] < JOYSTICK_AXIS_TRIGGER_MAX) a[1] = JOYSTICK_AXIS_MID; 
						 */
					 } else {
						 uint8_t x_val = hat2x[hat];
						 uint8_t y_val = hat2y[hat];
						 // cancel out with X analog axis if it pushes on the opposite direction
						 if(x_val < JOYSTICK_AXIS_TRIGGER_MIN) {
							// hat pointing left, compensate if analog is pointing right
							if (a[0] > JOYSTICK_AXIS_TRIGGER_MAX) { a[0] = JOYSTICK_AXIS_MID; } 
							else a[0] = x_val;
						 } else {
							if(x_val > JOYSTICK_AXIS_TRIGGER_MAX) {
								// hat pointing right, compensate if analog pointing left
								if (a[0] < JOYSTICK_AXIS_TRIGGER_MIN) { a[0] = JOYSTICK_AXIS_MID; } 
								else a[0] = x_val; 
							}
						 }
						 // same logic for Y axis
						 if(y_val < JOYSTICK_AXIS_TRIGGER_MIN) {
							// hat pointing down
							if (a[1] > JOYSTICK_AXIS_TRIGGER_MAX) { a[1] = JOYSTICK_AXIS_MID; } 
							else a[1] = y_val;
						 } else {
							if(y_val > JOYSTICK_AXIS_TRIGGER_MAX) {
								// hat pointing up
								if (a[1] < JOYSTICK_AXIS_TRIGGER_MIN) { a[1] = JOYSTICK_AXIS_MID; } 
								else a[1] = y_val; //otherwise override
							}
						}
					}
				} // end joystick hat handler
			
				//		iprintf("JOY X:%d Y:%d\n", a[0], a[1]);

				if(a[0] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
				if(a[0] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
				if(a[1] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_UP;
				if(a[1] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_DOWN;
				jmap |= btn << JOY_BTN_SHIFT;      // add buttons
				
				// report joystick 1 to OSD
				StateUsbIdSet( conf->vid, conf->pid, conf->joystick_mouse.button_count, iface->jindex);
				StateUsbJoySet( jmap, btn_extra, iface->jindex);
				
				// map virtual joypad
				uint16_t vjoy = jmap;
				vjoy |= btn_extra << 8;
				vjoy = virtual_joystick_mapping( conf->vid, conf->pid, vjoy );
				
				//iprintf("VIRTUAL JOY:%d\n", vjoy);
				//if (jmap != 0) iprintf("JMAP pre map:%d\n", jmap);
				
				//now go back to original variables for downstream processing
				btn_extra = ((vjoy & 0xFF00) >> 8);
				jmap = (vjoy & 0x00FF);
				
				//if (jmap != 0) iprintf("JMAP post map:%d\n", jmap);
				
				// report joysticks to OSD
				idx=iface->jindex;
				StateJoySet(jmap, idx);
				StateJoySetExtra( btn_extra, idx);
				// swap joystick 0 and 1 since 1 is the one 
				// used primarily on most systems
				
				if(idx == 0)      idx = 1;
				else if(idx == 1) idx = 0;
				//StateJoySetExtra( btn_extra, idx); 

        // if real DB9 mouse is preffered, switch the id back to 1
        idx = (idx == 0) && mist_cfg.joystick0_prefer_db9 ? 1 : idx;
				
				// don't run if not changed
				if (vjoy != iface->jmap) {
					user_io_digital_joystick(idx, jmap);
					// new API with all extra buttons
					user_io_digital_joystick_ext(idx, vjoy);
				}

				iface->jmap = vjoy;

				// also send analog values
				user_io_analog_joystick(idx, a[0]-128, a[1]-128);

				// do special 5200daptor treatment
				if(iface->is_5200daptor)
					handle_5200daptor(iface, buf);
				
				// apply keyboard mappings
				if ((!virt_joy_kbd_iface) || (virt_joy_kbd_iface == iface)) {
					bool ret = virtual_joystick_keyboard( vjoy );
					virt_joy_kbd_iface = NULL;
					if (ret)
						virt_joy_kbd_iface = iface;
				}
			
			} // end joystick handling
		 
		} // end hid custom report parsing
  } // end of HID complex parsing
}
	

static uint8_t usb_hid_poll(usb_device_t *dev) {
  usb_hid_info_t *info = &(dev->hid_info);
  int8_t i;

  if (!info->bPollEnable)
    return 0;
  
  for(i=0;i<info->bNumIfaces;i++) {
    usb_hid_iface_info_t *iface = info->iface+i;
    if(iface->device_type != HID_DEVICE_UNKNOWN) {
		
      if (iface->qNextPollTime <= timer_get_msec()) {
        //      hid_debugf("poll %d...", iface->ep.epAddr);
        uint16_t read = iface->ep.maxPktSize;
        uint8_t buf[iface->ep.maxPktSize];
        // clear buffer
        memset(buf, 0, iface->ep.maxPktSize);
        uint8_t rcode = usb_in_transfer(dev, &(iface->ep), &read, buf);
        if (rcode) {
          if (rcode != hrNAK)
            hid_debugf("%s() error: %d", __FUNCTION__, rcode);
        } else {
			usb_process_iface ( iface, read, buf);
        } 
        iface->qNextPollTime += iface->interval;   // poll at requested rate
      }
    
    } // end if known device
    
  } // end for loop (bNumIfaces)
  return 0;
}


void hid_set_kbd_led(unsigned char led, bool on) {
  // check if led state has changed
  if( (on && !(kbd_led_state&led)) || (!on && (kbd_led_state&led))) {
    if(on) kbd_led_state |=  led;
    else   kbd_led_state &= ~led;

    // search for all keyboard interfaces on all hid devices
    usb_device_t *dev = usb_get_devices();
    int i;
    for(i=0;i<USB_NUMDEVICES;i++) {
      if(dev[i].bAddress && (dev[i].class == &usb_hid_class)) {
	// search for keyboard interfaces
	int j;
	for(j=0;j<MAX_IFACES;j++)
	  if(dev[i].hid_info.iface[j].device_type == HID_DEVICE_KEYBOARD)
	    hid_set_report(dev+i, dev[i].hid_info.iface[j].iface_idx, 2, 0, 1, &kbd_led_state);
      }
    }
  }
}

int8_t hid_keyboard_present(void) {
  // check all USB devices for keyboards
  usb_device_t *dev = usb_get_devices();
  int i;
  for(i=0;i<USB_NUMDEVICES;i++) {
    if(dev[i].bAddress && (dev[i].class == &usb_hid_class)) {
      // search for keyboard interfaces
      int j;
      for(j=0;j<MAX_IFACES;j++)
	if(dev[i].hid_info.iface[j].device_type == HID_DEVICE_KEYBOARD)
	  return 1;
    }
  }
  return 0;
}

const usb_device_class_config_t usb_hid_class = {
  usb_hid_init, usb_hid_release, usb_hid_poll };  

