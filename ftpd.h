#ifndef FTPD_H
#define FTPD_H

// Minimal FTP server for the SD card on top of the lwIP raw TCP API.
// - up to three control sessions
// - passive mode (PASV/EPSV) and PORT restricted to the client's own address
// - file transfers use absolute FatFs paths (the firmware shares the volume's cwd)
// - never touches the files currently mounted as disk images by the core

void ftpd_init(void);
// Apply queued loads/controls and lease cleanup in the main loop, outside callbacks.
void ftpd_poll(void);
const char *ftpd_state(void);   // "listening" / "client online" / "transferring"

#endif
