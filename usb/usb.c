#include <stdio.h>
#include <string.h>

#include "timer.h"
#include "usb.h"
#include "debug.h"

static usb_device_t dev[USB_NUMDEVICES];

usb_device_t *usb_get_devices() {
	return dev;
}

void usb_init() {
	puts(__FUNCTION__);

	uint8_t i;
	for(i=0;i<USB_NUMDEVICES;i++)
		dev[i].bAddress = 0;

	usb_hw_init();
}

// list of supported device classes
static const usb_device_class_config_t *class_list[] = {
  &usb_hub_class,
  &usb_hid_class,
  &usb_xbox_class,
  &usb_asix_class,
#ifdef USB_STORAGE
  &usb_storage_class,
#endif
  &usb_usbrtc_class,
  &usb_pl2303_class,
  NULL
};

uint8_t usb_configure(uint8_t parent, uint8_t port, bool lowspeed) {
	uint8_t rcode = 0;
	iprintf("%s(parent=%x port=%d lowspeed=%d)\n", __FUNCTION__, parent, port, lowspeed);

	// find an empty device entry
	uint8_t i;
	usb_device_descriptor_t dev_desc;
	union {
		usb_string0_descriptor_t str0_desc;
		usb_string_descriptor_t str_desc;
		uint8_t buf[255];
	} str;

	for(i=0; i<USB_NUMDEVICES && dev[i].bAddress; i++);

	if(i < USB_NUMDEVICES) {
		iprintf("using free entry at %d\n", i);

		usb_device_t *d = &dev[i];
		memset(d, 0, sizeof(*d));

		// setup generic info
		d->parent = parent;
		d->lowspeed = lowspeed;
		d->port = port;

		// setup endpoint 0
		d->ep0.maxPktSize = 8;
		d->ep0.bmNakPower = USB_NAK_MAX_POWER;

		if((rcode = usb_get_dev_descr( d, 8, &dev_desc )))
			return rcode;
		d->ep0.maxPktSize = dev_desc.bMaxPacketSize0;
		usb_debugf("EP0 max packet size: %d", d->ep0.maxPktSize);
		// Assign new address to the device
		// (address is simply the number of the free slot + 1)
		iprintf("Setting addr %x\n", i+1);
		rcode = usb_set_addr(d, i+1);
		if(rcode) {
			iprintf("failed to assign address (rcode=%d)", rcode);
			return rcode;
		}
		uint32_t timer = timer_get_msec();
		do {
			rcode = usb_get_dev_descr( d, 8, &dev_desc );
		} while (rcode && !timer_check(timer, 5)); // Some recovery interval (2 ms as USB 2.0 9.2.6.3)
		if(rcode) return rcode;

		// --- enumerate device ---
		if((rcode = usb_get_dev_descr( d, sizeof(usb_device_descriptor_t), &dev_desc)))
			return rcode;
		usb_dump_device_descriptor(&dev_desc);
		iprintf("USB vendor ID: %04X, product ID: %04X\n", dev_desc.idVendor, dev_desc.idProduct);

		// save vid/pid
		d->vid = dev_desc.idVendor;
		d->pid = dev_desc.idProduct;

		// The Retroflag Classic USB Gamepad doesn't report movement until the string descriptors are read,
		// so read all of them here (and show them on the console)
		if (!usb_get_string_descr(d, sizeof(str), 0, 0, &str.str_desc)) { // supported languages descriptor
			uint16_t wLangId = str.str0_desc.wLANGID[0];
			iprintf("wLangId: %04X\n", wLangId);

			// Some gamepads (Retrobit) breaks if its strings are queried like below, so don't do it until it can be done safely.
#if 0
			if (dev_desc.iManufacturer && 
				!usb_get_string_descr(d, sizeof(str), dev_desc.iManufacturer, wLangId, &str.str_desc)) {
				for (i=0; i<((str.str_desc.bLength-2)/2); i++) {
					s[i] = ff_uni2oem(str.str_desc.bString[i], FF_CODE_PAGE);
				}
				s[i] = 0;
				iprintf("Manufacturer: %s\n", s);
			}
			if (dev_desc.iProduct && 
			    !usb_get_string_descr(d, sizeof(str), dev_desc.iProduct, wLangId, &str.str_desc)) {
				for (i=0; i<((str.str_desc.bLength-2)/2); i++) {
					s[i] = ff_uni2oem(str.str_desc.bString[i], FF_CODE_PAGE);
				}
				s[i] = 0;
				iprintf("Product: %s\n", s);
			}
			if (dev_desc.iSerialNumber && 
			    !usb_get_string_descr(d, sizeof(str), dev_desc.iSerialNumber, wLangId, &str.str_desc)) {
				for (i=0; i<((str.str_desc.bLength-2)/2); i++) {
					s[i] = ff_uni2oem(str.str_desc.bString[i], FF_CODE_PAGE);
				}
				s[i] = 0;
				iprintf("Serial no.: %s\n", s);
			}
#endif
		}

		// try to connect device to one of the supported classes
		uint8_t c;
		for(c=0;class_list[c];c++) {
			iprintf("trying to init class %d\n", c);
			rcode = class_list[c]->init(d, &dev_desc);

			if (!rcode) {
				d->class = class_list[c];

				puts(" -> accepted :-)");
				// ok, device accepted by class

				return 0;
			}

			puts(" -> not accepted :-(");
		}
	} else
		iprintf("no more free entries\n");

	iprintf("unsupported device\n");
	return 0;
}

