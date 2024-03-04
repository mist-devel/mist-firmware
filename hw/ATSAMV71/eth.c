#include <stdio.h>
#include "eth.h"
#include "hardware.h"
#include "debug.h"

#include "network/gmac.h"
#include "network/phy.h"
#include "network/ethd.h"
#include "network/gmii.h"

#include "user_io.h"

extern unsigned char sector_buffer[SECTOR_BUFFER_SIZE];

static struct _phy phy;
static struct _phy_desc phy_desc;
static struct _ethd ethd;
static uint8_t mac[] = {0x02, 0x00, 0x01, 0x02, 0x03, 0x04};

#define RX_BUFFERS 20
#define TX_BUFFERS 5
static uint8_t rx_buffer[ETH_RX_UNITSIZE*RX_BUFFERS] __attribute__ ((aligned));
static uint8_t tx_buffer[ETH_TX_UNITSIZE*TX_BUFFERS] __attribute__ ((aligned));
static struct _eth_desc rx_desc[RX_BUFFERS] __attribute__ ((aligned));
static struct _eth_desc tx_desc[TX_BUFFERS] __attribute__ ((aligned));

static char link = 0;
static char link_changed = 0;

static unsigned long timerMAC;

#define MAX_FRAMELEN 1536
static uint32_t old_status;

static void PIOAIrqHandler()
{
	volatile uint32_t isr = PIOA->PIO_ISR;
	volatile uint16_t phy_icsr;
	phy_read_icsr(&phy, &phy_icsr);
	link_changed = 1;
};

int eth_init()
{
	int retval;
	link = 0;
	link_changed = 0;
	//PIOA->PIO_SODR = PHY_RESET;
	ethd_configure(&ethd, ETH_TYPE_GMAC, GMAC0, 0, 0);
	ethd_set_mac_addr(&ethd, 0, mac);
	ethd_setup_queue(&ethd, 0, RX_BUFFERS, rx_buffer, rx_desc, TX_BUFFERS, tx_buffer, tx_desc, 0);

	phy_desc.phy_if = PHY_IF_GMAC;
	phy_desc.addr = GMAC0;
	phy_desc.phy_addr = 1;
	phy_desc.timeout.idle = 100;
	phy_desc.timeout.autoneg = 2000;
	phy.desc = &phy_desc;
	if(retval=phy_configure(&phy)) {
		eth_info("Error configuring Ethernet PHY %d\n", retval);
		return retval;
	}
	phy_write_icsr(&phy, 0x0500); // enable link-up/down interrupts
	//phy_dump_registers(&phy);

	NVIC_SetVector(ID_PIOA, (uint32_t) &PIOAIrqHandler);
	NVIC_EnableIRQ(ID_PIOA);
	PIOA->PIO_ESR = PHY_INT; // edge triggered irq
	PIOA->PIO_FELLSR = PHY_INT; // detect falling edge
	PIOA->PIO_IER = PHY_INT;
	timerMAC = 0;
	ethd_start(&ethd);
	return 0;
}

int eth_poll()
{
	if (link_changed) {
		NVIC_DisableIRQ(ID_PIOA); // disable link-up/down interrupts

		uint16_t bmsr;
		int status = phy_get_link_status(&phy);

		if (status == -1) {
			//
		} else if (link == status) {
			link_changed = 0;
		} else {
			link = status;
			eth_info("Link %s", link ? "detected" : "broken");
			if (link) phy_auto_negotiate(&phy, 1);
			//phy_dump_registers(&phy);
			link_changed = 0;
		}
		NVIC_EnableIRQ(ID_PIOA); // enable link-up/down interrupts
		return status;
	}

	if (CheckTimer(timerMAC)) {
		user_io_eth_send_mac(mac);
		timerMAC = GetTimer(2000);
	}

	if (!link) return 0;

	uint32_t status = user_io_eth_get_status();

	if(status != old_status) {
		eth_debug("fpga status changed to cmd %x, eq=%d, prx=%d, ptx=%d, len=%d",
		  status >> 24, (status & 0x40000)?1:0, (status & 0x20000)?1:0,
		  (status & 0x10000)?1:0, status & 0xffff);
		old_status = status;
	}

	if((status >> 24) == 0xa5) {
		uint16_t len = status & 0xffff;

		if(len <= MAX_FRAMELEN) {
			user_io_eth_receive_tx_frame(sector_buffer, len);
			//iprintf("sending packet: %d bytes\n", len);
			//hexdump(sector_buffer, len, 0);
			ethd_send(&ethd, 0, sector_buffer, len, 0);
		}
	}

	if(!(status & 0x20000)) {

		uint32_t recv_size = 0;
		if (ethd_poll(&ethd, 0, sector_buffer, SECTOR_BUFFER_SIZE, &recv_size) == ETH_OK) {
			if (recv_size) {
				user_io_eth_send_rx_frame(sector_buffer, recv_size);
				//iprintf("received packet: %d bytes\n", recv_size);
				//hexdump(sector_buffer, recv_size, 0);
			}
		}
	}
	return 0;
}

void eth_get_mac(uint8_t* mac)
{
	ethd_get_mac_addr(&ethd, 0, mac);
}

uint8_t eth_get_link_status()
{
	uint8_t retval = 0;
	if (link) {
		uint32_t ncfgr = GMAC0->GMAC_NCFGR;
		retval |= 0x01;
		if (ncfgr & GMAC_NCFGR_SPD) retval |= 0x02; // 100Mbps
		if (ncfgr & GMAC_NCFGR_FD) retval |= 0x04;  // Full-Duplex
	}
	return retval;
}
