import ftplib
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('ftp_control',
        Path(__file__).resolve().parents[1] / 'tools/ftp-control.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FTP:
    def __init__(self):
        self.calls = []
        self.owner = 'none'
        self.epoch = 1
        self.labels = 'Attack,Jump,Start,Coin,Core credits'

    def sendcmd(self, command):
        self.calls.append(command)
        if command == 'SITE STATUS':
            return (f'211-Status\n PROTOCOL=1\n OWNER={self.owner}\n EPOCH={self.epoch}'
                    f'\n BUTTONS={self.labels}\n211 End')
        if command.startswith('SITE INPUT '):
            if int(command.split()[-1]) != self.epoch:
                raise ftplib.error_perm('550 Input epoch changed')
            if self.owner == 'other':
                raise ftplib.error_temp('450 Owned by another session')
            self.owner = 'self'
        if command == 'SITE RELEASE':
            self.owner = 'none'
        if command == 'SITE RESET':
            self.epoch += 1
            self.owner = 'none'
        return '200 Applied'


class Controls(unittest.TestCase):
    def setUp(self):
        self.ftp = FTP()
        self.control = module.Control(self.ftp, port=1)

    def test_arc_mapping_and_held_release(self):
        self.control.execute(['press', 'coin'])
        self.assertEqual(self.ftp.calls[-1], 'SITE INPUT 1 00800 1')
        self.control.execute(['press', 'start', 'right'])
        self.assertEqual(self.control.masks[1], 0xc01)
        self.control.execute(['release', 'coin'])
        self.assertEqual(self.control.masks[1], 0x401)
        self.control.execute(['release'])
        self.assertFalse(self.control.leased)

    def test_changed_core_discards_prior_held_buttons(self):
        self.control.execute(['press', 'right'])
        self.ftp.epoch += 1
        self.ftp.owner = 'none'
        self.control.execute(['press', 'b1'])
        self.assertEqual(self.ftp.calls[-1], 'SITE INPUT 1 00010 2')

    def test_lost_lease_discards_prior_held_buttons(self):
        self.control.execute(['press', 'right'])
        self.ftp.owner = 'none'
        self.control.execute(['press', 'b1'])
        self.assertEqual(self.control.masks[1], 0x10)

    def test_pulse_preserves_other_held_buttons(self):
        self.control.execute(['press', 'right'])
        self.control.execute(['pulse', 'b1', '0'])
        commands = [c for c in self.ftp.calls if c.startswith('SITE INPUT')]
        self.assertEqual(commands[-2:], ['SITE INPUT 1 00011 1', 'SITE INPUT 1 00001 1'])

    def test_failed_heartbeat_never_reasserts_held_buttons(self):
        self.control.execute(['press', 'right'])
        self.control.wait = Mock(side_effect=ftplib.error_perm('550 No lease'))
        with self.assertRaises(ftplib.error_perm):
            self.control.execute(['pulse', 'b1', '100'])
        self.assertEqual(self.ftp.calls[-1], 'SITE RELEASE')
        self.assertFalse(self.control.leased)

    def test_reject_unknown_label_and_bad_raw_mask(self):
        with self.assertRaises(ValueError):
            self.control.execute(['press', 'service'])
        with self.assertRaises(ValueError):
            self.control.execute(['joy', '6', '0'])
        with self.assertRaises(ValueError):
            self.control.execute(['joy', '1', '100000'])

    def test_owner_conflict_does_not_change_local_mask(self):
        self.ftp.owner = 'other'
        with self.assertRaises(ftplib.error_temp):
            self.control.execute(['press', 'coin'])
        self.assertFalse(self.control.leased)
        self.assertEqual(self.control.masks[1], 0)

    def test_refresh_buttons_for_different_game(self):
        self.ftp.labels = 'A,B,C,D,Start,Coin,Core credits'
        self.control.refresh()
        self.assertEqual(self.control.button_mask(['coin']), 0x800)
        self.assertEqual(self.control.button_mask(['1p']), 0x400)

    def test_six_actions_and_reserved_controls_are_separate(self):
        self.ftp.labels = 'LP,MP,HP,LK,MK,HK,Start,Coin,Core credits'
        self.control.refresh()
        self.assertEqual(self.control.button_mask(['LP', 'MP', 'HP', 'LK', 'MK', 'HK']), 0x3f0)
        self.assertEqual(self.control.button_mask(['start', 'coin']), 0xc00)
        self.assertEqual(self.control.button_mask(['Core credits']), 0x1000)
        with self.assertRaises(ValueError):
            self.control.button_mask(['b7'])

    def test_explicit_arc_layout_preserves_consecutive_mapping(self):
        control = module.Control(self.ftp, layout='arc')
        self.assertEqual(control.button_mask(['start', 'coin']), 0xc0)
        self.assertEqual(control.button_mask(['b16']), 0x80000)

    def test_explicit_jtframe_layout_without_arc_labels(self):
        self.ftp.labels = ''
        control = module.Control(self.ftp, layout='jtframe')
        self.assertEqual(control.button_mask(['start', 'coin']), 0xc00)
        self.assertEqual(control.button_mask(['b6']), 0x200)

    def test_capture_watchdog_tracks_rows_and_post_capture_resume(self):
        watchdog = module.CABProgressWatchdog(0)
        active = {'ACTIVE': '1', 'DONE': '4', 'ROWS': '1', 'REQUESTED': '500', 'ACTUAL': '500'}
        self.assertFalse(watchdog.stalled(1, False, active))
        self.assertFalse(watchdog.stalled(20, False, dict(active)))
        progressed = dict(active, ROWS='2')
        self.assertFalse(watchdog.stalled(21, False, progressed))
        self.assertFalse(watchdog.stalled(50, True, progressed))
        self.assertTrue(watchdog.stalled(81, False, progressed))
        complete = dict(progressed, ACTIVE='0', DONE='40')
        self.assertFalse(watchdog.stalled(82, False, complete))
        self.assertFalse(watchdog.stalled(84, False, complete))
        self.assertTrue(watchdog.stalled(85.1, False, complete))


class CABFTP(FTP):
    def __init__(self):
        super().__init__()
        self.capable = True
        self.dip_capable = True
        self.dump_capable = True
        self.dump_bytes = 4
        self.dump_states = False
        self.dump_data = b'RAM!'
        self.bad_dump = False
        self.stored = None
        self.made_dirs = []
        self.corrupt = False
        self.states = [(2, 0, 7), (3, 2, 7)]

    def sendcmd(self, command):
        if command == 'SITE CABSTATUS':
            self.calls.append(command)
            state, frame, epoch = self.states.pop(0)
            if state == 3:
                self.owner = 'none'
            return (f'211-CAB\n STATE={state}\n FRAME={frame}\n FPGA_EPOCH={epoch}'
                    '\n ACCEPTED=2\n APPLIED=2\n ERROR=\n FPGA_ERROR=0'
                    f'\n DUMPS_DONE={int(state==3)}\n DUMP_DIR=/CABD12345678\n DUMP_BYTES={self.dump_bytes}\n211 End')
        if command.startswith('SITE CAB '):
            self.owner = 'self'
        result = super().sendcmd(command)
        if command == 'SITE STATUS':
            result += f'\n FRAME_SYNC={int(self.capable)}\n CAB_DIPS={int(self.dip_capable)}\n CORE=TROJAN\n FW=TEST\n FILE=TROJAN'
            result += f'\n CAB_DUMP={int(self.dump_capable)}\n DUMP_BYTES={self.dump_bytes}'
        return result

    def storbinary(self, command, data, blocksize):
        self.calls.append(command)
        self.stored = data.read()

    def mkd(self, path):
        self.made_dirs.append(path)
        self.calls.append('MKD ' + path)
        return path

    def retrbinary(self, command, callback):
        self.calls.append(command)
        if command.endswith('.JSON'):
            callback(json.dumps(dict(version=1,epoch=7,sequence=1,frame=0,bytes=4,
                         crc32=f'{zlib.crc32(self.dump_data):08x}',pause_effective='unverified')).encode())
            return
        if command.endswith('.RAM'):
            callback(b'FAIL' if self.bad_dump else self.dump_data)
            return
        callback(b'bad' if self.corrupt else self.stored)


class CabinetControls(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.source = Path(self.work.name) / 'test.cab'
        self.source.write_text('1 coin\n1\n')
        self.report = Path(self.work.name) / 'results/report.json'
        self.ftp = CABFTP()
        self.control = module.Control(self.ftp, frames=2, report=self.report)
        self.packer = patch.object(module, 'find_cab_packer', return_value=('packer', {})).start()
        self.run = patch.object(module.subprocess, 'run', side_effect=self.pack).start()
        patch.object(module.time, 'sleep').start()
        self.addCleanup(patch.stopall)

    def pack(self, command, **kwargs):
        self.assertEqual(Path(command[1]).read_bytes(), self.source.read_bytes())
        self.assertEqual(command[-1], '2')
        # This exporter is separately tested against CabinetScript's real parser.
        Path(command[2]).write_bytes(b'JTCAB01\0' + struct.pack('<6I', 1, 2, 2, 0, 0, 32)
                                    + struct.pack('<4I', 1, 1, 1, 0))
        return subprocess.CompletedProcess(command, 0, 'packed', '')

    def test_upload_readback_completion_and_source_snapshot_report(self):
        original = self.source.read_bytes()
        def edit_during_pack(command, **kwargs):
            result = self.pack(command, **kwargs)
            self.source.write_text('modified while playback runs')
            return result
        self.run.side_effect = edit_during_pack
        self.control.play_cab(self.source)
        result = json.loads(self.report.read_text())
        self.assertEqual(result['cab_sha256'], hashlib.sha256(original).hexdigest())
        self.assertEqual(result['core'], 'TROJAN')
        self.assertEqual(result['firmware'], 'TEST')
        self.assertEqual(result['status']['FRAME'], '2')
        self.assertEqual(result['timebase'], 'native-raster')
        self.assertGreaterEqual(result['wall_seconds'], 0)
        self.assertEqual(len(self.ftp.made_dirs), 1)
        remote_cab = next(c.removeprefix('SITE CAB ') for c in self.ftp.calls if c.startswith('SITE CAB '))
        self.assertTrue(remote_cab.startswith(self.ftp.made_dirs[0] + '/CAB'))
        self.assertEqual(result['remote_cab_dir'], self.ftp.made_dirs[0])
        self.assertEqual(result['remote_cab_path'], remote_cab)
        commands = self.ftp.calls
        self.assertLess(next(i for i,c in enumerate(commands) if c.startswith('RETR ')),
                        next(i for i,c in enumerate(commands) if c.startswith('SITE CAB ')))

    def test_cli_prefix_budget_and_original_cab_roundtrip(self):
        original = Path(self.work.name) / 'original.cab'
        original.write_text('original profile input\n')
        effective = Path(self.work.name) / 'effective.cab'
        effective.write_text('derived effective input\n')
        netrc_path = Path(self.work.name) / 'test.netrc'
        netrc_path.write_text('machine 192.0.2.1 login test password test\n')
        report = Path(self.work.name) / 'cli-report.json'

        class CLIFTP(CABFTP):
            def connect(self, *args, **kwargs):
                return 'connected'

            def login(self, *args, **kwargs):
                return 'logged in'

            def quit(self):
                return 'closed'

            def close(self):
                return None

        cli_ftp = CLIFTP()
        def pack(command, **kwargs):
            self.assertEqual(command[3:], ['--frame-budget', '2'])
            self.assertEqual(Path(command[1]).read_bytes(), effective.read_bytes())
            Path(command[2]).write_bytes(b'JTCAB01\0' + struct.pack('<6I', 1, 2, 2, 0, 0, 32)
                                        + struct.pack('<4I', 1, 1, 1, 0))
            return subprocess.CompletedProcess(command, 0, 'packed prefix', '')

        arguments = [
            'ftp-control.py', '192.0.2.1', '--netrc', str(netrc_path),
            '--frames', '2', '--cab-frame-budget', '2', '--source-cab', str(original),
            '--report', str(report), 'cab', str(effective),
        ]
        with patch.object(sys, 'argv', arguments), \
                patch.object(module.ftplib, 'FTP', return_value=cli_ftp), \
                patch.object(module, 'find_cab_packer', return_value=('fake-exporter', {})), \
                patch.object(module.subprocess, 'run', side_effect=pack), \
                patch.object(module.time, 'sleep'):
            self.assertEqual(module.main(), 0)

        data = json.loads(report.read_text())
        self.assertEqual(data['cab_sha256'], hashlib.sha256(effective.read_bytes()).hexdigest())
        self.assertEqual(data['cab_source_sha256'], hashlib.sha256(original.read_bytes()).hexdigest())
        self.assertEqual(data['cab_clip_mode'], 'prefix')
        self.assertEqual(data['cab_frame_budget'], 2)
        self.assertEqual(data['frames'], 2)
        self.assertFalse(self.control.leased)

    def test_capture_cadence_preflight_does_not_start_cab(self):
        for period in (0, -1, 3):
            self.control.capture_every = period
            with self.subTest(period=period), self.assertRaisesRegex(ValueError, 'cadence'):
                self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)
        self.assertFalse(any(c.startswith('SITE CAB ') for c in self.ftp.calls))

    def test_first_capture_preflight_does_not_start_cab(self):
        for period, first in ((None, 1), (0, 1), (1, 0), (1, -1), (1, 3), (2, 1)):
            self.control.capture_every, self.control.first_frame = period, first
            with self.subTest(period=period, first=first), self.assertRaisesRegex(ValueError, 'First frame'):
                self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)
        self.assertFalse(any(c.startswith('SITE CAB ') for c in self.ftp.calls))

    def setup_capture(self, frames, period, first, states):
        self.control.cab_frames = frames
        self.control.capture_every = period
        self.control.first_frame = first
        original_send = self.ftp.sendcmd
        self.ftp.sendcmd = lambda command: original_send(command) + ('\n VIDEO_CAPTURE=1' if command == 'SITE STATUS' else '')
        def pack(command, **kwargs):
            Path(command[2]).write_bytes(b'JTCAB01\0' + struct.pack('<6I', 1, frames, 2, 0, 0, 32))
            return subprocess.CompletedProcess(command, 0, 'packed', '')
        self.run.side_effect = pack
        history = []
        def status():
            state, frame = states.pop(0)
            history.append(('frame', frame))
            return dict(STATE=str(state), FRAME=str(frame), FPGA_EPOCH='7', ACCEPTED='2', APPLIED='2')
        self.control.cab_status = Mock(side_effect=status)
        def start(count, cadence, step):
            history.append(('video', count, cadence, step))
        self.control.start_video = Mock(side_effect=start)
        self.control.video_status = Mock(return_value={})
        self.control.wait_video = Mock(return_value=Path(self.work.name) / 'video')
        return history

    def test_delayed_first_capture_arms_before_exact_boundary(self):
        history = self.setup_capture(450, 100, 300,
                                    [(2, 0), (2, 100), (2, 199), (2, 201), (2, 300), (2, 400), (3, 450)])
        self.control.play_cab(self.source)
        self.control.start_video.assert_called_once_with(2, 100, 16)
        self.assertLess(history.index(('frame', 201)), history.index(('video', 2, 100, 16)))
        self.assertLess(history.index(('video', 2, 100, 16)), history.index(('frame', 300)))
        self.assertEqual(json.loads(self.report.read_text())['first_frame'], 300)
        self.assertEqual(self.control.cab_status.call_count, 7)

    def test_default_first_capture_starts_immediately_and_omits_report_override(self):
        history = self.setup_capture(2, 1, None, [(2, 0), (3, 2)])
        self.control.play_cab(self.source)
        self.assertEqual(history[0], ('video', 2, 1, 16))
        self.assertNotIn('first_frame', json.loads(self.report.read_text()))

    def test_count_limit_applies_to_delayed_schedule(self):
        self.setup_capture(12000, 1, 11999, [(2, 11998), (3, 12000)])
        self.control.play_cab(self.source)
        self.control.start_video.assert_called_once_with(2, 1, 16)
        self.control.first_frame = 1
        with self.assertRaisesRegex(ValueError, 'count exceeds'):
            self.control.play_cab(self.source)

    def test_missed_first_capture_window_fails_and_releases_inputs(self):
        self.setup_capture(400, 100, 300, [(2, 0), (2, 300)])
        with self.assertRaisesRegex(ValueError, 'Missed first capture'):
            self.control.play_cab(self.source)
        self.control.start_video.assert_not_called()
        self.assertIn('SITE RELEASE', self.ftp.calls)

    def test_watchdog_remains_active_before_first_capture(self):
        self.setup_capture(400, 100, 300, [(2, 0)])
        with patch.object(module.CABProgressWatchdog, 'stalled', return_value=True), \
                self.assertRaisesRegex(ValueError, 'stopped advancing'):
            self.control.play_cab(self.source)
        self.control.start_video.assert_not_called()
        self.assertIn('SITE RELEASE', self.ftp.calls)

    def test_missing_video_capability_preflight_does_not_start_cab(self):
        self.control.capture_every = 1
        with self.assertRaisesRegex(ValueError, 'native video'):
            self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)
        self.assertFalse(any(c.startswith('SITE CAB ') for c in self.ftp.calls))

    def test_capability_or_owner_rejection_precedes_packing(self):
        for capable,owner in [(False,'none'), (True,'other')]:
            self.ftp.capable, self.ftp.owner = capable, owner
            with self.assertRaises(ValueError):
                self.control.play_cab(self.source)
        self.run.assert_not_called()
        self.assertIsNone(self.ftp.stored)

    def test_unsupported_script_does_not_upload_or_start(self):
        self.run.return_value = subprocess.CompletedProcess([], 1, '', 'cheat is unsupported')
        self.run.side_effect = None
        with self.assertRaisesRegex(ValueError, 'cheat'):
            self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)
        self.assertFalse(any(c.startswith('SITE CAB ') for c in self.ftp.calls))

    def test_upload_checksum_failure_precedes_reset(self):
        self.ftp.corrupt = True
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.control.play_cab(self.source)
        self.assertFalse(any(c.startswith('SITE CAB ') for c in self.ftp.calls))

    def test_dip_format_requires_capability_before_upload(self):
        def dip_pack(command, **kwargs):
            Path(command[2]).write_bytes(b'JTCAB02\0' + struct.pack('<6I', 2, 2, 2, 0, 0, 32)
                                       + struct.pack('<6I', 1, 0x200000, 0, 1, 0x200001, 0xffffffff))
            return subprocess.CompletedProcess(command, 0, 'packed DIP', '')
        self.run.side_effect = dip_pack
        self.ftp.dip_capable = False
        with self.assertRaisesRegex(ValueError, 'lacks CAB DIP support'):
            self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)
        self.ftp.dip_capable = True
        self.control.play_cab(self.source)
        self.assertEqual(self.ftp.stored[:8], b'JTCAB02\0')
        self.assertEqual(json.loads(self.report.read_text())['packed_version'], 2)

    def dump_pack(self, command, **kwargs):
        Path(command[2]).write_bytes(b'JTCAB03\0' + struct.pack('<6I', 3, 2, 2, 1, 0, 32)
                                    + struct.pack('<6I', 1, 0x400000, 0, 1, 0, 0))
        return subprocess.CompletedProcess(command, 0, 'packed dump', '')

    def test_dump_capability_rejected_before_upload(self):
        self.run.side_effect = self.dump_pack
        for capable,size in [(False,4),(True,0)]:
            self.ftp.dump_capable,self.ftp.dump_bytes=capable,size
            with self.assertRaisesRegex(ValueError,'Save NVRAM'):
                self.control.play_cab(self.source)
        self.assertIsNone(self.ftp.stored)

    def test_dump_barrier_and_verified_file_retrieval(self):
        self.run.side_effect = self.dump_pack
        self.ftp.states = [(5,0,7),(6,0,7),(2,1,7),(3,2,7)]
        self.control.play_cab(self.source)
        report=json.loads(self.report.read_text())
        self.assertEqual(report['timebase'],'native-raster-with-dump-barriers')
        self.assertEqual(report['dumps_total'],1)
        self.assertEqual(Path(report['dumps'][0]['file']).read_bytes(),b'RAM!')
        self.assertEqual(report['dumps'][0]['pause_effective'],'unverified')

    def test_dump_checksum_failure_keeps_report_and_removes_partial(self):
        self.run.side_effect = self.dump_pack
        self.ftp.bad_dump = True
        with self.assertRaisesRegex(ValueError,'checksum'):
            self.control.play_cab(self.source)
        self.assertEqual(self.ftp.calls[-1],'SITE RELEASE')
        self.assertEqual(json.loads(self.report.read_text())['status']['DUMP_DIR'],'/CABD12345678')
        self.assertEqual(list(Path(self.work.name).rglob('*.part')),[])

    def test_dump_path_rejection_and_no_overwrite(self):
        with self.assertRaisesRegex(ValueError,'directory'):
            self.control.fetch_dumps({'DUMP_DIR':'/../bad'},7,1,self.source,2)
        status=dict(DUMP_DIR='/CABD12345678',DUMP_BYTES='4')
        self.control.fetch_dumps(status,7,1,self.source,2)
        with self.assertRaises(FileExistsError):
            self.control.fetch_dumps(status,7,1,self.source,2)

    def test_epoch_change_cancels_even_in_interactive_mode(self):
        self.ftp.states = [(2, 0, 7), (2, 1, 8)]
        with self.assertRaisesRegex(ValueError, 'epoch changed'):
            self.control.play_cab(self.source)
        self.assertEqual(self.ftp.calls[-1], 'SITE RELEASE')
        self.assertFalse(self.control.leased)
        self.assertEqual(json.loads(self.report.read_text())['status']['FPGA_EPOCH'], '8')

    def test_wrong_completion_counters_and_keyboard_interrupt_release(self):
        for states, failure in [([(3, 1, 7)], ValueError), ([], KeyboardInterrupt)]:
            self.ftp.states = states
            if failure == KeyboardInterrupt:
                with patch.object(self.control, 'cab_status', side_effect=KeyboardInterrupt):
                    with self.assertRaises(KeyboardInterrupt):
                        self.control.play_cab(self.source)
            else:
                with self.assertRaisesRegex(ValueError, 'completion counters'):
                    self.control.play_cab(self.source)
            self.assertEqual(self.ftp.calls[-1], 'SITE RELEASE')
            self.assertFalse(self.control.leased)


