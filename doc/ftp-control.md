# Remote game controls

Ethernet firmware extends the authenticated FTP control connection with
immediate input, status and game-reset commands. This uses the core's existing
8-bit MiST user-I/O protocol; an FPGA rebuild is unnecessary for these controls.

Load the game's ARC first so ROM selection and DIP/display settings are correct.
Configure netrc credentials as described in [core loading](ftp-core-loading.md).
The Python 3 standard-library client keeps a single connection open:

```sh
tools/ftp-control.py 192.0.2.1 --port 1
```

For JTFRAME on MiST/SiDi128, port **1 is player one**, and port 0 is player two;
these are raw protocol port numbers. Other cores may use different assignments.
The generic client defaults to port 0. Example interactive commands:

```text
status
reset
wait 3000
pulse coin 100
wait 300
pulse start 100
press right
press b1
release right b1
quit
```

`pulse` durations and `wait` are in **milliseconds**, without frame guarantees.
A pulse holds for at least the requested wait plus command/network overhead.
`press` holds inputs until a matching `release`, an all-input `release`, or the
session ends. Other held inputs are preserved during a pulse. The client detects
JTFRAME ARCs by their `Core credits` label: six action slots occupy bits 4..9,
Start is bit 10, Coin bit 11, and Core credits bit 12. These positions stay fixed
for games with fewer action buttons. The ARC supplies action names, not the
reserved controls' wire positions. This supports six-action games such as CPS1
as well as two-action games such as Trojan. Directions and `b1`..`b6` also work.
`--layout jtframe` explicitly selects this layout without ARC labels;
`--layout arc` selects consecutive ARC-label positions for other cores and
permits raw aliases `b1`..`b16`. `port N` selects another port; `joy PORT HEXMASK` sets its
entire 20-bit digital state. `help` lists commands.

Status, reset and a pulse can also be executed directly:

```sh
tools/ftp-control.py 192.0.2.1 status
tools/ftp-control.py 192.0.2.1 --port 1 pulse coin 100
```

Use an interactive session for held states: a one-command client releases
inputs as it exits. `--netrc` or `FTP_NETRC` selects another credentials file.
Piped commands use the same persistent session and fail on a command error.

## FTP extension (MISTCTRL version 1)

`FEAT` advertises `MISTCTRL 1 LOAD STATUS INPUT KEEPALIVE RELEASE RESET CAB CABSTATUS CABSTOP`.
`HELP SITE` and the compatibility spelling `SITE HELP` describe the commands.

| Command | Behavior |
| --- | --- |
| `SITE STATUS` | Multiline `211` response with firmware, selected core/file, ARC button labels, type, epoch, sequence, owner, lease time and effective joystick state. |
| `SITE INPUT <port> <hexmask> [epoch]` | Set port 0..5 to a full digital state, mask 00000..fffff. The first command claims all digital joystick ports. |
| `SITE KEEPALIVE` | Renew this session's input lease without changing held inputs. |
| `SITE RELEASE` | Release this session's injected states and restore cached physical inputs. |
| `SITE RESET` | Pulse the 8-bit game-reset status bit for 50 ms, preserving other status/DIP bits; release remote input and begin a new epoch. |
| `SITE LOAD <path>` | Existing complete ARC/RBF core loader; a successful load revokes remote input and begins a new epoch. |

INPUT, KEEPALIVE and RESET require writable FTP permission, an 8-bit-protocol
core and a closed OSD. They do not access the SD filesystem, so raw-SD/USB
storage ownership does not prevent them. A different session cannot modify
owned inputs, reset or load a core (`450`). Read-only status remains available.
RELEASE always permits the owner to give up its input state.

The lease expires **5,000 ms** after INPUT or KEEPALIVE. The client renews once
per second while it owns inputs. QUIT, connection closure/replacement, USER,
failed authentication, OSD opening and core changes also revoke ownership.
After an unnoticed broken connection, the lease bounds how long inputs stay
held while the MCU main loop is running. The normal loader can block that loop
while reconfiguring; this is not a hardware watchdog.

Ownership replaces the six digital joystick ports; physical USB/DB9 input
changes are cached and restored on release. Analog axes are neutral during
remote ownership. Physical keyboard input and local OSD controls retain their
existing behavior. Core/OSD changes release inputs, and game reset preserves the
latest physical cache. The PC client discards its held state after an epoch or
ownership change and never revives an expired pulse.

Commands are queued for the main loop, outside lwIP callbacks. `200` acknowledges
the state update and SPI send; it is not an independent FPGA readback. STATUS
likewise reports the MCU's effective output state. An optional INPUT epoch is
checked immediately before application to reject stale commands following a
reset/load; the client always supplies it. Epoch and sequence are wrapping
32-bit counters, scoped to the running MCU firmware. Reconnect and obtain fresh
status after an MCU restart. Sequence counts accepted input changes/releases, **not frames**.
The server reports `TIMING=immediate` outside CAB playback. `FRAME_SYNC=1`
indicates that the loaded FPGA advertises the frame-input queue; old cores
report zero. Immediate INPUT/pulse timing remains approximate in both cases.

## Frame scheduling and CAB dumps

[CAB playback](ftp-cab.md) now uses an eight-entry FPGA queue refilled by the
MCU. Runs hold an input state for a duration in native raster frames. Epoch,
sequence, credits and coherent FPGA counters detect invalid events or an
underrun. This requires an updated SiDi128 RBF as well as firmware; immediate
controls continue to work with older cores. `TIMING=raster-cab` denotes active
playback. CABSTATUS owner polls renew the five-second input lease.

The initial CAB timeline runs unpaused, including during boot with masked CPU
interrupts. Accepted-interrupt feedback, effective-pause detection, scheduled
capture and freezing CAB time during a confirmed capture pause remain future
work. Native raster count is not a claim about CPU progress or exact gameplay
state.

CAB `dump` now uses the existing OSD **Save NVRAM** readback on supporting cores,
with the length specified by the `R` configuration item and index -1 IOCTL data.
[CAB playback](ftp-cab.md) adds scheduled best-effort pause barriers, checked
files in a separate run directory and verified PC retrieval. Scene use depends
on which memories the core exposes. CPU quiescence is not acknowledged.

## Host validation

```sh
make -f Makefile.remote-test
python3 -m unittest discover -s tests -p '*test.py'
```

The C tests exercise ownership, physical state restoration, leases including
timer wrap, epoch changes and packed CAB validation. Client tests also cover
CAB capability/preflight checks, upload verification, completion counters,
epoch changes, cancellation and report identity.
