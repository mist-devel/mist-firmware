#include "video_recorder.h"
#include "video_format.h"
#include "video_schedule.h"
#ifdef VIDEO_RECORDER_TEST
#include "tests/video_recorder_test_support.h"
#else
#include "cab_player.h"
#include "remote_input.h"
#include "user_io.h"
#include "hardware.h"
#include "spi.h"
#include "fat_compat.h"
#include "usbdev.h"
#endif
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>

/* Row ownership ends at the next recorder poll; sector_buffer is SPI scratch. */
static uint8_t line_current[1024];
static uint8_t line_copy[1024];
static struct {
    char path[260], set[96];
    uint8_t valid;
} rom;
static struct {
    FIL index, output, previous;
    uint32_t owner, total, done, period, next_poll, start_ms, last_progress, stall_timeout;
    uint32_t width, height, stride, pixel_bytes, received;
    uint32_t requested, actual;
    uint32_t snapshot_queued_at, bmp_stride, bmp_size;
    uint16_t step, expected_y, residue, pending_y, replay_y, replay_residue;
    uint32_t replay_remaining;
    uint16_t tagged_width;
    uint8_t format;
    uint8_t active, index_open, output_open, previous_open, state;
    uint8_t have_previous, duplicate, pending_row, temporary_created;
    uint8_t snapshot_queued, snapshot_mode, snapshot_result;
    char dir[180], error[96], image_name[48], previous_name[48], snapshot_path[240];
    char temporary[240], final[240], previous_path[240];
} recorder;
static uint32_t run_sequence;

enum { REC_IDLE=0, REC_RECEIVE=1, REC_REPLAY=2, REC_PENDING=3,
       REC_FINALIZE=4, REC_PUBLISH=5 };

#define SNAPSHOT_OWNER 0x53534e50UL

static void put32(uint8_t *p,uint32_t value)
{
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}

static int open_output(void);

