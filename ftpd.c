// ftpd.c - minimal single-session FTP server for the SD card (lwIP raw API + FatFs)

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

#include "FatFs/ff.h"
#include "ftpd.h"
#include "mist_cfg.h"
#include "user_io.h"
#include "usbdev.h"
#include "fat_compat.h"
#include "menu.h"
#include "errors.h"
#include "remote_input.h"
#include "arc_file.h"
#include "osd.h"
#include "hardware.h"
#include "cab_player.h"
#include "video_recorder.h"
#include "video_format.h"
#include "cab_format.h"

extern const char version[];

#define FTP_PORT        21
#define PASV_PORT_MIN   49200
#define PASV_PORT_NUM   16
#define CMD_MAX         320
#define PATH_MAX_LEN    260
#define XFER_SIZE       4096    // per-session buffer (listings, and file transfers when the fast buffer is taken)
#define FAST_SIZE       32768   // shared buffer for one file transfer at a time: SD cards are far
                                // quicker with 32 KB multi-block commands than with 4 KB ones
#define IDLE_SECONDS    300     // control connection idle timeout
#define XFER_IDLE_SECS  30      // data connection stall timeout
#define FTP_SESSIONS    3       // Explorer / FileZilla open a 2nd connection on their own

typedef enum { OP_NONE = 0, OP_LIST, OP_NLST, OP_RETR, OP_STOR } ftp_op_t;

struct sess {
	struct tcp_pcb *ctl;        // control connection (single session)
	struct tcp_pcb *lst;        // passive listener
	struct tcp_pcb *dat;        // data connection
	uint8_t  dat_ready;         // data connection established
	uint8_t  dat_closing;       // tcp_close() must be retried from poll
	uint8_t  ctl_closing;
	ftp_op_t op;
	uint8_t  started;           // "150" sent, transfer running
	uint8_t  authed, user_ok, fails;
	uint8_t  port_set;
	ip_addr_t port_ip;
	uint16_t port_num;
	uint16_t idle, dat_idle, op_wait;
	uint16_t cmdlen;
	uint8_t  overflow, iac;
	uint8_t  file_open, dir_open, wr_error;
	uint16_t xlen, xpos;
	uint8_t *buf;               // file transfer buffer: fast_buf or xbuf
	uint16_t bufsz;
	uint8_t  eof;
	uint64_t rest;
	char cwd[PATH_MAX_LEN];
	char rnfr[PATH_MAX_LEN];
	uint8_t have_rnfr;
	char cmd[CMD_MAX];
	FIL  fil;
	DIR  dir;
	FILINFO fno;
	uint8_t xbuf[XFER_SIZE] __attribute__ ((aligned(4)));
};

static struct sess SS[FTP_SESSIONS];
static uint8_t fast_buf[FAST_SIZE] __attribute__ ((aligned(32)));
static struct sess *fast_owner;
static struct tcp_pcb *srv;      // listening on port 21
static uint16_t pasv_next;
typedef enum { CMD_LOAD, CMD_INPUT, CMD_RESET, CMD_RELEASE, CMD_KEEPALIVE, CMD_CAB, CMD_CABSTOP, CMD_VIDEO, CMD_VIDEOSTOP } control_cmd_t;
static struct sess *command_owner;
static uint32_t video_count,video_period;
static uint16_t video_step=16;
static control_cmd_t pending_command;
static unsigned pending_port;
static uint32_t pending_map;
static uint32_t pending_epoch;
static int pending_has_epoch;
static char load_path[PATH_MAX_LEN];

