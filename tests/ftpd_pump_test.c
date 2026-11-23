#define FTPD_PUMP_TEST
#include "../ftpd.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct tcp_pcb control_pcb={1},data_pcb={2};
static uint8_t source[4096],transfer_buffer[2048],received[4096];
static unsigned source_size,source_pos,send_room,queued_bytes,received_bytes;
static unsigned reads,writes,outputs,finishes,finish_success;
static unsigned fail_write,fail_output,fail_read;

static void reset(struct sess *s,unsigned bytes,unsigned buffer_bytes,unsigned room)
{
    memset(s,0,sizeof(*s));
    s->ctl=&control_pcb;s->dat=&data_pcb;s->dat_ready=1;s->started=1;
    s->op=OP_RETR;s->buf=transfer_buffer;s->bufsz=(uint16_t)buffer_bytes;s->dat_idle=8;
    source_size=bytes;source_pos=queued_bytes=received_bytes=0;send_room=room;
    reads=writes=outputs=finishes=finish_success=0;
    fail_write=fail_output=fail_read=0;
    for(unsigned i=0;i<sizeof(source);i++) source[i]=(uint8_t)i;
}
static u16_t tcp_sndbuf(struct tcp_pcb *pcb)
{
    assert(pcb==&data_pcb);
    return (u16_t)send_room;
}
static int tcp_write(struct tcp_pcb *pcb,const void *data,u16_t bytes,unsigned flags)
{
    assert(pcb==&data_pcb && flags==TCP_WRITE_FLAG_COPY);
    writes++;
    if(fail_write) {fail_write=0;return ERR_MEM;}
    assert(bytes<=send_room && queued_bytes+bytes<=sizeof(received));
    memcpy(received+queued_bytes,data,bytes);queued_bytes+=bytes;send_room-=bytes;
    return ERR_OK;
}
static int tcp_output(struct tcp_pcb *pcb)
{
    assert(pcb==&data_pcb);outputs++;
    if(fail_output) {fail_output=0;return ERR_MEM;}
    received_bytes=queued_bytes;
    return ERR_OK;
}
static int f_read(FIL *file,void *data,UINT bytes,UINT *read_bytes)
{
    (void)file;reads++;
    if(fail_read) {fail_read=0;return FR_DISK_ERR;}
    unsigned remaining=source_size-source_pos;
    if(bytes>remaining) bytes=remaining;
    memcpy(data,source+source_pos,bytes);source_pos+=bytes;*read_bytes=bytes;
    return FR_OK;
}
static void list_next(struct sess *s) {(void)s;assert(!"unexpected directory read");}
static void op_finish(struct sess *s,int success,const char *message)
{
    assert(!success || !message);
    finishes++;finish_success=(unsigned)success;s->started=0;s->op=OP_NONE;
}

static void partial_buffer_exhaustion_flushes(void)
{
    struct sess s;reset(&s,2048,2048,1024);
    pump(&s);
    assert(outputs==1 && received_bytes==1024 && s.xpos==1024 && writes==1 && finishes==0);
    send_room=1024;pump(&s);
    assert(outputs==2 && received_bytes==2048 && writes==2 && finishes==0);
    send_room=1024;pump(&s);
    assert(finishes==1 && finish_success && !memcmp(source,received,2048));
}
static void exact_buffer_exhaustion_flushes(void)
{
    struct sess s;reset(&s,1024,1024,1024);
    pump(&s);
    assert(outputs==1 && received_bytes==1024 && reads==1 && finishes==0);
    send_room=1024;pump(&s);
    assert(finishes==1 && finish_success && received_bytes==1024);
    pump(&s);assert(finishes==1);
}
static void write_error_does_not_advance_or_duplicate(void)
{
    struct sess s;reset(&s,64,64,1024);fail_write=1;
    pump(&s);
    assert(writes==1 && outputs==1 && s.xpos==0 && s.xlen==64 && queued_bytes==0 && finishes==0);
    pump(&s);
    assert(queued_bytes==64 && received_bytes==64 && finishes==0 && !memcmp(source,received,64));
    send_room=1024;pump(&s);assert(finishes==1 && finish_success);
}
static void output_retry_keeps_accepted_position(void)
{
    struct sess s;reset(&s,2048,2048,1024);fail_output=1;
    pump(&s);
    assert(outputs==1 && received_bytes==0 && s.xpos==1024 && queued_bytes==1024 && writes==1);
    pump(&s);
    assert(outputs==2 && received_bytes==1024 && s.xpos==1024 && queued_bytes==1024 && writes==1);
    send_room=1024;pump(&s);
    assert(received_bytes==2048 && queued_bytes==2048 && writes==2 && !memcmp(source,received,2048));
    send_room=1024;pump(&s);assert(finishes==1 && finish_success);
}
static void eof_and_read_error_finish_once(void)
{
    struct sess s;reset(&s,0,64,1024);pump(&s);pump(&s);
    assert(outputs==1 && finishes==1 && finish_success);
    reset(&s,64,64,1024);fail_read=1;pump(&s);pump(&s);
    assert(finishes==1 && !finish_success && queued_bytes==0);
}
int main(void)
{
    partial_buffer_exhaustion_flushes();exact_buffer_exhaustion_flushes();
    write_error_does_not_advance_or_duplicate();output_retry_keeps_accepted_position();
    eof_and_read_error_finish_once();
    puts("ftpd pump tests passed");
    return 0;
}