static void send_spi(unsigned cmd,const uint8_t *data,unsigned bytes)
{
    spi_uio_cmd_cont(cmd);
    for(unsigned i=0;i<bytes;i++) SPI(data[i]);
    DisableIO();
}
static int read_info(uint16_t *width,uint16_t *height,uint32_t *frame)
{
    uint8_t info[8];
    spi_uio_cmd_cont(0x90);
    for(unsigned i=0;i<sizeof(info);i++) info[i]=SPI(0);
    DisableIO();
    *width=video_le16(info);
    *height=video_le16(info+2);
    *frame=(uint32_t)info[4]|((uint32_t)info[5]<<8)|((uint32_t)info[6]<<16)|((uint32_t)info[7]<<24);
    return 1;
}
static int write_all(FIL *f,const void *p,unsigned bytes)
{
    UINT n;
    return f_write(f,p,bytes,&n)==FR_OK && n==bytes;
}
static int seek_exact(FIL *f,uint32_t offset)
{
    return f_lseek(f,(FSIZE_t)offset)==FR_OK && f_tell(f)==(FSIZE_t)offset;
}
static int read_exact(FIL *f,void *p,unsigned bytes)
{
    UINT n=0;
    return f_read(f,p,bytes,&n)==FR_OK && n==bytes;
}
static int index_record(const char *format,...)
{
    char line[768];
    va_list ap;
    va_start(ap,format);
    int n=vsnprintf(line,sizeof(line),format,ap);
    va_end(ap);
    return n>0 && (unsigned)n<sizeof(line) && write_all(&recorder.index,line,(unsigned)n) &&
           f_sync(&recorder.index)==FR_OK;
}
static void json_escape(char *dst,unsigned size,const char *src)
{
    unsigned n=0;
    while(*src && n+2<size) {
        unsigned char c=(unsigned char)*src++;
        if(c=='"' || c=='\\') {dst[n++]='\\';dst[n++]=(char)c;}
        else if(c<0x20) {dst[n++]='_';}
        else dst[n++]=(char)c;
    }
    dst[n]=0;
}
static void abort_fpga(void) {send_spi(0x99,NULL,0);}
void video_recorder_stop(void)
{
    int snapshot=recorder.snapshot_queued || (recorder.snapshot_mode && recorder.active);
    if(recorder.active) {
        abort_fpga();
        if(recorder.index_open)
            index_record("{\"type\":\"cancelled\",\"completed\":%lu,\"error\":\"%s\"}\n",
                         (unsigned long)recorder.done,recorder.error);
    }
    if(recorder.output_open) {
        f_close(&recorder.output);recorder.output_open=0;
    }
    if(recorder.temporary_created) {
        f_unlink(recorder.snapshot_mode ? recorder.final : recorder.temporary);
        recorder.temporary_created=0;
    }
    if(recorder.index_open) {f_close(&recorder.index);recorder.index_open=0;}
    if(recorder.previous_open) {f_close(&recorder.previous);recorder.previous_open=0;}
    recorder.active=0;
    if(!cab_player_active()) remote_input_release(recorder.owner);
    if(snapshot && recorder.snapshot_result==0) {
        recorder.snapshot_result=2;
        if(!recorder.error[0]) snprintf(recorder.error,sizeof(recorder.error),"Snapshot cancelled");
    }
    recorder.snapshot_queued=0;
}
static int failed(const char *reason)
{
    snprintf(recorder.error,sizeof(recorder.error),"%s",reason);
    video_recorder_stop();
    return 0;
}
void video_rom_clear(void)
{
    video_recorder_stop();
    memset(&rom,0,sizeof(rom));
}
void video_rom_loaded(FIL *file,const char *path)
{
    (void)file;
    const char *base=strrchr(path,'/');
    base=base ? base+1 : path;
    memset(&rom,0,sizeof(rom));
    int path_bytes=path[0]=='/' ? snprintf(rom.path,sizeof(rom.path),"%s",path) :
        snprintf(rom.path,sizeof(rom.path),"%s/%s",CurrentDirectoryName(),path);
    if(path_bytes<0 || (unsigned)path_bytes>=sizeof(rom.path)) return;
    const char *end=strrchr(base,'.');
    if(!end) end=base+strlen(base);
    unsigned len=0;
    while(base<end && len<sizeof(rom.set)-1) {
        unsigned char c=(unsigned char)*base++;
        rom.set[len++]=isalnum(c)||c=='_'||c=='-' ? (char)tolower(c) : '_';
    }
    if(!len || base!=end) return;
    rom.valid=1;
}
static int open_previous(void)
{
    if(!recorder.have_previous) return 1;
    if(f_open(&recorder.previous,recorder.previous_path,FA_READ)!=FR_OK)
        return failed("Cannot open previous image");
    recorder.previous_open=1;
    uint8_t header[4];
    if(f_size(&recorder.previous)!=recorder.pixel_bytes+4U ||
       !seek_exact(&recorder.previous,0) || !read_exact(&recorder.previous,header,sizeof(header)) ||
       video_le16(header)!=recorder.tagged_width || video_le16(header+2)!=recorder.height) {
        return failed("Previous image geometry or length is invalid");
    }
    return 1;
}
static int start_capture(void)
{
    uint8_t payload[6];
    video_put16(payload,(uint16_t)recorder.requested);
    payload[2]=(uint8_t)(recorder.requested>>16);
    payload[3]=(uint8_t)(recorder.requested>>24);
    video_put16(payload+4,recorder.step);
    recorder.received=0;
    recorder.expected_y=0;
    recorder.residue=0;
    recorder.actual=0;
    recorder.pending_row=0;
    recorder.duplicate=0;
    recorder.replay_remaining=0;
    recorder.state=REC_RECEIVE;
    recorder.last_progress=(uint32_t)GetRTTC();
    recorder.next_poll=recorder.last_progress;
    if(!open_previous()) return 0;
    send_spi(0x91,payload,sizeof(payload));
    return 1;
}