static const char *const months[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

static void dat_close(struct sess *s);
static void pump(struct sess *s);

// ------------------------------------------------------------------ helpers

static char *u64dec(uint64_t v, char *buf24)   // NUL terminated decimal, returns pointer inside buf24
{
	char *p = buf24 + 23;
	*p = 0;
	do { *--p = '0' + (char)(v % 10); v /= 10; } while (v);
	return p;
}

static void reply(struct sess *s, const char *fmt, ...)
{
	char b[384];
	va_list ap;
	int n;
	if (!s->ctl) return;
	va_start(ap, fmt);
	n = vsniprintf(b, sizeof(b) - 2, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if (n > (int)sizeof(b) - 3) n = sizeof(b) - 3;
	b[n++] = '\r'; b[n++] = '\n';
	tcp_write(s->ctl, b, n, TCP_WRITE_FLAG_COPY);
	tcp_output(s->ctl);
}

static int ct_equal(const char *a, const char *b)   // constant-time-ish compare
{
	size_t la = strlen(a), lb = strlen(b), i;
	uint8_t d = (la != lb);
	for (i = 0; i < la; i++) d |= (uint8_t)(a[i] ^ b[i < lb ? i : 0]);
	return d == 0;
}

// resolve 'arg' (client supplied) against cwd -> normalised absolute path in 'out'
static int resolve(struct sess *s, const char *arg, char *out)
{
	char tmp[PATH_MAX_LEN * 2];
	size_t o = 0;
	const char *p;

	if (arg[0] == '/') {
		if (strlen(arg) >= sizeof(tmp)) return -1;
		strcpy(tmp, arg);
	} else {
		if (strlen(s->cwd) + 1 + strlen(arg) >= sizeof(tmp)) return -1;
		strcpy(tmp, s->cwd);
		strcat(tmp, "/");
		strcat(tmp, arg);
	}

	out[o++] = '/';
	p = tmp;
	while (*p) {
		const char *e;
		size_t l;
		while (*p == '/') p++;
		if (!*p) break;
		e = p;
		while (*e && *e != '/') e++;
		l = e - p;
		if (l == 1 && p[0] == '.') {
			// skip
		} else if (l == 2 && p[0] == '.' && p[1] == '.') {
			if (o > 1) {
				o--;                                  // drop trailing '/'... then back to previous '/'
				while (o > 1 && out[o - 1] != '/') o--;
			}
		} else {
			if (o + l + 1 >= PATH_MAX_LEN) return -1;
			memcpy(out + o, p, l);
			o += l;
			out[o++] = '/';
		}
		p = e;
	}
	if (o > 1) o--;                                   // strip final '/'
	out[o] = 0;
	return 0;
}

static int is_root(const char *path) { return path[0] == '/' && path[1] == 0; }

static const char *fr_text(FRESULT r)
{
	switch (r) {
		case FR_NO_FILE: case FR_NO_PATH: return "No such file or directory";
		case FR_DENIED: case FR_WRITE_PROTECTED: return "Permission denied";
		case FR_EXIST: return "File exists";
		case FR_DISK_ERR: case FR_NOT_READY: case FR_NOT_ENABLED: case FR_NO_FILESYSTEM: return "Storage not available";
		case FR_INVALID_NAME: return "Invalid name";
		default: return "Operation failed";
	}
}

// True if the file is currently mounted as a disk image by the core (writing to it behind the core's back corrupts it)
static int in_use_by_core(const char *path)
{
	FIL f;
	int used;
	if(cab_player_output_path_in_use(path) || video_output_path_in_use(path)) return 1;
	if (f_open(&f, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) return 0;
	used = user_io_file_is_mounted(&f) || cab_player_file_in_use(&f);
	f_close(&f);
	return used;
}

static int writes_allowed(struct sess *s)
{
	if (mist_cfg.ftp_readonly) { reply(s, "550 Server is read-only"); return 0; }
	// another writer with its own view of the FAT would corrupt the card
	if (usb_storage_is_configured()) { reply(s, "550 SD card is exported over USB, read-only"); return 0; }
	if (user_io_raw_sd_in_use()) { reply(s, "550 SD card is in use by the running core, read-only"); return 0; }
	return 1;
}

// ------------------------------------------------------------------ transfers

static void op_cleanup(struct sess *s)
{
	if (s->file_open) { f_close(&s->fil); s->file_open = 0; }
	if (s->dir_open)  { f_closedir(&s->dir); s->dir_open = 0; }
	s->op = OP_NONE;
	s->started = 0;
	s->xlen = s->xpos = 0;
	s->eof = 0;
	s->rest = 0;
	if (fast_owner == s) fast_owner = NULL;
	s->buf = s->xbuf;
	s->bufsz = XFER_SIZE;
}

static void op_finish(struct sess *s, int ok, const char *err)
{
	int was_store = (s->op == OP_STOR);
	if (was_store && s->file_open) {
		if (f_close(&s->fil) != FR_OK) ok = 0;
		s->file_open = 0;
	}
	op_cleanup(s);
	dat_close(s);
	if (ok) reply(s, "226 Transfer complete");
	else    reply(s, "%s", err ? err : "451 Transfer failed");
}

static void pcb_detach(struct tcp_pcb *pcb)
{
	tcp_arg(pcb, NULL);
	tcp_recv(pcb, NULL);
	tcp_sent(pcb, NULL);
	tcp_err(pcb, NULL);
	tcp_poll(pcb, NULL, 0);
}

static err_t dat_poll(void *arg, struct tcp_pcb *pcb);
static void dat_err(void *arg, err_t err);

// close the data pcb; callbacks are removed BEFORE tcp_close() because lwIP may free the pcb inside it
static int dat_try_close(struct sess *s, struct tcp_pcb *pcb)
{
	pcb_detach(pcb);
	if (tcp_close(pcb) == ERR_OK) return 1;
	tcp_arg(pcb, s);              // keep it alive and retry from the poll callback
	tcp_err(pcb, dat_err);
	tcp_poll(pcb, dat_poll, 2);
	return 0;
}

static void dat_close(struct sess *s)
{
	struct tcp_pcb *pcb = s->dat;
	s->dat_ready = 0;
	s->port_set = 0;
	if (s->lst) { tcp_close(s->lst); s->lst = NULL; }
	if (!pcb) return;
	if (dat_try_close(s, pcb)) { s->dat = NULL; s->dat_closing = 0; }
	else s->dat_closing = 1;
}

// fill s->xbuf with the next directory line(s)
static void list_next(struct sess *s)
{
	s->xlen = s->xpos = 0;
	if (s->eof) return;
	for (;;) {
		FILINFO *fi = &s->fno;
		if (f_readdir(&s->dir, fi) != FR_OK || !fi->fname[0]) { s->eof = 1; return; }
		if (s->op == OP_NLST) {
			s->xlen = (uint16_t)siprintf((char *)s->xbuf, "%s\r\n", fi->fname);
		} else {
			char num[24];
			char *n = u64dec((uint64_t)fi->fsize, num);
			int mo = ((fi->fdate >> 5) & 15);
			if (mo < 1 || mo > 12) mo = 1;
			s->xlen = (uint16_t)siprintf((char *)s->xbuf, "%s 1 sd sd %s %s %2d  %4d %s\r\n",
				(fi->fattrib & AM_DIR) ? "drwxr-xr-x" : ((fi->fattrib & AM_RDO) ? "-r--r--r--" : "-rw-r--r--"),
				n, months[mo - 1], (fi->fdate & 31), 1980 + (fi->fdate >> 9), fi->fname);
		}
		if (s->xlen) return;
	}
}

static void pump(struct sess *s)
{
	struct tcp_pcb *pcb = s->dat;
	if (!pcb || !s->dat_ready || !s->started) return;
	if (s->op != OP_RETR && s->op != OP_LIST && s->op != OP_NLST) return;

	for (;;) {
		u16_t room = tcp_sndbuf(pcb);
		if (s->xpos >= s->xlen) {
			if (s->eof) break;
			if (room < 1024) return;     // wait for ACKs before reading more from the SD card
			if (s->op == OP_RETR) {
				UINT br = 0;
				if (f_read(&s->fil, s->buf, s->bufsz, &br) != FR_OK) { op_finish(s, 0, "451 Read error"); return; }
				s->xlen = (uint16_t)br; s->xpos = 0;
				if (br == 0) { s->eof = 1; break; }
			} else {
				list_next(s);
				if (s->eof && !s->xlen) break;
			}
		}
		{
			const uint8_t *src = (s->op == OP_RETR) ? s->buf : s->xbuf;
			u16_t n = s->xlen - s->xpos;
			if (n > room) n = room;
			if (n == 0) return;
			if (tcp_write(pcb, src + s->xpos, n, TCP_WRITE_FLAG_COPY) != ERR_OK) { tcp_output(pcb); return; }
			s->xpos += n;
			s->dat_idle = 0;
		}
	}
	// everything queued
	tcp_output(pcb);
	op_finish(s, 1, NULL);
}

static void start_transfer(struct sess *s)
{
	if (s->op == OP_NONE || s->started || !s->dat_ready) return;
	s->started = 1;
	reply(s, "150 Opening data connection");
	if (s->op == OP_RETR || s->op == OP_LIST || s->op == OP_NLST) pump(s);
}

static err_t dat_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return ERR_OK;
	(void)err;
	if (!p) {   // remote closed its side
		if (s->op == OP_STOR && s->started) {
			if (s->xlen && !s->wr_error) {
				UINT bw;
				if (f_write(&s->fil, s->buf, s->xlen, &bw) != FR_OK || bw != s->xlen) s->wr_error = 1;
				s->xlen = 0;
			}
			if (s->wr_error) op_finish(s, 0, "452 Write error (disk full?)");
			else            op_finish(s, 1, NULL);
		} else if (s->op != OP_NONE && s->started) {
			op_cleanup(s);
			reply(s, "426 Connection closed; transfer aborted");
			dat_close(s);
		} else {
			dat_close(s);
		}
		return ERR_OK;
	}

	s->dat_idle = 0;
	if (s->op == OP_STOR && s->started && !s->wr_error) {
		struct pbuf *q;
		for (q = p; q; q = q->next) {
			const uint8_t *src = (const uint8_t *)q->payload;
			u16_t left = q->len;
			while (left) {
				u16_t n = s->bufsz - s->xlen;
				if (n > left) n = left;
				memcpy(s->buf + s->xlen, src, n);
				s->xlen += n; src += n; left -= n;
				if (s->xlen == s->bufsz) {
					UINT bw;
					if (f_write(&s->fil, s->buf, s->bufsz, &bw) != FR_OK || bw != s->bufsz) { s->wr_error = 1; s->xlen = 0; break; }
					s->xlen = 0;
				}
			}
			if (s->wr_error) break;
		}
	}
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	if (s->wr_error && s->op == OP_STOR && s->started) {
		op_finish(s, 0, "452 Write error (disk full?)");
	}
	return ERR_OK;
}

static err_t dat_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return ERR_OK;
	(void)pcb; (void)len;
	s->dat_idle = 0;
	pump(s);
	return ERR_OK;
}

static void dat_err(void *arg, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return;
	(void)err;
	// pcb already freed by lwIP
	s->dat = NULL;
	s->dat_ready = 0;
	s->dat_closing = 0;
	if (s->op != OP_NONE) {
		int started = s->started;
		op_cleanup(s);
		reply(s, started ? "426 Data connection lost" : "425 Can't open data connection");
	}
}

static err_t dat_poll(void *arg, struct tcp_pcb *pcb)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return ERR_OK;
	(void)pcb;
	if (s->dat_closing && s->dat) {
		if (dat_try_close(s, s->dat)) { s->dat = NULL; s->dat_closing = 0; }
		return ERR_OK;
	}
	if (s->op != OP_NONE && s->started) {
		pump(s);
		if (++s->dat_idle > XFER_IDLE_SECS && s->op != OP_NONE) {
			op_cleanup(s);
			reply(s, "426 Transfer timed out");
			dat_close(s);
		}
	}
	return ERR_OK;
}

