//
// asix.c
//
// http://lxr.free-electrons.com/source/drivers/net/usb/asix.c?v=3.1

#include <stdio.h>
#include <string.h>  // for memcpy

#include "debug.h"
#include "usb.h"
#include "asix.h"
#include "timer.h"
#include "mii.h"
#include "asix_const.h"
#include "max3421e.h"
#include "hardware.h"
#include "tos.h"

#define MAX_FRAMELEN 1536

static unsigned char rx_buf[MAX_FRAMELEN];
static uint16_t rx_cnt;

static unsigned char tx_buf[MAX_FRAMELEN];
static uint16_t tx_cnt, tx_offset;

static bool eth_present = 0;

// currently only AX88772 is supported as that's the only
// device i have
#define ASIX_TYPE_AX88772 0x01

// list of suppoerted/tested devices
static const struct {
  uint16_t vid;
  uint16_t pid;
  uint8_t type;
} asix_devs[] = {
  // DLink DUB-E100 H/W Ver B1 Alternate
  { 0x2001, 0x3c05, ASIX_TYPE_AX88772 },
  { 0, 0, 0 }
};

#define ASIX_REQ_OUT   USB_SETUP_HOST_TO_DEVICE|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE
#define ASIX_REQ_IN    USB_SETUP_DEVICE_TO_HOST|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE

static uint8_t asix_write_cmd(usb_device_t *dev, uint8_t cmd, uint16_t value, uint16_t index,
			      uint16_t size, uint8_t *data) {
  //  asix_debugf("%s() cmd=0x%02x value=0x%04x index=0x%04x size=%d", __FUNCTION__,
  //  	      cmd, value, index, size);

  return(usb_ctrl_req( dev, ASIX_REQ_OUT, cmd, value&0xff, value>>8, index, size, data));
}

static uint8_t asix_read_cmd(usb_device_t  *dev, uint8_t cmd, uint16_t value, uint16_t index,
			     uint16_t size, void *data) {
  //  asix_debugf("asix_read_cmd() cmd=0x%02x value=0x%04x index=0x%04x size=%d",
  //  	      cmd, value, index, size);
 
  return(usb_ctrl_req( dev, ASIX_REQ_IN, cmd, value&0xff, value>>8, index, size, data));
}

static uint8_t asix_write_gpio(usb_device_t *dev, uint16_t value, uint16_t sleep) {
  uint8_t rcode;
  
  asix_debugf("%s() value=0x%04x sleep=%d", __FUNCTION__, value, sleep);

  rcode = asix_write_cmd(dev, AX_CMD_WRITE_GPIOS, value, 0, 0, NULL);
  if(rcode) asix_debugf("Failed to write GPIO value 0x%04x: %02x\n", value, rcode);

  if (sleep) timer_delay_msec(sleep);
  
  return rcode;
}

static uint8_t asix_write_medium_mode(usb_device_t *dev, uint16_t mode) {
  uint8_t rcode;
 
  asix_debugf("asix_write_medium_mode() - mode = 0x%04x", mode);
  rcode = asix_write_cmd(dev, AX_CMD_WRITE_MEDIUM_MODE, mode, 0, 0, NULL);
  if(rcode != 0)
    asix_debugf("Failed to write Medium Mode mode to 0x%04x: %02x", mode, rcode);

  return rcode;
}

static uint16_t asix_read_medium_status(usb_device_t *dev) {
  uint16_t v;
  
  uint8_t rcode = asix_read_cmd(dev, AX_CMD_READ_MEDIUM_STATUS, 0, 0, 2, (uint8_t*)&v);
  if (rcode != 0) {
    asix_debugf("Error reading Medium Status register: %02x", rcode);
    return rcode;
  }
  return v;
}

static inline uint8_t asix_set_sw_mii(usb_device_t *dev) {
  uint8_t rcode = asix_write_cmd(dev, AX_CMD_SET_SW_MII, 0x0000, 0, 0, NULL);
  if(rcode != 0)
    asix_debugf("Failed to enable software MII access");

  return rcode;
}

