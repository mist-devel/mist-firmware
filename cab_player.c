#include "video_recorder.h"
#include "cab_player.h"
#include "cab_format.h"
#include "remote_input.h"
#include "user_io.h"
#include "hardware.h"
#include "spi.h"
#include "data_io.h"
#include "mist_cfg.h"
#include "usbdev.h"
#include <stdio.h>
#include <string.h>

static struct {
	FIL file, output;
	data_io_rx_t rx;
	cab_fpga_status_t status;
	cab_header_t header;
	uint32_t owner, sent, last_progress, last_frame, next_poll;
	uint32_t dump_bytes, dumps_done, dump_started, dump_start_raster, dump_start_ms;
	uint8_t open, active, output_open, receiving;
	char dump_dir[24], error[96];
} player;

static int read_status(void)
{
	uint8_t raw[64]={0};
	uint32_t features=user_io_get_core_features();
	unsigned version=features&FEAT_CAB_DUMP ? 3 : features&FEAT_CAB_DIPS ? 2 : 1;
	unsigned bytes=version==3 ? 64 : version==2 ? 40 : 32;
	spi_uio_cmd_cont(version==3 ? 0x89 : version==2 ? 0x87 : 0x81);
	for (unsigned i=0;i<bytes;i++) raw[i]=SPI(0);
	DisableIO();
	if (raw[0]!=0xca || raw[1]!=version) return 0;
	player.status.state=raw[2]; player.status.error=raw[3];
	player.status.epoch=cab_le32(raw+4); player.status.frames=cab_le32(raw+8);
	player.status.accepted=cab_le32(raw+12); player.status.applied=cab_le32(raw+16);
	player.status.raster=cab_le32(raw+20); player.status.queued=raw[24];
	player.status.credits=raw[25]; player.status.inputs=cab_le32(raw+28);
	player.status.dipsw=cab_le32(raw+32); player.status.dipsw_valid=raw[36];
	player.status.dump_seq=cab_le32(raw+40); player.status.dump_frame=cab_le32(raw+44);
	player.status.dump_raster=cab_le32(raw+48);
	return 1;
}

static void dump_path(char *path, uint32_t seq, const char *ext)
{
	sniprintf(path,64,"%s/D%08lx.%s",player.dump_dir,(unsigned long)seq,ext);
}

int cab_player_stop(void)
{
	int ok=1;
	char path[64];
	if (player.receiving) { data_io_rx_end(&player.rx); player.receiving=0; }
	if (player.output_open) {
		f_close(&player.output); player.output_open=0;
	}
	if (player.dump_dir[0]) {
		dump_path(path,player.dumps_done+1,"TMP"); f_unlink(path);
		dump_path(path,player.dumps_done+1,"MET"); f_unlink(path);
	}
	if (player.active) {
		// Guard byte lets STOP cross the SPI/sys CDC before CS rises.
		spi_uio_cmd8(0x85,0);
		ok=read_status() && player.status.state==0 && player.status.inputs==0;
		if (!ok) strcpy(player.error,"FPGA did not acknowledge CAB stop; reload the core");
		remote_input_release(player.owner); player.active=0;
	}
	if (player.open) { f_close(&player.file); player.open=0; }
	return ok;
}

static const char *failed(const char *reason)
{
	strncpy(player.error,reason,sizeof(player.error)-1);
	player.error[sizeof(player.error)-1]=0;
	cab_player_stop(); player.status.state=4;
	return player.error;
}

static int fill_queue(void)
{
	uint8_t raw[12];
	UINT count;
	unsigned credits=player.status.credits;
	if (credits>8) return 0;
	while (credits-- && player.sent<player.header.events) {
		if (f_read(&player.file,raw,player.header.record_bytes,&count)!=FR_OK || count!=player.header.record_bytes || !cab_valid_record(raw,player.header.version)) return 0;
		spi_uio_cmd_cont(player.header.version>1 ? 0x86 : 0x83);
		spi32le(player.status.epoch); spi32le(player.sent);
		spi32le(cab_le32(raw)); spi32le(cab_le32(raw+4));
		if (player.header.version>1) spi32le(cab_le32(raw+8));
		DisableIO(); player.sent++;
	}
	return read_status() && player.status.accepted==player.sent && player.status.state!=4;
}

uint32_t cab_player_dump_bytes(void)
{
	uint32_t length=0;
	if (!(user_io_get_core_features()&FEAT_CAB_DUMP)) return 0;
	for (unsigned i=0;i<255;i++) {
		const char *item=user_io_8bit_get_string(i);
		if (!item || !*item) { if(i>1) break; else continue; }
		if (item[0]!='R') continue;
		uint32_t value=cab_parse_dump_length(item);
		if (!value || length) return 0; // ambiguous/invalid save interface
		length=value;
	}
	return length;
}