static void dat_attach(struct sess *s, struct tcp_pcb *pcb)
{
	s->dat = pcb;
	s->dat_ready = 1;
	s->dat_idle = 0;
	tcp_nagle_disable(pcb);
	tcp_arg(pcb, s);
	tcp_recv(pcb, dat_recv);
	tcp_sent(pcb, dat_sent);
	tcp_err(pcb, dat_err);
	tcp_poll(pcb, dat_poll, 2);
}

static err_t pasv_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (err != ERR_OK || !newpcb) return ERR_VAL;
	if (!s) { tcp_abort(newpcb); return ERR_ABRT; }
	// only accept the data connection from the control peer
	if (!s->ctl || !ip_addr_eq(&newpcb->remote_ip, &s->ctl->remote_ip) || s->dat) {
		tcp_abort(newpcb);
		return ERR_ABRT;
	}
	if (s->lst) { tcp_close(s->lst); s->lst = NULL; }
	dat_attach(s, newpcb);
	start_transfer(s);
	return ERR_OK;
}

static err_t active_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (!s) { tcp_abort(pcb); return ERR_ABRT; }
	if (err != ERR_OK) return err;
	dat_attach(s, pcb);
	start_transfer(s);
	return ERR_OK;
}

static void active_connect(struct sess *s)
{
	struct tcp_pcb *pcb = tcp_new();
	if (!pcb) { op_cleanup(s); reply(s, "425 Out of memory"); return; }
	tcp_arg(pcb, s);
	tcp_err(pcb, dat_err);
	s->dat = pcb;
	s->dat_ready = 0;
	if (tcp_connect(pcb, &s->port_ip, s->port_num, active_connected) != ERR_OK) {
		tcp_err(pcb, NULL);
		tcp_abort(pcb);
		s->dat = NULL;
		op_cleanup(s);
		reply(s, "425 Can't open data connection");
	}
}

// common tail of LIST/NLST/RETR/STOR once the object is opened
static void begin_op(struct sess *s, ftp_op_t op)
{
	s->op = op;
	s->started = 0;
	s->wr_error = 0;
	s->xlen = s->xpos = 0;
	s->eof = 0;
	s->buf = s->xbuf;
	s->bufsz = XFER_SIZE;
	if ((op == OP_RETR || op == OP_STOR) && !fast_owner) {
		fast_owner = s;
		s->buf = fast_buf;
		s->bufsz = FAST_SIZE;
	}
	if (s->dat && s->dat_ready) start_transfer(s);
	else if (s->port_set && !s->dat) active_connect(s);
	// else: passive client has not connected yet -> pasv_accept() starts it
}

static int data_channel_available(struct sess *s)
{
	return (s->dat && s->dat_ready) || s->lst || s->port_set;
}

// ------------------------------------------------------------------ commands

static void cmd_pasv(struct sess *s, int extended)
{
	struct tcp_pcb *l;
	uint16_t port = 0;
	int i;
	const ip4_addr_t *ip = netif_ip4_addr(netif_default);

	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }
	if (s->lst) { tcp_close(s->lst); s->lst = NULL; }
	if (s->dat) dat_close(s);
	s->port_set = 0;

	l = tcp_new();
	if (!l) { reply(s, "425 Out of memory"); return; }
	for (i = 0; i < PASV_PORT_NUM; i++) {
		port = PASV_PORT_MIN + (pasv_next++ % PASV_PORT_NUM);
		if (tcp_bind(l, IP_ADDR_ANY, port) == ERR_OK) break;
		port = 0;
	}
	if (!port) { tcp_abort(l); reply(s, "425 No free data port"); return; }
	l = tcp_listen(l);
	if (!l) { reply(s, "425 Out of memory"); return; }
	tcp_arg(l, s);
	tcp_accept(l, pasv_accept);
	s->lst = l;

	if (extended) {
		reply(s, "229 Entering Extended Passive Mode (|||%u|)", port);
	} else {
		uint32_t a = ip4_addr_get_u32(ip);
		reply(s, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
			(unsigned)(a & 255), (unsigned)((a >> 8) & 255), (unsigned)((a >> 16) & 255), (unsigned)(a >> 24),
			(unsigned)(port >> 8), (unsigned)(port & 255));
	}
}

static void cmd_port(struct sess *s, const char *arg)
{
	unsigned v[6];
	int i;
	const char *p = arg;
	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }
	for (i = 0; i < 6; i++) {
		char *e;
		unsigned long x;
		if (!isdigit((unsigned char)*p)) { reply(s, "501 Syntax error"); return; }
		x = strtoul(p, &e, 10);
		if (x > 255) { reply(s, "501 Syntax error"); return; }
		v[i] = (unsigned)x;
		p = e;
		if (i < 5) { if (*p != ',') { reply(s, "501 Syntax error"); return; } p++; }
	}
	{
		ip4_addr_t a;
		IP4_ADDR(&a, v[0], v[1], v[2], v[3]);
		// refuse third-party / bounce attacks: only the control peer, only unprivileged ports
		if (ip4_addr_get_u32(ip_2_ip4(&s->ctl->remote_ip)) != ip4_addr_get_u32(&a)) {
			reply(s, "504 Address must match the control connection");
			return;
		}
		if (s->lst) { tcp_close(s->lst); s->lst = NULL; }
		if (s->dat) dat_close(s);
		ip_addr_copy_from_ip4(s->port_ip, a);
		s->port_num = (uint16_t)((v[4] << 8) | v[5]);
		if (s->port_num < 1024) { reply(s, "504 Port not allowed"); return; }
		s->port_set = 1;
		reply(s, "200 PORT command successful");
	}
}