static inline uint8_t asix_set_hw_mii(usb_device_t *dev) {
  uint8_t rcode = asix_write_cmd(dev, AX_CMD_SET_HW_MII, 0x0000, 0, 0, NULL);
  if(rcode != 0)
    asix_debugf("Failed to enable hardware MII access");

  return rcode;
}

static inline int8_t asix_get_phy_addr(usb_device_t *dev) {
  uint8_t buf[2];

  uint8_t ret = asix_read_cmd(dev, AX_CMD_READ_PHY_ID, 0, 0, sizeof(buf), &buf);
  
  asix_debugf("%s()", __FUNCTION__);
 
  if (ret != 0) {
    asix_debugf("Error reading PHYID register: %02x", ret);
    return ret;
  }

  asix_debugf("returning 0x%02x%02x", buf[1], buf[0]);

  return buf[1];
}

static uint16_t asix_mdio_read(usb_device_t *dev, uint8_t phy_id, uint8_t loc) {
  uint16_t res;

  asix_set_sw_mii(dev);
  asix_read_cmd(dev, AX_CMD_READ_MII_REG, phy_id, loc, 2, &res);
  asix_set_hw_mii(dev);

  asix_debugf("asix_mdio_read() phy_id=0x%02x, loc=0x%02x, returns=0x%04x", phy_id, loc, res);
  return res;
}

static void asix_mdio_write(usb_device_t *dev, uint8_t phy_id, uint8_t loc, uint16_t val) {
  asix_debugf("asix_mdio_write() phy_id=0x%02x, loc=0x%02x, val=0x%04x", phy_id, loc, val);

  asix_set_sw_mii(dev);
  asix_write_cmd(dev, AX_CMD_WRITE_MII_REG, phy_id, loc, 2, (uint8_t*)&val);
  asix_set_hw_mii(dev);
}

#if 1
/* Get the PHY Identifier from the PHYSID1 & PHYSID2 MII registers */
static uint32_t asix_get_phyid(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);

  int16_t phy_reg;
  uint32_t phy_id;
 
  phy_reg = asix_mdio_read(dev, info->phy_id, MII_PHYSID1);
  if(phy_reg < 0) return 0;

  phy_id = (phy_reg & 0xffff) << 16;
  phy_reg = asix_mdio_read(dev, info->phy_id, MII_PHYSID2);
  if(phy_reg < 0) return 0;

  phy_id |= (phy_reg & 0xffff);
  return phy_id;
}
#else
/* Get the PHY Identifier from the PHYSID1 & PHYSID2 MII registers */
static uint32_t asix_get_phyid(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);
  int16_t phy_reg;
  uint32_t phy_id;
  int i;

  /* Poll for the rare case the FW or phy isn't ready yet.  */
  for (i = 0; i < 100; i++) {
    phy_reg = asix_mdio_read(dev, info->phy_id, MII_PHYSID1);
    if (phy_reg != 0 && phy_reg != 0xFFFF)
      break;
    timer_delay_msec(1);
  }

  if (phy_reg <= 0 || phy_reg == 0xFFFF)
    return 0;

  phy_id = (phy_reg & 0xffff) << 16;
  
  phy_reg = asix_mdio_read(dev, info->phy_id, MII_PHYSID2);
  if (phy_reg < 0)
    return 0;
  
  phy_id |= (phy_reg & 0xffff);
  
  return phy_id;
}
#endif

static uint8_t asix_sw_reset(usb_device_t *dev, uint8_t flags) {
  uint8_t rcode;
  
  rcode = asix_write_cmd(dev, AX_CMD_SW_RESET, flags, 0, 0, NULL);
  if (rcode != 0)
    asix_debugf("Failed to send software reset: %02x", rcode);
  else
    timer_delay_msec(150);

  return rcode;
}
 
