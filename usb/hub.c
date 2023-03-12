#include <stdio.h>

#include "usb.h"
#include "timer.h"

static uint8_t usb_hub_clear_hub_feature(usb_device_t *dev, uint8_t fid )  {
  return( usb_ctrl_req( dev, USB_HUB_REQ_CLEAR_HUB_FEATURE, 
       USB_REQUEST_CLEAR_FEATURE, fid, 0, 0, 0, NULL));
}

// Clear Port Feature
static uint8_t usb_hub_clear_port_feature(usb_device_t *dev, uint8_t fid, uint8_t port, uint8_t sel )  {
  return( usb_ctrl_req( dev , USB_HUB_REQ_CLEAR_PORT_FEATURE, 
       USB_REQUEST_CLEAR_FEATURE, fid, 0, ((0x0000|port)|(sel<<8)), 0, NULL));
}
// Get Hub Descriptor
static uint8_t usb_hub_get_hub_descriptor(usb_device_t *dev, uint8_t index, 
					  uint16_t nbytes, usb_hub_descriptor_t *dataptr )  {
  return( usb_ctrl_req( dev, USB_HUB_REQ_GET_HUB_DESCRIPTOR, 
			USB_REQUEST_GET_DESCRIPTOR, index, 0x29, 0, nbytes, (uint8_t*)dataptr));
}

// Set Port Feature
static uint8_t usb_hub_set_port_feature(usb_device_t *dev, uint8_t fid, uint8_t port, uint8_t sel ) {
  return( usb_ctrl_req( dev, USB_HUB_REQ_SET_PORT_FEATURE, 
       USB_REQUEST_SET_FEATURE, fid, 0, (((0x0000|sel)<<8)|port), 0, NULL));
}

// Get Port Status
static uint8_t usb_hub_get_port_status(usb_device_t *dev, uint8_t port, uint16_t nbytes, uint8_t* dataptr )  {
  return( usb_ctrl_req( dev, USB_HUB_REQ_GET_PORT_STATUS, 
       USB_REQUEST_GET_STATUS, 0, 0, port, nbytes, dataptr));
}

static uint8_t usb_hub_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len, ep_t *pep) {
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
      usb_dump_interface_descriptor(&p->iface_desc);
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      usb_dump_endpoint_descriptor(&p->ep_desc);
      pep->epAddr     = p->ep_desc.bEndpointAddress & 0x7f;
      pep->maxPktSize = p->ep_desc.wMaxPacketSize[0];
      if (pep->maxPktSize != 0) return 0;
      break;

    default:
      iprintf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
    }

    // advance to next descriptor
    if (!p->conf_desc.bLength || p->conf_desc.bLength > len) break;
    len -= p->conf_desc.bLength;
    p = (union buf_u*)(p->raw + p->conf_desc.bLength);
  }

  if(len != 0) {
    iprintf("Config underrun: %d", len);
    return USB_ERROR_CONFIGURATION_SIZE_MISMATCH;
  }

  return USB_ERROR_INVALID_MAX_PKT_SIZE;
}