static void cmd_list(struct sess *s, const char *arg, ftp_op_t op)
{
	char path[PATH_MAX_LEN];
	FRESULT r;

	while (*arg == '-') {                       // ignore "ls" style flags
		while (*arg && *arg != ' ') arg++;
		while (*arg == ' ') arg++;
	}
	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }
	if (resolve(s, *arg ? arg : ".", path)) { reply(s, "501 Path too long"); return; }
	if (!data_channel_available(s)) { reply(s, "425 Use PASV first"); return; }
	r = f_opendir(&s->dir, path);
	if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
	s->dir_open = 1;
	begin_op(s, op);
}

static void cmd_retr(struct sess *s, const char *arg)
{
	char path[PATH_MAX_LEN];
	FRESULT r;

	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }
	if (!*arg || resolve(s, arg, path)) { reply(s, "501 Bad path"); return; }
	if (!data_channel_available(s)) { reply(s, "425 Use PASV first"); return; }
	r = f_open(&s->fil, path, FA_READ | FA_OPEN_EXISTING);
	if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
	if (cab_player_output_in_use(&s->fil)) {
		f_close(&s->fil); reply(s,"450 CAB output is not complete"); return;
	}
	s->file_open = 1;
	if (s->rest) {
		if (s->rest > f_size(&s->fil) || f_lseek(&s->fil, (FSIZE_t)s->rest) != FR_OK) {
			f_close(&s->fil); s->file_open = 0; s->rest = 0;
			reply(s, "554 Invalid restart position");
			return;
		}
	}
	s->rest = 0;
	begin_op(s, OP_RETR);
}

static void cmd_stor(struct sess *s, const char *arg, int append)
{
	char path[PATH_MAX_LEN];
	FRESULT r;
	BYTE mode;

	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }
	if (!writes_allowed(s)) return;
	if (!*arg || resolve(s, arg, path) || is_root(path)) { reply(s, "501 Bad path"); return; }
	if (!data_channel_available(s)) { reply(s, "425 Use PASV first"); return; }
	if (in_use_by_core(path)) { reply(s, "550 File is in use by the running core"); return; }

	mode = FA_WRITE | ((s->rest || append) ? FA_OPEN_ALWAYS : FA_CREATE_ALWAYS);
	r = f_open(&s->fil, path, mode);
	if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
	s->file_open = 1;
	if (append) {
		r = f_lseek(&s->fil, f_size(&s->fil));
	} else if (s->rest) {
		r = (s->rest > f_size(&s->fil)) ? FR_INVALID_PARAMETER : f_lseek(&s->fil, (FSIZE_t)s->rest);
		if (r == FR_OK) r = f_truncate(&s->fil);     // restart: everything after the offset is rewritten
	}
	s->rest = 0;
	if (r != FR_OK) {
		f_close(&s->fil); s->file_open = 0;
		reply(s, "554 Invalid restart position");
		return;
	}
	begin_op(s, OP_STOR);
}

static uint32_t control_id(struct sess *s) { return (uint32_t)(s - SS) + 1; }

static void site_help(struct sess *s)
{
	reply(s, "214-SITE commands:\r\n LOAD <path.arc|path.rbf>\r\n STATUS\r\n INPUT <port 0-5> <hex mask 00000-fffff> [epoch]\r\n KEEPALIVE\r\n RELEASE\r\n RESET\r\n CAB <packed script path>\r\n CABSTATUS\r\n CABSTOP\r\n VIDEO <count> <period> [row-step]\r\n VIDSTATUS\r\n VIDSTOP\r\n214 Immediate INPUT or FPGA-raster-clocked CAB; lease 5000 ms");
}

static int control_ready(struct sess *s)
{
	if (mist_cfg.ftp_readonly) { reply(s, "550 Server is read-only"); return 0; }
	if (user_io_core_type() != CORE_TYPE_8BIT) { reply(s, "550 Control requires an 8-bit protocol core"); return 0; }
	if (user_io_osd_is_visible()) { reply(s, "450 Close the OSD before controlling the game"); return 0; }
	if (remote_input_owner() && remote_input_owner() != control_id(s)) {
		reply(s, "450 Inputs are owned by another session"); return 0;
	}
	return 1;
}

static void site_status(struct sess *s)
{
	uint32_t owner = remote_input_owner();
	reply(s, "211-MiST control status\r\n FW=%s\r\n CORE=%s\r\n FILE=%s\r\n TYPE=%02x\r\n BUTTONS=%s",
		version + 5, user_io_get_core_name(), OsdCoreName(), user_io_core_type(), arc_get_buttons());
	reply(s, " PROTOCOL=1\r\n TIMING=%s\r\n FRAME_SYNC=%u\r\n OWNER=%s\r\n LEASE_MS=%lu\r\n EPOCH=%lu\r\n SEQ=%lu\r\n OSD=%u",
		cab_player_active() ? "raster-cab" : "immediate", !!(user_io_get_core_features()&FEAT_CAB_STREAM),
		!owner ? "none" : owner == control_id(s) ? "self" : "other",
		(unsigned long)remote_input_remaining((uint32_t)GetRTTC()),
		(unsigned long)remote_input_epoch(), (unsigned long)remote_input_sequence(), user_io_osd_is_visible());
	reply(s," CAB_DIPS=%u",!!(user_io_get_core_features()&FEAT_CAB_DIPS));
	reply(s," CAB_DUMP=%u\r\n DUMP_BYTES=%lu",!!(user_io_get_core_features()&FEAT_CAB_DUMP),(unsigned long)cab_player_dump_bytes());
	reply(s," VIDEO_CAPTURE=%u\r\n STACK_USED=%u",!!(user_io_get_core_features()&FEAT_VIDEO_CAPTURE),video_stack_used());
	for (unsigned port = 0; port < REMOTE_INPUT_PORTS; port++)
		reply(s, " JOY%u=%05lx", port, (unsigned long)remote_input_value(port));
	reply(s, "211 End");
}

