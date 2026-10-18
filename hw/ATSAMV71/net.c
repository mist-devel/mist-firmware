// net.c - lwIP glue for the MiST firmware (SAMV71 / SiDi128)
//
// The GMAC is shared with the FPGA core, which keeps using specific address 1
// (the historic fixed MAC). lwIP uses specific address 2 (see eth.c) so both
// can live on the same LAN with their own IP.

#include <string.h>
#include <stdio.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

#include "net.h"
#include "eth.h"
#include "ftpd.h"
#include "mist_cfg.h"

extern volatile unsigned long timer_ticks;

static struct netif nif;
static uint8_t fw_mac[6];
static uint8_t started;
static uint8_t link_up;
static uint8_t use_dhcp;
static uint8_t dhcp_started;
static uint8_t ip_reported;

u32_t sys_now(void)
{
	return (u32_t)timer_ticks;
}

unsigned int net_rand(void)
{
	static uint32_t x;
	x = x * 1664525u + 1013904223u + (uint32_t)timer_ticks + fw_mac[5] + (fw_mac[4] << 8);
	return x;
}

static err_t net_linkoutput(struct netif *n, struct pbuf *p)
{
	static uint8_t txbuf[1536] __attribute__ ((aligned(4)));
	(void)n;
	if (p->tot_len > sizeof(txbuf)) return ERR_BUF;
	pbuf_copy_partial(p, txbuf, p->tot_len, 0);
	return eth_tx_raw(txbuf, p->tot_len) ? ERR_OK : ERR_IF;
}

static err_t net_netif_init(struct netif *n)
{
	n->name[0] = 'e';
	n->name[1] = 'n';
	n->output = etharp_output;
	n->linkoutput = net_linkoutput;
	n->mtu = 1500;
	n->hwaddr_len = 6;
	memcpy(n->hwaddr, fw_mac, 6);
	n->hostname = "sidi128";
	n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
	return ERR_OK;
}

void net_init(const uint8_t *mac)
{
	memcpy(fw_mac, mac, 6);
	started = 0;
	link_up = 0;
	use_dhcp = 0;
	dhcp_started = 0;
	ip_reported = 0;
}

static void net_apply_link(void)
{
	if (link_up) {
		netif_set_link_up(&nif);
		if (use_dhcp && !dhcp_started) {
			dhcp_started = 1;
			dhcp_start(&nif);
		}
	} else {
		netif_set_link_down(&nif);
		ip_reported = 0;
	}
}

static void net_start(void)
{
	ip4_addr_t ip, mask, gw;
	int static_ip;

	ip4_addr_set_zero(&ip);
	ip4_addr_set_zero(&mask);
	ip4_addr_set_zero(&gw);

	static_ip = mist_cfg.net_ip[0] && ip4addr_aton(mist_cfg.net_ip, &ip);
	if (static_ip) {
		if (!mist_cfg.net_netmask[0] || !ip4addr_aton(mist_cfg.net_netmask, &mask))
			IP4_ADDR(&mask, 255, 255, 255, 0);
		if (mist_cfg.net_gateway[0]) ip4addr_aton(mist_cfg.net_gateway, &gw);
	}

	lwip_init();
	netif_add(&nif, &ip, &mask, &gw, NULL, net_netif_init, ethernet_input);
	netif_set_default(&nif);
	netif_set_up(&nif);
	started = 1;
	use_dhcp = static_ip ? 0 : 1;
	dhcp_started = 0;
	ftpd_init();
	net_apply_link();
	iprintf("Network: FTP server enabled (%s)\n", static_ip ? "static IP" : "DHCP");
}

void net_link(int up)
{
	link_up = up ? 1 : 0;
	if (started) net_apply_link();
}

int net_active(void)
{
	return started;
}

void net_poll(void)
{
	if (!started) {
		// config is read from mist.ini after eth_init(): start lazily
		if (mist_cfg.ftp_enable && mist_cfg.ftp_pass[0]) net_start();
		return;
	}

	sys_check_timeouts();

	if (!ip_reported && link_up && !ip4_addr_isany_val(*netif_ip4_addr(&nif))) {
		ip_reported = 1;
		iprintf("Network: IP address %s\n", ip4addr_ntoa(netif_ip4_addr(&nif)));
	}
}

int net_rx(uint8_t *frame, uint16_t len)
{
	struct pbuf *p;
	int for_us, bcast;

	if (!started || len < 14) return 0;

	for_us = !memcmp(frame, fw_mac, 6);
	bcast = frame[0] == 0xff && frame[1] == 0xff && frame[2] == 0xff &&
	        frame[3] == 0xff && frame[4] == 0xff && frame[5] == 0xff;
	if (!for_us && !bcast) return 0;

	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p) {
		pbuf_take(p, frame, len);
		if (nif.input(p, &nif) != ERR_OK) pbuf_free(p);
	}
	return for_us ? 1 : 0;
}

uint32_t net_get_ip(void)
{
	if (!started || !link_up) return 0;
	return ip4_addr_get_u32(netif_ip4_addr(&nif));
}

const char *net_ftp_state(void)
{
	if (!mist_cfg.ftp_enable) return "off";
	if (!mist_cfg.ftp_pass[0]) return "no password";
	if (!started) return "starting";
	if (!link_up) return "no link";
	if (!net_get_ip()) return "waiting DHCP";
	return ftpd_state();
}
