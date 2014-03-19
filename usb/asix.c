//
// asix.c
//
// http://lxr.free-electrons.com/source/drivers/net/usb/asix.c?v=3.1

/*
    Interface Descriptor:
      bLength                 9
      bDescriptorType         4
      bInterfaceNumber        0
      bAlternateSetting       0
      bNumEndpoints           3
      bInterfaceClass       255 Vendor Specific Class
      bInterfaceSubClass    255 Vendor Specific Subclass
      bInterfaceProtocol      0 
      iInterface              7 
      Endpoint Descriptor:
        bLength                 7
        bDescriptorType         5
        bEndpointAddress     0x81  EP 1 IN
        bmAttributes            3
          Transfer Type            Interrupt
          Synch Type               None
          Usage Type               Data
        wMaxPacketSize     0x0008  1x 8 bytes
        bInterval              11
      Endpoint Descriptor:
        bLength                 7
        bDescriptorType         5
        bEndpointAddress     0x82  EP 2 IN
        bmAttributes            2
          Transfer Type            Bulk
          Synch Type               None
          Usage Type               Data
        wMaxPacketSize     0x0200  1x 512 bytes
        bInterval               0
      Endpoint Descriptor:
        bLength                 7
        bDescriptorType         5
        bEndpointAddress     0x03  EP 3 OUT
        bmAttributes            2
          Transfer Type            Bulk
          Synch Type               None
          Usage Type               Data
        wMaxPacketSize     0x0200  1x 512 bytes
        bInterval               0
*/

#include <stdio.h>

#include "debug.h"
#include "usb.h"
#include "asix.h"
#include "timer.h"
#include "mii.h"
#include "max3421e.h"

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

#define ETH_ALEN        6

/* ASIX AX8817X based USB 2.0 Ethernet Devices */
#define AX_CMD_SET_SW_MII               0x06
#define AX_CMD_READ_MII_REG             0x07
#define AX_CMD_WRITE_MII_REG            0x08
#define AX_CMD_SET_HW_MII               0x0a
#define AX_CMD_READ_EEPROM              0x0b
#define AX_CMD_WRITE_EEPROM             0x0c
#define AX_CMD_WRITE_ENABLE             0x0d
#define AX_CMD_WRITE_DISABLE            0x0e
#define AX_CMD_READ_RX_CTL              0x0f
#define AX_CMD_WRITE_RX_CTL             0x10
#define AX_CMD_READ_IPG012              0x11
#define AX_CMD_WRITE_IPG0               0x12
#define AX_CMD_WRITE_IPG1               0x13
#define AX_CMD_READ_NODE_ID             0x13
#define AX_CMD_WRITE_IPG2               0x14
#define AX_CMD_WRITE_MULTI_FILTER       0x16
#define AX88172_CMD_READ_NODE_ID        0x17
#define AX_CMD_READ_PHY_ID              0x19
#define AX_CMD_READ_MEDIUM_STATUS       0x1a
#define AX_CMD_WRITE_MEDIUM_MODE        0x1b
#define AX_CMD_READ_MONITOR_MODE        0x1c
#define AX_CMD_WRITE_MONITOR_MODE       0x1d
#define AX_CMD_READ_GPIOS               0x1e
#define AX_CMD_WRITE_GPIOS              0x1f
#define AX_CMD_SW_RESET                 0x20
#define AX_CMD_SW_PHY_STATUS            0x21
#define AX_CMD_SW_PHY_SELECT            0x22

#define AX_SWRESET_CLEAR                0x00
#define AX_SWRESET_RR                   0x01
#define AX_SWRESET_RT                   0x02
#define AX_SWRESET_PRTE                 0x04
#define AX_SWRESET_PRL                  0x08
#define AX_SWRESET_BZ                   0x10
#define AX_SWRESET_IPRL                 0x20
#define AX_SWRESET_IPPD                 0x40

/* AX88772 & AX88178 RX_CTL values */
#define AX_RX_CTL_SO                    0x0080
#define AX_RX_CTL_AP                    0x0020
#define AX_RX_CTL_AM                    0x0010
#define AX_RX_CTL_AB                    0x0008
#define AX_RX_CTL_SEP                   0x0004
#define AX_RX_CTL_AMALL                 0x0002
#define AX_RX_CTL_PRO                   0x0001
#define AX_RX_CTL_MFB_2048              0x0000
#define AX_RX_CTL_MFB_4096              0x0100
#define AX_RX_CTL_MFB_8192              0x0200
#define AX_RX_CTL_MFB_16384             0x0300