class VideoImages(unittest.TestCase):
    @staticmethod
    def image(width=3, height=2, pixels=None, rgb555=False):
        if pixels is None:
            pixels = bytes.fromhex('123456abc0') * height
        tagged_width = width | (0x8000 if rgb555 else 0)
        return struct.pack('<HH', tagged_width, height) + pixels

    def test_rgb444_png_nonblank(self):
        result = module.decode_video_image(self.image())
        self.assertEqual((result['width'], result['height']), (3, 2))
        self.assertTrue(result['png'].startswith(b'\x89PNG\r\n\x1a\n'))
        png = result['png']; at = 8; rgb = None
        while at < len(png):
            size = struct.unpack_from('>I', png, at)[0]
            kind, data = png[at+4:at+8], png[at+8:at+8+size]
            self.assertEqual(zlib.crc32(kind+data), struct.unpack_from('>I', png, at+8+size)[0])
            if kind == b'IDAT': rgb = zlib.decompress(data)
            at += size+12
        self.assertEqual(rgb, bytes.fromhex('00112233445566aabbcc') * 2)

    def test_rgb555_png_preserves_low_bits(self):
        colors = [1, 0x1235, 0x7fff]
        pixels = b''.join(struct.pack('<H', c) for c in colors)
        result = module.decode_video_image(self.image(height=1, pixels=pixels, rgb555=True))
        self.assertTrue(result['rgb555'])
        png = result['png']; at = 8; rgb = None
        while at < len(png):
            size = struct.unpack_from('>I', png, at)[0]
            kind, data = png[at+4:at+8], png[at+8:at+8+size]
            self.assertEqual(zlib.crc32(kind+data), struct.unpack_from('>I', png, at+8+size)[0])
            if kind == b'IDAT': rgb = zlib.decompress(data)
            at += size+12
        expected = []
        for color in colors:
            for channel in ((color>>10)&31, (color>>5)&31, color&31):
                expected.append((channel<<3)|(channel>>2))
        self.assertEqual(rgb, bytes([0, *expected]))
        malformed = bytearray(self.image(height=1, pixels=pixels, rgb555=True))
        malformed[-1] |= 0x80
        with self.assertRaisesRegex(ValueError, 'high bit'):
            module.decode_video_image(malformed)

    def test_geometry_length_and_padding(self):
        good = self.image()
        for at in (0, 1, 2, 3):
            with self.subTest(at=at):
                raw = bytearray(good); raw[at] ^= 1
                with self.assertRaises(ValueError): module.decode_video_image(raw)
        with self.assertRaises(ValueError): module.decode_video_image(good[:-1])
        with self.assertRaises(ValueError): module.decode_video_image(good+b'\0')
        bad_padding = self.image(pixels=bytes.fromhex('123456abc1') * 2)
        with self.assertRaises(ValueError): module.decode_video_image(bad_padding)

