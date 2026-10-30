#!/usr/bin/env python3
"""Immediate controls and FPGA-frame-clocked CAB playback over authenticated FTP."""
import argparse
import ftplib
import netrc
import os
import select
import shlex
import sys
import time
import hashlib
import json
from pathlib import Path
import shutil
import secrets
import struct
import subprocess
import tempfile
import re
import zlib


class CABProgressWatchdog:
    """Track CAB frames and native video rows with bounded no-progress windows."""
    FRAME_TIMEOUT = 3
    CAPTURE_TIMEOUT = 30

    def __init__(self, now):
        self.last_progress = now
        self.video_marker = None

    def stalled(self, now, frame_advanced, video_status=None):
        if frame_advanced:
            self.last_progress = now
        if video_status is not None and video_status.get('ACTIVE') == '1':
            marker = tuple(video_status.get(key) for key in
                           ('DONE', 'ROWS', 'REQUESTED', 'ACTUAL'))
            if marker != self.video_marker:
                self.video_marker = marker
                self.last_progress = now
            timeout = self.CAPTURE_TIMEOUT
        else:
            if self.video_marker is not None:
                self.video_marker = None
                self.last_progress = now
            timeout = self.FRAME_TIMEOUT
        return now - self.last_progress > timeout


class Control:
    def __init__(self, ftp, port=0, layout='auto', cab_packer=None, frames=None, report=None, dump_dir=None, capture_every=None, video_dir=None, video_step=16, cab_frame_budget=None, source_cab=None, first_frame=None):
        self.ftp, self.port = ftp, port
        self.requested_layout = layout
        self.cab_packer, self.cab_frames, self.report = cab_packer, frames, report
        self.cab_frame_budget = cab_frame_budget
        self.source_cab = source_cab
        self.first_frame = first_frame
        self.dump_dir = dump_dir
        self.capture_every, self.video_dir, self.video_step = capture_every, video_dir, video_step
        self.remote_cab_dir = None
        self.remote_cab_path = None
        self.video_remote_dir = None
        self.video_local_dir = None
        self.video_retrieved = False
        self.masks = [0] * 6
        self.epoch = None
        self.leased = False
        self.last_heartbeat = 0
        self.refresh()

    def video_status(self):
        response = self.ftp.sendcmd('SITE VIDSTATUS')
        status = dict(line.strip().split('=', 1) for line in response.splitlines() if '=' in line)
        if status.get('DIR'):
            self.video_remote_dir = status['DIR']
        return status

    def start_video(self, count, period, step=None):
        self.refresh()
        if self.status.get('VIDEO_CAPTURE') != '1':
            raise ValueError('Selected core/firmware lacks native video capture')
        if not 1 <= count <= 10000 or not 1 <= period <= 10000000:
            raise ValueError('Invalid video count or frame cadence')
        step = self.video_step if step is None else step
        if not 1 <= step <= 512:
            raise ValueError('Video row step must be 1..512')
        print(self.ftp.sendcmd(f'SITE VIDEO {count} {period} {step}'))
        self.leased = True

    def wait_video(self):
        started = time.monotonic()
        try:
            while True:
                status = self.video_status()
                if status.get('ACTIVE') == '0':
                    if status.get('ERROR') or status.get('DONE') != status.get('TOTAL'):
                        raise ValueError(f"Video capture failed: {status.get('ERROR') or status}")
                    self.video_remote_dir = status['DIR']
                    result = self.fetch_video(status['DIR'])
                    self.video_local_dir = str(result)
                    self.video_retrieved = True
                    print(f"Video complete: {status['DONE']} captures in {time.monotonic()-started:.3f}s; {result}")
                    self.refresh()
                    return result
                time.sleep(0.1)
        except BaseException:
            try:
                self.ftp.sendcmd('SITE VIDSTOP')
            except (OSError, EOFError, ftplib.Error):
                pass
            raise

    def fetch_video(self, remote):
        if not re.fullmatch(r'/video/[a-z0-9_-]+/run_[a-f0-9_]+', remote):
            raise ValueError('Invalid recording directory')
        base = Path(self.video_dir or ((self.report + '.video') if self.report else 'video'))
        destination = base / Path(remote).name
        destination.mkdir(parents=True, exist_ok=False)
        def retrieve(name):
            if not re.fullmatch(r'(frame_[0-9]+\.raw|index\.jsonl|ROMPATH\.TXT)', name):
                raise ValueError('Unsafe recording file name')
            path = destination / name
            with path.open('xb') as data:
                self.ftp.retrbinary('RETR ' + remote + '/' + name, data.write)
            return path
        index = retrieve('index.jsonl')
        records = [json.loads(line) for line in index.read_text().splitlines()]
        if not records or records[0].get('type') != 'run' or records[-1].get('type') != 'complete':
            raise ValueError('Incomplete recording index')
        captures = [r for r in records if r.get('type') == 'capture']
        if len(captures) != records[0].get('count') or len(captures) != records[-1].get('captures'):
            raise ValueError('Recording index capture count mismatch')
        run = records[0]
        if (not isinstance(run.get('width'), int) or not 1 <= run['width'] <= 512 or
                not isinstance(run.get('height'), int) or not 1 <= run['height'] <= 512):
            raise ValueError('Invalid recording geometry')
        retrieve('ROMPATH.TXT')
        images = {}
        image_format = None
        for capture in captures:
            name = capture['image']
            if name not in images:
                raw = retrieve(name)
                images[name] = decode_video_image(raw.read_bytes())
                decoded = images[name]
                if (decoded['width'], decoded['height']) != (run['width'], run['height']):
                    raise ValueError('Image geometry differs from recording INFO')
                if image_format is None:
                    image_format = decoded['rgb555']
                elif decoded['rgb555'] != image_format:
                    raise ValueError('Pixel format changed during recording')
                raw.with_suffix('.png').write_bytes(images[name]['png'])
            if not isinstance(capture.get('requested'), int) or not isinstance(capture.get('actual'), int):
                raise ValueError('Invalid requested/actual checkpoint in video index')
        return destination

    def refresh(self):
        response = self.ftp.sendcmd('SITE STATUS')
        self.status = dict(line.strip().split('=', 1) for line in response.splitlines()
                           if '=' in line)
        if self.status.get('PROTOCOL') != '1':
            raise ValueError('Firmware does not support MISTCTRL protocol 1')
        epoch = int(self.status['EPOCH'])
        if epoch != self.epoch or self.status['OWNER'] != 'self':
            self.masks = [0] * 6
            self.leased = False
        self.epoch = epoch
        self.buttons = self.status.get('BUTTONS', '').split(',')
        # JTFRAME's MiST top fixes BUTTONS=6 independently of the game's ARC.
        # Its ARC generator appends the distinctive "Core credits" label.
        labels = [label.strip().casefold() for label in self.buttons]
        self.layout = ('jtframe' if 'core credits' in labels else 'arc') \
            if self.requested_layout == 'auto' else self.requested_layout
        return response

    def set_mask(self, port, mask):
        if not 0 <= port < 6 or not 0 <= mask <= 0xfffff:
            raise ValueError('Port must be 0..5 and mask must be 00000..fffff')
        self.ftp.sendcmd(f'SITE INPUT {port} {mask:05x} {self.epoch}')
        self.masks[port] = mask
        self.leased = True
        self.last_heartbeat = time.monotonic()

    def button_mask(self, names):
        mask = 0
        directions = dict(right=1, left=2, down=4, up=8)
        aliases = {'1p': 'start'}
        labels = [label.strip().casefold() for label in self.buttons]
        reserved = {'start': 10, 'coin': 11, 'core credits': 12}
        actions = [label for label in labels if label and label not in reserved]
        limit = 6 if self.layout == 'jtframe' else 16
        for name in names:
            name = aliases.get(name.casefold(), name.casefold())
            if name in directions:
                mask |= directions[name]
            elif self.layout == 'jtframe' and name in reserved:
                mask |= 1 << reserved[name]
            elif name.startswith('b') and name[1:].isdigit() and 1 <= int(name[1:]) <= limit:
                mask |= 1 << (3 + int(name[1:]))
            elif self.layout == 'jtframe' and name in actions and actions.index(name) < 6:
                mask |= 1 << (4 + actions.index(name))
            elif self.layout == 'arc' and name in labels and labels.index(name) < 16:
                mask |= 1 << (4 + labels.index(name))
            else:
                raise ValueError(f'Unknown input {name!r}; use b1..b{limit}, directions, '
                                 'or a button label from the selected ARC')
        return mask

    def heartbeat(self):
        if self.leased and time.monotonic() - self.last_heartbeat >= 1:
            self.ftp.sendcmd('SITE KEEPALIVE')
            self.last_heartbeat = time.monotonic()

    def wait(self, milliseconds):
        if not 0 <= milliseconds <= 3600000:
            raise ValueError('Duration must be between 0 and 3600000 milliseconds')
        end = time.monotonic() + milliseconds / 1000
        while time.monotonic() < end:
            self.heartbeat()
            time.sleep(min(0.25, max(0, end - time.monotonic())))

    def release(self):
        self.ftp.sendcmd('SITE RELEASE')
        self.masks = [0] * 6
        self.leased = False

    def cab_status(self):
        response = self.ftp.sendcmd('SITE CABSTATUS')
        return dict(line.strip().split('=', 1) for line in response.splitlines() if '=' in line)

    def fetch_dumps(self, status, epoch, count, source, frames):
        remote = status.get('DUMP_DIR', '')
        if not re.fullmatch(r'/CABD[0-9a-fA-F]{8}', remote):
            raise ValueError('Invalid CAB dump directory')
        base = Path(self.dump_dir) if self.dump_dir else (
            Path(self.report).with_suffix('.dumps') if self.report else Path.cwd() / (source.stem + '.dumps'))
        destination = base / remote[1:]
        destination.mkdir(parents=True, exist_ok=False)
        results, previous_frame = [], -1
        for sequence in range(1, count + 1):
            name = f'D{sequence:08x}'
            chunks = bytearray()
            def metadata_chunk(chunk):
                chunks.extend(chunk)
                if len(chunks) > 2048:
                    raise ValueError('Oversized dump metadata')
            self.ftp.retrbinary(f'RETR {remote}/{name}.JSON', metadata_chunk)
            metadata = json.loads(chunks)
            if not isinstance(metadata, dict):
                raise ValueError('Invalid dump metadata object')
            frame, size = metadata.get('frame'), metadata.get('bytes')
            if (metadata.get('version') != 1 or metadata.get('epoch') != epoch
                    or metadata.get('sequence') != sequence or not isinstance(frame, int)
                    or not previous_frame < frame < frames or size != int(status['DUMP_BYTES'])
                    or not isinstance(size, int) or not 0 < size <= 0x1000000
                    or not re.fullmatch(r'[0-9a-fA-F]{8}', str(metadata.get('crc32', '')))):
                raise ValueError('Dump metadata does not match CAB run')
            previous_frame = frame
            temporary = destination / (name + '.part')
            output = destination / (name + '.RAM')
            digest, crc, received = hashlib.sha256(), 0, 0
            try:
                with temporary.open('xb') as data:
                    def chunk_received(chunk):
                        nonlocal crc, received
                        received += len(chunk)
                        if received > size:
                            raise ValueError('Dump exceeds declared length')
                        data.write(chunk); digest.update(chunk); crc = zlib.crc32(chunk, crc)
                    self.ftp.retrbinary(f'RETR {remote}/{name}.RAM', chunk_received)
                if received != size or crc != int(metadata['crc32'], 16):
                    raise ValueError('Dump download length/checksum mismatch')
                temporary.rename(output)
            except BaseException:
                temporary.unlink(missing_ok=True)
                raise
            metadata_path = destination / (name + '.JSON')
            with metadata_path.open('xb') as data:
                data.write(chunks)
            results.append(dict(file=str(output.resolve()), metadata=str(metadata_path.resolve()),
                                frame=frame, bytes=size, sha256=digest.hexdigest(),
                                pause_effective=metadata.get('pause_effective')))
        print(f'Retrieved {len(results)} dumps to {destination}')
        return results

    def play_cab(self, filename):
        self.refresh()
        if self.status.get('FRAME_SYNC') != '1':
            raise ValueError('Selected core lacks CAB frame queue; upload an updated SiDi128 RBF')
        if self.status['OWNER'] == 'other':
            raise ValueError('Inputs are owned by another session')
        packer, env = find_cab_packer(self.cab_packer)
        source = Path(filename).resolve()
        source_cab = Path(self.source_cab).resolve() if self.source_cab else source
        if not source_cab.is_file():
            raise ValueError('Original source CAB is unavailable')
        source_cab_digest = file_sha256(source_cab)
        with tempfile.TemporaryDirectory(prefix='mist-cab-') as work:
            packed = Path(work) / 'script.jtcab'
            source_snapshot = Path(work) / 'input.cab'
            shutil.copyfile(source, source_snapshot)
            source_digest = file_sha256(source_snapshot)
            command = [packer, str(source_snapshot), str(packed)]
            if self.cab_frame_budget is not None:
                if self.cab_frames is None or self.cab_frame_budget != self.cab_frames:
                    raise ValueError('Prefix CAB budget must equal the requested playback frame count')
                if not 1 <= self.cab_frame_budget <= 0x7fffffff:
                    raise ValueError('Prefix CAB budget must be between 1 and 2147483647')
                command.extend(['--frame-budget', str(self.cab_frame_budget)])
            elif self.cab_frames is not None:
                command.append(str(self.cab_frames))
            result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=120)
            if result.returncode:
                raise ValueError(result.stderr.strip() or 'CAB packer failed')
            if result.stderr:
                print(result.stderr, end='', file=sys.stderr)
            print(result.stdout.strip())
            with packed.open('rb') as data:
                header = data.read(32)
            if len(header) != 32 or header[:8] not in (b'JTCAB01\0', b'JTCAB02\0', b'JTCAB03\0'):
                raise ValueError('Invalid output from CAB packer')
            version, frames, events, flags, crc, size = struct.unpack('<6I', header[8:])
            if version not in (1, 2, 3) or header[:8] != f'JTCAB0{version}\0'.encode() or (flags if version < 3 else not 0 < flags <= frames) or size != 32:
                raise ValueError('Unsupported packed CAB format')
            if version > 1 and self.status.get('CAB_DIPS') != '1':
                raise ValueError('Selected core/firmware lacks CAB DIP support; update both before playback')
            dumps_total = flags if version == 3 else 0
            if dumps_total and (self.status.get('CAB_DUMP') != '1' or int(self.status.get('DUMP_BYTES', '0')) <= 0):
                raise ValueError('Selected core/firmware lacks scheduled Save NVRAM support')
            if self.first_frame is not None:
                if self.capture_every is None or self.capture_every <= 0:
                    raise ValueError('First frame requires a positive capture cadence')
                if (self.first_frame <= 0 or self.first_frame % self.capture_every
                        or self.first_frame > frames - frames % self.capture_every):
                    raise ValueError('First frame must be a positive cadence multiple within the CAB horizon')
            if self.capture_every is not None:
                if not 1 <= self.capture_every <= frames:
                    raise ValueError('Capture cadence must fit the CAB frame budget')
                first = self.first_frame if self.first_frame is not None else self.capture_every
                capture_count = (frames - first) // self.capture_every + 1
                if capture_count > 10000:
                    raise ValueError('Capture count exceeds the 10000-image limit')
                if dumps_total:
                    raise ValueError('Video cannot overlap legacy unverified dump barriers')
                if self.status.get('VIDEO_CAPTURE') != '1':
                    raise ValueError('Selected core/firmware lacks native video capture')
            digest = file_sha256(packed)
            remote_dir = '/REG169_' + secrets.token_hex(8)
            self.ftp.mkd(remote_dir)
            remote = remote_dir + '/CAB' + digest[:16] + '.JTC'
            self.remote_cab_dir = remote_dir
            self.remote_cab_path = remote
            if self.report:
                state_path = Path(str(self.report) + '.upload.json')
                state_path.parent.mkdir(parents=True, exist_ok=True)
                state_path.write_text(json.dumps(dict(status='uploading', remote_cab_dir=remote_dir,
                                                      remote_cab_path=remote, packed_sha256=digest),
                                                  indent=2) + '\n')
            with packed.open('rb') as data:
                self.ftp.storbinary('STOR ' + remote, data, blocksize=32768)
            readback = hashlib.sha256()
            self.ftp.retrbinary('RETR ' + remote, readback.update)
            if readback.hexdigest() != digest:
                raise ValueError('CAB upload readback checksum mismatch')
            started = time.monotonic()
            print(self.ftp.sendcmd('SITE CAB ' + remote))
            self.leased = True
            self.masks = [0] * 6
            epoch, last_progress = None, None
            watchdog = CABProgressWatchdog(time.monotonic())
            status, dumps = {}, []
            video_result = None
            try:
                video_started = False
                if self.capture_every:
                    if first == self.capture_every:
                        self.start_video(capture_count, self.capture_every, self.video_step)
                        video_started = True
                while True:
                    # Owner CABSTATUS polls renew the lease; observers cannot renew it.
                    status = self.cab_status()
                    actual_epoch = int(status['FPGA_EPOCH'])
                    if epoch is not None and epoch != actual_epoch:
                        raise ValueError('FPGA CAB epoch changed during playback')
                    epoch = actual_epoch
                    state, frame = int(status['STATE']), int(status['FRAME'])
                    if state == 3:
                        if frame != frames or int(status['ACCEPTED']) != events or int(status['APPLIED']) != events:
                            raise ValueError('CAB completion counters do not match packed script')
                        if dumps_total:
                            if int(status.get('DUMPS_DONE', '-1')) != dumps_total:
                                raise ValueError('CAB completed with missing dumps')
                            dumps = self.fetch_dumps(status, epoch, dumps_total, source, frames)
                        if self.capture_every:
                            if not video_started:
                                raise ValueError('CAB completed before the first capture could be armed')
                            video_result = self.wait_video()
                        print(f'CAB complete: {frame} raster frames, {events} input runs, epoch {epoch}')
                        self.refresh()
                        break
                    if state not in (2, 5, 6):
                        raise ValueError(f"CAB failed at frame {frame}: {status.get('ERROR')} "
                                         f"(FPGA error {status.get('FPGA_ERROR')})")
                    if self.capture_every and not video_started and frame >= first - self.capture_every:
                        # The MCU schedules the next absolute cadence boundary.
                        # Arm during the preceding interval, never at first itself.
                        if frame >= first:
                            raise ValueError('Missed first capture arming window')
                        self.start_video(capture_count, self.capture_every, self.video_step)
                        video_started = True
                    current_progress = (frame, state, status.get('DUMP_SEQ'), status.get('DUMPS_DONE'), status.get('DUMP_WRITTEN'))
                    frame_advanced = current_progress != last_progress
                    if frame_advanced:
                        last_progress = current_progress
                    video_progress = self.video_status() if video_started else None
                    if watchdog.stalled(time.monotonic(), frame_advanced, video_progress):
                        raise ValueError('CAB frame counter stopped advancing')
                    time.sleep(0.01 if self.capture_every and not video_started else 0.1)
            except BaseException:
                # Interactive mode may continue after an error. Do not keep an
                # uncertain script alive through the shell's normal heartbeat.
                try:
                    self.release()
                except (OSError, EOFError, ftplib.Error):
                    pass
                raise
            finally:
                if self.report:
                    report = dict(cab=str(source), cab_sha256=source_digest,
                                  cab_source=str(source_cab), cab_source_sha256=source_cab_digest,
                                  cab_clip_mode='prefix' if self.cab_frame_budget is not None else 'strict',
                                  cab_frame_budget=self.cab_frame_budget if self.cab_frame_budget is not None else frames,
                                  packed_sha256=digest, packed_version=version, frames=frames, events=events,
                                  core=self.status.get('CORE'), firmware=self.status.get('FW'),
                                  selected_file=self.status.get('FILE'),
                                  timebase='native-game-with-validated-video-pauses' if self.capture_every else 'native-raster-with-dump-barriers' if dumps_total else 'native-raster',
                                  capture_every=self.capture_every, video_dir=str(video_result) if video_result else None,
                                  video_local_dir=self.video_local_dir,
                                  video_remote_dir=self.video_remote_dir,
                                  video_retrieved=self.video_retrieved,
                                  remote_cab_dir=self.remote_cab_dir,
                                  remote_cab_path=self.remote_cab_path,
                                  dumps_total=dumps_total, dumps=dumps, wall_seconds=time.monotonic()-started, status=status)
                    if self.first_frame is not None:
                        report['first_frame'] = self.first_frame
                    path = Path(self.report)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(json.dumps(report, indent=2) + '\n')

    def execute(self, words):
        if not words:
            return True
        command, *args = words
        command = command.casefold()
        if command in ('quit', 'exit') and not args:
            return False
        if command == 'help' and not args:
            print('status | reset | port N | joy PORT HEXMASK | press INPUT... | '
                  'release [INPUT...] | pulse INPUT [MS=100] | wait MS | quit')
            print('cab FILE.cab | cabstatus | cabstop (requires a CAB-capable FPGA core)')
            print('video COUNT PERIOD [STEP] | videostart COUNT PERIOD [STEP] | videowait | vidstatus | vidstop')
            print('INPUT: directions, action buttons, or ARC labels such as coin/start. '
                  'Timing is milliseconds, not video frames.')
        elif command == 'status' and not args:
            print(self.refresh())
        elif command == 'reset' and not args:
            print(self.ftp.sendcmd('SITE RESET'))
            self.refresh()
        elif command == 'cab' and len(args) == 1:
            self.play_cab(args[0])
        elif command in ('video', 'videostart') and len(args) in (2, 3):
            self.start_video(int(args[0]), int(args[1]), int(args[2]) if len(args)==3 else None)
            if command == 'video':
                self.wait_video()
        elif command == 'videowait' and not args:
            self.wait_video()
        elif command == 'vidstatus' and not args:
            print(json.dumps(self.video_status(), indent=2))
        elif command == 'vidstop' and not args:
            print(self.ftp.sendcmd('SITE VIDSTOP'))
            self.refresh()
        elif command == 'cabstatus' and not args:
            print(json.dumps(self.cab_status(), indent=2))
        elif command == 'cabstop' and not args:
            print(self.ftp.sendcmd('SITE CABSTOP'))
            self.refresh()
        elif command == 'port' and len(args) == 1 and 0 <= int(args[0]) < 6:
            self.port = int(args[0])
        elif command == 'wait' and len(args) == 1:
            self.wait(int(args[0]))
        elif command == 'release' and not args:
            self.release()
        elif command in ('press', 'release') and args:
            self.refresh()
            mask = self.button_mask(args)
            self.set_mask(self.port, self.masks[self.port] | mask if command == 'press'
                          else self.masks[self.port] & ~mask)
        elif command == 'pulse' and 1 <= len(args) <= 2:
            duration = int(args[1]) if len(args) == 2 else 100
            if not 0 <= duration <= 3600000:
                raise ValueError('Invalid pulse duration')
            self.refresh()
            mask, previous = self.button_mask(args[:1]), self.masks[self.port]
            self.set_mask(self.port, previous | mask)
            try:
                self.wait(duration)
            except BaseException:
                # Never reacquire an expired lease and revive earlier held inputs.
                try:
                    self.release()
                except (OSError, EOFError, ftplib.Error):
                    pass
                raise
            else:
                self.refresh()
                if not self.leased:
                    raise ValueError('Input lease or core epoch changed during pulse')
                self.set_mask(self.port, previous)
        elif command == 'joy' and len(args) == 2:
            self.refresh()
            self.set_mask(int(args[0]), int(args[1], 16))
        else:
            raise ValueError('Invalid command; type help for usage')
        return True


