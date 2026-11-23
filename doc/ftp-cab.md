# Frame-clocked CAB inputs on SiDi128

Run an existing JTFRAME `.cab` file against the currently loaded game:

```sh
cd /path/to/jtcores
tools/ftp-control.py 192.0.2.1 \
  cab cores/trojan/ver/trojan/reg.cab --frames 2000 --report /tmp/trojan-cab.json
```

Use an arbitrary CAB path to test a different scenario. Load the matching
ARC/RBF/ROM first using [ftp-load.sh](ftp-core-loading.md). Credentials come
from netrc as for immediate controls. The script controls both players using
CAB logical input names; `--port` applies only to immediate controls.

Requirements: Ethernet-enabled SAMV71 firmware with CAB support, a newly built
JTFRAME SiDi128 RBF advertising `FRAME_SYNC=1`, Python 3 and a C++17 compiler on
the PC. The client locates `jtframe-cab-pack` from the current JTCORES checkout,
PATH or `$JTFRAME`; `--cab-packer PATH` overrides discovery. The Bash helper
caches its native exporter and rebuilds it when its sources change.

## Script semantics

The exporter shares `CabinetScript` with jtsim. Comments, case-insensitive
names, relative durations, default one-frame actions, absolute events and
`loop`/`repeat` retain their simulation meanings. For example:

```text
180
3 coin
4
1 1P
```

This waits 180 frames, presses coin for three, releases for four, then presses
player-one start for one. `=5 coin` asserts coin at frame 5 after idle frames
0 through 4. `repeat n` runs its loop body n times in total.

The current CAB schema supports coin, service, start, directions, three action
buttons per player, test and reset. Immediate controls separately support six
action buttons per player. Extending the shared CAB schema to six actions is
future work; unsupported names fail during parsing.

`--frames N` sets a total raster-frame budget. It pads the end with idle inputs
and refuses to truncate a longer script. Without it the complete script length
is used. Limits are 1 through 2,147,483,647 frames. `reg.yaml` profile resolution
is not implemented by this client; supply its chosen script and budget.

`dipsw=<hex-value>` sets the full 32-bit game DIP input starting at that CAB
frame. The `0x` prefix is optional. It persists across later lines without a
DIP statement and through idle padding until another statement changes it.
Zero is an explicit value. For example:

```text
dipsw=fffc
180
3 coin
=300 dipsw=fffd
```

The first DIP value is preloaded with frame-zero inputs when reset is released;
the second takes effect at frame 300. Inputs and DIP changes share the FPGA
queue and frame boundary. This sets the core's `dipsw` input, independently of
the ARC's packed status positions. It preserves other settings, module/cheat
bits and the stored OSD/ARC configuration. No extra reset is issued for a DIP
change: a game that samples a switch only on reset still needs the appropriate
reset sequence. A temporary DIP override ends on completion, cancellation or
failure, restoring the configured DIP value. It does not rewrite settings on SD.

DIP scripts require `CAB_DIPS=1` from both updated firmware and FPGA core;
ordinary input-only CAB scripts remain compatible with older CAB-capable RBFs.
`CABSTATUS` includes `DIP_VALUE` (hex) and `DIP_VALID` (override currently active).
After completion, the last scripted value remains in diagnostic readback while
`DIP_VALID=0` indicates that the configured value is effective again.

`dump` invokes the core's existing OSD **Save NVRAM** readback at its CAB frame.
This requires the scheduled-dump capability and a valid `R<length>,...` config
item. Unsupported cores are rejected before upload/reset. The bytes/layout are
whatever that core exposes, not a full machine save state. For example, Mikie
exposes 2,048 bytes of VRAM.

```text
180
3 coin
30
1 1p
=400 dump
100
dump
```

`--dump-dir DIRECTORY` selects the local output base. The default is
`REPORT.dumps`, or `CABNAME.dumps` in the current directory without a report.
Each run gets a new subdirectory. The PC downloads completed dumps after
playback, verifies lengths and MCU CRC-32, then records local paths and SHA-256
hashes in the report. Existing local run directories/files are never overwritten.

A dump requests pause and holds the CAB cursor, inputs and DIP at a checkpoint
barrier. After two raster intervals the MCU reads incrementally through the same
index -1 IOCTL path as Save NVRAM. It closes the SPI upload and SD files before
acknowledging the checkpoint. Playback resumes at a raster boundary and executes
the complete pending CAB frame. `2 dump` produces separate dumps on both CAB
frames. Absolute statements, loops and final-frame dumps retain their semantics.
The eight-entry queue can be refilled during a barrier.

**Pause effectiveness remains unverified.** Boot code with masked interrupts
and raster-sensitive games can keep changing state while CAB is held. These
are best-effort debug checkpoints, not proof of coherent single-frame state.
The report declares a timeline with dump barriers; held CAB frames must not be
treated as continuous CPU/raster progress. Sidecars record epoch, sequence,
requested CAB frame, request/start/end raster counts, byte count, CRC and
`pause_effective="unverified"`. Accepted-IRQ/CPU-quiescence feedback remains
future work for strict regression synchronization.

