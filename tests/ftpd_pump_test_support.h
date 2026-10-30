#ifndef FTPD_PUMP_TEST_SUPPORT_H
#define FTPD_PUMP_TEST_SUPPORT_H
#include <stdint.h>
#include <stddef.h>

typedef uint16_t u16_t;
typedef unsigned UINT;
typedef struct { unsigned unused; } FIL;
struct tcp_pcb { unsigned marker; };
enum { OP_NONE, OP_LIST, OP_NLST, OP_RETR, OP_STOR };
enum { ERR_OK, ERR_MEM, FR_OK, FR_DISK_ERR };
#define TCP_WRITE_FLAG_COPY 1
struct sess {
    struct tcp_pcb *ctl, *dat;
    unsigned dat_ready, started, op, eof, dat_idle;
    uint16_t xpos, xlen, bufsz;
    uint8_t *buf;
    uint8_t xbuf[4096];
    FIL fil;
};
static u16_t tcp_sndbuf(struct tcp_pcb *pcb);
static int tcp_write(struct tcp_pcb *pcb,const void *data,u16_t bytes,unsigned flags);
static int tcp_output(struct tcp_pcb *pcb);
static int f_read(FIL *file,void *data,UINT bytes,UINT *read_bytes);
static void list_next(struct sess *s);
static void op_finish(struct sess *s,int success,const char *message);
#endif
