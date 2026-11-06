#define _POSIX_C_SOURCE 200809L
#include "video_recorder_test_support.h"
#include "../video_recorder.c"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned test_width=7, test_format=VIDEO_FORMAT_RGB555;
static unsigned test_stride(void) {return video_stride(test_width,test_format);}
unsigned char sector_buffer[8192];
static uint8_t source_rows[2][512][1024];
static unsigned row_order[512], test_height, test_step, row_reads, current_y;
static unsigned capture_number, spi_command, spi_pos, info_pos, input_owner;
static unsigned abort_commands, index_creates, raw_creates;
static uint32_t test_clock=100, temp_creates, image_payload_writes, published_images, bmp_bytes_written;
static int inject_clip, inject_short_read, inject_short_write, inject_close, inject_sync, inject_rename;
static int inject_bmp_collision, osd_visible, medium_present=1, cab_active_state;
static int usb_exported, raw_sd_busy;
static uint32_t keepalive_calls;
static uint32_t core_features=FEAT_VIDEO_CAPTURE;
static char host_root[PATH_MAX];
static char collision_path[PATH_MAX];
static uint32_t lease_deadline;

typedef struct { FIL *fil; FILE *fp; char logical[260]; } OPEN_FILE;
static OPEN_FILE opened[8];

static void host_path(char out[PATH_MAX],const char *logical)
{
    snprintf(out,PATH_MAX,"%s%s",host_root,logical);
}

static OPEN_FILE *file_handle(FIL *fil)
{
    for(unsigned i=0;i<8;i++) if(opened[i].fil==fil) return &opened[i];
    return NULL;
}