uint8_t usb_release_device(uint8_t parent, uint8_t port) {
	iprintf("%s(parent=%x, port=%d\n", __FUNCTION__, parent, port);

	uint8_t i;
	for(i=0; i<USB_NUMDEVICES; i++) {
		if(dev[i].bAddress && dev[i].parent == parent && dev[i].port == port) {
			iprintf("  -> device with address %x\n", dev[i].bAddress);

			// check if this is a hub (parent of some other device)
			// and release its kids first
			uint8_t j;
			for(j=0; j<USB_NUMDEVICES; j++) {
				if(dev[j].parent == dev[i].bAddress)
					usb_release_device(dev[i].bAddress, dev[j].port);
			}

			uint8_t rcode = 0;
			if(dev[i].class)
				rcode = dev[i].class->release(dev+i);

			dev[i].bAddress = 0;
			return rcode;
		}
	}

	// this should never happen ...
	return 0;
}

uint8_t usb_get_dev_descr( usb_device_t *dev, uint16_t nbytes, usb_device_descriptor_t* p )  {
  return( usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR, 
	       0x00, USB_DESCRIPTOR_DEVICE, 0x0000, nbytes, (uint8_t*)p));
}

uint8_t usb_get_dev_qualifier_descr( usb_device_t *dev, uint16_t nbytes, usb_device_qualifier_descriptor_t* p )  {
  return( usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
                        0x00, USB_DESCRIPTOR_DEVICE_QUALIFIER, 0x0000, nbytes, (uint8_t*)p));
}


//get configuration descriptor  
uint8_t usb_get_conf_descr( usb_device_t *dev, uint16_t nbytes, 
			    uint8_t conf, usb_configuration_descriptor_t* p )  {
	return( usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR, 
	        conf, USB_DESCRIPTOR_CONFIGURATION, 0x0000, nbytes, (uint8_t*)p));
}

uint8_t usb_get_other_speed_descr( usb_device_t *dev, uint16_t nbytes,
                                   uint8_t conf, usb_configuration_descriptor_t* p )  {
  return( usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
                        conf, USB_DESCRIPTOR_OTHER_SPEED, 0x0000, nbytes, (uint8_t*)p));
}

uint8_t usb_set_addr( usb_device_t *dev, uint8_t newaddr )  {
	iprintf("%s(new=%x)\n", __FUNCTION__, newaddr);

	uint8_t rcode = usb_ctrl_req( dev, USB_REQ_SET, USB_REQUEST_SET_ADDRESS, newaddr, 
	                              0x00, 0x0000, 0x0000, NULL);
	if(!rcode) dev->bAddress = newaddr;
	return rcode;
}

//get configuration
uint8_t usb_get_conf( usb_device_t *dev, uint8_t *conf_value )  {
  return( usb_ctrl_req( dev, USB_REQ_GET, USB_REQUEST_GET_CONFIGURATION,
	                0x00, 0x00, 0x0000, 1, conf_value));
}

//set configuration
uint8_t usb_set_conf( usb_device_t *dev, uint8_t conf_value )  {
  return( usb_ctrl_req( dev, USB_REQ_SET, USB_REQUEST_SET_CONFIGURATION,
	                conf_value, 0x00, 0x0000, 0x0000, NULL));
}

uint8_t usb_get_string_descr( usb_device_t *dev, uint16_t nbytes, uint8_t index, uint16_t lang_id, usb_string_descriptor_t* dataptr ) {
  return( usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR, 
	       index, USB_DESCRIPTOR_STRING, lang_id, nbytes, (uint8_t*)dataptr));
}