static void cmd_site(struct sess *s, char *arg)
{
	char *path_arg = strchr(arg, ' ');
	char path[PATH_MAX_LEN];
	const char *extension;
	FILINFO fi;
	if (path_arg) {
		*path_arg++ = 0;
		while (*path_arg == ' ') path_arg++;
	}
	if (!strcasecmp(arg, "HELP")) {
		site_help(s);
		return;
	}
	remote_input_poll((uint32_t)GetRTTC());
    if(!strcasecmp(arg,"VIDSTATUS")) {
        if(path_arg && *path_arg) {reply(s,"501 VIDSTATUS takes no arguments");return;}
        if(video_recorder_active() && !mist_cfg.ftp_readonly) remote_input_keepalive(control_id(s),(uint32_t)GetRTTC());
        reply(s,"211-Video status\r\n ACTIVE=%u\r\n DONE=%lu\r\n TOTAL=%lu\r\n DIR=%s\r\n ERROR=%s\r\n STACK_USED=%u",
            video_recorder_active(),(unsigned long)video_recorder_done(),(unsigned long)video_recorder_total(),video_recorder_dir(),video_recorder_error(),video_stack_used());
        reply(s," ROWS=%lu\r\n WIDTH=%lu\r\n HEIGHT=%lu\r\n STEP=%u\r\n REQUESTED=%lu\r\n ACTUAL=%lu\r\n211 End",
            (unsigned long)video_recorder_rows(),(unsigned long)video_recorder_width(),
            (unsigned long)video_recorder_height(),(unsigned)video_recorder_step(),
            (unsigned long)video_recorder_requested(),(unsigned long)video_recorder_actual());return;
    }
    if(!strcasecmp(arg,"VIDEO") || !strcasecmp(arg,"VIDSTOP")) {
        if(!control_ready(s))return;
        if(remote_input_owner() && remote_input_owner()!=control_id(s)) {reply(s,"450 Inputs owned by another session");return;}
        if(!strcasecmp(arg,"VIDEO")) {
            char *end;unsigned long count,period,step=16;
            if(!path_arg || !isdigit((unsigned char)*path_arg)) {reply(s,"501 VIDEO requires count and period");return;}
            errno=0;count=strtoul(path_arg,&end,10);
            if(errno || *end!=' ' || !count || count>10000) {reply(s,"501 Invalid video count");return;}
            while(*end==' ')end++;path_arg=end;errno=0;period=strtoul(path_arg,&end,10);
            if(errno || !period || period>10000000) {reply(s,"501 Invalid video period");return;}
            while(*end==' ')end++;
            if(*end) {errno=0;step=strtoul(end,&end,10);if(errno || *end || !step || step>512) {reply(s,"501 Invalid row step");return;}}
            video_count=count;video_period=period;video_step=step;pending_command=CMD_VIDEO;
        } else {
            if(path_arg && *path_arg) {reply(s,"501 VIDSTOP takes no arguments");return;}pending_command=CMD_VIDEOSTOP;
        }
        command_owner=s;return;
    }
	if (!strcasecmp(arg,"CABSTATUS")) {
		const cab_fpga_status_t *state=cab_player_status();
		if (path_arg && *path_arg) { reply(s,"501 CABSTATUS takes no arguments"); return; }
		if (cab_player_active() && !mist_cfg.ftp_readonly)
			remote_input_keepalive(control_id(s),(uint32_t)GetRTTC());
		reply(s,"211-CAB status\r\n STATE=%u\r\n ACTIVE=%u\r\n FPGA_EPOCH=%lu\r\n FRAME=%lu\r\n TOTAL=%lu\r\n ACCEPTED=%lu\r\n APPLIED=%lu",
			state->state,cab_player_active(),(unsigned long)state->epoch,(unsigned long)state->frames,
			(unsigned long)cab_player_frames(),(unsigned long)state->accepted,(unsigned long)state->applied);
		reply(s," RASTER=%lu\r\n QUEUED=%u\r\n CREDITS=%u\r\n INPUTS=%06lx\r\n DIP_VALUE=%08lx\r\n DIP_VALID=%u\r\n FPGA_ERROR=%u\r\n ERROR=%s",
			(unsigned long)state->raster,state->queued,state->credits,(unsigned long)state->inputs,
			(unsigned long)state->dipsw,state->dipsw_valid,state->error,cab_player_error());
		reply(s," DUMP_SEQ=%lu\r\n DUMP_FRAME=%lu\r\n DUMP_RASTER=%lu\r\n DUMPS_DONE=%lu\r\n DUMPS_TOTAL=%lu\r\n DUMP_DIR=%s\r\n DUMP_BYTES=%lu\r\n DUMP_WRITTEN=%lu\r\n PAUSE_EFFECTIVE=unverified\r\n211 End",
			(unsigned long)state->dump_seq,(unsigned long)state->dump_frame,(unsigned long)state->dump_raster,
			(unsigned long)cab_player_dumps_done(),(unsigned long)cab_player_dump_total(),cab_player_dump_dir(),
			(unsigned long)cab_player_dump_bytes(),(unsigned long)cab_player_dump_written());
		return;
	}
	if (!strcasecmp(arg,"CABSTOP")) {
		if (path_arg && *path_arg) { reply(s,"501 CABSTOP takes no arguments"); return; }
		if (remote_input_owner() && remote_input_owner()!=control_id(s)) { reply(s,"450 Inputs are owned by another session"); return; }
		pending_command=CMD_CABSTOP; command_owner=s; return;
	}
	if (!strcasecmp(arg, "STATUS")) {
		if (path_arg && *path_arg) { reply(s, "501 STATUS takes no arguments"); return; }
		site_status(s); return;
	}
	if (!strcasecmp(arg, "RELEASE") || !strcasecmp(arg, "KEEPALIVE") || !strcasecmp(arg, "RESET")) {
		if (path_arg && *path_arg) { reply(s, "501 Unexpected argument"); return; }
		if (!strcasecmp(arg, "RELEASE")) {
			if (remote_input_owner() && remote_input_owner() != control_id(s)) {
				reply(s, "450 Inputs are owned by another session"); return;
			}
			pending_command = CMD_RELEASE;
		} else {
			if (!control_ready(s)) return;
			if (!strcasecmp(arg, "KEEPALIVE")) {
				if (remote_input_owner() != control_id(s)) { reply(s, "550 No input lease"); return; }
				pending_command = CMD_KEEPALIVE;
			} else pending_command = CMD_RESET;
		}
		command_owner = s; return;
	}
	if (!strcasecmp(arg, "INPUT")) {
		char *end;
		unsigned long port, map;
		if (!control_ready(s)) return;
		if (cab_player_active()) { reply(s,"450 CAB playback owns inputs; stop it first"); return; }
		if (!path_arg || !isdigit((unsigned char)*path_arg)) { reply(s, "501 INPUT requires port and hex mask"); return; }
		errno = 0; port = strtoul(path_arg, &end, 10);
		if (errno || port >= REMOTE_INPUT_PORTS || *end != ' ') { reply(s, "501 Invalid input port"); return; }
		while (*end == ' ') end++;
		if (!isxdigit((unsigned char)*end)) { reply(s, "501 Invalid input mask"); return; }
		path_arg = end; errno = 0; map = strtoul(path_arg, &end, 16);
		if (errno || map > REMOTE_INPUT_MASK || (*end && *end != ' ')) { reply(s, "501 Invalid input mask"); return; }
		while (*end == ' ') end++;
		pending_has_epoch = *end != 0;
		if (pending_has_epoch) {
			if (!isdigit((unsigned char)*end)) { reply(s, "501 Invalid input epoch"); return; }
			path_arg = end; errno = 0; pending_epoch = strtoul(path_arg, &end, 10);
			while (*end == ' ') end++;
			if (errno || *end) { reply(s, "501 Invalid input epoch"); return; }
		}
		pending_port = port; pending_map = map;
		pending_command = CMD_INPUT; command_owner = s; return;
	}
	if (strcasecmp(arg, "LOAD") && strcasecmp(arg,"CAB")) { reply(s, "502 Unknown SITE command"); return; }
	if (!path_arg || !*path_arg) { reply(s, "501 SITE LOAD requires a path"); return; }
	if (!writes_allowed(s)) return;
	if (remote_input_owner() && remote_input_owner() != control_id(s)) {
		reply(s, "450 Inputs are owned by another session"); return;
	}
	for (int i = 0; i < FTP_SESSIONS; i++) {
		if (SS[i].op != OP_NONE) { reply(s, "450 Transfer in progress"); return; }
	}
	if (resolve(s, path_arg, path)) { reply(s, "550 Path too long"); return; }
	if (!strcasecmp(arg,"CAB")) {
		if (!control_ready(s)) return;
		strcpy(load_path,path); pending_command=CMD_CAB; command_owner=s; return;
	}
	extension = GetExtension(path);
	if (!extension || (strcasecmp(extension, "ARC") && strcasecmp(extension, "RBF"))) {
		reply(s, "550 Expected an ARC or RBF file"); return;
	}
	if (f_stat(path, &fi) != FR_OK || (fi.fattrib & AM_DIR) || !fi.fsize) {
		reply(s, "550 No such core file or file is empty"); return;
	}
	strcpy(load_path, path);
	pending_command = CMD_LOAD;
	command_owner = s;
	// No final reply until the main loop has completed the load.
}