FRESULT f_mkdir(const TCHAR *path)
{
    char host[PATH_MAX]; host_path(host,path);
    if(mkdir(host,0700)==0 || errno==EEXIST) {memset(sector_buffer,0x5a,sizeof(sector_buffer));return FR_OK;}
    return FR_DISK_ERR;
}
FRESULT f_open(FIL *fil,const TCHAR *path,BYTE mode)
{
    size_t path_len=strlen(path);
    if(inject_bmp_collision && path_len>=4 && !strcmp(path+path_len-4,".bmp")) {
        inject_bmp_collision=0;
        host_path(collision_path,path);
        FILE *sentinel=fopen(collision_path,"wb");
        if(sentinel) {fwrite("old",1,3,sentinel);fclose(sentinel);}
        return FR_EXIST;
    }
    char host[PATH_MAX]; host_path(host,path);
    struct stat st;
    int exists=stat(host,&st)==0;
    if((mode&FA_CREATE_NEW) && exists) return FR_EXIST;
    if((mode&FA_READ) && !exists && !(mode&FA_CREATE_NEW)) return FR_NO_FILE;
    FILE *fp=fopen(host,(mode&FA_READ) && (mode&FA_WRITE) ? "r+b" :
                        (mode&FA_READ) ? "rb" : "w+b");
    if(!fp) return FR_DISK_ERR;
    OPEN_FILE *slot=NULL;
    for(unsigned i=0;i<8;i++) if(!opened[i].fil) {slot=&opened[i];break;}
    if(!slot) {fclose(fp);return FR_TOO_MANY_OPEN_FILES;}
    memset(fil,0,sizeof(*fil));
    slot->fil=fil; slot->fp=fp;
    snprintf(slot->logical,sizeof(slot->logical),"%s",path);
    struct stat current;
    if(fstat(fileno(fp),&current)==0) fil->obj.objsize=(FSIZE_t)current.st_size;
    if(!strcmp(strrchr(path,'/') ? strrchr(path,'/')+1 : path,"active.tmp")) temp_creates++;
    const char *base=strrchr(path,'/');base=base?base+1:path;
    if((mode&FA_CREATE_NEW) && !strcmp(base,"index.jsonl")) index_creates++;
    if((mode&FA_CREATE_NEW) && strstr(base,".raw")) raw_creates++;
    memset(sector_buffer,0x5a,sizeof(sector_buffer));
    return FR_OK;
}
FRESULT f_close(FIL *fil)
{
    OPEN_FILE *f=file_handle(fil); if(!f) return FR_INVALID_OBJECT;
    fclose(f->fp); memset(f,0,sizeof(*f));
    if(inject_close) {inject_close=0;return FR_DISK_ERR;}
    return FR_OK;
}
FRESULT f_read(FIL *fil,void *buffer,UINT bytes,UINT *read_bytes)
{
    OPEN_FILE *f=file_handle(fil); if(!f) return FR_INVALID_OBJECT;
    UINT want=inject_short_read && bytes ? bytes-1:bytes;
    *read_bytes=(UINT)fread(buffer,1,want,f->fp);
    fil->fptr=(FSIZE_t)ftell(f->fp);
    if(inject_short_read) inject_short_read=0;
    memset(sector_buffer,0xa5,sizeof(sector_buffer));
    return FR_OK;
}
FRESULT f_write(FIL *fil,const void *buffer,UINT bytes,UINT *written)
{
    OPEN_FILE *f=file_handle(fil); if(!f) return FR_INVALID_OBJECT;
    UINT want=inject_short_write && bytes ? bytes-1:bytes;
    FSIZE_t before=fil->fptr;
    *written=(UINT)fwrite(buffer,1,want,f->fp);
    fil->fptr=(FSIZE_t)ftell(f->fp);
    FSIZE_t end=before+*written;
    if(end>fil->obj.objsize) fil->obj.objsize=end;
    const char *base=strrchr(f->logical,'/'); base=base?base+1:f->logical;
    if(!strcmp(base,"active.tmp") && before>=4) image_payload_writes+=*written;
    if(strstr(base,".bmp")) bmp_bytes_written+=*written;
    if(inject_short_write) inject_short_write=0;
    memset(sector_buffer,0x3c,sizeof(sector_buffer));
    return FR_OK;
}
FRESULT f_lseek(FIL *fil,FSIZE_t offset)
{
    OPEN_FILE *f=file_handle(fil); if(!f) return FR_INVALID_OBJECT;
    FSIZE_t actual=offset;
    if(inject_clip && actual>2) {actual=2;inject_clip=0;}
    if(fseeko(f->fp,(off_t)actual,SEEK_SET)!=0) return FR_DISK_ERR;
    fil->fptr=(FSIZE_t)actual;
    memset(sector_buffer,0x96,sizeof(sector_buffer));
    return FR_OK;
}
FRESULT f_sync(FIL *fil)
{
    OPEN_FILE *f=file_handle(fil); if(!f) return FR_INVALID_OBJECT;
    fflush(f->fp);
    memset(sector_buffer,0x69,sizeof(sector_buffer));
    if(inject_sync) {inject_sync=0;return FR_DISK_ERR;}
    return FR_OK;
}
FRESULT f_unlink(const TCHAR *path)
{
    char host[PATH_MAX]; host_path(host,path);
    return unlink(host)==0 || errno==ENOENT ? FR_OK : FR_DISK_ERR;
}
FRESULT f_rename(const TCHAR *old_path,const TCHAR *new_path)
{
    char old_host[PATH_MAX],new_host[PATH_MAX];
    host_path(old_host,old_path); host_path(new_host,new_path);
    if(inject_rename) {inject_rename=0;return FR_DISK_ERR;}
    if(rename(old_host,new_host)!=0) return FR_DISK_ERR;
    const char *base=strrchr(new_path,'/'); base=base?base+1:new_path;
    if(!strncmp(base,"frame_",6)) published_images++;
    return FR_OK;
}