static uint8_t usb_hub_init(usb_device_t *dev, usb_device_descriptor_t *dev_desc) {
  iprintf("%s()\n", __FUNCTION__);

  uint8_t rcode;
  uint8_t i;

  usb_hub_info_t *info = &(dev->hub_info);

  union {
    usb_configuration_descriptor_t conf_desc;
    usb_hub_descriptor_t hub_desc;
  } buf;

  // reset status
  info->bNbrPorts = 0; 
  info->qLastPollTime = 0;
  info->bPollEnable = false;

  info->ep.epAddr	= 1;
  info->ep.maxPktSize	= 8;  //kludge
  info->ep.epType       = EP_TYPE_INTR;
  info->ep.epAttribs     = 0;
  info->ep.bmNakPower	= USB_NAK_NOWAIT;

  // Extract device class from device descriptor
  // If device class is not a hub return
  if (dev_desc->bDeviceClass != USB_CLASS_HUB) {
    puts("not a hub!");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  // Get hub descriptor
  rcode = usb_hub_get_hub_descriptor(dev, 0, 8, &buf.hub_desc);
    
  if (rcode) {
    puts("failed to get hub descriptor");
    return rcode;
  }
  
  // Save number of ports for future use
  info->bNbrPorts = buf.hub_desc.bNbrPorts;
    
  // Read configuration Descriptor in Order To Obtain Proper Configuration Value
  rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &buf.conf_desc);
  if (rcode) {
    puts("failed to read configuration descriptor");
    return rcode;
  }
  usb_dump_conf_descriptor(&buf.conf_desc);

  // Set Configuration Value
  rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);
  if (rcode) {
    iprintf("failed to set configuration to %d\n", buf.conf_desc.bConfigurationValue);
    return rcode;
  }

  rcode = usb_hub_parse_conf(dev, 0, buf.conf_desc.wTotalLength, &info->ep);
  if (rcode) {
    iprintf("failed to get endpoint data (%d)\n", rcode);
    return rcode;
  }

  // Power on all ports
  for (i=1; i<=info->bNbrPorts; i++)
    usb_hub_set_port_feature(dev, HUB_FEATURE_PORT_POWER, i, 0);	// HubPortPowerOn(i);
    
  if(!dev->parent)
    usb_SetHubPreMask();

  info->bPollEnable = true;

  return 0;
}

static uint8_t usb_hub_release(usb_device_t *dev) {
  puts(__FUNCTION__);

  // root hub unplugged
  if(!dev->parent)
    usb_ResetHubPreMask();

  return 0;
}

static void usb_hub_show_port_status(uint8_t port, uint16_t status, uint16_t changed) {
  iprintf("Status of port %d:\n", port);

  if(status & USB_HUB_PORT_STATUS_PORT_CONNECTION)    puts(" connected");
  if(status & USB_HUB_PORT_STATUS_PORT_ENABLE)        puts(" enabled");
  if(status & USB_HUB_PORT_STATUS_PORT_SUSPEND)       puts(" suspended");
  if(status & USB_HUB_PORT_STATUS_PORT_OVER_CURRENT)  puts(" over current");
  if(status & USB_HUB_PORT_STATUS_PORT_RESET)         puts(" reset");
  if(status & USB_HUB_PORT_STATUS_PORT_POWER)         puts(" powered");
  if(status & USB_HUB_PORT_STATUS_PORT_LOW_SPEED)     puts(" low speed");
  if(status & USB_HUB_PORT_STATUS_PORT_HIGH_SPEED)    puts(" high speed");
  if(status & USB_HUB_PORT_STATUS_PORT_TEST)          puts(" test");
  if(status & USB_HUB_PORT_STATUS_PORT_INDICATOR)     puts(" indicator");

  iprintf("Changes on port %d:\n", port);
  if(changed & USB_HUB_PORT_STATUS_PORT_CONNECTION)   puts(" connected");
  if(changed & USB_HUB_PORT_STATUS_PORT_ENABLE)       puts(" enabled");
  if(changed & USB_HUB_PORT_STATUS_PORT_SUSPEND)      puts(" suspended");
  if(changed & USB_HUB_PORT_STATUS_PORT_OVER_CURRENT) puts(" over current");
  if(changed & USB_HUB_PORT_STATUS_PORT_RESET)        puts(" reset");
}

