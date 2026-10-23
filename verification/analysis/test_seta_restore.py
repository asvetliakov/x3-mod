"""SETA restore after a gate transit (X3M_SETA_RESTORE, docs/architecture/seta-restore.md): the arm/cancel state machine
and the return scan of src/proxy/seta_restore_core.h compiled on the host (verification/probe/seta_restore_host.cpp), the
launcher flag on hermetic `manage.py launch --dry-run` runs (nothing launched), the config entry and its generated views,
and the production wiring (the read site, the audited roots, the build). No game, no Wine."""
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import verification.analysis.test_launcher_defaults as launcher_defaults

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402


class Core(unittest.TestCase):
    def test_state_machine_and_return_scan(self):
        compiler = shutil.which('c++') or shutil.which('clang++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-seta-restore-') as d:
            exe = Path(d) / 'seta_restore_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/seta_restore_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('checks PASS', run.stdout)
            # 153 since the autopilot gate, the player-ship re-bind and the speed record (rebind(), speeds()); 205 since
            # the ship-script identity and the deferred sector check of the re-bind
            self.assertGreaterEqual(int(re.search(r'checks=(\d+)', run.stdout).group(1)), 205)


class StorySites(unittest.TestCase):
    """The SETA sites are detected at run time from the loaded object (src/proxy/story_detect.h detect_seta): the detector,
    compiled on the host over in-memory VM images of the objects on disk (temporary files only), reproduces both known
    builds' expectations (story_builds.h), finds nothing in x3intro / x3config and names each failure."""
    X3 = Path('/mnt/c/X3')

    @classmethod
    def setUpClass(cls):
        if not (cls.X3 / 'addon/07.cat').is_file() or not (cls.X3 / 'addon/04.cat').is_file():
            raise unittest.SkipTest('the game catalogues are not present')
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
        import story_vm_image
        import story_builds_table
        from kc_obj import load_member
        cls.img, cls.table, cls.load = story_vm_image, story_builds_table, staticmethod(load_member)
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-seta-detect-')
        cls.host, log = story_vm_image.compile_host(cls.tmp.name)
        if cls.host is None:
            cls.tmp.cleanup()
            raise AssertionError(log)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def detect(self, raw, **kw):
        image = Path(self.tmp.name) / 'image.bin'
        image.write_bytes(self.img.build_image(raw, **kw))
        try:
            return self.img.run_host(self.host, image)['seta']
        finally:
            image.unlink()

    def test_table_and_detection_per_build(self):
        entries = {e['name']: e for e in self.table.entries()}
        self.assertEqual(sorted(entries), ['mayhem', 'stock_ap'])
        self.assertEqual(self.table.row_errors(), [])
        for name, cat in (('mayhem', 'addon/07.cat'), ('stock_ap', 'addon/04.cat')):
            e = entries[name]
            self.assertTrue(e['seta_stop_ip'] and e['seta_gate_return'], name)
            s = self.detect(self.load(self.X3 / cat))
            self.assertEqual((s['fail'], s['stop_ip'], s['gate_return'], s['name_gate']),
                             ('none', '0x%05x' % e['seta_stop_ip'], '0x%05x' % e['seta_gate_return'], 'gate'), name)
        for member in ('L/x3intro.obj', 'L/x3config.obj'):
            self.assertNotEqual(self.detect(self.load(self.X3 / 'addon/07.cat', member))['fail'], 'none', member)

    def test_failure_modes(self):
        raw = self.load(self.X3 / 'addon/07.cat')
        e = self.table.entries()[0]

        def no_native(code, entries):    # StopFastForward's TI native gone (another command)
            code[e['seta_stop_ip'] - 2] ^= 0x7f

        def two_calls(code, entries):    # a second StopFastForward call in StartGateWarp
            at = e['seta_gate_return'] - 5
            entry = entries[(150, 'StartGateWarp')]
            code[entry + 1:entry + 6] = code[at:at + 5]
        for mutate, fail, where in ((no_native, 'no_site', '150::StopFastForward'), (two_calls, 'ambiguous_site', '150::StartGateWarp')):
            s = self.detect(raw, build=1234567890, mutate=mutate)
            self.assertEqual((s['fail'], s['where']), (fail, where), mutate.__name__)
        self.assertEqual(self.detect(raw, build=1510658513)['fail'], 'table_mismatch')   # a known build must match its entry
        self.assertEqual(self.detect(raw, build=1234567890)['fail'], 'none')             # an unknown build: the detection alone


