//
// storage.c
//

#include <stdio.h>
#include <string.h>  // for memcpy

#include "debug.h"
#include "usb.h"
#include "storage.h"
#include "timer.h"
#include "mii.h"
#include "max3421e.h"
#include "hardware.h"
#include "tos.h"
#include "fat.h"

uint8_t storage_devices = 0;

static uint32_t swap32(uint32_t a) {
  return (((a&0xfful)<<24)|((a&0xff00ul)<<8)|((a&0xff0000ul)>>8)|((a&0xff000000ul)>>24));
}

static uint8_t storage_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
  usb_storage_info_t *info = &(dev->storage_info);
  uint8_t rcode;
  bool is_good_interface = false;

  union buf_u {
    usb_configuration_descriptor_t conf_desc;
    usb_interface_descriptor_t iface_desc;
    usb_endpoint_descriptor_t ep_desc;
    uint8_t raw[len];
  } buf, *p;

  if(rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)) 
    return rcode;

  /* scan through all descriptors */
  p = &buf;
  while(len > 0) {
    switch(p->conf_desc.bDescriptorType) {

    case USB_DESCRIPTOR_CONFIGURATION:
      break;

    case USB_DESCRIPTOR_INTERFACE:
      // only STORAGE interfaces are supported
      if((p->iface_desc.bInterfaceClass == USB_CLASS_MASS_STORAGE) &&
	 (p->iface_desc.bInterfaceSubClass == STORAGE_SUBCLASS_SCSI) &&
	 (p->iface_desc.bInterfaceProtocol == STORAGE_PROTOCOL_BULK_ONLY)) {
	storage_debugf("iface is MASS_STORAGE/SCSI/BULK_ONLY");
	is_good_interface = true;
      } else {
	storage_debugf("Unsupported class/subclass/proto = %x/%x/%x", 
		       p->iface_desc.bInterfaceClass, p->iface_desc.bInterfaceSubClass,
		       p->iface_desc.bInterfaceProtocol);
	is_good_interface = false;
      }
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      if(is_good_interface) { 
	int8_t epidx = -1;
	
	if((p->ep_desc.bmAttributes & 0x03) == 2) {
	  if((p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
	    storage_debugf("bulk in ep %d, size = %d", p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.wMaxPacketSize[0]);
	    epidx = STORAGE_EP_IN;
	  } else {
	      storage_debugf("bulk out ep %d, size = %d", p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.wMaxPacketSize[0]);
	    epidx = STORAGE_EP_OUT;
	  }
	}
	
	if(epidx != -1) {
	  // Fill in the endpoint info structure
	  info->ep[epidx].epAddr     = (p->ep_desc.bEndpointAddress & 0x0F);
	  info->ep[epidx].maxPktSize = p->ep_desc.wMaxPacketSize[0];
	  info->ep[epidx].epAttribs  = 0;
	  info->ep[epidx].bmNakPower = USB_NAK_NOWAIT;
	}
      }
      break;
      
    default:
      storage_debugf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
    }

    // advance to next descriptor
    len -= p->conf_desc.bLength;
    p = (union buf_u*)(p->raw + p->conf_desc.bLength);
  }
  
  if(len != 0) {
    storage_debugf("Config underrun: %d", len);
    return USB_ERROR_CONFIGURAION_SIZE_MISMATCH;
  }

  return is_good_interface?0:USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
}

static uint8_t clear_ep_halt(usb_device_t *dev, uint8_t index) {
  usb_storage_info_t *info = &(dev->storage_info);

  return usb_ctrl_req(dev, USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_ENDPOINT, 
		      USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, 0, info->ep[index].epAddr, 0, NULL);
}

static uint8_t get_max_lun(usb_device_t *dev, uint8_t *plun) {
  usb_storage_info_t *info = &(dev->storage_info);
  info->last_error = usb_ctrl_req(dev, STORAGE_REQ_MASSIN, STORAGE_REQ_GET_MAX_LUN, 0, 0, 0, 1, plun);

  timer_delay_msec(10);
  
  if (info->last_error == hrSTALL) {
    storage_debugf("%s() stall", __FUNCTION__);
    *plun = 0;
    info->last_error = clear_ep_halt(dev, STORAGE_EP_IN);
    return 0;
  }

  if(info->last_error)
    storage_debugf("%s() failed", __FUNCTION__);

  return info->last_error;
}