def shell(control):
    terminal = sys.stdin.isatty()
    pending = b''
    if terminal:
        print('Connected. Type help for commands. Ctrl-D or quit releases inputs.')
        print('mist> ', end='', flush=True)
    while True:
        control.heartbeat()
        if b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
        else:
            ready, _, _ = select.select([sys.stdin], [], [], 0.25)
            if not ready:
                continue
            data = os.read(sys.stdin.fileno(), 4096)
            if data:
                pending += data
                continue
            if not pending:
                break
            line, pending = pending, b''
        try:
            if not control.execute(shlex.split(line.decode(), comments=True)):
                break
        except (ValueError, ftplib.error_perm, ftplib.error_temp) as error:
            if not terminal:
                raise
            print(f'Error: {error}')
        if terminal:
            print('mist> ', end='', flush=True)


def decode_video_image(raw):
    if len(raw) < 4:
        raise ValueError('Truncated video geometry')
    tagged_width, height = struct.unpack_from('<HH', raw)
    rgb555 = bool(tagged_width & 0x8000)
    width = tagged_width & 0x7fff
    if not 1 <= width <= 512 or not 1 <= height <= 512:
        raise ValueError('Unsupported native video geometry')
    stride = width*2 if rgb555 else (width*3+1)//2
    length = stride*height
    if len(raw) != 4+length:
        raise ValueError('Native video pixel length mismatch')
    pixels = raw[4:]
    scanlines = bytearray()
    for y in range(height):
        row = pixels[y*stride:(y+1)*stride]
        scanlines.append(0)  # PNG filter None
        if rgb555:
            for x in range(width):
                pixel = struct.unpack_from('<H', row, 2*x)[0]
                if pixel & 0x8000:
                    raise ValueError('RGB555 pixel has reserved high bit set')
                r, g, b = (pixel>>10)&31, (pixel>>5)&31, pixel&31
                scanlines.extend(((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2)))
            continue
        if width % 2 and row[-1] & 15:
            raise ValueError('Nonzero odd-width RGB444 padding')
        for x in range(width):
            at = x*3//2
            pixel = ((row[at]&15)<<8 | row[at+1]) if x%2 else (row[at]<<4 | row[at+1]>>4)
            scanlines.extend(((pixel>>8)*17, ((pixel>>4)&15)*17, (pixel&15)*17))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))
    return dict(width=width, height=height, rgb555=rgb555, png=png)


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as data:
        for chunk in iter(lambda: data.read(32768), b''):
            digest.update(chunk)
    return digest.hexdigest()