void ftpd_poll(void)
{
	struct sess *s = command_owner;
	char previous[PATH_MAX_LEN];
	static char snapshot_message[280];
	const char *snapshot_path=NULL,*snapshot_error=NULL;
	char *name;
	unsigned char err;
	cab_player_poll();
    video_recorder_poll();
	if(video_snapshot_take_result(&snapshot_path,&snapshot_error)) {
		if(snapshot_path) {
			snprintf(snapshot_message,sizeof(snapshot_message),"Snapshot saved:\n%s",snapshot_path);
			InfoMessage(snapshot_message);
		} else {
			snprintf(snapshot_message,sizeof(snapshot_message),"%s",
				snapshot_error ? snapshot_error : "Snapshot failed");
			ErrorMessage(snapshot_message,0);
		}
	}
	user_io_remote_poll();
	if (!s) return;
	if (!s->ctl || !s->authed || s->ctl_closing) { command_owner = NULL; return; }
	if (pending_command != CMD_LOAD) {
		uint32_t id = control_id(s);
		int ok = 1;
		if (pending_command == CMD_RELEASE || pending_command==CMD_CABSTOP) {
			video_recorder_stop();ok=cab_player_stop(); remote_input_release(id);
		}
		else if (!control_ready(s)) { command_owner = NULL; return; }
        else if(pending_command==CMD_VIDEO) {
            const char *error=video_recorder_begin(id,video_count,video_period,video_step);
            if(error) reply(s,"550 %s",error);else reply(s,"200 Video recording started DIR=%s",video_recorder_dir());
            command_owner=NULL;return;
        } else if(pending_command==CMD_VIDEOSTOP) video_recorder_stop();
		else if (pending_command==CMD_CAB) {
			const char *error=video_recorder_active() ? "Stop video recording before starting a new CAB" : cab_player_begin(load_path,id);
			if (error) reply(s,"550 %s",error);
			else reply(s,"200 CAB started epoch=%lu frames=%lu",(unsigned long)cab_player_status()->epoch,(unsigned long)cab_player_frames());
			user_io_remote_poll(); command_owner=NULL; return;
		}
		else if (pending_command == CMD_INPUT && pending_has_epoch && pending_epoch != remote_input_epoch()) {
			reply(s, "550 Input epoch changed; query STATUS again");
			command_owner = NULL; return;
		} else if (pending_command == CMD_INPUT)
			ok = remote_input_set(id, pending_port, pending_map, (uint32_t)GetRTTC());
		else if (pending_command == CMD_KEEPALIVE)
			ok = remote_input_keepalive(id, (uint32_t)GetRTTC());
		else if (pending_command == CMD_RESET) {
			video_recorder_stop();
			if (!cab_player_stop()) {
				reply(s,"550 %s",cab_player_error()); command_owner=NULL; return;
			}
			remote_input_restart();
			user_io_remote_poll();
			user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
			WaitTimer(50);
			user_io_8bit_set_status(0, UIO_STATUS_RESET);
		}
		user_io_remote_poll();
		if (ok) reply(s, "200 Control applied epoch=%lu seq=%lu", (unsigned long)remote_input_epoch(), (unsigned long)remote_input_sequence());
		else reply(s, "550 Control failed: %s",cab_player_error()[0] ? cab_player_error() : "input lease expired");
		command_owner = NULL;
		return;
	}
	// All filesystem commands are blocked while a load is queued. The path is
	// absolute, but ARC-relative RBF and ROM names need the menu's directory.
	strcpy(previous, CurrentDirectoryName());
	name = strrchr(load_path, '/');
	*name++ = 0;
	if (ChangeDirectoryName((const unsigned char *)(*load_path ? load_path : "/")) != FR_OK) {
		reply(s, "550 Cannot open core directory");
	} else {
		err = MenuLoadCore(name);
		if (err == ERROR_NONE) reply(s, "200 Core loaded");
		else {
			ChangeDirectoryName((const unsigned char *)previous);
			reply(s, "550 Core load failed (error %u)", err);
		}
	}
	user_io_remote_poll();
	command_owner = NULL;
}