class LiveImage(unittest.TestCase):
    """The install comparison over the image seta_restore::initialize() sees (Run 189 refused bytes_mismatch on an
    unchanged EXE: engine_patch::verify_bytes compares 5..16 bytes only, case_rest is 34): X3AP.exe mapped flat, the
    claim bytes of the features installed before it in capture.cpp (music_keep, then music_trace) applied, then the chase
    restore's four sites (the loader claims them later; an install-order change must not matter either), then every claim
    site the tree declares."""
    EXE = Path('/mnt/c/X3/X3AP.exe')
    SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
    BASE = 0x00400000

    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        if not cls.EXE.is_file() or hashlib.sha256(cls.EXE.read_bytes()).hexdigest() != cls.SHA256:
            raise unittest.SkipTest('The pinned X3AP.exe is required at %s' % cls.EXE)
        cls.directory = tempfile.TemporaryDirectory(prefix='x3-seta-live-')
        cls.host = Path(cls.directory.name) / 'seta_restore_host'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/seta_restore_host.cpp'), '-o', str(cls.host)],
                               capture_output=True, text=True, timeout=120)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        listing = subprocess.run([str(cls.host), '--windows'], capture_output=True, text=True, timeout=30, check=True)
        cls.windows = [(n, int(va, 16), int(length)) for n, va, length in (l.split() for l in listing.stdout.splitlines())]
        data = cls.EXE.read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
        sections = [struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * i + 8) for i in range(count)]
        image = bytearray(max(va + max(vs, rs) for vs, va, rs, _ in sections))
        for vs, va, rs, raw in sections:
            image[va:va + rs] = data[raw:raw + rs]
        cls.image = image
        keep = (ROOT / 'src/proxy/music_keep_core.h').read_text()
        site = lambda name: int(re.search(r'\b%s = (0x[0-9a-fA-F]+)' % name, keep).group(1), 16)  # noqa: E731
        # (address, kind): the claim's jmp rel32 to its arena dispatcher, or a call site's re-targeted rel32
        cls.music_keep = [(site('stop_all_va'), 'jmp'), (site('a_site_va'), 'jmp'), (site('t_stop_movie_site_va'), 'jmp'),
                          (site('c_site_va'), 'call'), (site('d_site_va'), 'call'), (site('c_run_call_va'), 'call')]
        cls.music_trace = [(site('play_va'), 'jmp')]  # the stop-all and MOV_StopMovie entries are shared, already live
        chase = (ROOT / 'verification/probe/verify_chase_restore_sites.py').read_text()
        cls.chase_restore = [(int(a, 16), 'jmp') for a in re.findall(r"H\('chase_restore_[a-z_]+',(0x[0-9a-f]+)", chase)]

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def apply(self, image, claims, arena=0x041a0000):
        for k, (address, kind) in enumerate(claims):
            at = address - self.BASE
            self.assertTrue(0 <= at <= len(image) - 5, hex(address))
            if kind == 'call':
                self.assertEqual(image[at], 0xe8, hex(address))  # claim_call re-targets an existing call
            image[at] = 0xe8 if kind == 'call' else 0xe9
            image[at + 1:at + 5] = struct.pack('<i', arena + 0x20 * k - (address + 5))

    def install_check(self, image):
        path = Path(self.directory.name) / 'image.bin'
        path.write_bytes(bytes(image))  # RVA-indexed: the host maps byte i at VA 0x00400000 + i
        run = subprocess.run([str(self.host), '--image', str(path)], capture_output=True, text=True, timeout=60)
        return run.returncode, run.stdout.strip()

    def flat(self):
        return bytearray(self.image)

    def test_claims_in_capture_order(self):
        self.assertEqual(len(self.windows), 10)
        self.assertEqual(len(self.chase_restore), 4)
        image = self.flat()
        self.assertEqual(self.install_check(image), (0, 'image windows=10 mismatch=none'))  # the EXE on disk
        self.apply(image, self.music_keep)
        self.apply(image, self.music_trace, arena=0x041a0400)
        self.assertEqual(self.install_check(image), (0, 'image windows=10 mismatch=none'))  # Run 189's live image
        self.apply(image, self.chase_restore, arena=0x041a0800)
        self.assertEqual(self.install_check(image), (0, 'image windows=10 mismatch=none'))  # chase restore first

    def test_every_declared_claim(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import verify_collide_sites
        claims = verify_collide_sites.other_claims(ROOT, skip_prefix='seta_restore', own_names=())
        self.assertGreater(len(claims), 100)
        hits = [(name, hex(a), w) for name, a, length in claims for w, va, n in self.windows if a < va + n and va < a + length]
        self.assertEqual(hits, [])  # no window holds another feature's claim: the raw comparison is the right rule
        image = self.flat()
        for name, address, _ in claims:
            at = address - self.BASE
            if 0 <= at < len(image) - 5:
                image[at:at + 5] = b'\xe9\0\0\0\0'
        self.assertEqual(self.install_check(image), (0, 'image windows=10 mismatch=none'))

    def test_foreign_write_names_the_window(self):
        image = self.flat()
        self.apply(image, self.music_keep)
        hook = dict((n, va) for n, va, _ in self.windows)['hook']
        image[hook - self.BASE:hook - self.BASE + 5] = b'\xe9\x00\x00\x00\x00'
        self.assertEqual(self.install_check(image), (1, 'image windows=10 mismatch=hook va=0x004b148c readable=1 got=e9000000008b5301'))
        image = self.flat()
        wrapper = dict((n, va) for n, va, _ in self.windows)['wrapper_body']
        image[wrapper - self.BASE + 172] ^= 0xff  # the wrapper's last byte, past every prologue bound
        code, line = self.install_check(image)
        self.assertEqual((code, line.split(' got=')[0]), (1, 'image windows=10 mismatch=wrapper_body va=0x0049f4c0 readable=1'))


class Launcher(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = launcher_defaults.load_manage()
        cls.directory = tempfile.TemporaryDirectory()
        game = Path(cls.directory.name) / 'game'
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        cls.game = game
        cls.wine = Path(cls.directory.name) / 'wine'
        cls.wine.touch()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def launch(self, *args, vanilla=False, inherited=None):
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(self.game), *args]
        environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
        environ.update(inherited or {})
        output, error = io.StringIO(), io.StringIO()
        module = self.module
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', self.wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, None, error.getvalue()
        return 0, json.loads(output.getvalue())['env'], error.getvalue()

    def test_default_on_and_opt_out(self):
        code, env, error = self.launch()
        self.assertEqual(code, 0, error)
        self.assertEqual(env['X3M_SETA_RESTORE'], '1')
        self.assertEqual(self.launch('--seta-restore')[1]['X3M_SETA_RESTORE'], '1')
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--no-seta-restore')[1])
        # a stale shell value never decides: on unless opted out, dropped when opted out
        self.assertEqual(self.launch(inherited={'X3M_SETA_RESTORE': '0'})[1]['X3M_SETA_RESTORE'], '1')
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--no-seta-restore', inherited={'X3M_SETA_RESTORE': '1'})[1])

    def test_not_forwarded_under_vanilla(self):
        code, env, error = self.launch(vanilla=True, inherited={'X3M_SETA_RESTORE': '1'})
        self.assertEqual(code, 0, error)
        self.assertNotIn('X3M_SETA_RESTORE', env)
        code, _, error = self.launch('--seta-restore', vanilla=True)
        self.assertEqual(code, 2)
        self.assertIn('--seta-restore cannot be combined with --vanilla', error)

    def test_player_mode(self):
        # --config: the DLL's default (1) stands in; the opt-out is sent empty, which the read site takes as off.
        self.assertNotIn('X3M_SETA_RESTORE', self.launch('--config')[1])
        self.assertEqual(self.launch('--config', '--no-seta-restore')[1]['X3M_SETA_RESTORE'], '')


class Config(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['seta_restore']
        self.assertEqual((e['env'], e['type'], e['default'], e['launcher'], e['developer'], e['env_only']),
                         ('X3M_SETA_RESTORE', 'bool', '1', '--seta-restore', False, False))
        # the section of the flight options (the ship steering and gate approach live in [engine])
        self.assertEqual(e['section'], schema.BY_KEY['ship_steering']['section'])
        self.assertEqual(e['section'], 'engine')

    def test_generated_views(self):
        header = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertRegex(header, r'\{"X3M_SETA_RESTORE", "seta_restore", Type::Bool, "1",')
        self.assertIn('constexpr bool seta_restore = true;', header)
        self.assertRegex((ROOT / 'assets/x3m.ini').read_text(), r'(?m)^;seta_restore = 1$')

    def test_read_site(self):
        source = (ROOT / 'src/proxy/seta_restore.cpp').read_text()
        self.assertIn('x3m::config::get(L"X3M_SETA_RESTORE", setting, 4)', source)
        self.assertIn("length == 1 && setting[0] == L'1'", source)  # anything but exactly 1 (an empty opt-out too) is off


class Wiring(unittest.TestCase):
    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/seta_restore.cpp').read_text()
        # every window of the core table is compared whole before the claim (verify_bytes takes 5..16 bytes: Run 189)
        core = (ROOT / 'src/proxy/seta_restore_core.h').read_text()
        table = core[core.index('constexpr Window windows[] = {'):core.index('constexpr unsigned window_count')]
        for window in ('hook_bytes', 'case_rest', 'dispatch_head', 'thunk_body', 'table_entry', 'native_args', 'native_ip',
                       'return_scan', 'wrapper_body', 'stop_call'):
            self.assertRegex(table, r'\{"[a-z_]+", [a-z_]+, %s, ' % window, window)
        self.assertNotIn('verify_bytes', source)
        self.assertIn('compare_windows(', source)
        self.assertIn('engine_patch::read_code(at,', source)
        self.assertIn('window=0x%08lx window_name=%s expected=%s got=%s', source)  # the refusal row names the window
        self.assertLess(source.index('windows_match()) {'), source.index('engine_patch::claim(hook_site_, spec)'))
        self.assertLess(source.index('engine_patch::claim(hook_site_, spec)'), source.index('inject_attach(&consumer_'))
        self.assertIn('engine_patch::restore(hook_site_)', source)  # rollback of the hook when the attach fails
        # the transit's player-ship re-bind, the manual-pass skip and the record-only speeds reach the log
        self.assertIn('"seta_restore %s frame=%llu frames_since_arm=%llu ms_since_arm=%llu old_ship=', source)
        self.assertIn('r.row == Row::rebound ? "rebound" : "rebind_pending"', source)
        self.assertIn('script_id=0x%08lx new_script_id=0x%08lx', source)  # rebound/rebind_pending and cancelled rows
        self.assertIn('new_flags_read=%u new_flags=0x%08lx new_autopilot=%u', source)
        self.assertIn('ship_check=%s new_ship=0x%08lx', source)  # a ship_change cancel keeps ship= the ticket's own
        self.assertIn('load(ship + ship_script, id)', source)
        self.assertIn('"skipped reason="', source)
        self.assertIn('autopilot=%u flags=0x%08lx', source)
        self.assertIn('ship_check=%s', source)
        self.assertEqual(source.count('finish(r);'), 4)  # every transition row goes through record_speed
        self.assertEqual(source.count('emit(r);'), 1)  # only inside finish()
        self.assertIn('load(ship + ship_flags, f)', source)
        # the story sites come from the run-time detection, once per VM key at a SETA stop, outside the lock
        for needle in ('x3m::story::detect_seta(reader, vm_root, sites_, scratch_);', 'if (!sites_.detected || sites_.key != key) {',
                       'w.sites = &sites_;', 'detect=%s method=%s', 'x3m::story::fail_name(r.detect_fail)', 'detect=runtime expected=%s'):
            self.assertIn(needle, source)
        self.assertLess(source.index('x3m::story::detect_seta('), source.index('        Guard guard;\n        if (machine_.active() || stop_shaped)'))
        self.assertIsNone(re.search(r'\bstory_build\b', core))
        roots = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        for symbol in ('_x3m_seta_restore_ti_stub', '_x3m_seta_restore_factor_write'):
            self.assertIn(f"'{symbol}'", roots)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/seta_restore.cpp'), 2)  # the DLL and the -fno-exceptions list
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('seta_restore::initialize();', capture)
        self.assertIn('seta_restore::present(ctx.frame);', capture)
        chase = (ROOT / 'src/proxy/chase_transition.cpp').read_text()
        # the injection site is claimed once for both consumers
        self.assertEqual(chase.count('restore_sites_install(restore_specs)'), 1)
        self.assertIn('inject_acquire(&inject_status)', chase)


if __name__ == '__main__':
    unittest.main()