uint32_t GetRTTC(void) {return test_clock++;}
void spi_uio_cmd_cont(unsigned command)
{
    spi_command=command; spi_pos=0;
    if(command==0x99) abort_commands++;
    if(command==0x90) info_pos=0;
    if(command==0x91) {capture_number++;row_reads=0;}
    if(command==0x94) {
        current_y=row_reads<test_height ? row_order[row_reads++] : 0;
    }
}
void DisableIO(void) {}
uint8_t video_test_spi(uint8_t value)
{
    (void)value;
    if(spi_command==0x90) {
        uint8_t info[8];
        video_put16(info,(uint16_t)(test_width|(test_format==VIDEO_FORMAT_RGB555 ? VIDEO_FORMAT_FLAG : 0)));
        video_put16(info+2,(uint16_t)test_height);
        uint32_t frame=100U+capture_number;
        info[4]=(uint8_t)frame;info[5]=(uint8_t)(frame>>8);
        info[6]=(uint8_t)(frame>>16);info[7]=(uint8_t)(frame>>24);
        return info[info_pos++];
    }
    if(spi_command==0x94) {
        if(spi_pos++==0) return row_reads<=test_height ? 0xff : 0;
        unsigned at=spi_pos-2;
        return at<test_stride() ? source_rows[capture_number-1][current_y][at] : 0;
    }
    return 0;
}
uint32_t user_io_get_core_features(void) {return core_features;}
int user_io_osd_is_visible(void) {return osd_visible;}
uint32_t remote_input_owner(void) {return input_owner;}
int remote_input_set(uint32_t owner,uint32_t a,uint32_t b,uint32_t now)
{(void)a;(void)b;if(input_owner && input_owner!=owner)return 0;input_owner=owner;lease_deadline=now+5000;return 1;}
int remote_input_keepalive(uint32_t owner,uint32_t now)
{keepalive_calls++;if(input_owner!=owner || (int32_t)(now-lease_deadline)>=0)return 0;lease_deadline=now+5000;return 1;}
void remote_input_release(uint32_t owner) {if(input_owner==owner) input_owner=0;}
int cab_player_active(void) {return cab_active_state;}
uint32_t cab_player_dump_total(void) {return 0;}
uint32_t cab_player_frames(void) {return 0;}
const char *CurrentDirectoryName(void) {return "/";}
unsigned video_stack_used(void) {return 2048;}
int8_t fat_medium_present(void) {return medium_present;}
uint8_t usb_storage_is_configured(void) {return (uint8_t)usb_exported;}
int user_io_raw_sd_in_use(void) {return raw_sd_busy;}