static uint16_t asix_read_rx_ctl(usb_device_t *dev) {
  // this only works on little endian which the arm is
  uint16_t v;
  uint8_t rcode = asix_read_cmd(dev, AX_CMD_READ_RX_CTL, 0, 0, 2, &v);
  if(rcode != 0) {
    asix_debugf("Error reading RX_CTL register: %02x", rcode);
    return rcode;
  }

  return v;
}

static uint16_t asix_write_rx_ctl(usb_device_t *dev, uint16_t mode) {
  uint8_t rcode = asix_write_cmd(dev, AX_CMD_WRITE_RX_CTL, mode, 0, 0, NULL);
  if(rcode != 0)
    asix_debugf("Error writing RX_CTL register: %02x", rcode);

  return rcode;
}

/**
 * mii_nway_restart - restart NWay (autonegotiation) for this interface
 * @mii: the MII interface
 *
 * Returns 0 on success, negative on error.
 */
void mii_nway_restart(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);

  /* if autoneg is off, it's an error */
  uint16_t bmcr = asix_mdio_read(dev, info->phy_id, MII_BMCR);
  if(bmcr & BMCR_ANENABLE) {
    bmcr |= BMCR_ANRESTART;
    asix_mdio_write(dev, info->phy_id, MII_BMCR, bmcr);
  } else
    asix_debugf("%s() failed", __FUNCTION__);
}

static uint8_t asix_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
  usb_asix_info_t *info = &(dev->asix_info);
  uint8_t rcode;
  uint8_t epidx = 0;

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
      iprintf("conf descriptor size %d\n", p->conf_desc.bLength);
      // we already had this, so we simply ignore it
      break;

    case USB_DESCRIPTOR_INTERFACE:
      iprintf("iface descriptor size %d\n", p->iface_desc.bLength);

      /* check the interface descriptors for supported class */
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      iprintf("endpoint descriptor size %d\n", p->ep_desc.bLength);

      if(epidx < 3) {
	//	hexdump(p, p->conf_desc.bLength, 0);

	// Handle interrupt endpoints
	if ((p->ep_desc.bmAttributes & 0x03) == 3 && 
	    (p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
	  asix_debugf("irq endpoint %d, interval = %dms", 
		  p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.bInterval);

	  // Handling bInterval correctly is rather tricky. The meaning of 
	  // this field differs between low speed/full speed vs. high speed.
	  // We are using a high speed device on a full speed link. Which 
	  // rate is correct then? Furthermore this seems
	  // to be a common problem: http://www.lvr.com/usbfaq.htm
	  info->ep_int_idx = epidx;
	  info->int_poll_ms = p->ep_desc.bInterval;
	}

	if ((p->ep_desc.bmAttributes & 0x03) == 2 && 
	    (p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
	  asix_debugf("bulk in endpoint %d", p->ep_desc.bEndpointAddress & 0x0F);
	}

	if ((p->ep_desc.bmAttributes & 0x03) == 2 && 
	    (p->ep_desc.bEndpointAddress & 0x80) == 0x00) {
	  asix_debugf("bulk out endpoint %d", p->ep_desc.bEndpointAddress & 0x0F);
	}
	
	// Fill in the endpoint info structure
	info->ep[epidx].epAddr	 = (p->ep_desc.bEndpointAddress & 0x0F);
	info->ep[epidx].maxPktSize = p->ep_desc.wMaxPacketSize[0];
	info->ep[epidx].epAttribs	 = 0;
	info->ep[epidx].bmNakPower = USB_NAK_NOWAIT;
	epidx++;
      }
      break;

    default:
      iprintf("unsupported descriptor type %d size %d\n", p->raw[1], p->raw[0]);
    }

    // advance to next descriptor
    len -= p->conf_desc.bLength;
    p = (union buf_u*)(p->raw + p->conf_desc.bLength);
  }
  
  if(len != 0) {
    iprintf("URGS, underrun: %d\n", len);
    return USB_ERROR_CONFIGURAION_SIZE_MISMATCH;
  }

  return 0;
}

