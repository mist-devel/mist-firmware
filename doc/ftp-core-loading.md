# Loading cores over FTP

On Ethernet-enabled firmware, an authenticated FTP client can upload files to
the SD card and select an `.ARC` or `.RBF` with:

```
SITE LOAD /GNG.ARC
```

This follows the OSD core-selection path, including ARC configuration, FPGA
configuration, ROM loading and input initialization. Use an ARC when the game
needs its own ROM name, DIP defaults or other ARC settings. Upload all companion
files before issuing the command.

## Setup

Enable FTP in `MIST.INI`, choose a username and password, and allow writes:

```ini
[MIST]
FTP_ENABLE=1
FTP_USER=mist
FTP_PASS=choose-a-password
FTP_READONLY=0
```

Restart the firmware after changing these settings. Find the DHCP address in
the network menu; a DHCP reservation can keep it stable. Store the credentials
on the PC in `~/.netrc` (permissions `0600`):

```
machine 192.0.2.1 login mist password choose-a-password
```

The Bash/curl helper uploads the specified files to the SD root and loads the
first file:

```sh
tools/ftp-load.sh 192.0.2.1 GNG.ARC GNG.ROM JTGNG.RBF
```

Local paths are resolved from the caller's working directory, so the helper
can also be run from a project checkout:

```sh
/path/to/mist-firmware/tools/ftp-load.sh 192.0.2.1 \
  rom/trojan.arc release/sidi128/jttrojan.rbf rom/trojan.rom
```

Generate the ARC from the same game metadata used for the ROM. Trojan needs its
ARC settings for correct display; uploading just its RBF and ROM is insufficient.

For direct RBF loading, one supplied `.ROM` is uploaded as `<rbf-name>.ROM`
(`jttrojan.ROM` here), which the MCU's normal loader expects when the FPGA's
internal core name matches its RBF basename. The local ROM is unchanged. Use
an ARC for a different internal core name, multiple ROMs, or game-specific
settings. In ARC mode, uploaded filenames are preserved. Paths with spaces
are supported when quoted. `--help` shows usage.

Set `FTP_NETRC` to use a different credentials file. Filenames inside the ARC
must match the uploaded files. The helper uploads to the root; FTP clients can
also upload and launch files in subdirectories. Runtime selection takes effect
immediately; the normal startup configuration controls the next power-on.

## Command behavior

`HELP SITE` (or `SITE HELP`) describes the extension. `SITE LOAD` accepts paths relative to the
FTP session's current directory or absolute paths from the SD root. ARC-relative
RBF and ROM names resolve as they do when selected in the menu, including the
existing root-directory fallback for the RBF.

The server queues the request and performs the load in the main loop, outside
lwIP callbacks. It replies `200 Core loaded` after the firmware loader returns
success, or `550` for a missing/unsupported file, inaccessible directory or
loader error. A successful reply reports completion of the normal loader; it
does not validate game execution or the displayed image. Allow time for FPGA
configuration and ROM loading in the client's command timeout.

Loading requires authentication and the same writable SD access as uploads.
It is refused when FTP is read-only, the SD is exported over USB, the running
core uses raw SD access, or any FTP session has a transfer in progress (`450`).
Commands accessing the filesystem are refused while a load is pending.
Disconnecting or logging out before the queued load starts cancels it.
As with OSD selection, a hardware/bitstream failure after reconfiguration starts
may require selecting a valid core again or resetting the board.

See [remote controls](ftp-control.md) for status, game reset and immediate inputs,
including the input lease that prevents another session loading a core during
remote ownership.
