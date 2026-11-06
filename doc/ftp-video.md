# Native RGB444/RGB555 video recording

Build matching SAMV71 firmware and FPGA images with `JTFRAME_REMOTE`.
The SiDi128 target enables both remote interfaces in normal and release
builds. Load the matching ARC/RBF/ROM pair before recording. The FPGA-side
command and row format are documented in `modules/jtframe/doc/video-capture.md`.

```sh
tools/ftp-control.py 192.0.2.1 video 10 100 --video-step 16 --video-dir captures
tools/ftp-control.py 192.0.2.1 cab scenario.cab --frames 2000 \
  --capture-every 100 --report run.json --video-dir captures
```

`SITE VIDEO COUNT PERIOD [STEP]` starts a recording. Standalone checkpoints
start at the current INFO frame plus PERIOD and continue at that cadence. For
CAB playback, the first target is the next absolute PERIOD multiple after INFO;
the requested sequence must fit within the CAB's declared raster-frame count.
`SITE VIDSTATUS` reports MCU-local progress (rows, dimensions,
requested/actual frame, errors, and output directory); it does not query FPGA
capture status. `SITE VIDSTOP` cancels recording. The interactive client also
offers `videostart`, `vidstatus`, `videowait`, and `vidstop`.

Rows use a deterministic residue-major order. The default step is 16; the
client accepts steps from 1 through 512. It polls READ through its normal
service loop and the FPGA holds rows until the last payload bit arrives.
Backpressure waits for a free bank instead of overwriting data.

Files are stored below a unique `/video/<rom-basename>/run_.../` directory.
Each `.raw` image starts with tagged-width16 and height16 in little endian.
Width bit 15 marks RGB555; the remaining bits give width. RGB444 pixels stay
packed as before. RGB555 pixels are little-endian words `{0,R5,G5,B5}`.
`index.jsonl` records requested and actual checkpoints and references the prior
image when exact row comparison finds a duplicate. A full duplicate creates
and writes no image file. If a row differs, the firmware replays the already
verified equal prefix from the prior image using two owned row buffers, then
writes each row of the new image once. `ROMPATH.TXT` records the loaded ROM
path. The PC client validates geometry, format consistency, file length and
padding/reserved bits, then writes `frame_*.png` files. There are no map files,
per-row headers, hashes, CRCs, or transport retry protocol.

Capture pause assumes the game finishes its current update and waits after its
frame interrupt is suppressed. The recorder does not verify CPU quiescence; a
game that continues drawing or depends on raster timing may produce mixed or
different images. INFO records the actual checkpoint when the first row is
received. Widths/heights above 512 are rejected. RGB555 is selected at compile
time for cores with COLORW>=5; lower COLORW cores retain RGB444 conversion.
The MCU owns two 1,024-byte row buffers; the 8 KiB FatFs sector buffer is only
SPI scratch and is never retained across filesystem service calls.

Owner lease loss, OSD interruption, game reset, disconnect, ABORT, and SD write
errors cancel the capture, release capture-owned pause, close files, and remove
an unpublished temporary image. CAB dump barriers cannot overlap video capture.

## Save snapshot from the System menu

On a core that advertises native video capture, open the OSD's **System** page
and select **Save snapshot**. The menu closes before capture starts. The
firmware saves one native frame as an uncompressed, top-down 24-bit BMP under
`/video/<rom-basename>/snapshot_<time>_<sequence>.bmp`; existing files are
never overwritten. The success dialog shows the completed path. No FTP client
or network connection is needed.

Snapshots retain the core's RGB444 or compile-time RGB555 input precision and
expand those channels to eight bits in BGR order for BMP. SF2's COLORW8 inputs
therefore use the same most-significant-five-bit RGB555 capture as the paired
developer recorder, not lossless RGB888. Rows are written directly from the
two bounded capture buffers. Reopening the OSD, resetting/loading a core, or
another control operation cancels the capture and removes the incomplete BMP.