static void check(int ok,const char *why)
{
    if(!ok) {fprintf(stderr,"FAIL %s\n",why);exit(1);}
}
static void setup(unsigned height,unsigned step,int diff_order)
{
    strcpy(host_root,"/tmp/video-recorder-test-XXXXXX");
    check(mkdtemp(host_root)!=NULL,"temporary file store");
    test_height=height;test_step=step;capture_number=0;row_reads=0;input_owner=0;
    abort_commands=index_creates=raw_creates=0;
    keepalive_calls=0;osd_visible=0;medium_present=1;cab_active_state=0;usb_exported=raw_sd_busy=0;core_features=FEAT_VIDEO_CAPTURE;
    temp_creates=0;image_payload_writes=0;published_images=0;bmp_bytes_written=0;inject_clip=inject_short_read=0;
    inject_short_write=inject_close=inject_sync=inject_rename=0;
    memset(source_rows,0,sizeof(source_rows));
    unsigned y=0,residue=0;
    for(unsigned i=0;i<height;i++) {
        row_order[i]=y;
        for(unsigned x=0;x<test_width;x++) {
            uint16_t pixel=(uint16_t)((x*791U+y*1237U+1U)&0x7fff);
            if(test_format==VIDEO_FORMAT_RGB555) {
                video_put16(&source_rows[0][y][2*x],pixel);
                video_put16(&source_rows[1][y][2*x],pixel);
            } else {
                unsigned color=(x*0x137U+y*0x2a5U+0x135U)&0xfff;
                unsigned at=(x>>1)*3U;
                if(!(x&1)) {
                    source_rows[0][y][at]=(uint8_t)(color>>4);
                    source_rows[0][y][at+1]=(uint8_t)(color<<4);
                    source_rows[1][y][at]=source_rows[0][y][at];
                    source_rows[1][y][at+1]=source_rows[0][y][at+1];
                } else {
                    source_rows[0][y][at+1]|=(uint8_t)(color>>8);
                    source_rows[0][y][at+2]=(uint8_t)color;
                    source_rows[1][y][at+1]=source_rows[0][y][at+1];
                    source_rows[1][y][at+2]=source_rows[0][y][at+2];
                }
            }
        }
        unsigned sum=y+step;
        if(sum<height) y=sum; else y=++residue;
    }
    if(diff_order>=0) {
        unsigned ydiff=row_order[(unsigned)diff_order];
        source_rows[1][ydiff][0]^=1;
    }
    /* The next capture starts again at the first row of the deterministic order. */
    y=0;residue=0;
    for(unsigned i=0;i<height;i++) {
        row_order[i]=y;
        unsigned sum=y+step;
        if(sum<height) y=sum; else y=++residue;
    }
    test_clock=100;
    video_rom_loaded(NULL,"/GNG/GNG.ROM");
}
static void poll_until_done(uint32_t target)
{
    for(unsigned n=0;n<20000 && video_recorder_done()<target && video_recorder_active();n++)
        video_recorder_poll();
    if(video_recorder_done()<target)
        fprintf(stderr,"poll timeout target=%lu done=%lu active=%d rows=%lu error=%s row_reads=%u creates=%lu writes=%lu\n",
                (unsigned long)target,(unsigned long)video_recorder_done(),video_recorder_active(),
                (unsigned long)video_recorder_rows(),video_recorder_error(),row_reads,
                (unsigned long)temp_creates,(unsigned long)image_payload_writes);
    check(video_recorder_done()>=target,"recorder reached requested completion");
}
static void begin_two(void)
{
    const char *error=video_recorder_begin(1,2,1,(uint16_t)test_step);
    check(error==NULL,"recorder begins with tagged RGB555 geometry");
}
static int image_count(void)
{
    return (int)published_images;
}
static void verify_last_image(void)
{
    char logical[300],host[PATH_MAX];
    snprintf(logical,sizeof(logical),"%s/%s",video_recorder_dir(),"frame_00000102.raw");
    host_path(host,logical);
    FILE *fp=fopen(host,"rb"); check(fp!=NULL,"changed image is published");
    uint8_t got[4+512*1024];
    size_t n=fread(got,1,sizeof(got),fp); fclose(fp);
    check(n==4+test_height*test_stride(),"published image has exact tagged size");
    check(video_le16(got)==(test_width|(test_format==VIDEO_FORMAT_RGB555 ? VIDEO_FORMAT_FLAG : 0)) && video_le16(got+2)==test_height,
          "published image carries tagged geometry");
    for(unsigned y=0;y<test_height;y++)
        check(!memcmp(got+4+y*test_stride(),source_rows[1][y],test_stride()),
              "published canonical pixels match independent frame oracle");
}
static void run_difference(unsigned height,unsigned step,int order_index)
{
    setup(height,step,order_index);begin_two();
    poll_until_done(1);
    uint32_t creates=temp_creates,writes=image_payload_writes;
    poll_until_done(2);
    check(temp_creates-creates==1,"changed image creates exactly one staged file");
    check(image_payload_writes-writes==test_height*test_stride(),"each unique row is written once");
    check(image_count()==2,"two unique images are published");
    verify_last_image();
    check(input_owner==0,"completed recorder releases input owner");
}
static void run_duplicate(void)
{
    setup(7,3,-1);begin_two();
    poll_until_done(1);
    uint32_t creates=temp_creates,writes=image_payload_writes;
    poll_until_done(2);
    check(temp_creates==creates,"duplicate creates no image temp file");
    check(image_payload_writes==writes,"duplicate performs zero image pixel writes");
    check(image_count()==1,"duplicate index points to one existing image");
}
static void run_faults(void)
{
    setup(7,3,3);begin_two();poll_until_done(1);
    inject_clip=1;
    for(unsigned i=0;i<1000 && video_recorder_active();i++) video_recorder_poll();
    check(!video_recorder_active() && input_owner==0,"clipped-success seek aborts and releases owner");
    char logical[300],host[PATH_MAX];
    snprintf(logical,sizeof(logical),"%s/active.tmp",video_recorder_dir());host_path(host,logical);
    check(access(host,F_OK)!=0,"clipped seek removes staged file");

    setup(7,3,3);begin_two();poll_until_done(1);inject_short_read=1;
    for(unsigned i=0;i<1000 && video_recorder_active();i++) video_recorder_poll();
    check(!video_recorder_active() && input_owner==0,"short previous read aborts and releases owner");

    setup(7,3,-1);
    check(video_recorder_begin(1,1,1,(uint16_t)test_step)==NULL,"short-write case begins");
    inject_short_write=1;
    for(unsigned i=0;i<1000 && video_recorder_active();i++) video_recorder_poll();
    check(!video_recorder_active() && input_owner==0,"short image write aborts and releases owner");
    snprintf(logical,sizeof(logical),"%s/active.tmp",video_recorder_dir());host_path(host,logical);
    check(access(host,F_OK)!=0,"short image write removes staged file");

    for(unsigned fault=0;fault<3;fault++) {
        setup(7,3,-1);
        check(video_recorder_begin(1,1,1,(uint16_t)test_step)==NULL,"publication-fault case begins");
        while(video_recorder_active() && image_payload_writes<test_height*test_stride())
            video_recorder_poll();
        if(fault==0) inject_sync=1;
        if(fault==1) inject_close=1;
        if(fault==2) inject_rename=1;
        for(unsigned i=0;i<100 && video_recorder_active();i++) video_recorder_poll();
        check(!video_recorder_active() && input_owner==0,"publication error aborts and releases owner");
        snprintf(logical,sizeof(logical),"%s/active.tmp",video_recorder_dir());host_path(host,logical);
        check(access(host,F_OK)!=0,"publication error removes staged file");
    }

    setup(7,3,2);begin_two();poll_until_done(1);
    /* Two equal rows then a differing row opens the temp; one later poll replays one row. */
    while(video_recorder_rows()<2 && video_recorder_active()) video_recorder_poll();
    while(image_payload_writes==test_height*test_stride() && video_recorder_active()) video_recorder_poll();
    while(image_payload_writes<test_height*test_stride()+test_stride() && video_recorder_active()) video_recorder_poll();
    video_recorder_stop();
    snprintf(logical,sizeof(logical),"%s/active.tmp",video_recorder_dir());host_path(host,logical);
    check(access(host,F_OK)!=0 && input_owner==0,"stop during prefix replay removes staged image and owner");

    setup(7,3,0);begin_two();poll_until_done(1);
    while(video_recorder_active() && temp_creates<2) video_recorder_poll();
    check(temp_creates==2 && video_recorder_rows()==0,"first difference is held pending before commit");
    video_recorder_stop();
    snprintf(logical,sizeof(logical),"%s/active.tmp",video_recorder_dir());host_path(host,logical);
    check(access(host,F_OK)!=0 && input_owner==0,"stop with pending first-difference row removes staging file");
}