static uint8_t usb_asix_init(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);
  uint8_t i, rcode = 0;

  // only one ethernet dongle is supported at a time
  if(eth_present)
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

  // reset status
  info->qNextPollTime = 0;
  info->bPollEnable = false;
  info->linkDetected = false;

  for(i=0;i<3;i++) {
    info->ep[i].epAddr	   = 1;
    info->ep[i].maxPktSize = 8;
    info->ep[i].epAttribs  = 0;
    info->ep[i].bmNakPower = USB_NAK_NOWAIT;
  }

  asix_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

  union {
    usb_device_descriptor_t dev_desc;
    usb_configuration_descriptor_t conf_desc;
    uint8_t eaddr[ETH_ALEN];
  } buf;

  // read full device descriptor 
  rcode = usb_get_dev_descr( dev, sizeof(usb_device_descriptor_t), &buf.dev_desc );
  if( rcode ) {
    asix_debugf("failed to get device descriptor");
    return rcode;
  }

  // If device class is not vendor specific return
  if (buf.dev_desc.bDeviceClass != USB_CLASS_VENDOR_SPECIFIC)
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
 
  asix_debugf("vid/pid = %x/%x", buf.dev_desc.idVendor, buf.dev_desc.idProduct);

  // search for vid/pid in supported device list
  for(i=0;asix_devs[i].type && 
	((asix_devs[i].vid != buf.dev_desc.idVendor) || 
	 (asix_devs[i].pid != buf.dev_desc.idProduct));i++);

  if(!asix_devs[i].type) {
    asix_debugf("Not a supported ASIX device");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }
    
  // Set Configuration Value
  iprintf("conf value = %d\n", buf.conf_desc.bConfigurationValue);
  rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);
  
  uint8_t num_of_conf = buf.dev_desc.bNumConfigurations;
  iprintf("number of configurations: %d\n", num_of_conf);

  for(i=0; i<num_of_conf; i++) {
    if(rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &buf.conf_desc)) 
      return rcode;
    
    iprintf("conf descriptor %d has total size %d\n", i, buf.conf_desc.wTotalLength);

    // extract number of interfaces
    iprintf("number of interfaces: %d\n", buf.conf_desc.bNumInterfaces);
    
    // parse directly if it already fitted completely into the buffer
    if((rcode = asix_parse_conf(dev, i, buf.conf_desc.wTotalLength)) != 0) {
      asix_debugf("parse conf failed");
      return rcode;
    }
  }

  asix_debugf("supported device");

  if ((rcode = asix_write_gpio(dev, AX_GPIO_RSE | AX_GPIO_GPO_2 | AX_GPIO_GPO2EN, 5)) < 0) {
    asix_debugf("GPIO write failed");
    return rcode;
  }
    
  /* 0x10 is the phy id of the embedded 10/100 ethernet phy */
  int8_t embd_phy = ((asix_get_phy_addr(dev) & 0x1f) == 0x10 ? 1 : 0);
  asix_debugf("embedded phy = %d", embd_phy);
  if((rcode = asix_write_cmd(dev, AX_CMD_SW_PHY_SELECT, embd_phy, 0, 0, NULL)) != 0) {
    asix_debugf("Select PHY #1 failed");
    return rcode;
  }

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_IPPD | AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_IPPD | AX_SWRESET_PRL) failed");
    return rcode;
  }

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_CLEAR)) != 0) {
    asix_debugf("reset(AX_SWRESET_CLEAR) failed");
    return rcode;
  }

  if ((rcode = asix_sw_reset(dev, embd_phy?AX_SWRESET_IPRL:AX_SWRESET_PRTE)) != 0) {
    asix_debugf("reset(AX_SWRESET_IPRL/PRTE) failed");
    return rcode;
  }

  uint16_t rx_ctl = asix_read_rx_ctl(dev);
  asix_debugf("RX_CTL is 0x%04x after software reset", rx_ctl);

  if((rcode = asix_write_rx_ctl(dev, 0x0000)) != 0) {
    asix_debugf("write_rx_ctl(0) failed");
    return rcode;
  }

  rx_ctl = asix_read_rx_ctl(dev);
  asix_debugf("RX_CTL is 0x%04x setting to 0x0000", rx_ctl);

  /* Get the MAC address */
  if ((rcode = asix_read_cmd(dev, AX_CMD_READ_NODE_ID,
			   0, 0, ETH_ALEN, &buf.eaddr)) != 0) {
    asix_debugf("Failed to read MAC address: %d", rcode);
    return rcode;
  }

  iprintf("ASIX: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", 
	  buf.eaddr[0], buf.eaddr[1], buf.eaddr[2], 
	  buf.eaddr[3], buf.eaddr[4], buf.eaddr[5]); 

  // tell fpga about the mac address
  user_io_eth_send_mac(buf.eaddr);

  info->phy_id = asix_get_phy_addr(dev);

  uint32_t phyid = asix_get_phyid(dev);
  iprintf("ASIX: PHYID=0x%08x\n", phyid);

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_PRL) failed");
    return rcode;
  }
  
  if ((rcode = asix_sw_reset(dev, AX_SWRESET_IPRL | AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_IPRL | AX_SWRESET_PRL) failed");
    return rcode;
  }
  
  asix_mdio_write(dev, info->phy_id, MII_BMCR, BMCR_RESET);
  asix_mdio_write(dev, info->phy_id, MII_ADVERTISE, ADVERTISE_ALL | ADVERTISE_CSMA);

  mii_nway_restart(dev);

  if ((rcode = asix_write_medium_mode(dev, AX88772_MEDIUM_DEFAULT)) != 0) {
    asix_debugf("asix_write_medium_mode(AX88772_MEDIUM_DEFAULT) failed\n");
    return rcode;
  }

  if ((rcode = asix_write_cmd(dev, AX_CMD_WRITE_IPG0,
			      AX88772_IPG0_DEFAULT | AX88772_IPG1_DEFAULT,
			      AX88772_IPG2_DEFAULT, 0, NULL)) != 0) {
    asix_debugf("Write IPG,IPG1,IPG2 failed: %d", rcode);
    return rcode;
  }
 
  /* Set RX_CTL to default values with 2k buffer, and enable cactus */
  if ((rcode = asix_write_rx_ctl(dev, AX_DEFAULT_RX_CTL)) != 0)
    return rcode;

  rx_ctl = asix_read_rx_ctl(dev);
  iprintf("ASIX: RX_CTL is 0x%04x after all initializations\n", rx_ctl);

  rx_ctl = asix_read_medium_status(dev);
  iprintf("ASIX: Medium Status is 0x%04x after all initializations\n", rx_ctl);

  info->bPollEnable = true;

  rx_cnt = tx_cnt = 0;  // reset buffers

  // finally inform core about ethernet support
  tos_update_sysctrl(tos_system_ctrl() | TOS_CONTROL_ETHERNET);

  eth_present = 1;

  return 0;
}