class VideoDownloads(unittest.TestCase):
    def test_recording_rejects_mixed_geometry_or_format(self):
        remote='/video/gng/run_00000001_00000003'
        cases=[
            (dict(type='run',count=2,width=3,height=2,step=16),
             VideoImages.image(),
             struct.pack('<HH',4,2)+bytes(12),
             'geometry'),
            (dict(type='run',count=2,width=3,height=2,step=16),
             VideoImages.image(),
             VideoImages.image(pixels=bytes(12),rgb555=True),
             'format'),
        ]
        for run,first,second,error in cases:
            with self.subTest(error=error), tempfile.TemporaryDirectory() as base:
                records=[run,
                    dict(type='capture',requested=1,actual=1,image='frame_1.raw',duplicate=False),
                    dict(type='capture',requested=2,actual=2,image='frame_2.raw',duplicate=False),
                    dict(type='complete',captures=2)]
                files={'ROMPATH.TXT':b'/GNG/GNG.ROM','frame_1.raw':first,'frame_2.raw':second,
                       'index.jsonl':''.join(json.dumps(r)+'\n' for r in records).encode()}
                ftp=FTP()
                def retrieve(command,callback):
                    callback(files[command.rsplit('/',1)[-1]])
                ftp.retrbinary=retrieve
                control=module.Control(ftp,video_dir=base)
                with self.assertRaisesRegex(ValueError,error): control.fetch_video(remote)

    def test_duplicate_payload_download_without_maps(self):
        remote = '/video/gng/run_00000001_00000002'
        raw = VideoImages.image()
        run = dict(type='run', count=2, width=3, height=2, step=16)
        records = [run]
        files = {'ROMPATH.TXT': b'/GNG/GNG.ROM', 'frame_00020.raw': raw}
        for requested, actual in ((20, 20), (40, 20)):
            records.append(dict(type='capture', requested=requested, actual=actual,
                                image='frame_00020.raw', duplicate=requested != actual))
        records.append(dict(type='complete', captures=2))
        files['index.jsonl'] = ''.join(json.dumps(r)+'\n' for r in records).encode()
        ftp = FTP()
        downloaded = []
        def retrieve(command, callback):
            name = command.rsplit('/', 1)[-1]; downloaded.append(name); callback(files[name])
        ftp.retrbinary = retrieve
        with tempfile.TemporaryDirectory() as base:
            control = module.Control(ftp, video_dir=base)
            result = control.fetch_video(remote)
            self.assertEqual(len(list(result.glob('*.raw'))), 1)
            self.assertEqual(len(list(result.glob('*.map'))), 0)
            self.assertEqual(downloaded.count('frame_00020.raw'), 1)
            self.assertTrue((result/'frame_00020.png').is_file())
            with self.assertRaises(FileExistsError): control.fetch_video(remote)


if __name__ == '__main__':
    unittest.main()