static void pixel_rgb(unsigned x,unsigned y,unsigned *r,unsigned *g,unsigned *b)
{
    if(test_format==VIDEO_FORMAT_RGB555) {
        unsigned p=video_le16(&source_rows[0][y][2*x]);
        unsigned cr=(p>>10)&31,cg=(p>>5)&31,cb=p&31;
        *r=(cr<<3)|(cr>>2); *g=(cg<<3)|(cg>>2); *b=(cb<<3)|(cb>>2);
    } else {
        unsigned at=(x>>1)*3U;
        unsigned p=(x&1) ? (((source_rows[0][y][at+1]&15)<<8)|source_rows[0][y][at+2]) :
                           ((source_rows[0][y][at]<<4)|(source_rows[0][y][at+1]>>4));
        *r=((p>>8)&15)*17U; *g=((p>>4)&15)*17U; *b=(p&15)*17U;
    }
}

static void verify_bmp(const char *path)
{
    char host[PATH_MAX];host_path(host,path);
    FILE *fp=fopen(host,"rb");check(fp!=NULL,"BMP output is readable");
    unsigned stride=(3U*test_width+3U)&~3U;
    unsigned size=54U+stride*test_height;
    uint8_t *bmp=malloc(size);check(bmp!=NULL,"BMP test allocation");
    check(fread(bmp,1,size,fp)==size && fgetc(fp)==EOF,"BMP has exact length");fclose(fp);
    check(bmp[0]=='B' && bmp[1]=='M' && video_le16(bmp+26)==1 && video_le16(bmp+28)==24,
          "BMP signature and pixel format");
    uint32_t file_size=(uint32_t)bmp[2]|((uint32_t)bmp[3]<<8)|((uint32_t)bmp[4]<<16)|((uint32_t)bmp[5]<<24);
    check(file_size==size,"BMP header file size");
    uint32_t bmp_height=(uint32_t)bmp[22]|((uint32_t)bmp[23]<<8)|((uint32_t)bmp[24]<<16)|((uint32_t)bmp[25]<<24);
    check(video_le16(bmp+18)==test_width && bmp_height==(uint32_t)(0U-test_height),"BMP top-down dimensions");
    uint32_t image_size=(uint32_t)bmp[34]|((uint32_t)bmp[35]<<8)|((uint32_t)bmp[36]<<16)|((uint32_t)bmp[37]<<24);
    check(bmp[6]==0 && bmp[7]==0 && bmp[8]==0 && bmp[9]==0 && bmp[10]==54 && bmp[11]==0 &&
          bmp[12]==0 && bmp[13]==0 && video_le16(bmp+14)==40 && video_le16(bmp+30)==0 &&
          image_size==stride*test_height,"BMP headers, uncompressed pixel offset and image size");
    for(unsigned y=0;y<test_height;y++) {
        for(unsigned x=0;x<test_width;x++) {
            unsigned r,g,b,at=54+y*stride+x*3;
            pixel_rgb(x,y,&r,&g,&b);
            check(bmp[at]==b && bmp[at+1]==g && bmp[at+2]==r,"BMP BGR pixel matches source");
        }
        for(unsigned p=test_width*3;p<stride;p++) check(bmp[54+y*stride+p]==0,"BMP row padding is zero");
    }
    free(bmp);
}

