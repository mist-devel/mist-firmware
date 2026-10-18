#ifndef NET_H
#define NET_H

#include <stdint.h>

// firmware-side IP stack (lwIP) used by the FTP server.
// It shares the GMAC with the FPGA core: it owns a second MAC address and only
// consumes frames addressed to it; broadcasts are seen by both sides.

void net_init(const uint8_t *mac);          // called once from eth_init()
void net_link(int up);                      // PHY link state changed
void net_poll(void);                        // lwIP timers, lazy start, called from eth_poll()
int  net_active(void);                      // stack running (needs GMAC polling independent of the core)
int  net_rx(uint8_t *frame, uint16_t len);  // feed a received frame; returns 0 if the core should get it too

// status for the OSD
// returns 0 if no address yet, otherwise the IPv4 address in network byte order
uint32_t net_get_ip(void);
const char *net_ftp_state(void);            // short text (<= 13 chars)

#endif