static int start_snapshot(void)
{
    uint16_t tagged_width,height;
    uint32_t frame;
    if(recorder.active || !rom.valid || !fat_medium_present() || usb_storage_is_configured() ||
       user_io_raw_sd_in_use() || cab_player_active() ||
       !(user_io_get_core_features()&FEAT_VIDEO_CAPTURE) || remote_input_owner()) {
        snprintf(recorder.error,sizeof(recorder.error),"Snapshot conditions changed before capture");
        return 0;
    }
    if(!read_info(&tagged_width,&height,&frame)) {
        snprintf(recorder.error,sizeof(recorder.error),"Video INFO transaction failed");
        return 0;
    }
    unsigned format=(tagged_width&VIDEO_FORMAT_FLAG) ? VIDEO_FORMAT_RGB555 : VIDEO_FORMAT_RGB444;
    unsigned width=tagged_width&VIDEO_WIDTH_MASK;
    if(!width || !height || width>512 || height>512 || frame==UINT32_MAX) {
        snprintf(recorder.error,sizeof(recorder.error),"Native geometry or frame is unsupported");
        return 0;
    }
    if(!remote_input_set(SNAPSHOT_OWNER,0,0,(uint32_t)GetRTTC())) {
        snprintf(recorder.error,sizeof(recorder.error),"Inputs are owned by another session");
        return 0;
    }
    memset(&recorder,0,sizeof(recorder));
    recorder.owner=SNAPSHOT_OWNER;
    recorder.active=1;
    recorder.snapshot_mode=1;
    recorder.total=1;
    recorder.width=width;
    recorder.height=height;
    recorder.format=(uint8_t)format;
    recorder.tagged_width=tagged_width;
    recorder.stride=video_stride(width,format);
    recorder.pixel_bytes=recorder.stride*height;
    recorder.bmp_stride=(3U*width+3U)&~3U;
    recorder.bmp_size=54U+recorder.bmp_stride*height;
    recorder.step=height<16 ? (uint16_t)height : 16;
    recorder.requested=frame+1;
    recorder.stall_timeout=15000;
    recorder.start_ms=(uint32_t)GetRTTC();
    recorder.last_progress=recorder.start_ms;
    recorder.next_poll=recorder.start_ms;
    snprintf(recorder.dir,sizeof(recorder.dir),"/video/%s",rom.set);
    FRESULT r=f_mkdir("/video");
    if(r!=FR_OK && r!=FR_EXIST) return failed("Cannot create video directory");
    r=f_mkdir(recorder.dir);
    if(r!=FR_OK && r!=FR_EXIST) return failed("Cannot create game snapshot directory");
    if(!start_capture()) return 0;
    return 1;
}