static uint8_t handle_usb_error(usb_device_t *dev, uint8_t index) {
  usb_storage_info_t *info = &(dev->storage_info);
  uint8_t count = 3;

  while(info->last_error && count) {
    switch(info->last_error) {
    case hrSUCCESS:
      return 0;

    case hrJERR: 
      info->last_error = 0;
      return STORAGE_ERR_DEVICE_DISCONNECTED;

    case hrSTALL:
      info->last_error = clear_ep_halt(dev, index);
      break;
      
    default:
      return STORAGE_ERR_GENERAL_USB_ERROR;
    }
    count --;
  } // while
  
  return STORAGE_ERR_SUCCESS;
}

static uint8_t transaction(usb_device_t *dev, command_block_wrapper_t *cbw, uint16_t size, void *buf, uint8_t flags) {
  storage_debugf("%s(%d)", __FUNCTION__, size);

  usb_storage_info_t *info = &(dev->storage_info);
  uint16_t read;
  uint8_t ret;

  info->last_error = usb_out_transfer(dev, &(info->ep[STORAGE_EP_OUT]), sizeof(command_block_wrapper_t), (uint8_t*)cbw);
  if((ret= handle_usb_error(dev, STORAGE_EP_OUT))) {
    storage_debugf("Sending CBW failed");
    return ret;
  }

  if (size && buf) {
    if (cbw->bmCBWFlags & STORAGE_CMD_DIR_IN) {
      read = size;

      if ((flags & STORAGE_TRANS_FLG_CALLBACK) == STORAGE_TRANS_FLG_CALLBACK) {
	// limit transfer size to 64 byte chunks when reading

	const uint8_t	bufSize = 64;
	uint16_t	total	= size;
	uint16_t	count	= 0;
	uint8_t		rbuf[bufSize];

	iprintf(">>>>>>>>>>>>>>>> CHECKME!!! <<<<<<<<<<<<<<<<<<<<\n");
	
	read = bufSize;
				
	while(count < total && 
	      ((info->last_error = usb_int_transfer(dev, &(info->ep[STORAGE_EP_IN]), &read, (uint8_t*)rbuf)) == hrSUCCESS)) {
	  iprintf("IN:\n");
	  hexdump(rbuf, read, count);

	  //	  ((USBReadParser*)buf)->Parse(read, rbuf, count);

	  count += read;
	  read = bufSize;
	}

	if (info->last_error == hrSTALL)
	  info->last_error = clear_ep_halt(dev, STORAGE_EP_IN);

	if (info->last_error) {
	  storage_debugf("RDR %d", info->last_error);
	  return STORAGE_ERR_GENERAL_USB_ERROR;
	}
      } // if ((flags & 1) == 1)
      else {
	// read with retry for max 10ms
	msec_t retry_until = timer_get_msec()+10;

	do {
	  read = size;
	  info->last_error = usb_in_transfer(dev, &(info->ep[STORAGE_EP_IN]), &read, (uint8_t*)buf);
	} while((info->last_error == hrNAK) && (timer_get_msec() < retry_until));
	
	if(info->last_error == 0) {
	  iprintf("read %d:\n", read);
	  hexdump(buf, read, 0);
	}
      }
    } else if (cbw->bmCBWFlags & STORAGE_CMD_DIR_OUT)
      info->last_error = usb_out_transfer(dev, &(info->ep[STORAGE_EP_OUT]), read, (uint8_t*)buf);
  }

  if(handle_usb_error(dev, (cbw->bmCBWFlags & STORAGE_CMD_DIR_IN) ? STORAGE_EP_IN: STORAGE_EP_OUT)) {
    storage_debugf("response failed");
    return STORAGE_ERR_GENERAL_USB_ERROR;
  }

  command_status_wrapper_t csw;
  read = sizeof(command_status_wrapper_t);

  info->last_error = usb_in_transfer(dev, &(info->ep[STORAGE_EP_IN]), &read, (uint8_t*)&csw);
  if((ret = handle_usb_error(dev, STORAGE_EP_IN))) {
    storage_debugf("command status read failed");
    return ret;
  }

  return csw.bCSWStatus;
}