static uint8_t usb_hub_port_status_change(usb_device_t *dev, uint8_t port, hub_event_t evt) {
  usb_hub_info_t *info = &(dev->hub_info);
  uint8_t rcode;

  iprintf("status change on port %d, 0x%x\n", port, evt.bmEvent);
  usb_hub_show_port_status(port, evt.bmStatus, evt.bmChange);

  static bool bResetInitiated = false;

  switch (evt.bmEvent) {
    // Device connected event
  case USB_HUB_PORT_EVENT_CONNECT:
  case USB_HUB_PORT_EVENT_LS_CONNECT:
    iprintf(" dev %x: port %d connect!\n", dev->bAddress, port);

    if (bResetInitiated) {
      iprintf("reset already in progress\n");
      return 0;
    }

    //    timer_delay_msec(100);

    iprintf("resetting port %d\n", port);
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_ENABLE, port, 0);
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_CONNECTION, port, 0);
    usb_hub_set_port_feature(dev, HUB_FEATURE_PORT_RESET, port, 0);	
    bResetInitiated = true;
    return HUB_ERROR_PORT_HAS_BEEN_RESET;
    
    // Device disconnected event
  case USB_HUB_PORT_EVENT_DISCONNECT:
    iprintf(" port %d disconnect!\n", port);

    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_ENABLE, port, 0);
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_CONNECTION, port, 0);
    bResetInitiated = false;

    usb_release_device(dev->bAddress, port);

    return 0;
    
    // Reset complete event
  case USB_HUB_PORT_EVENT_RESET_COMPLETE:
  case USB_HUB_PORT_EVENT_LS_RESET_COMPLETE:
    iprintf(" port %d reset complete!\n", port);
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_RESET, port, 0);
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_CONNECTION, port, 0);

    rcode = usb_configure(dev->bAddress, port,
        (evt.bmStatus & USB_HUB_PORT_STATUS_PORT_LOW_SPEED)!=0 );
    if (rcode) iprintf("USB configure error: %d\n", rcode);

    bResetInitiated = false;
    break;
  }

  return 0;
}

static uint8_t usb_hub_check_hub_status(usb_device_t *dev, uint8_t ports) {
  usb_hub_info_t *info = &(dev->hub_info);

  uint8_t	rcode;
  uint8_t	buf[8];
  uint16_t	read = 1;

  //   iprintf("%s(addr=%x)\n", __FUNCTION__, dev->bAddress);

  rcode = usb_in_transfer(dev, &(info->ep), &read, buf);
  if(rcode)
    return rcode;

  uint8_t port, mask;
  for(port=1,mask=0x02; port<8; mask<<=1, port++) {
    if (buf[0] & mask) {
      hub_event_t evt;
      evt.bmEvent = 0;

      rcode = usb_hub_get_port_status(dev, port, sizeof(evt.evtBuff), evt.evtBuff);
      if (rcode)
	continue;

      rcode = usb_hub_port_status_change(dev, port, evt);
      
      if (rcode == HUB_ERROR_PORT_HAS_BEEN_RESET)
	return 0;
      
      if (rcode)
	return rcode;
    }
  } // for
  
  for (port=1; port<=ports; port++) {
    hub_event_t	evt;
    evt.bmEvent = 0;
    
    rcode = usb_hub_get_port_status(dev, port, 4, evt.evtBuff);
    if (rcode)
      continue;
    
    if ((evt.bmStatus & USB_HUB_PORT_STATE_CHECK_DISABLED) != USB_HUB_PORT_STATE_DISABLED)
      continue;
    
    // Emulate connection event for the port
    evt.bmChange |= USB_HUB_PORT_STATUS_PORT_CONNECTION;
    
    rcode = usb_hub_port_status_change(dev, port, evt);
    if (rcode == HUB_ERROR_PORT_HAS_BEEN_RESET)
      return 0;
    
    if (rcode)
      return rcode;
  }
  return 0;
}

static uint8_t usb_hub_poll(usb_device_t *dev) {
  usb_hub_info_t *info = &(dev->hub_info);

  uint8_t rcode = 0;

  if (!info->bPollEnable)
    return 0;
  
  if (timer_check(info->qLastPollTime, 100)) { // poll 10 times a second
    rcode = usb_hub_check_hub_status(dev, info->bNbrPorts);
    info->qLastPollTime = timer_get_msec();
  }

  return rcode;
}

const usb_device_class_config_t usb_hub_class = {
  usb_hub_init, usb_hub_release, usb_hub_poll };  