const char *video_recorder_begin(uint32_t owner,uint32_t count,uint32_t period,uint16_t step)
{
    char path[260],escaped_rom[522];
    uint16_t width,height;
    uint32_t frame, first_target;
    int cab_active;
    uint32_t cab_total;
    if(recorder.active) return "Video recorder is already running";
    if(recorder.snapshot_queued) return "A local snapshot request is pending";
    if(!count || count>10000 || !period || period>10000000 || !step || step>512)
        return "Invalid capture count, cadence or row step";
    if(!(user_io_get_core_features()&FEAT_VIDEO_CAPTURE)) return "Core lacks native video capture";
    if(!rom.valid) return "Actual primary ROM path is unavailable; reload the ARC";
    if(cab_player_active() && cab_player_dump_total())
        return "Video and legacy unverified CAB dump barriers cannot overlap";
    cab_active=cab_player_active();
    cab_total=cab_player_frames();
    if(!read_info(&width,&height,&frame)) return "Video INFO transaction failed";
    unsigned format=(width&VIDEO_FORMAT_FLAG) ? VIDEO_FORMAT_RGB555 : VIDEO_FORMAT_RGB444;
    width&=VIDEO_WIDTH_MASK;
    if(!width || !height || width>512 || height>512)
        return "Native geometry exceeds supported bounds";
    uint32_t stride=video_stride(width,format),pixel_bytes=stride*height;
    switch(video_schedule_first(frame,period,count,cab_active,cab_total,&first_target)) {
        case VIDEO_SCHEDULE_OK: break;
        case VIDEO_SCHEDULE_CAB_RANGE:
            return "Video schedule exceeds remaining CAB frames";
        case VIDEO_SCHEDULE_OVERFLOW:
            return "Video frame schedule would overflow";
        default:
            return "Invalid video frame schedule";
    }
    memset(&recorder,0,sizeof(recorder));
    if(remote_input_owner()!=owner && !remote_input_set(owner,0,0,(uint32_t)GetRTTC()))
        return "Inputs owned by another session";
    recorder.owner=owner;
    recorder.total=count;
    recorder.period=period;
    recorder.stall_timeout=period*100U+15000U;
    recorder.width=width;
    recorder.height=height;
    recorder.format=(uint8_t)format;
    recorder.tagged_width=(uint16_t)(width | (format==VIDEO_FORMAT_RGB555 ? VIDEO_FORMAT_FLAG : 0));
    recorder.stride=stride;
    recorder.pixel_bytes=pixel_bytes;
    recorder.step=step>height ? height : step;
    recorder.requested=first_target;
    recorder.start_ms=(uint32_t)GetRTTC();
    if(f_mkdir("/video")==FR_OK) { /* directory already exists or was created */ }
    snprintf(path,sizeof(path),"/video/%s",rom.set);
    f_mkdir(path);
    int made=0;
    for(unsigned tries=0;tries<100;tries++) {
        snprintf(recorder.dir,sizeof(recorder.dir),"/video/%s/run_%08lx_%08lx",rom.set,
                 (unsigned long)GetRTTC(),(unsigned long)++run_sequence);
        FRESULT r=f_mkdir(recorder.dir);
        if(r==FR_OK) {made=1;break;}
        if(r!=FR_EXIST) break;
    }
    if(!made) {failed("Cannot create unique video directory");return recorder.error;}
    snprintf(path,sizeof(path),"%s/index.jsonl",recorder.dir);
    if(f_open(&recorder.index,path,FA_WRITE|FA_CREATE_NEW)!=FR_OK) {
        failed("Cannot create video index");return recorder.error;
    }
    recorder.index_open=1;
    recorder.active=1;
    recorder.last_progress=recorder.start_ms;
    json_escape(escaped_rom,sizeof(escaped_rom),rom.path);
    if(!index_record("{\"type\":\"run\",\"rom_path\":\"%s\",\"width\":%lu,\"height\":%lu,\"period\":%lu,\"step\":%u,\"count\":%lu}\n",
                     escaped_rom,(unsigned long)width,(unsigned long)height,(unsigned long)period,
                     (unsigned)recorder.step,(unsigned long)count)) {
        failed("Cannot write video run index");return recorder.error;
    }
    snprintf(path,sizeof(path),"%s/ROMPATH.TXT",recorder.dir);
    FIL identity_file;
    if(f_open(&identity_file,path,FA_WRITE|FA_CREATE_NEW)!=FR_OK) {
        failed("Cannot save primary ROM path");return recorder.error;
    }
    int ok=write_all(&identity_file,rom.path,strlen(rom.path));
    FRESULT closed=f_close(&identity_file);
    if(!ok || closed!=FR_OK) {failed("Cannot save primary ROM path");return recorder.error;}
    if(!start_capture()) return recorder.error;
    return NULL;
}
static void advance_row(uint16_t *y,uint16_t *residue)
{
    unsigned sum=(unsigned)*y+recorder.step;
    if(sum<recorder.height) *y=(uint16_t)sum;
    else *y=++*residue;
}
static int close_previous(void)
{
    if(!recorder.previous_open) return 1;
    FRESULT r=f_close(&recorder.previous);
    recorder.previous_open=0;
    if(r!=FR_OK) return failed("Cannot close previous video image");
    return 1;
}
static int receive_line(void)
{
    spi_uio_cmd_cont(0x94);
    uint8_t prefix=SPI(0);
    if(prefix==0) {DisableIO();return 0;}
    unsigned bytes=prefix==0xff ? recorder.stride : (unsigned)prefix*2;
    if(!bytes || bytes>sizeof(sector_buffer)) {DisableIO();failed("Invalid video row prefix");return -1;}
    for(unsigned i=0;i<bytes;i++) sector_buffer[i]=SPI(0);
    DisableIO();
    int raw=prefix==0xff;
    if(!video_decode_line(sector_buffer,bytes,recorder.format,raw,recorder.width,line_current)) {
        failed("Malformed native video row");return -1;
    }
    if(recorder.received==0) {
        uint16_t width,height;
        if(!read_info(&width,&height,&recorder.actual) || width!=recorder.tagged_width || height!=recorder.height) {
            failed("Video geometry or format changed during capture");return -1;
        }
    }
    recorder.pending_y=recorder.expected_y;
    if(recorder.have_previous) {
        uint32_t offset=4U+recorder.pending_y*recorder.stride;
        if(!seek_exact(&recorder.previous,offset) || !read_exact(&recorder.previous,line_copy,recorder.stride)) {
            failed("Cannot read previous video row");return -1;
        }
        if(!memcmp(line_current,line_copy,recorder.stride)) {
            recorder.received++;
            if(recorder.received==recorder.height) {
                if(!close_previous()) return -1;
                recorder.duplicate=1;
                snprintf(recorder.image_name,sizeof(recorder.image_name),"%s",recorder.previous_name);
                recorder.state=REC_PUBLISH;
            } else advance_row(&recorder.expected_y,&recorder.residue);
            recorder.last_progress=(uint32_t)GetRTTC();
            return 1;
        }
        recorder.pending_row=1;
        recorder.replay_remaining=recorder.received;
        recorder.replay_y=0;
        recorder.replay_residue=0;
        snprintf(recorder.image_name,sizeof(recorder.image_name),"frame_%08lu.raw",(unsigned long)recorder.actual);
        if(!open_output()) return -1;
        if(recorder.replay_remaining) recorder.state=REC_REPLAY;
        else {
            if(!close_previous()) return -1;
            recorder.have_previous=0;
            recorder.state=REC_PENDING;
        }
    } else {
        recorder.pending_row=1;
        if(!recorder.output_open) {
            snprintf(recorder.image_name,sizeof(recorder.image_name),"frame_%08lu.raw",(unsigned long)recorder.actual);
            if(!open_output()) return -1;
        }
        recorder.state=REC_PENDING;
    }
    recorder.last_progress=(uint32_t)GetRTTC();
    return 1;
}
static int open_output(void)
{
    if(recorder.snapshot_mode) {
        uint8_t header[54]={0};
        header[0]='B'; header[1]='M';
        put32(header+2,recorder.bmp_size);
        put32(header+10,54);
        put32(header+14,40);
        put32(header+18,recorder.width);
        put32(header+22,0U-recorder.height);
        video_put16(header+26,1);
        video_put16(header+28,24);
        put32(header+30,0);
        put32(header+34,recorder.bmp_stride*recorder.height);
        put32(header+38,2835); put32(header+42,2835);
        int created=0;
        for(unsigned tries=0;tries<100;tries++) {
            snprintf(recorder.final,sizeof(recorder.final),"%s/snapshot_%08lx_%04lx.bmp",
                     recorder.dir,(unsigned long)GetRTTC(),(unsigned long)(++run_sequence&0xffff));
            if(f_open(&recorder.output,recorder.final,FA_WRITE|FA_CREATE_NEW)==FR_OK) {
                created=1;break;
            }
        }
        if(!created) return failed("Cannot create unique BMP snapshot");
        recorder.output_open=1;
        recorder.temporary_created=1;
        snprintf(recorder.snapshot_path,sizeof(recorder.snapshot_path),"%s",recorder.final);
        if(!write_all(&recorder.output,header,sizeof(header))) return failed("BMP header write failed");
        return 1;
    }
    snprintf(recorder.final,sizeof(recorder.final),"%s/%s",recorder.dir,recorder.image_name);
    snprintf(recorder.temporary,sizeof(recorder.temporary),"%s/active.tmp",recorder.dir);
    if(f_open(&recorder.output,recorder.temporary,FA_WRITE|FA_CREATE_NEW)!=FR_OK)
        return failed("Cannot create staged video image");
    recorder.output_open=1;
    recorder.temporary_created=1;
    uint8_t header[4];
    video_put16(header,recorder.tagged_width);
    video_put16(header+2,(uint16_t)recorder.height);
    if(!write_all(&recorder.output,header,sizeof(header)))
        return failed("SD video header write failed");
    return 1;
}
static int close_output(void)
{
    if(recorder.snapshot_mode) {
        FRESULT synced=f_size(&recorder.output)==recorder.bmp_size ? f_sync(&recorder.output) : FR_DISK_ERR;
        FRESULT closed=f_close(&recorder.output);
        recorder.output_open=0;
        if(synced!=FR_OK || closed!=FR_OK) return failed("BMP snapshot sync or close failed");
        return 1;
    }
    FRESULT synced=f_size(&recorder.output)==recorder.pixel_bytes+4U ? f_sync(&recorder.output) : FR_DISK_ERR;
    FRESULT closed=f_close(&recorder.output);
    recorder.output_open=0;
    FRESULT renamed=FR_OK;
    if(synced==FR_OK && closed==FR_OK) renamed=f_rename(recorder.temporary,recorder.final);
    if(synced!=FR_OK || closed!=FR_OK || renamed!=FR_OK) {
        char reason[96];
        f_unlink(recorder.temporary);
        snprintf(reason,sizeof(reason),"SD publication phase=%u sync=%u close=%u rename=%u",
                 recorder.state,(unsigned)synced,(unsigned)closed,(unsigned)renamed);
        return failed(reason);
    }
    recorder.temporary_created=0;
    return 1;
}
static int write_row(uint16_t y,const uint8_t *data)
{
    if(recorder.snapshot_mode) {
        if(!seek_exact(&recorder.output,54U+(uint32_t)y*recorder.bmp_stride))
            return failed("BMP row seek failed");
        uint8_t chunk[96];
        for(unsigned x=0;x<recorder.width;) {
            unsigned count=recorder.width-x;
            if(count>32) count=32;
            for(unsigned i=0;i<count;i++) {
                unsigned px=x+i,r,g,b;
                if(recorder.format==VIDEO_FORMAT_RGB555) {
                    unsigned pixel=video_le16(data+px*2U);
                    r=(pixel>>10)&31; g=(pixel>>5)&31; b=pixel&31;
                    r=(r<<3)|(r>>2); g=(g<<3)|(g>>2); b=(b<<3)|(b>>2);
                } else {
                    unsigned at=(px>>1)*3U;
                    unsigned pixel=(px&1) ? (((data[at+1]&15)<<8)|data[at+2]) : ((data[at]<<4)|(data[at+1]>>4));
                    r=((pixel>>8)&15)*17U; g=((pixel>>4)&15)*17U; b=(pixel&15)*17U;
                }
                chunk[3*i]=(uint8_t)b;
                chunk[3*i+1]=(uint8_t)g;
                chunk[3*i+2]=(uint8_t)r;
            }
            if(!write_all(&recorder.output,chunk,count*3U)) return failed("BMP pixel write failed");
            x+=count;
        }
        unsigned padding=recorder.bmp_stride-recorder.width*3U;
        if(padding) {
            static const uint8_t zero[3]={0,0,0};
            if(!write_all(&recorder.output,zero,padding)) return failed("BMP row padding write failed");
        }
        return 1;
    }
    uint32_t offset=4U+(uint32_t)y*recorder.stride;
    if(!seek_exact(&recorder.output,offset) || !write_all(&recorder.output,data,recorder.stride))
        return failed("SD video row write or seek failed");
    return 1;
}
static int publish_capture(void)
{
    if(recorder.snapshot_mode) {
        recorder.temporary_created=0;
        recorder.active=0;
        remote_input_release(recorder.owner);
        recorder.state=REC_IDLE;
        recorder.snapshot_result=1;
        return 1;
    }
    if(!recorder.duplicate) {
        snprintf(recorder.previous_path,sizeof(recorder.previous_path),"%s/%s",recorder.dir,recorder.image_name);
        snprintf(recorder.previous_name,sizeof(recorder.previous_name),"%s",recorder.image_name);
        recorder.have_previous=1;
    }
    if(!index_record("{\"type\":\"capture\",\"requested\":%lu,\"actual\":%lu,\"image\":\"%s\",\"duplicate\":%s}\n",
        (unsigned long)recorder.requested,(unsigned long)recorder.actual,recorder.image_name,
        recorder.duplicate ? "true" : "false")) return failed("Cannot index video image");
    recorder.done++;
    if(recorder.done==recorder.total) {
        if(!index_record("{\"type\":\"complete\",\"captures\":%lu,\"elapsed_ms\":%lu,\"stack_used\":%u}\n",
            (unsigned long)recorder.done,(unsigned long)((uint32_t)GetRTTC()-recorder.start_ms),video_stack_used()))
            return failed("Cannot finalize video index");
        FRESULT closed=f_close(&recorder.index);
        recorder.index_open=0;
        if(closed!=FR_OK) return failed("Cannot close video index");
        recorder.active=0;
        if(!cab_player_active()) remote_input_release(recorder.owner);
        recorder.state=REC_IDLE;
        return 1;
    }
    if(recorder.requested>UINT32_MAX-recorder.period) return failed("Video frame schedule overflow");
    recorder.requested+=recorder.period;
    return start_capture();
}
void video_recorder_poll(void)
{
    uint32_t now=(uint32_t)GetRTTC();
    if(recorder.snapshot_queued && !recorder.active) {
        if((uint32_t)(now-recorder.snapshot_queued_at)>10000U) {
            recorder.snapshot_queued=0;
            recorder.snapshot_result=2;
            snprintf(recorder.error,sizeof(recorder.error),"Snapshot request expired");
            return;
        }
        if(user_io_osd_is_visible()) return;
        recorder.snapshot_queued=0;
        if(!start_snapshot()) {recorder.snapshot_result=2;return;}
    }
    if(!recorder.active) return;
    if(recorder.snapshot_mode && (!fat_medium_present() || usb_storage_is_configured() ||
       user_io_raw_sd_in_use())) {
        failed("SD storage became unavailable during snapshot");return;
    }
    if(recorder.snapshot_mode && !remote_input_keepalive(recorder.owner,now)) {
        failed("Snapshot cancelled");return;
    }
    if(remote_input_owner()!=recorder.owner || user_io_osd_is_visible()) {
        failed("Video ownership or OSD interruption");return;
    }
    if((int32_t)(now-recorder.next_poll)<0) return;
    if(now-recorder.last_progress>recorder.stall_timeout) {failed("Video capture stalled");return;}
    recorder.next_poll=now+2;
    if(recorder.state==REC_RECEIVE) {
        int got=receive_line();
        if(got<0 || !recorder.active) return;
        if(got==0) recorder.next_poll=now+2;
    } else if(recorder.state==REC_REPLAY) {
        uint32_t offset=4U+(uint32_t)recorder.replay_y*recorder.stride;
        if(!seek_exact(&recorder.previous,offset) || !read_exact(&recorder.previous,line_copy,recorder.stride)) {
            failed("Cannot read verified image row for replay");return;
        }
        if(!write_row(recorder.replay_y,line_copy)) return;
        recorder.replay_remaining--;
        recorder.last_progress=now;
        if(recorder.replay_remaining==0) {
            if(!close_previous()) return;
            recorder.have_previous=0;
            recorder.state=REC_PENDING;
        } else advance_row(&recorder.replay_y,&recorder.replay_residue);
    } else if(recorder.state==REC_PENDING) {
        if(!recorder.pending_row || !write_row(recorder.pending_y,line_current)) return;
        recorder.pending_row=0;
        recorder.received++;
        if(recorder.received<recorder.height)
            advance_row(&recorder.expected_y,&recorder.residue);
        recorder.last_progress=now;
        recorder.state=recorder.received==recorder.height ? REC_FINALIZE : REC_RECEIVE;
    } else if(recorder.state==REC_FINALIZE) {
        if(close_output()) recorder.state=REC_PUBLISH;
    } else if(recorder.state==REC_PUBLISH) publish_capture();
}
int video_recorder_active(void) {return recorder.active;}
uint32_t video_recorder_owner(void) {return recorder.owner;}
uint32_t video_recorder_done(void) {return recorder.done;}
uint32_t video_recorder_total(void) {return recorder.total;}
const char *video_recorder_error(void) {return recorder.error;}
const char *video_recorder_dir(void) {return recorder.dir;}
uint32_t video_recorder_width(void) {return recorder.width;}
uint32_t video_recorder_height(void) {return recorder.height;}
uint32_t video_recorder_requested(void) {return recorder.requested;}
uint32_t video_recorder_actual(void) {return recorder.actual;}
uint32_t video_recorder_rows(void) {return recorder.received;}
uint16_t video_recorder_step(void) {return recorder.step;}
int video_snapshot_supported(void)
{
    return !recorder.active && !recorder.snapshot_queued && rom.valid &&
           (user_io_get_core_features()&FEAT_VIDEO_CAPTURE) && fat_medium_present() &&
           !usb_storage_is_configured() && !user_io_raw_sd_in_use() &&
           !cab_player_active() && !remote_input_owner();
}
const char *video_snapshot_request(void)
{
    if(!rom.valid) return "Load a game before saving a snapshot";
    if(!(user_io_get_core_features()&FEAT_VIDEO_CAPTURE)) return "Core does not support snapshots";
    if(!fat_medium_present()) return "SD card is not available";
    if(usb_storage_is_configured()) return "SD card is exported over USB";
    if(user_io_raw_sd_in_use()) return "SD card is in use by the running core";
    if(cab_player_active()) return "Stop CAB playback before saving a snapshot";
    if(recorder.active || recorder.snapshot_queued) return "Video capture is already busy";
    if(remote_input_owner()) return "Inputs are owned by another session";
    recorder.snapshot_queued=1;
    recorder.snapshot_queued_at=(uint32_t)GetRTTC();
    recorder.snapshot_result=0;
    recorder.error[0]=0;
    return NULL;
}
int video_snapshot_take_result(const char **path,const char **error)
{
    if(!recorder.snapshot_result) return 0;
    if(path) *path=recorder.snapshot_result==1 ? recorder.snapshot_path : NULL;
    if(error) *error=recorder.snapshot_result==2 ? recorder.error : NULL;
    recorder.snapshot_result=0;
    return 1;
}
int video_output_path_in_use(const char *path)
{
    unsigned n=strlen(recorder.dir);
    if(!recorder.active || !n) return 0;
    return (!strncasecmp(path,recorder.dir,n) && (!path[n] || path[n]=='/')) ||
           (!strncasecmp(path,recorder.dir,strlen(path)) && recorder.dir[strlen(path)]=='/');
}