#define AX88772_IPG0_DEFAULT            0x15
#define AX88772_IPG1_DEFAULT            0x0c
#define AX88772_IPG2_DEFAULT            0x12

/* AX88772 & AX88178 Medium Mode Register */
#define AX_MEDIUM_PF            0x0080
#define AX_MEDIUM_JFE           0x0040
#define AX_MEDIUM_TFC           0x0020
#define AX_MEDIUM_RFC           0x0010
#define AX_MEDIUM_ENCK          0x0008
#define AX_MEDIUM_AC            0x0004
#define AX_MEDIUM_FD            0x0002
#define AX_MEDIUM_GM            0x0001
#define AX_MEDIUM_SM            0x1000
#define AX_MEDIUM_SBP           0x0800
#define AX_MEDIUM_PS            0x0200
#define AX_MEDIUM_RE            0x0100

#define AX88178_MEDIUM_DEFAULT  \
        (AX_MEDIUM_PS | AX_MEDIUM_FD | AX_MEDIUM_AC | \
         AX_MEDIUM_RFC | AX_MEDIUM_TFC | AX_MEDIUM_JFE | \
         AX_MEDIUM_RE )

#define AX88772_MEDIUM_DEFAULT  \
        (AX_MEDIUM_FD | AX_MEDIUM_RFC | \
         AX_MEDIUM_TFC | AX_MEDIUM_PS | \
         AX_MEDIUM_AC | AX_MEDIUM_RE )

/* AX88772 & AX88178 RX_CTL values */
#define AX_RX_CTL_SO                    0x0080
#define AX_RX_CTL_AP                    0x0020
#define AX_RX_CTL_AM                    0x0010
#define AX_RX_CTL_AB                    0x0008
#define AX_RX_CTL_SEP                   0x0004
#define AX_RX_CTL_AMALL                 0x0002
#define AX_RX_CTL_PRO                   0x0001
#define AX_RX_CTL_MFB_2048              0x0000
#define AX_RX_CTL_MFB_4096              0x0100
#define AX_RX_CTL_MFB_8192              0x0200
#define AX_RX_CTL_MFB_16384             0x0300

#define AX_DEFAULT_RX_CTL       \
        (AX_RX_CTL_SO | AX_RX_CTL_AB )

/* GPIO 0 .. 2 toggles */
#define AX_GPIO_GPO0EN          0x01    /* GPIO0 Output enable */
#define AX_GPIO_GPO_0           0x02    /* GPIO0 Output value */
#define AX_GPIO_GPO1EN          0x04    /* GPIO1 Output enable */
#define AX_GPIO_GPO_1           0x08    /* GPIO1 Output value */
#define AX_GPIO_GPO2EN          0x10    /* GPIO2 Output enable */
#define AX_GPIO_GPO_2           0x20    /* GPIO2 Output value */
#define AX_GPIO_RESERVED        0x40    /* Reserved */
#define AX_GPIO_RSE             0x80    /* Reload serial EEPROM */

#define ASIX_REQ_OUT   USB_SETUP_HOST_TO_DEVICE|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE
#define ASIX_REQ_IN    USB_SETUP_DEVICE_TO_HOST|USB_SETUP_TYPE_VENDOR|USB_SETUP_RECIPIENT_DEVICE

static uint8_t asix_write_cmd(usb_device_t *dev, uint8_t cmd, uint16_t value, uint16_t index,
			      uint16_t size, uint8_t *data) {
  asix_debugf("%s() cmd=0x%02x value=0x%04x index=0x%04x size=%d", __FUNCTION__,
	      cmd, value, index, size);

  return(usb_ctrl_req( dev, ASIX_REQ_OUT, cmd, index, value, 0, size, data));
}