def find_cab_packer(explicit=None):
    env = dict(os.environ)
    if explicit:
        return explicit, env
    executable = shutil.which('jtframe-cab-pack')
    if executable:
        return executable, env
    roots = [parent / 'modules/jtframe' for parent in [Path.cwd(), *Path.cwd().parents]]
    if os.environ.get('JTFRAME'):
        roots.append(Path(os.environ['JTFRAME']))
    for root in roots:
        executable = root / 'bin/jtframe-cab-pack'
        if executable.is_file():
            env['JTFRAME'] = str(root)
            return str(executable), env
    raise ValueError('Cannot find jtframe-cab-pack; run from a JTCORES checkout, '
                     'source setprj.sh, or pass --cab-packer PATH')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host')
    parser.add_argument('--port', type=int, choices=range(6), default=0,
                        help='Joystick port (JTFRAME MiST/SiDi128: 1=P1, 0=P2)')
    parser.add_argument('--ftp-port', type=int, default=21)
    parser.add_argument('--layout', choices=('auto', 'jtframe', 'arc'), default='auto',
                        help='auto selects JTFRAME for Core credits ARC labels; '
                             'jtframe has six action slots plus Start/Coin; '
                             'arc uses consecutive label positions')
    parser.add_argument('--netrc', default=os.environ.get('FTP_NETRC'),
                        help='Credentials file (default: ~/.netrc)')
    parser.add_argument('--cab-packer', help='Path to jtframe-cab-pack (auto-detected from checkout)')
    parser.add_argument('--frames', type=int, help='CAB playback frame budget; strict unless --cab-frame-budget is set')
    parser.add_argument('--cab-frame-budget', type=int,
                        help='Opt in to canonical prefix clipping of the source CAB to this frame budget')
    parser.add_argument('--source-cab', help='Original configured CAB when packing a derived effective script')
    parser.add_argument('--report', help='Write CAB result metadata as JSON')
    parser.add_argument('--capture-every', type=int, help='Capture native video every N CAB frames (e.g. 100)')
    parser.add_argument('--first-frame', type=int,
                        help='First absolute CAB capture checkpoint (positive multiple of --capture-every)')
    parser.add_argument('--video-step', type=int, default=16, help='Deterministic native row stride (1..512)')
    parser.add_argument('--video-dir', help='Local base directory for verified raw/PNG recordings')
    parser.add_argument('--dump-dir', help='Local base directory for verified CAB dumps (default: REPORT.dumps or CAB.dumps)')
    parser.add_argument('command', nargs='*', help='status, reset, pulse INPUT [MS]; '
                        'omit for a persistent interactive session')
    args = parser.parse_intermixed_args()
    ftp, control = None, None
    try:
        credentials = netrc.netrc(args.netrc).authenticators(args.host)
        if not credentials:
            raise ValueError(f'No netrc credentials for {args.host}')
        ftp = ftplib.FTP(timeout=10)
        ftp.connect(args.host, args.ftp_port)
        ftp.login(credentials[0], credentials[2])
        control = Control(ftp, args.port, args.layout, args.cab_packer, args.frames, args.report, args.dump_dir, args.capture_every, args.video_dir, args.video_step, args.cab_frame_budget, args.source_cab, args.first_frame)
        if args.command:
            if args.command[0] not in ('status', 'reset', 'pulse', 'cab', 'cabstatus', 'cabstop', 'video', 'vidstatus', 'vidstop'):
                raise ValueError('Use an interactive session for held inputs; '
                                 'use pulse INPUT MS for a single timed press')
            control.execute(args.command)
        else:
            shell(control)
        return 0
    except (OSError, EOFError, ValueError, netrc.NetrcParseError, ftplib.Error, subprocess.TimeoutExpired) as error:
        print(f'Control failed: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        if ftp:
            try:
                if control and control.leased:
                    control.release()
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                pass
            ftp.close()


if __name__ == '__main__':
    sys.exit(main())