static int make_dump_dir(void)
{
	uint32_t nonce=(uint32_t)GetRTTC();
	for (unsigned i=0;i<256;i++) {
		sniprintf(player.dump_dir,sizeof(player.dump_dir),"/CABD%08lx",(unsigned long)(nonce+i));
		FRESULT result=f_mkdir(player.dump_dir);
		if (result==FR_OK) return 1;
		if (result!=FR_EXIST) break;
	}
	player.dump_dir[0]=0;
	return 0;
}

const char *cab_player_begin(const char *path, uint32_t owner)
{
	uint8_t raw[32];
	UINT count;
	uint32_t crc=0xffffffffUL;
	uint64_t frames=0, dumps=0;
	if (!(user_io_get_core_features()&FEAT_CAB_STREAM)) return "Core lacks frame-input queue; load an updated SiDi128 RBF";
	if (!cab_player_stop()) return player.error;
	memset(&player,0,sizeof(player));
	if (f_open(&player.file,path,FA_READ)!=FR_OK) return failed("Cannot open packed CAB file");
	player.open=1;
	if (f_read(&player.file,raw,32,&count)!=FR_OK || count!=32 || !cab_decode_header(raw,f_size(&player.file),&player.header)) return failed("Invalid packed CAB header or length");
	if (player.header.version>1 && !(user_io_get_core_features()&FEAT_CAB_DIPS)) return failed("Core lacks CAB DIP support; load an updated SiDi128 RBF");
	if (player.header.dumps && !(player.dump_bytes=cab_player_dump_bytes())) return failed("Core lacks scheduled Save NVRAM support");
	// Validate all bytes and declared dump count before reset or ownership.
	for (uint32_t i=0;i<player.header.events;i++) {
		if (f_read(&player.file,raw,player.header.record_bytes,&count)!=FR_OK || count!=player.header.record_bytes || !cab_valid_record(raw,player.header.version)) return failed("Invalid packed CAB record");
		crc=cab_crc32(crc,raw,count); frames+=cab_le32(raw);
		if (cab_le32(raw+4)&0x400000UL) dumps+=cab_le32(raw);
		if (frames>player.header.frames || dumps>player.header.dumps) return failed("Packed CAB totals exceed header");
	}
	if ((crc^0xffffffffUL)!=player.header.crc || frames!=player.header.frames || dumps!=player.header.dumps) return failed("Packed CAB checksum or totals mismatch");
	if (f_lseek(&player.file,32)!=FR_OK) return failed("Cannot rewind packed CAB file");
	if (player.header.dumps && !make_dump_dir()) return failed("Cannot create CAB dump directory");
	remote_input_restart();
	if (!remote_input_set(owner,0,0,(uint32_t)GetRTTC())) return failed("Cannot claim CAB input ownership");
	player.owner=owner; player.active=1; user_io_remote_poll();
	spi_uio_cmd_cont(0x82); spi32le(player.header.frames); spi32le(player.header.events); SPI(1); DisableIO();
	WaitTimer(50);
	if (!read_status() || player.status.state!=1 || !fill_queue()) return failed("FPGA rejected CAB arm/prefill");
	spi_uio_cmd_cont(0x84); spi32le(player.status.epoch); DisableIO();
	if (!read_status() || (player.status.state!=2 && player.status.state!=5)) return failed("FPGA rejected CAB start");
	player.last_progress=(uint32_t)GetRTTC(); player.next_poll=player.last_progress;
	return NULL;
}