static void handle_command(struct sess *s, char *line)
{
	char *arg;
	char verb[8];
	char path[PATH_MAX_LEN];
	FILINFO fi;
	FRESULT r;
	int i;

	// split verb / argument
	arg = line;
	while (*arg && *arg != ' ') arg++;
	i = (int)(arg - line);
	if (i >= (int)sizeof(verb)) { reply(s, "500 Unknown command"); return; }
	for (int k = 0; k < i; k++) verb[k] = (char)toupper((unsigned char)line[k]);
	verb[i] = 0;
	while (*arg == ' ') arg++;

	s->idle = 0;

	if (!strcmp(verb, "USER")) {
		remote_input_release(control_id(s));
		s->authed = 0;
		s->user_ok = ct_equal(arg, mist_cfg.ftp_user);
		reply(s, "331 Password required");
		return;
	}
	if (!strcmp(verb, "PASS")) {
		if (s->user_ok && ct_equal(arg, mist_cfg.ftp_pass)) {
			s->authed = 1; s->fails = 0;
			reply(s, "230 Logged in");
		} else {
			remote_input_release(control_id(s));
			s->authed = 0; s->user_ok = 0;
			if (++s->fails >= 3) { reply(s, "421 Too many failed logins"); s->ctl_closing = 1; }
			else reply(s, "530 Login incorrect");
		}
		return;
	}
	if (!strcmp(verb, "QUIT")) { reply(s, "221 Goodbye"); s->ctl_closing = 1; return; }
	if (!strcmp(verb, "NOOP")) { reply(s, "200 OK"); return; }
	if (!strcmp(verb, "FEAT")) { reply(s, "211-Features:\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n EPSV\r\n MISTCTRL 1 LOAD STATUS INPUT KEEPALIVE RELEASE RESET CAB CABSTATUS CABSTOP VIDEO VIDSTATUS VIDSTOP\r\n211 End"); return; }
	if (!strcmp(verb, "HELP")) {
		if (!*arg || !strcasecmp(arg, "SITE")) site_help(s);
		else reply(s, "502 No help for that command");
		return;
	}
	if (!strcmp(verb, "SYST")) { reply(s, "215 UNIX Type: L8"); return; }
	if (!s->authed) { reply(s, "530 Please login with USER and PASS"); return; }
	if (command_owner) { reply(s, "450 Control command pending"); return; }
	if (!strcmp(verb, "SITE")) { cmd_site(s, arg); return; }

	if (!strcmp(verb, "PWD") || !strcmp(verb, "XPWD")) { reply(s, "257 \"%s\" is the current directory", s->cwd); return; }
	if (!strcmp(verb, "TYPE")) { reply(s, "200 Type set"); return; }
	if (!strcmp(verb, "MODE") || !strcmp(verb, "STRU")) { reply(s, "200 OK"); return; }
	if (!strcmp(verb, "OPTS")) { reply(s, "501 Option not supported"); return; }
	if (!strcmp(verb, "PASV")) { cmd_pasv(s, 0); return; }
	if (!strcmp(verb, "EPSV")) { cmd_pasv(s, 1); return; }
	if (!strcmp(verb, "PORT")) { cmd_port(s, arg); return; }
	if (!strcmp(verb, "LIST")) { cmd_list(s, arg, OP_LIST); return; }
	if (!strcmp(verb, "NLST")) { cmd_list(s, arg, OP_NLST); return; }
	if (!strcmp(verb, "RETR")) { cmd_retr(s, arg); return; }
	if (!strcmp(verb, "STOR")) { cmd_stor(s, arg, 0); return; }
	if (!strcmp(verb, "APPE")) { cmd_stor(s, arg, 1); return; }
	if (!strcmp(verb, "REST")) {
		s->rest = strtoull(arg, NULL, 10);
		{
			char num[24];
			reply(s, "350 Restarting at %s", u64dec(s->rest, num));
		}
		return;
	}
	if (!strcmp(verb, "ABOR")) {
		if (s->op != OP_NONE) {
			int started = s->started;
			op_cleanup(s);
			dat_close(s);
			if (started) reply(s, "426 Transfer aborted");
		}
		reply(s, "226 Abort successful");
		return;
	}

	// everything below must not run while a transfer is using the shared buffers
	if (s->op != OP_NONE) { reply(s, "425 Transfer in progress"); return; }

	if (!strcmp(verb, "CWD") || !strcmp(verb, "XCWD") || !strcmp(verb, "CDUP") || !strcmp(verb, "XCUP")) {
		if (!strcmp(verb, "CDUP") || !strcmp(verb, "XCUP")) arg = (char *)"..";
		if (resolve(s, arg, path)) { reply(s, "550 Path too long"); return; }
		if (!is_root(path)) {
			r = f_stat(path, &fi);
			if (r != FR_OK || !(fi.fattrib & AM_DIR)) { reply(s, "550 No such directory"); return; }
		}
		strcpy(s->cwd, path);
		reply(s, "250 Directory changed");
		return;
	}
	if (!strcmp(verb, "SIZE") || !strcmp(verb, "MDTM")) {
		if (resolve(s, arg, path) || is_root(path)) { reply(s, "550 Bad path"); return; }
		r = f_stat(path, &fi);
		if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
		if (verb[0] == 'S') {
			char num[24];
			if (fi.fattrib & AM_DIR) { reply(s, "550 Not a regular file"); return; }
			reply(s, "213 %s", u64dec((uint64_t)fi.fsize, num));
		} else {
			reply(s, "213 %04d%02d%02d%02d%02d%02d", 1980 + (fi.fdate >> 9), (fi.fdate >> 5) & 15, fi.fdate & 31,
				fi.ftime >> 11, (fi.ftime >> 5) & 63, (fi.ftime & 31) * 2);
		}
		return;
	}
	if (!strcmp(verb, "MKD") || !strcmp(verb, "XMKD")) {
		if (!writes_allowed(s)) return;
		if (resolve(s, arg, path) || is_root(path)) { reply(s, "550 Bad path"); return; }
		if (cab_player_output_path_in_use(path) || video_output_path_in_use(path)) { reply(s,"550 CAB output directory is in use"); return; }
		r = f_mkdir(path);
		if (r == FR_OK) reply(s, "257 \"%s\" created", path); else reply(s, "550 %s", fr_text(r));
		return;
	}
	if (!strcmp(verb, "RMD") || !strcmp(verb, "XRMD") || !strcmp(verb, "DELE")) {
		if (!writes_allowed(s)) return;
		if (resolve(s, arg, path) || is_root(path)) { reply(s, "550 Bad path"); return; }
		if (cab_player_output_path_in_use(path) || video_output_path_in_use(path)) { reply(s,"550 CAB output directory is in use"); return; }
		r = f_stat(path, &fi);
		if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
		if (verb[0] == 'D') {
			if (fi.fattrib & AM_DIR) { reply(s, "550 Is a directory"); return; }
			if (in_use_by_core(path)) { reply(s, "550 File is in use by the running core"); return; }
		} else if (!(fi.fattrib & AM_DIR)) { reply(s, "550 Not a directory"); return; }
		r = f_unlink(path);
		if (r == FR_OK) reply(s, "250 Deleted"); else reply(s, "550 %s", r == FR_DENIED ? "Directory not empty" : fr_text(r));
		return;
	}
	if (!strcmp(verb, "RNFR")) {
		if (!writes_allowed(s)) return;
		if (resolve(s, arg, s->rnfr) || is_root(s->rnfr)) { reply(s, "550 Bad path"); return; }
		r = f_stat(s->rnfr, &fi);
		if (r != FR_OK) { reply(s, "550 %s", fr_text(r)); return; }
		s->have_rnfr = 1;
		reply(s, "350 Ready for RNTO");
		return;
	}
	if (!strcmp(verb, "RNTO")) {
		if (!writes_allowed(s)) return;
		if (!s->have_rnfr) { reply(s, "503 RNFR required first"); return; }
		s->have_rnfr = 0;
		if (resolve(s, arg, path) || is_root(path)) { reply(s, "550 Bad path"); return; }
		if (in_use_by_core(s->rnfr) || in_use_by_core(path)) { reply(s, "550 File is in use by the running core"); return; }
		r = f_rename(s->rnfr, path);
		if (r == FR_OK) reply(s, "250 Renamed"); else reply(s, "550 %s", fr_text(r));
		return;
	}

	reply(s, "502 Command not implemented");
}