static void run_snapshot(unsigned width,unsigned height,unsigned format,int collide)
{
    test_width=width;test_format=format;setup(height,height<16 ? height : 16,-1);
    osd_visible=1;
    check(video_snapshot_supported(),"snapshot capability is available from the open menu");
    check(video_snapshot_request()==NULL,"local snapshot queues without FTP owner");
    for(unsigned i=0;i<5;i++) video_recorder_poll();
    check(!video_recorder_active() && capture_number==0,"queued snapshot waits until OSD is hidden");
    osd_visible=0;
    if(collide) inject_bmp_collision=1;
    video_recorder_poll();
    check(video_recorder_active(),"snapshot starts when OSD becomes hidden");
    for(unsigned i=0;i<100 && !recorder.snapshot_path[0];i++) video_recorder_poll();
    check(recorder.snapshot_path[0] && video_output_path_in_use(recorder.snapshot_path),
          "open BMP is protected from other filesystem operations");
    for(unsigned i=0;i<20000 && !recorder.snapshot_result;i++) {
        if(height>200) test_clock+=2000;
        video_recorder_poll();
    }
    check(recorder.snapshot_result!=0,"snapshot completes asynchronously");
    check(!video_recorder_active() && input_owner==0 && keepalive_calls>0,"snapshot releases owner after heartbeats");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && path && !error,"snapshot result exposes completed BMP path");
    verify_bmp(path);
    check(!strncmp(path,"/video/gng/snapshot_",20) && strstr(path,".bmp")!=NULL,"snapshot is saved directly in game folder");
    check(!video_output_path_in_use(path),"completed BMP is no longer marked in use");
    check(temp_creates==0 && image_payload_writes==0 && published_images==0 && index_creates==0 && raw_creates==0,
          "snapshot creates no RAW/index image");
    if(collide) {
        FILE *old=fopen(collision_path,"rb");char sentinel[4]={0};
        check(old && fread(sentinel,1,3,old)==3 && !memcmp(sentinel,"old",3),"filename collision preserves existing file");
        fclose(old);
    }
}

