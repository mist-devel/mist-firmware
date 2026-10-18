#ifndef FTPD_H
#define FTPD_H

// Minimal FTP server for the SD card on top of the lwIP raw TCP API.
// - one control session at a time
// - passive mode (PASV/EPSV) and PORT restricted to the client's own address
// - always uses absolute FatFs paths, never f_chdir() (the firmware shares the volume's cwd)
// - never touches the files currently mounted as disk images by the core

void ftpd_init(void);
const char *ftpd_state(void);   // "listening" / "client online" / "transferring"

#endif
