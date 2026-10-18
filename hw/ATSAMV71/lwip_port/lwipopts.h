/*
 * lwIP configuration for the MiST/SiDi128 firmware (SAMV71).
 * Bare-metal, polled, single threaded (NO_SYS=1, raw TCP API only).
 * Only used for the built-in FTP server (see ftpd.c / net.c).
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NOASSERT               1

#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1      /* needed by DHCP */
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_ACD                    0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_DNS                    0
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_ALTCP                  0
#define LWIP_STATS                  0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_SINGLE_NETIF           1

#define IP_REASSEMBLY               0
#define IP_FRAG                     0

/* memory: everything is static. Sized for 3 FTP sessions (ftpd.c FTP_SESSIONS). */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (28*1024)
#define MEMP_NUM_PBUF               8
#define MEMP_NUM_UDP_PCB            2
#define MEMP_NUM_TCP_PCB            10     /* 3 x (control + data) + closing/TIME_WAIT spares */
#define MEMP_NUM_TCP_PCB_LISTEN     5      /* ftp:21 + 3 passive ports + spare */
#define MEMP_NUM_TCP_SEG            40
#define MEMP_NUM_SYS_TIMEOUT        8
#define PBUF_POOL_SIZE              16

#define TCP_MSS                     1460
/* a large receive window keeps the PC sending while the firmware is busy writing 32 KB to the SD card
   (the GMAC RX ring in eth.c must hold a whole window: 14 x 12 units < 224) */
#define TCP_WND                     (14*TCP_MSS)
#define TCP_SND_BUF                 (12*TCP_MSS)
#define TCP_SND_QUEUELEN            ((2*TCP_SND_BUF)/TCP_MSS)
#define TCP_QUEUE_OOSEQ             0

#define LWIP_CHECKSUM_ON_COPY       0

extern unsigned int net_rand(void);
#define LWIP_RAND()                 ((u32_t)net_rand())

#endif /* LWIPOPTS_H */