static uint8_t asix_read_cmd(usb_device_t  *dev, uint8_t cmd, uint16_t value, uint16_t index,
			     uint16_t size, void *data) {
  uint8_t buf[size];

  asix_debugf("asix_read_cmd() cmd=0x%02x value=0x%04x index=0x%04x size=%d",
	      cmd, value, index, size);
 
  return(usb_ctrl_req( dev, ASIX_REQ_IN, cmd, index, value, 0, size, data));
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
  union {
    uint8_t b[2];
    uint16_t w;
  } buf;

  uint8_t ret = asix_read_cmd(dev, AX_CMD_READ_PHY_ID, 0, 0, sizeof(buf), &buf);
  
  asix_debugf("%s()", __FUNCTION__);
 
  if (ret != 0) {
    asix_debugf("Error reading PHYID register: %02x", ret);
    return ret;
  }

  asix_debugf("returning 0x%04x", buf.w);

  return buf.b[1];
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

static uint8_t asix_sw_reset(usb_device_t *dev, uint8_t flags) {
  uint8_t rcode;
  
  rcode = asix_write_cmd(dev, AX_CMD_SW_RESET, flags, 0, 0, NULL);
  if (rcode != 0)
    asix_debugf("Failed to send software reset: %02x", rcode);

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

static uint8_t usb_asix_init(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);
  uint8_t i, rcode = 0;

  // reset status
  info->qNextPollTime = 0;
  info->bPollEnable = false;

  info->ep.epAddr	= 1;
  info->ep.maxPktSize	= 8;
  info->ep.epAttribs	= 0;
  info->ep.bmNakPower	= USB_NAK_NOWAIT;

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
    
  asix_debugf("supported device");

  if((rcode = asix_write_gpio(dev, AX_GPIO_RSE | AX_GPIO_GPO_2 | AX_GPIO_GPO2EN, 5)) != 0) {
    asix_debugf("setting gpio failed\n");
    return rcode;
  }

  /* 0x10 is the phy id of the embedded 10/100 ethernet phy */
  int8_t embd_phy = ((asix_get_phy_addr(dev) & 0x1f) == 0x10 ? 1 : 0);
  if((rcode = asix_write_cmd(dev, AX_CMD_SW_PHY_SELECT, embd_phy, 0, 0, NULL)) != 0) {
    asix_debugf("Select PHY #1 failed");
    return rcode;
  }

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_IPPD | AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_IPPD | AX_SWRESET_PRL) failed");
    return rcode;
  }

  timer_delay_msec(150);

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_CLEAR)) != 0) {
    asix_debugf("reset(AX_SWRESET_CLEAR) failed");
    return rcode;
  }

  timer_delay_msec(150);

  if (embd_phy) {
    if ((rcode = asix_sw_reset(dev, AX_SWRESET_IPRL)) != 0) {
      asix_debugf("reset(AX_SWRESET_IPRL) failed");
      return rcode;
    }
  } else {
    if ((rcode = asix_sw_reset(dev, AX_SWRESET_PRTE)) != 0) {
      asix_debugf("reset(AX_SWRESET_PRTE) failed");
      return rcode;
    }
  }
  
  timer_delay_msec(150);

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

  info->phy_id = asix_get_phy_addr(dev);

  uint32_t phyid = asix_get_phyid(dev);
  iprintf("ASIX: PHYID=0x%08x\n", phyid);

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_PRL) failed");
    return rcode;
  }
  
  timer_delay_msec(150);

  if ((rcode = asix_sw_reset(dev, AX_SWRESET_IPRL | AX_SWRESET_PRL)) != 0) {
    asix_debugf("reset(AX_SWRESET_IPRL | AX_SWRESET_PRL) failed");
    return rcode;
  }
  
  timer_delay_msec(150);

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

  iprintf("status = %x (%x)\n", asix_mdio_read(dev, info->phy_id, MII_BMSR),  12);

  rx_ctl = asix_read_rx_ctl(dev);
  iprintf("ASIX: RX_CTL is 0x%04x after all initializations\n", rx_ctl);

  rx_ctl = asix_read_medium_status(dev);
  iprintf("ASIX: Medium Status is 0x%04x after all initializations\n", rx_ctl);

  info->bPollEnable = true;

  return 0;
}

static uint8_t usb_asix_release(usb_device_t *dev) {
  asix_debugf("%s()", __FUNCTION__);
  return 0;
}

static uint8_t usb_asix_poll(usb_device_t *dev) {
  usb_asix_info_t *info = &(dev->asix_info);
  uint8_t rcode = 0;

  if (!info->bPollEnable)
    return 0;
  
  if (info->qNextPollTime <= timer_get_msec()) {
    uint16_t read = 1; // info->ep.maxPktSize;
    uint8_t buf[info->ep.maxPktSize];
    uint8_t rcode = 
      usb_in_transfer(dev, &(info->ep), &read, buf);

    /* d = hrJERR */

    if (rcode) {
      //      if (rcode != hrNAK)
      //	iprintf("%s() error: %x\n", __FUNCTION__, rcode);
      //	else
      //	  puts("nak");
    } else {
      iprintf("ASIX: %d bytes\n", read);

      
    }

    info->qNextPollTime = timer_get_msec() + 1000;   // poll 1 times a second
  }

  return rcode;
}

const usb_device_class_config_t usb_asix_class = {
  usb_asix_init, usb_asix_release, usb_asix_poll };  