// ------------------------------------------------------------------ control connection

static void ctl_reset_state(struct sess *s)
{
	if (command_owner == s) command_owner = NULL;
	remote_input_release(control_id(s));
	if (s->op != OP_NONE) op_cleanup(s);
	dat_close(s);
	if (s->lst) { tcp_close(s->lst); s->lst = NULL; }
	s->ctl = NULL;
	s->ctl_closing = 0;
	s->authed = s->user_ok = s->fails = 0;
	s->have_rnfr = 0;
	s->rest = 0;
	s->cmdlen = 0;
	s->overflow = 0;
	s->iac = 0;
	strcpy(s->cwd, "/");
}

// returns ERR_ABRT if the pcb had to be aborted (the calling lwIP callback must then return ERR_ABRT)
static err_t ctl_close(struct sess *s, struct tcp_pcb *pcb)
{
	err_t r = ERR_OK;
	pcb_detach(pcb);
	if (tcp_close(pcb) != ERR_OK) { tcp_abort(pcb); r = ERR_ABRT; }
	ctl_reset_state(s);
	return r;
}

static err_t ctl_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return ERR_OK;
	struct pbuf *q;
	(void)err;

	if (!p) return ctl_close(s, pcb);

	for (q = p; q; q = q->next) {
		const uint8_t *d = (const uint8_t *)q->payload;
		u16_t i;
		for (i = 0; i < q->len; i++) {
			uint8_t c = d[i];
			if (s->iac) { s->iac--; continue; }
			if (c == 0xff) { s->iac = 1; continue; }     // telnet IAC + 1 byte (ABOR sends IAC IP IAC SYNCH)
			if (c == '\n') {
				if (s->overflow) { reply(s, "500 Line too long"); }
				else {
					if (s->cmdlen && s->cmd[s->cmdlen - 1] == '\r') s->cmdlen--;
					s->cmd[s->cmdlen] = 0;
					if (s->cmdlen) handle_command(s, s->cmd);
				}
				s->cmdlen = 0;
				s->overflow = 0;
				if (s->ctl_closing) break;
			} else if (s->cmdlen < CMD_MAX - 1) {
				s->cmd[s->cmdlen++] = (char)c;
			} else {
				s->overflow = 1;
			}
		}
		if (s->ctl_closing) break;
	}
	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	if (s->ctl_closing) return ctl_close(s, pcb);
	return ERR_OK;
}

static void ctl_err(void *arg, err_t err)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return;
	(void)err;
	ctl_reset_state(s);      // pcb already freed
}

static err_t ctl_poll(void *arg, struct tcp_pcb *pcb)
{
	struct sess *s = (struct sess *)arg;
	if (!s) return ERR_OK;
	if (s->op != OP_NONE && !s->started) {
		if (++s->op_wait > 30) {          // passive client never connected
			op_cleanup(s);
			dat_close(s);
			reply(s, "425 Data connection timed out");
		}
	} else s->op_wait = 0;
	if (++s->idle > IDLE_SECONDS) {
		reply(s, "421 Idle timeout");
		return ctl_close(s, pcb);
	}
	return ERR_OK;
}

static err_t srv_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	static const char busy[] = "421 Too many sessions, try again later\r\n";
	struct sess *s = NULL, *victim = NULL;
	int i;
	(void)arg;
	if (err != ERR_OK || !newpcb) return ERR_VAL;

	// free slot: no control connection and no data pcb still closing
	for (i = 0; i < FTP_SESSIONS; i++)
		if (!SS[i].ctl && !SS[i].dat && !SS[i].lst) { s = &SS[i]; break; }

	if (!s) {
		// all taken: a client that vanished (cable, Wi-Fi, PC asleep) leaves a session behind until its
		// idle timeout; drop the longest-idle session that is not transferring instead of refusing
		for (i = 0; i < FTP_SESSIONS; i++)
			if (SS[i].ctl && SS[i].op == OP_NONE && (!victim || SS[i].idle > victim->idle)) victim = &SS[i];
		if (victim) {
			struct tcp_pcb *old = victim->ctl;
			reply(victim, "421 Session replaced by a new connection");
			pcb_detach(old);
			if (tcp_close(old) != ERR_OK) tcp_abort(old);
			ctl_reset_state(victim);
			if (!victim->dat && !victim->lst) s = victim;
		}
	}
	if (!s) {
		tcp_write(newpcb, busy, sizeof(busy) - 1, 0);
		tcp_output(newpcb);
		if (tcp_close(newpcb) != ERR_OK) { tcp_abort(newpcb); return ERR_ABRT; }
		return ERR_OK;
	}

	ctl_reset_state(s);
	s->ctl = newpcb;
	s->idle = 0;
	tcp_arg(newpcb, s);
	tcp_recv(newpcb, ctl_recv);
	tcp_err(newpcb, ctl_err);
	tcp_poll(newpcb, ctl_poll, 2);
	reply(s, "220 MiST FTP server ready");
	return ERR_OK;
}

void ftpd_init(void)
{
	struct tcp_pcb *pcb;
	if (srv) return;
	for (int i = 0; i < FTP_SESSIONS; i++) {
		strcpy(SS[i].cwd, "/");
		SS[i].buf = SS[i].xbuf;
		SS[i].bufsz = XFER_SIZE;
	}
	pcb = tcp_new();
	if (!pcb) return;
	if (tcp_bind(pcb, IP_ADDR_ANY, FTP_PORT) != ERR_OK) { tcp_abort(pcb); return; }
	pcb = tcp_listen(pcb);
	if (!pcb) return;
	tcp_accept(pcb, srv_accept);
	srv = pcb;
}

const char *ftpd_state(void)
{
	int i, online = 0, xfer = 0;
	if (!srv) return "not running";
	for (i = 0; i < FTP_SESSIONS; i++) {
		if (SS[i].ctl) online++;
		if (SS[i].ctl && SS[i].op != OP_NONE) xfer++;
	}
	if (xfer) return "transferring";
	if (online) return "client online";
	return "listening";
}