static uint8_t scsi_command_in(usb_device_t *dev, uint8_t lun, uint16_t bsize, uint8_t *buf,
			       uint8_t cmd, uint8_t cblen) {
  command_block_wrapper_t cbw; 
  uint8_t i;
	
  cbw.dCBWSignature		= STORAGE_CBW_SIGNATURE;
  cbw.dCBWTag			= 0xdeadbeef;
  cbw.dCBWDataTransferLength	= bsize;
  cbw.bmCBWFlags		= STORAGE_CMD_DIR_IN;
  cbw.bmCBWLUN			= lun;
  cbw.bmCBWCBLength		= cblen;

  for(i=0; i<16; i++)
    cbw.CBWCB[i] = 0;

  cbw.CBWCB[0] = cmd;
  cbw.CBWCB[4] = bsize;

  return transaction(dev, &cbw, bsize, buf, 0);
}

static uint8_t inquiry(usb_device_t *dev, uint8_t lun, inquiry_response_t *buf) {
  return scsi_command_in(dev, lun, sizeof(inquiry_response_t), (uint8_t*)buf, SCSI_CMD_INQUIRY, 6);
}

static uint8_t request_sense(usb_device_t *dev, uint8_t lun, request_sense_response_t *buf) {
  return scsi_command_in(dev, lun, sizeof(request_sense_response_t), (uint8_t*)buf, SCSI_CMD_REQUEST_SENSE, 6);
}

static uint8_t read_capacity(usb_device_t *dev, uint8_t lun, read_capacity_response_t *buf) {
  return scsi_command_in(dev, lun, sizeof(read_capacity_response_t), (uint8_t*)buf, SCSI_CMD_READ_CAPACITY_10, 10);
}

static uint8_t test_unit_ready(usb_device_t *dev, uint8_t lun) {
  command_block_wrapper_t cbw; 
  uint8_t i;
	
  cbw.dCBWSignature		= STORAGE_CBW_SIGNATURE;
  cbw.dCBWTag			= 0xdeadbeef;
  cbw.dCBWDataTransferLength	= 0;
  cbw.bmCBWFlags		= STORAGE_CMD_DIR_OUT;
  cbw.bmCBWLUN		        = lun;
  cbw.bmCBWCBLength		= 6;

  for(i=0; i<16; i++)
    cbw.CBWCB[i] = 0;

  cbw.CBWCB[0] = SCSI_CMD_TEST_UNIT_READY;
  
  return transaction(dev, &cbw, 0, NULL, 0);
}

static uint8_t read(usb_device_t *dev, uint8_t lun, 
		    uint32_t addr, uint16_t bsize, char *buf) {
  command_block_wrapper_t cbw; 
  uint8_t i;
  
  cbw.dCBWSignature             = STORAGE_CBW_SIGNATURE;
  cbw.dCBWTag                   = 0xdeadbeef;
  cbw.dCBWDataTransferLength    = bsize;
  cbw.bmCBWFlags                = STORAGE_CMD_DIR_IN;
  cbw.bmCBWLUN                  = lun;
  cbw.bmCBWCBLength             = 10;

  for (i=0; i<16; i++)
    cbw.CBWCB[i] = 0;

  cbw.CBWCB[0] = SCSI_CMD_READ_10;
  cbw.CBWCB[8] = 1;
  cbw.CBWCB[5] = (addr & 0xff);
  cbw.CBWCB[4] = ((addr >> 8) & 0xff);
  cbw.CBWCB[3] = ((addr >> 16) & 0xff);
  cbw.CBWCB[2] = ((addr >> 24) & 0xff);
  
  return transaction(dev, &cbw, bsize, buf, 0);
}