SD output uses a fresh `/CABDxxxxxxxx/` directory containing `Dxxxxxxxx.RAM` and
`Dxxxxxxxx.JSON`, leaving the game's root `.RAM` untouched. Data/metadata use
temporary names until checked write/close; completed count advances only after
both files exist. The active output directory is protected from FTP mutations,
and open output files cannot be downloaded. Cancellation removes temporary
files and retains completed dumps for recovery. Failure reports retain the SD
directory and completed count. Dump I/O failures release pause and inputs.
There is a 30-second per-dump transfer limit, plus raster/ownership checks.

`cheat=` remains rejected before upload/reset. `tracing_on` remains a diagnostic
marker without hardware waveforms; neither feature is part of this work.

## Timeline and buffering

The MCU validates the complete packed file before claiming inputs or asserting
game reset. It then holds reset for at least 50 ms while arming and prefilling
the FPGA queue. START preloads frame-zero inputs and releases reset. Native
game `LVBL` rising edges outside dump barriers advance durations; after N CAB frames the run finishes at
frame N and releases inputs. It does not count HDMI/scaler output frames.

Only eight duration/state/DIP records are stored in FPGA: **96 bytes of queue
storage**, with registers for control and status. The script remains on SD.
The MCU polls about every 2 ms, reads free credits and refills ahead. Sequence
and epoch checks prevent stale or reordered events. Underrun, overflow, wrong
totals and invalid commands fail playback instead of moving inputs to a later
frame. No full script or framebuffer is allocated in FPGA BRAM.

CAB inputs replace physical game inputs after mapping/rotation; beta input
locking still applies. Local OSD remains available and opening it aborts the
remote lease. Save settings/NVRAM and load the correct ARC for repeatable tests.
A reset pulse alone does not guarantee identical RAM or power-on state.

Between dump barriers the timebase is **unpaused native raster**, including boot when games ignore
vertical-blank interrupts. It does not establish interrupt acceptance or CPU
quiescence. Effective-pause detection, frame stepping and screenshots are not
implemented; dump barriers are the best-effort debug mode described above.
A CAB `reset` hold can stop the
video timer in some cores; if raster progress stops, the MCU aborts after about
two seconds and releases reset/inputs rather than inventing frame boundaries.

## Control, ownership and failure handling

| Command | Result |
| --- | --- |
| `SITE CAB <absolute-or-session-relative-path>` | Validate a packed file, arm/prefill/reset/start the FPGA, then reply `200`. Completion is separate. |
| `SITE CABSTATUS` | Coherent cached FPGA state/progress; owner polls renew the input lease. Observer polls do not. |
| `SITE CABSTOP` | Stop this session's run, verify FPGA IDLE and neutral inputs, close the file and release ownership. |
| `SITE RELEASE` | Stop CAB and release inputs; also used by client error/exit cleanup. |

States: 0 IDLE, 1 ARMED, 2 RUNNING, 3 DONE, 4 ERROR, 5 DUMP_WAIT, 6 RESUME_WAIT.
Status includes FPGA epoch,
completed frame count, total frames, accepted/applied events, independent raster
count, queue occupancy/credits, current logical inputs and errors. Dump status
includes sequence/frame/request raster, total/completed counts, bytes written
and SD directory; SITE STATUS exposes `CAB_DUMP` and `DUMP_BYTES`. It is sampled
by the MCU main loop, not an acknowledgement that a CPU serviced an interrupt.

CAB requires writable filesystem ownership, a closed OSD and an 8-bit protocol
core with the queue capability. The active packed file is protected from FTP
overwrite/delete. Another session cannot change inputs, stop, reset or load a
core while ownership is held. Manual INPUT during CAB is refused even for its
owner. The owner may reset or load another core, which first stops playback.

The client polls every 100 ms. Closing its connection, Ctrl-C, opening the OSD,
loading another core or letting its five-second input lease expire cancels the
run. If PC processing/network activity stops while the MCU keeps running, the
lease bounds playback continuation. FPGA raster stalls have a separate MCU
watchdog; the PC also rejects stalled progress, epoch changes and inconsistent
completion counters. There is no automatic resume of uncertain inputs.

The JSON report records source snapshot and packed-file hashes, core/firmware,
frame/event counts, wall time, declared timebase and final status. The client
verifies uploaded packed bytes by SHA-256 readback before starting. Packed
files use content-derived names on SD and are retained for reuse/inspection;
they are not automatically deleted. Reports are not screenshot comparisons or
proof of visible gameplay.

Use `cabstatus` to observe from a second client. A separate client cannot issue
`cabstop` for an active owner's run; interrupt its controlling process instead.
Interactive mode accepts `cab FILE.cab`, `cabstatus` and `cabstop` as well.

Input-only scripts retain version-1 eight-byte packed records. Scripts with
DIP statements use version-2 twelve-byte records. Dump scripts use version 3,
also twelve-byte records, with flag bit 22 and a declared dump count. All retain CRC validation
and SHA-256 upload verification; JSON reports include `packed_version`.

The versioned file/SPI specification is in JTCORES
`modules/jtframe/doc/cab-stream.md`. Ordinary SiDi128 builds enable this feature
even without debug flags; the firmware rejects older RBFs rather than falling
back to wall-clock input timing.
