#ifndef ETH_H
#define ETH_H

#include <stdint.h>

int eth_init();
int eth_poll();
void eth_get_mac(uint8_t* mac);
uint8_t eth_get_link_status();

#endif