static uint8_t usb_storage_init(usb_device_t *dev) {
  usb_storage_info_t *info = &(dev->storage_info);
  uint8_t i, rcode = 0;

  for(i=0;i<2;i++)
    info->ep[i].epAddr = 0;

  storage_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

  union {
    usb_device_descriptor_t dev_desc;
    usb_configuration_descriptor_t conf_desc;
    inquiry_response_t inquiry_rsp;
    read_capacity_response_t read_cap_rsp;
    uint8_t data[12];
  } buf;

  // read full device descriptor 
  rcode = usb_get_dev_descr( dev, sizeof(usb_device_descriptor_t), &buf.dev_desc );
  if( rcode ) {
    storage_debugf("failed to get device descriptor");
    return rcode;
  }

  if((buf.dev_desc.bDeviceClass != USB_CLASS_USE_CLASS_INFO) && 
     (buf.dev_desc.bDeviceClass != USB_CLASS_MASS_STORAGE)) {
    storage_debugf("Unsuppored device class %x\n", buf.dev_desc.bDeviceClass);
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  uint8_t num_of_conf = buf.dev_desc.bNumConfigurations;
  storage_debugf("number of configurations: %d", num_of_conf);

  // scan all configurations for a usable one
  int8_t good_conf = -1;
  for(i=0; (i < num_of_conf)&&(good_conf == -1); i++) {
    if(rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &buf.conf_desc)) 
      return rcode;
    
    storage_debugf("conf descriptor %d has total size %d", i, buf.conf_desc.wTotalLength);

    // parse directly if it already fitted completely into the buffer
    if((rcode = storage_parse_conf(dev, i, buf.conf_desc.wTotalLength)) == 0)
      good_conf = buf.conf_desc.bConfigurationValue;
    else
      storage_debugf("parse conf failed");
  }

  if(!good_conf) {
    storage_debugf("no good configuration");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  // Set Configuration Value
  storage_debugf("good conf = %d", good_conf);
  rcode = usb_set_conf(dev, good_conf);

  // found a usb mass storage device. now try to talk to it
  rcode = get_max_lun(dev, &info->max_lun);
  if(rcode == 0)
    storage_debugf("Max lun: %d", info->max_lun);

  // request basic infos ...
  rcode = inquiry(dev, 0, &buf.inquiry_rsp);
  if(rcode) {
    storage_debugf("Inquiry failed");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  iprintf("STORAGE: Vendor:    %.8s\n", buf.inquiry_rsp.VendorID);
  iprintf("STORAGE: Product:   %.16s\n", buf.inquiry_rsp.ProductID);
  iprintf("STORAGE: Rev:       %.4s\n", buf.inquiry_rsp.RevisionID);
  iprintf("STORAGE: Removable: %s\n", buf.inquiry_rsp.Removable?"yes":"no");

  rcode = read_capacity(dev, 0, &buf.read_cap_rsp);
  if(rcode) {
    storage_debugf("Read capacity failed");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  iprintf("STORAGE: Capacity:     %ld blocks\n", swap32(buf.read_cap_rsp.dwBlockAddress));
  iprintf("STORAGE: Block length: %ld bytes\n", swap32(buf.read_cap_rsp.dwBlockLength));

  storage_devices++;
  storage_debugf("supported device, total USB storage devices now %d", storage_devices);

  rcode = read(dev, 0, 0, 512, sector_buffer);
  if(rcode) {
    storage_debugf("Read sector 0 failed");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  return 0;
}

static uint8_t usb_storage_release(usb_device_t *dev) {
  storage_debugf("%s()", __FUNCTION__);
  storage_devices--;

  return 0;
}

static uint8_t usb_storage_poll(usb_device_t *dev) {
  usb_storage_info_t *info = &(dev->storage_info);
  uint8_t rcode = 0;

  return rcode;
}

const usb_device_class_config_t usb_storage_class = {
  usb_storage_init, usb_storage_release, usb_storage_poll };  