static void run_snapshot_cancel(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(224,16,-1);
    check(video_snapshot_request()==NULL,"interruption snapshot queues");
    for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
    check(video_recorder_active(),"snapshot starts after OSD is hidden");
    for(unsigned i=0;i<1000 && video_recorder_rows()<2;i++) video_recorder_poll();
    check(video_recorder_rows()>=2,"snapshot has partial rows before cancellation");
    snprintf(collision_path,sizeof(collision_path),"%s",recorder.snapshot_path);
    osd_visible=1;
    input_owner=0; // user_io_osd_key_enable releases the local lease on OSD open
    video_recorder_poll();
    char host[PATH_MAX];host_path(host,collision_path);
    check(access(host,F_OK)!=0 && input_owner==0 && abort_commands>0,
          "OSD interruption removes partial BMP, sends ABORT and releases owner");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"interruption reports failure");
    check(!strcmp(error,"Snapshot cancelled"),"local interruption uses user-facing cancellation text");
}

static void run_snapshot_expired_lease(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(224,16,-1);
    check(video_snapshot_request()==NULL,"lease-gap snapshot queues");
    for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
    for(unsigned i=0;i<1000 && video_recorder_rows()<2;i++) video_recorder_poll();
    check(video_recorder_active() && video_recorder_rows()>=2,"lease-gap snapshot has partial rows");
    snprintf(collision_path,sizeof(collision_path),"%s",recorder.snapshot_path);
    test_clock+=5001;
    video_recorder_poll();
    check(!video_recorder_active() && input_owner==0 && abort_commands>0 && recorder.snapshot_result==2,
          "expired local lease aborts without reacquiring ownership");
    char host[PATH_MAX];host_path(host,collision_path);
    check(access(host,F_OK)!=0,"expired lease removes partial BMP");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"lease loss is reported");
}

static void run_snapshot_reset(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(224,16,-1);
    check(video_snapshot_request()==NULL,"core-reset snapshot queues");
    for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
    for(unsigned i=0;i<1000 && video_recorder_rows()<2;i++) video_recorder_poll();
    check(video_recorder_active() && video_recorder_rows()>=2,"core-reset snapshot has partial rows");
    snprintf(collision_path,sizeof(collision_path),"%s",recorder.snapshot_path);
    video_rom_clear();
    char host[PATH_MAX];host_path(host,collision_path);
    check(!video_recorder_active() && access(host,F_OK)!=0 && input_owner==0 && abort_commands>0,
          "core reset removes partial BMP, sends ABORT and releases owner");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"core reset reports cancellation");
}

static void run_snapshot_storage_interruption(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(224,16,-1);
    check(video_snapshot_request()==NULL,"storage-interruption snapshot queues");
    for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
    for(unsigned i=0;i<1000 && video_recorder_rows()<2;i++) video_recorder_poll();
    check(video_recorder_active() && video_recorder_rows()>=2,"storage-interruption has partial rows");
    snprintf(collision_path,sizeof(collision_path),"%s",recorder.snapshot_path);
    raw_sd_busy=1;
    video_recorder_poll();
    check(!video_recorder_active() && input_owner==0 && abort_commands>0 && recorder.snapshot_result==2,
          "raw-SD export starting mid-capture aborts and releases owner");
    char host[PATH_MAX];host_path(host,collision_path);
    check(access(host,F_OK)!=0,"storage interruption removes incomplete BMP");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"storage interruption is reported");
    raw_sd_busy=0;
}

static void run_snapshot_rejections(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(7,7,-1);
    input_owner=123;
    check(video_snapshot_request()!=NULL,"competing input owner is rejected");
    input_owner=0;core_features=0;
    check(video_snapshot_request()!=NULL,"unsupported core is rejected");
    core_features=FEAT_VIDEO_CAPTURE;medium_present=0;
    check(video_snapshot_request()!=NULL,"missing SD medium is rejected");
    medium_present=1;cab_active_state=1;
    check(video_snapshot_request()!=NULL,"active CAB is rejected");
    cab_active_state=0;usb_exported=1;
    check(video_snapshot_request()!=NULL,"USB-exported SD is rejected");
    usb_exported=0;raw_sd_busy=1;
    check(video_snapshot_request()!=NULL,"raw-SD use by the core is rejected");
    raw_sd_busy=0;
    osd_visible=1;
    check(video_snapshot_request()==NULL,"pending request is accepted before OSD closes");
    video_rom_clear();
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"ROM reset cancels pending request");
}