static int dump_poll(void)
{
	char path[64], final[64], metadata[512];
	UINT count;
	uint32_t seq=player.status.dump_seq;
	if (seq!=player.dumps_done+1 || seq>player.header.dumps || player.status.dump_frame!=player.status.frames) return 0;
	if (mist_cfg.ftp_readonly || usb_storage_is_configured() || user_io_raw_sd_in_use()) return 0;
	if (!player.output_open) {
		// Settling is a pause request, never an acknowledgement of CPU quiescence.
		if ((uint32_t)(player.status.raster-player.status.dump_raster)<2)
			return (uint32_t)GetRTTC()-player.last_progress<=2000;
		dump_path(path,seq,"TMP");
		if (f_open(&player.output,path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK) return 0;
		player.output_open=1; player.dump_started=player.status.dump_raster;
		player.dump_start_raster=player.status.raster; player.dump_start_ms=(uint32_t)GetRTTC();
		data_io_rx_begin(&player.rx,-1,player.dump_bytes); player.receiving=1;
	}
	if ((uint32_t)GetRTTC()-player.dump_start_ms>30000) return 0;
	if (data_io_rx_step(&player.rx,&player.output)!=FR_OK) return 0;
	if (player.rx.remaining) return 1;
	data_io_rx_end(&player.rx); player.receiving=0;
	if (f_sync(&player.output)!=FR_OK) return 0;
	FRESULT result=f_close(&player.output); player.output_open=0;
	if (result!=FR_OK || !read_status() || player.status.state!=5 || player.status.dump_seq!=seq) return 0;
	dump_path(path,seq,"TMP"); dump_path(final,seq,"RAM");
	if (f_rename(path,final)!=FR_OK) return 0;
	int len=sniprintf(metadata,sizeof(metadata),"{\"version\":1,\"epoch\":%lu,\"sequence\":%lu,\"frame\":%lu,\"request_raster\":%lu,\"start_raster\":%lu,\"end_raster\":%lu,\"bytes\":%lu,\"crc32\":\"%08lx\",\"pause_effective\":\"unverified\"}\n",
		(unsigned long)player.status.epoch,(unsigned long)seq,(unsigned long)player.status.dump_frame,
		(unsigned long)player.dump_started,(unsigned long)player.dump_start_raster,(unsigned long)player.status.raster,
		(unsigned long)player.dump_bytes,(unsigned long)(player.rx.crc^0xffffffffUL));
	if (len<0 || len>=sizeof(metadata)) return 0;
	dump_path(path,seq,"MET");
	if (f_open(&player.output,path,FA_WRITE|FA_CREATE_NEW)!=FR_OK) return 0;
	player.output_open=1;
	result=f_write(&player.output,metadata,len,&count);
	if (result!=FR_OK || count!=len || f_sync(&player.output)!=FR_OK) return 0;
	result=f_close(&player.output); player.output_open=0;
	if (result!=FR_OK) return 0;
	dump_path(final,seq,"JSON");
	if (f_rename(path,final)!=FR_OK) return 0;
	player.dumps_done++;
	spi_uio_cmd_cont(0x88); spi32le(player.status.epoch); spi32le(seq); DisableIO();
	if (!read_status() || (player.status.state!=6 && player.status.state!=2)) return 0;
	player.last_progress=(uint32_t)GetRTTC();
	return 1;
}

void cab_player_poll(void)
{
	uint32_t now=(uint32_t)GetRTTC();
	if (!player.active) return;
	if (remote_input_owner()!=player.owner) { failed("CAB input lease ended"); return; }
	if ((int32_t)(now-player.next_poll)<0) return;
	player.next_poll=now+2;
	if (!read_status()) { failed("CAB status protocol failed"); return; }
	if (player.status.state==4) {
		char reason[96];
		sniprintf(reason,sizeof(reason),"FPGA CAB error %u at frame %lu",player.status.error,(unsigned long)player.status.frames);
		failed(reason); return;
	}
	if (player.status.state==3) {
		if (player.status.frames!=player.header.frames || player.status.accepted!=player.header.events || player.dumps_done!=player.header.dumps) { failed("CAB completion counters mismatch"); return; }
		f_close(&player.file); player.open=0; player.active=0; if(!video_recorder_active() || video_recorder_owner()!=player.owner) remote_input_release(player.owner); return;
	}
	if (player.status.state==5) {
		if (!fill_queue() || !dump_poll()) failed("CAB dump transfer or publication failed");
		return;
	}
	if (player.status.state!=2 && player.status.state!=6) { failed("CAB stopped unexpectedly"); return; }
	if (player.status.frames!=player.last_frame) { player.last_frame=player.status.frames; player.last_progress=now; }
	else if ((uint32_t)(now-player.last_progress)>2000) { failed("CAB native raster stopped advancing"); return; }
	if (!fill_queue()) failed("CAB refill rejected or file read failed");
}

int cab_player_active(void) { return player.active; }
const cab_fpga_status_t *cab_player_status(void) { return &player.status; }
const char *cab_player_error(void) { return player.error; }
uint32_t cab_player_frames(void) { return player.header.frames; }
uint32_t cab_player_dump_total(void) { return player.header.dumps; }
uint32_t cab_player_dumps_done(void) { return player.dumps_done; }
uint32_t cab_player_dump_written(void) { return player.receiving ? player.dump_bytes-player.rx.remaining : 0; }
const char *cab_player_dump_dir(void) { return player.dump_dir; }
int cab_player_output_in_use(const FIL *file)
{
	return player.output_open && file->obj.fs==player.output.obj.fs && file->obj.sclust==player.output.obj.sclust;
}
int cab_player_output_path_in_use(const char *path)
{
	size_t len=strlen(player.dump_dir);
	return player.active && len && !strncasecmp(path,player.dump_dir,len) && (path[len]=='/' || path[len]==0);
}
int cab_player_file_in_use(const FIL *file)
{
	return cab_player_output_in_use(file) || (player.open && file->obj.fs==player.file.obj.fs && file->obj.sclust==player.file.obj.sclust);
}