static uint8_t usb_asix_release(usb_device_t *dev) {
  asix_debugf("%s()", __FUNCTION__);

  // remove/disable ethernet support
  tos_update_sysctrl(tos_system_ctrl() & (~TOS_CONTROL_ETHERNET));
  eth_present = 0;

  return 0;
}

void usb_asix_xmit(uint8_t *data, uint16_t len) {
  asix_debugf("out %d", len);

  if(!tx_cnt && (len <= MAX_FRAMELEN)) {
    memcpy(tx_buf, data, len);
    tx_cnt = len;
    tx_offset = 0;
  }
}

char testframe[] = {
  0x3c, 0x00, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x12, 0x34, 0x80, 0x5d, 0x4c, 0x67,
  0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x00, 0x01, 0x80, 0x5d, 0x4c, 0x67,
  0xc0, 0xa8, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0x64, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
};

static uint8_t usb_asix_poll(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);
  uint8_t rcode = 0;

  if (!info->bPollEnable)
    return 0;

  // poll interrupt endpoint
  if (info->qNextPollTime <= timer_get_msec()) {

    // ------------ v TEST STUFF v --------------
    { static cnt = 20; if(!--cnt) {
	iprintf("ETH status: %x\n", user_io_eth_get_status());

	usb_asix_xmit(testframe, sizeof(testframe));
	cnt = 20;
      }}
    // ------------ ^ TEST STUFF ^ --------------

    uint16_t read = info->ep[info->ep_int_idx].maxPktSize;
    uint8_t buf[info->ep[info->ep_int_idx].maxPktSize];
    uint8_t rcode = usb_in_transfer(dev, &(info->ep[info->ep_int_idx]), &read, buf);

    if (rcode) {
      if (rcode != hrNAK)
	iprintf("%s() error: %x\n", __FUNCTION__, rcode);
    } else {
      //      iprintf("ASIX: int %d bytes\n", read);
      //      hexdump(buf, read, 0);

      // primary or secondary link detected?
      bool link_detected = ((buf[2] & 3) != 0); 

      if(link_detected != info->linkDetected) {
	if(link_detected) {
	  iprintf("ASIX: Link detected\n");	  
	} else
	  iprintf("ASIX: Link lost\n");
	
	info->linkDetected = link_detected;
      }
    }


    // TODO: Do RX/TX handling at a much higher rate ...


    // check if there's something to transmit
    if(tx_cnt) {
      uint16_t bytes2send = (tx_cnt-tx_offset > info->ep[2].maxPktSize)?
	info->ep[2].maxPktSize:(tx_cnt-tx_offset);
      
      //      asix_debugf("bulk out %d of %d (ep %d), off %d", bytes2send, tx_cnt, info->ep[2].maxPktSize, tx_offset);  
      rcode = usb_out_transfer(dev, &(info->ep[2]), bytes2send, tx_buf + tx_offset);
      //      asix_debugf("%s() error: %x", __FUNCTION__, rcode);  

      tx_offset += bytes2send;

      // mark buffer as free after last pkt was sent
      if(bytes2send != info->ep[2].maxPktSize)
	tx_cnt = 0;
    }

    // Try to read from bulk in endpoint (ep 2). Raw packets are received this way.
    // The last USB packet being part of an ethernet frame is marked by being shorter
    // than the USB FIFO size. If the last packet is exaclty if FIFO size, then an
    // additional 0 byte packet is appended
    {
      uint16_t read = info->ep[1].maxPktSize;
      
      // the rx buffer size (1536) is a multiple of the maxPktSize (64),
      // so a transfer still fits into the buffer or it is already 
      // completely full. If it's full we drop all data. This will leave 
      // the buffered packet incomplete which isn't a problem since
      // the packet was too long, anyway.
      uint8_t *data = (rx_cnt <= MAX_FRAMELEN - info->ep[1].maxPktSize)?(rx_buf + rx_cnt):NULL;
      rcode = usb_in_transfer(dev, &(info->ep[1]), &read, data);
      
      if (rcode) {
	if (rcode != hrNAK)
	  asix_debugf("%s() error: %x", __FUNCTION__, rcode);
      } else {
	rx_cnt += read;
	
	if(read == info->ep[1].maxPktSize) {
	  
	} else {
	  asix_debugf("in %d", rx_cnt);
	  //	hexdump(rx_buf, rx_cnt, 0);
	  rx_cnt = 0;
	}
      }
    }    
    info->qNextPollTime = timer_get_msec() + info->int_poll_ms;
  }

  return rcode;
}

const usb_device_class_config_t usb_asix_class = {
  usb_asix_init, usb_asix_release, usb_asix_poll };  