static void run_snapshot_faults(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;setup(7,7,-1);
    check(video_snapshot_request()==NULL,"header write fault request queues");
    inject_short_write=1;
    for(unsigned i=0;i<1000 && !recorder.snapshot_result;i++) video_recorder_poll();
    check(recorder.snapshot_result==2 && input_owner==0,"short BMP header write aborts and releases owner");
    char host[PATH_MAX];host_path(host,recorder.final);
    check(access(host,F_OK)!=0,"short BMP header is removed");
    const char *path=NULL,*error=NULL;
    check(video_snapshot_take_result(&path,&error) && !path && error,"header fault is reported");

    for(unsigned fault=0;fault<2;fault++) {
        setup(7,7,-1);check(video_snapshot_request()==NULL,"BMP finalize fault request queues");
        for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
        while(video_recorder_active() && recorder.state!=REC_FINALIZE) video_recorder_poll();
        check(video_recorder_active() && recorder.state==REC_FINALIZE,"all BMP rows reach finalize phase");
        if(fault==0) inject_sync=1; else inject_close=1;
        for(unsigned i=0;i<5 && video_recorder_active();i++) {test_clock+=3;video_recorder_poll();}
        check(!video_recorder_active() && input_owner==0 && recorder.snapshot_result==2,"BMP sync/close error aborts and releases owner");
        host_path(host,recorder.final);
        check(access(host,F_OK)!=0,"failed BMP publication is removed");
        check(video_snapshot_take_result(&path,&error) && !path && error,"finalize error is reported");
    }

    setup(224,16,-1);check(video_snapshot_request()==NULL,"middle-row fault request queues");
    for(unsigned i=0;i<1000 && !video_recorder_active();i++) video_recorder_poll();
    while(video_recorder_active() && video_recorder_rows()<3) video_recorder_poll();
    inject_short_write=1;
    for(unsigned i=0;i<1000 && video_recorder_active();i++) video_recorder_poll();
    check(!video_recorder_active() && input_owner==0 && abort_commands>0 && recorder.snapshot_result==2,
          "middle-row short write aborts and releases owner");
    host_path(host,recorder.final);
    check(access(host,F_OK)!=0,"middle-row failure removes partial BMP");
    check(video_snapshot_take_result(&path,&error) && !path && error,"middle-row failure is reported");
}

static void run_capture_stall_timeout(void)
{
    setup(224,16,-1);
    check(video_recorder_begin(1,1,100,16)==NULL,"recorder starts before capture-stall check");
    uint32_t timeout=recorder.stall_timeout;
    check(timeout==25000,"100-frame cadence keeps a bounded 25-second capture watchdog");
    test_clock+=timeout+1;
    video_recorder_poll();
    check(!video_recorder_active() && input_owner==0,"stalled video capture aborts and releases owner");
    check(!strcmp(video_recorder_error(),"Video capture stalled"),"stalled capture reports its timeout");
}

int main(void)
{
    test_width=7;test_format=VIDEO_FORMAT_RGB555;
    run_difference(7,3,0);
    run_difference(7,3,3);
    run_difference(7,3,6);
    run_difference(224,16,223);
    run_duplicate();
    run_faults();
    run_snapshot(7,7,VIDEO_FORMAT_RGB444,0);
    run_snapshot(1,5,VIDEO_FORMAT_RGB555,1);
    run_snapshot(512,512,VIDEO_FORMAT_RGB555,0);
    run_snapshot(2,7,VIDEO_FORMAT_RGB444,0);
    run_snapshot(384,224,VIDEO_FORMAT_RGB555,0);
    run_snapshot_cancel();
    run_snapshot_expired_lease();
    run_snapshot_reset();
    run_snapshot_storage_interruption();
    run_snapshot_rejections();
    run_snapshot_faults();
    run_capture_stall_timeout();
    puts("video recorder: FTP dedup/faults plus local BMP RGB444/RGB555, padding, width512, queued OSD-hidden start, collision, lease, rejection, cancel, I/O cleanup, and bounded capture-stall abort PASS");
    return 0;
}
