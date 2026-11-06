[![Build firmware](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml)

MIST Firmware source code
=========================

This is the source code of the MIST firmware. Binaries can be found
in the [mist-binaries repository](https://github.com/mist-devel/mist-binaries/tree/master/firmware).

For instructions how to compile this see the [wiki](https://github.com/mist-devel/mist-board/wiki/HowToCompileTheFirmware).

Ethernet-enabled firmware supports [uploading and loading cores from a PC](doc/ftp-core-loading.md).
It also supports [remote digital inputs, status and game reset](doc/ftp-control.md).
JTFRAME SiDi128 cores with the frame-input queue support [CAB script playback](doc/ftp-cab.md).
