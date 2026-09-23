"""Host tests of the X3AP.exe identity: structure plus verified sites, hashes as provenance.

verification/probe/exe_identity.py mirrors src/proxy/executable_identity.h
(docs/reverse-engineering/executable-identity.md). A synthetic image with the
known structure and anchors but any other content passes the identity (its hash
is unknown and only reported); the LARGE_ADDRESS_AWARE bit and the CheckSum are
free; a changed anchor, stamp, size or section fails it; a changed hook site
fails that hook's verifier while the identity still passes. The C++ gate holds
no file hash, every engine global the proxy names is anchored, no anchor sits on
a patched site, and the launcher records the executable without refusing it.
The installed X3AP.exe, when present, is only read (variants go to a temporary
directory).
"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import exe_identity as ident  # noqa: E402

HEADER = ROOT / 'src/proxy/executable_identity.h'
PROXY = ROOT / 'src/proxy'
REAL = ident.DEFAULT_EXE
# Data-range literals in src/proxy that are not reads of a global: range ends
# named in comments and d3dx9_37 addresses (not X3AP.exe) in loading_trace.cpp.
NOT_READ = {0x0059695f: 'collide_memo_core.h: end of the root block comment',
            0x00608533: 'collide_memo_core.h: end of the contact record comment',
            0x00587c94: 'loading_trace.cpp: d3dx9_37 address', 0x00587e06: 'loading_trace.cpp: d3dx9_37 address',
            0x00587e6b: 'loading_trace.cpp: d3dx9_37 address'}
# Modules that call executable_verified() and patch nothing: they only read
# engine globals, which the gate anchors.
READ_ONLY = {'camera_state.cpp', 'capture.cpp', 'sun_light_poll.cpp', 'motion_output_shadow_adaptive_inc.h'}
SITE_CHECK = re.compile(r'engine_patch::claim\(|engine_patch::claim_call\(|verify_bytes\(|memcmp\(|install_group\(')


def synthetic(laa=False, checksum=0, filler=0x90):
    """A file of the known size, headers and section table, anchors in place, filler elsewhere."""
    data = bytearray([filler]) * ident.FILE_SIZE
    data[0:0x400] = bytes(0x400)
    data[0:2] = b'MZ'
    pe = 0x120
    struct.pack_into('<I', data, 0x3c, pe)
    data[pe:pe + 4] = b'PE\0\0'
    characteristics = ident.CHARACTERISTICS_WITHOUT_LAA | (ident.LAA_BIT if laa else 0)
    struct.pack_into('<HHIIIHH', data, pe + 4, 0x14c, len(ident.SECTIONS), ident.TIME_DATE_STAMP, 0, 0, 0xe0, characteristics)
    o = pe + 24
    struct.pack_into('<H', data, o, 0x10b)
    struct.pack_into('<I', data, o + 16, ident.ENTRY_POINT)
    struct.pack_into('<I', data, o + 28, ident.IMAGE_BASE)
    struct.pack_into('<I', data, o + 56, ident.IMAGE_SIZE)
    struct.pack_into('<I', data, o + 64, checksum)
    for i, (name, virtual_size, virtual_address, raw_size, raw_pointer, flags) in enumerate(ident.SECTIONS):
        s = o + 0xe0 + 40 * i
        data[s:s + 8] = name.encode().ljust(8, b'\0')
        struct.pack_into('<IIII', data, s + 8, virtual_size, virtual_address, raw_size, raw_pointer)
        struct.pack_into('<I', data, s + 36, flags)
    text = ident.SECTIONS[0]
    for anchor in ident.ANCHORS:
        offset = anchor[0] - ident.IMAGE_BASE - text[2] + text[4]
        body = ident.anchor_bytes(anchor)
        data[offset:offset + len(body)] = body
    return bytes(data)


def header_tables():
    text = HEADER.read_text()
    constant = lambda name: int(re.search(r'\b%s = (0x[0-9a-f]+|\d+)' % name, text).group(1), 0)
    sections = [(''.join(chr(int(c)) if c.isdigit() else c.strip("'") for c in re.findall(r"'[^']*'|\b0\b", m.group(1))).rstrip('\0'),
                 *(int(v, 16) for v in m.group(2).split(', ')))
                for m in re.finditer(r"\{\{([^}]*)\}, (0x[0-9a-f]{8}, 0x[0-9a-f]{8}, 0x[0-9a-f]{8}, 0x[0-9a-f]{8}, 0x[0-9a-f]{8})\}", text)]
    anchors = []
    for m in re.finditer(r'\{(0x[0-9a-f]{8}), (0x[0-9a-f]{8}), (\d), \{([^}]*)\}(?:, (\d), (0x[0-9a-f]{2}))?\}', text):
        prefix = bytes(int(v, 16) for v in m.group(4).split(', '))
        if len(prefix) != int(m.group(3)):
            raise AssertionError(m.group(0))
        suffix = bytes([int(m.group(6), 16)]) if m.group(5) == '1' else b''
        anchors.append((int(m.group(1), 16), int(m.group(2), 16), prefix, suffix))
    return constant, sections, anchors


class Synthetic(unittest.TestCase):
    def test_structure_and_anchors_pass_with_an_unknown_hash(self):
        data = synthetic()
        self.assertTrue(ident.identity_ok(data))
        info = ident.info(data)
        self.assertIsNone(info['known'])
        self.assertFalse(info['laa'])
        self.assertTrue(ident.identity_ok(synthetic(filler=0xcc)))
        self.assertNotEqual(ident.raw_sha256(synthetic(filler=0xcc)), info['sha256'])

    def test_laa_bit_and_checksum_are_free(self):
        plain, laa, summed = synthetic(), synthetic(laa=True), synthetic(laa=True, checksum=0x1234)
        for data in (plain, laa, summed):
            self.assertTrue(ident.identity_ok(data))
        self.assertEqual((ident.laa(plain), ident.laa(laa)), (False, True))
        self.assertEqual(len({ident.raw_sha256(d) for d in (plain, laa, summed)}), 3)
        self.assertEqual(len({ident.identity_digest(d) for d in (plain, laa, summed)}), 1)
        self.assertEqual(ident.with_laa(plain, on=True), laa)
        self.assertEqual(ident.with_laa(laa, on=False), plain)

    def test_each_structural_change_fails(self):
        base = bytearray(synthetic())
        pe = 0x120
        cases = {'stamp': (pe + 8, 4), 'image_size': (pe + 24 + 56, 4), 'entry': (pe + 24 + 16, 4),
                 'section_size': (pe + 24 + 0xe0 + 8, 4), 'dll_bit': (pe + 22, 2)}
        for name, (offset, width) in cases.items():
            with self.subTest(name):
                data = bytearray(base)
                value = int.from_bytes(data[offset:offset + width], 'little') ^ (0x2000 if name == 'dll_bit' else 0x10)
                data[offset:offset + width] = value.to_bytes(width, 'little')
                self.assertFalse(ident.structure_ok(bytes(data)))
        self.assertFalse(ident.structure_ok(bytes(base) + b'\0'))

    def test_each_anchor_byte_is_checked(self):
        base = synthetic()
        text = ident.SECTIONS[0]
        for anchor in ident.ANCHORS:
            with self.subTest(hex(anchor[0])):
                data = bytearray(base)
                data[anchor[0] - ident.IMAGE_BASE - text[2] + text[4] + len(anchor[2])] ^= 1  # the global's low byte
                result = ident.anchors(bytes(data))
                self.assertEqual([va for va, ok in result.items() if not ok], [anchor[0]])

    def test_foreign_image_is_refused(self):
        data = bytearray(synthetic())
        pe = 0x120
        struct.pack_into('<HH', data, pe + 4, 0x8664, 5)            # Machine AMD64, five sections
        struct.pack_into('<I', data, pe + 24 + 28, 0x10000000)      # ImageBase
        checks = ident.structure(bytes(data))
        self.assertEqual({k for k, ok in checks.items() if not ok}, {'pe32_i386', 'preferred_base', 'sections'})
        self.assertFalse(ident.identity_ok(bytes(data)))

    def test_pe_checksum_excludes_its_own_field(self):
        # Agreement with imagehlp's stored values: verification/results/executable-identity/pe_checksum_check.py.
        blob = bytearray(synthetic())
        struct.pack_into('<I', blob, 0x120 + 24 + 64, 0xdeadbeef)
        self.assertEqual(ident.pe_checksum(bytes(blob)), ident.pe_checksum(synthetic()))


class SourceParity(unittest.TestCase):
    def test_header_matches_the_python_mirror(self):
        """Table check: the constants and anchors parsed from executable_identity.h equal
        the Python mirror. It does not run the compiled C++ check."""
        constant, sections, anchors = header_tables()
        self.assertEqual((constant('image_base'), constant('image_size'), constant('entry_point'), constant('time_date_stamp'),
                          constant('characteristics_without_laa'), constant('file_size')),
                         (ident.IMAGE_BASE, ident.IMAGE_SIZE, ident.ENTRY_POINT, ident.TIME_DATE_STAMP,
                          ident.CHARACTERISTICS_WITHOUT_LAA, ident.FILE_SIZE))
        self.assertEqual(sections, [tuple(s) for s in ident.SECTIONS])
        self.assertEqual(anchors, list(ident.ANCHORS))
        self.assertEqual(len({a[1] for a in anchors}), len(anchors))

    def test_gates_hold_no_file_hash(self):
        trace = (PROXY / 'object_trace.cpp').read_text()
        lifetime = (PROXY / 'object_lifetime.cpp').read_text()
        gate = trace[trace.index('bool verified_image()'):trace.index('bool executable_verified()')]
        for body in (gate, lifetime[lifetime.index('bool initialize(){'):lifetime.index('bool active(){')]):
            self.assertNotIn('CALG_SHA_256', body)
            for call in ('known_structure(read_memory)', 'anchors_match(read_memory)', 'known_file_size(module)'):
                self.assertIn(call, body)
        for path in PROXY.iterdir():
            if path.suffix in ('.h', '.cpp'):
                self.assertNotIn('fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab', path.read_text(errors='replace'), path.name)
                self.assertNotIn('0xfd,0xbf,0x34,0x18', path.read_text(errors='replace'), path.name)

    def test_identity_line_logs_laa_and_raw_hash(self):
        source = (PROXY / 'proxy_identity.cpp').read_text()
        self.assertIn('exe_sha256=%s exe_bytes=%llu exe_hash_us=%llu exe_laa=%d exe_max_app=%08lx', source)
        self.assertIn('GetSystemInfo(&system)', source)
        self.assertIn('object_trace::large_address_aware()', source)
        # attach_us keeps its old scope: its end stamp is taken before the executable hash starts.
        body = source[source.index('void log_identity('):source.index('void log_loaded_module(const wchar_t*')]
        self.assertLess(body.index('QueryPerformanceCounter(&end)'), body.index('QueryPerformanceCounter(&exe_begin)'))
        self.assertLess(body.index('QueryPerformanceCounter(&exe_begin)'), body.index('hash_file(exe,exe_hex,exe_bytes)'))
        self.assertLess(body.index('hash_file(exe,exe_hex,exe_bytes)'), body.index('QueryPerformanceCounter(&exe_end)'))

    def test_gate_cache_is_atomic(self):
        trace = (PROXY / 'object_trace.cpp').read_text()
        gate = trace[trace.index('bool verified_image()'):trace.index('bool executable_verified()')]
        self.assertIn('static std::atomic<int> cached{-1};', gate)

    def test_every_named_engine_global_is_anchored(self):
        anchored = {a[1] for a in ident.ANCHORS}
        text_section, data_section = ident.SECTIONS[0], ident.SECTIONS[2]
        lo = ident.IMAGE_BASE + ident.SECTIONS[1][2] + ident.SECTIONS[1][1]  # past .rdata
        hi = ident.IMAGE_BASE + data_section[2] + data_section[1]
        missing = {}
        for path in sorted(PROXY.iterdir()):
            if path.suffix not in ('.h', '.cpp') or path.name == HEADER.name:
                continue
            text = path.read_text(errors='replace')
            values = [int(v, 16) for v in re.findall(r'(?<![0-9A-Za-z_])0x([0-9a-fA-F]{6,8})\b', text)]
            values += [ident.IMAGE_BASE + int(v, 16) for v in re.findall(r'base\s*\+\s*0x([0-9a-fA-F]+)', text)]
            for value in values:
                if lo <= value < hi and value not in anchored and value not in NOT_READ:
                    missing.setdefault(hex(value), set()).add(path.name)
        self.assertEqual(missing, {})
        self.assertTrue(all(ident.IMAGE_BASE + text_section[2] <= a[0] < ident.IMAGE_BASE + text_section[2] + text_section[1]
                            for a in ident.ANCHORS))

    def test_every_gated_module_checks_its_sites_or_only_reads_anchored_globals(self):
        unchecked = set()
        for path in sorted(PROXY.iterdir()):
            if path.suffix not in ('.h', '.cpp') or path.name == 'object_trace.cpp':
                continue
            text = path.read_text(errors='replace')
            code = path.suffix == '.cpp' or path.name.endswith('_inc.h')  # other headers only mention the gate
            if code and 'executable_verified()' in text and not SITE_CHECK.search(text):
                unchecked.add(path.name)
        self.assertEqual(unchecked, READ_ONLY)

    def test_no_anchor_overlaps_a_patched_site(self):
        spans = []
        for path in PROXY.iterdir():
            if path.suffix not in ('.h', '.cpp') or path.name == HEADER.name:
                continue
            text = path.read_text(errors='replace')
            spans += [(int(a, 16), int(n)) for a, n in re.findall(r'\{\s*"[^"]+"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{[^}]*\}\s*,\s*(\d+)\s*,', text)]
            spans += [(int(a, 16), 8) for a in re.findall(r'\w*site_va\s*=\s*(0x[0-9a-fA-F]+)', text)]
        self.assertGreater(len(spans), 50)
        for anchor in ident.ANCHORS:
            length = len(ident.anchor_bytes(anchor))
            hits = [hex(a) for a, n in spans if a < anchor[0] + length and anchor[0] < a + n]
            self.assertEqual(hits, [], hex(anchor[0]))


def load_manage():
    spec = importlib.util.spec_from_file_location('exe_identity_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_apply_laa():
    spec = importlib.util.spec_from_file_location('apply_laa', ROOT / 'tools/analysis/apply_laa.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Launcher(unittest.TestCase):
    def dry_run(self, directory, exe_bytes):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').write_bytes(exe_bytes)
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game)]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
                code = 0
            except SystemExit as exit_error:
                code = exit_error.code
        return code, output.getvalue(), error.getvalue()

    def test_dry_run_records_an_unknown_executable_and_warns(self):
        with tempfile.TemporaryDirectory() as directory:
            data = synthetic(laa=True)
            code, output, error = self.dry_run(directory, data)
            self.assertEqual(code, 0, error)
            record = json.loads(output)['executable']
            self.assertEqual(record, {'sha256': ident.raw_sha256(data), 'bytes': ident.FILE_SIZE, 'laa': True,
                                      'checksum': '0x00000000', 'known': None, 'identity_ok': True})
            self.assertIn('Info: X3AP.exe SHA-256', error)
            self.assertNotIn('disables every hook module', error)

    def test_dry_run_warns_when_the_identity_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            data = bytearray(synthetic())
            struct.pack_into('<I', data, 0x120 + 8, ident.TIME_DATE_STAMP ^ 1)  # another build's link stamp
            code, output, error = self.dry_run(directory, bytes(data))
            self.assertEqual(code, 0, error)
            self.assertFalse(json.loads(output)['executable']['identity_ok'])
            self.assertIn('the structural gate disables every hook module', error)
            self.assertNotIn('Info:', error)

    def test_dry_run_accepts_a_non_pe_file(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.dry_run(directory, b'')
            self.assertEqual(code, 0, error)
            record = json.loads(output)['executable']
            self.assertIsNone(record['laa'])
            self.assertFalse(record['identity_ok'])

    def test_install_passes_the_record_to_the_manifest(self):
        module = load_manage()
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / 'game'
            game.mkdir()
            (game / 'X3AP.exe').write_bytes(synthetic())
            source = Path(directory) / 'd3d9.dll'
            source.write_bytes(b'dll')
            argv = ['manage.py', 'install', '--game-dir', str(game), '--dll-source', str(source)]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(module.media_package, 'install') as install, \
                    mock.patch.object(module, 'source_commit', return_value=('abc1234', 'marker')), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                module.main()
            provenance = install.call_args[0][2]
            self.assertEqual(provenance['executable']['sha256'], ident.raw_sha256(synthetic()))
            self.assertFalse(provenance['executable']['laa'])
            self.assertTrue(provenance['executable']['identity_ok'])

    @unittest.skipUnless(REAL.is_file(), 'installed executable not present')
    def test_known_variants_are_recorded_without_warning(self):
        module = load_manage()
        shipped = REAL.read_bytes()
        variants = {ident.SHIPPED_SHA256: shipped, ident.LAA_CLEARED_SHA256: ident.with_laa(shipped, on=False),
                    ident.NTCORE_4GB_SHA256: ident.with_laa(shipped, on=True, new_checksum=ident.NTCORE_4GB_CHECKSUM)}
        with tempfile.TemporaryDirectory() as directory:
            for sha, data in variants.items():
                path = Path(directory) / 'X3AP.exe'
                path.write_bytes(data)
                error = io.StringIO()
                with contextlib.redirect_stderr(error):
                    record = module.executable_record(path)
                self.assertEqual(record['sha256'], sha)
                self.assertEqual(record['known'], ident.KNOWN_SHA256[sha])
                self.assertTrue(record['identity_ok'])
                self.assertEqual(error.getvalue(), '')


class ApplyLaa(unittest.TestCase):
    def test_sets_the_bit_once_with_a_backup(self):
        tool = load_apply_laa()
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'X3AP.exe'
            exe.write_bytes(synthetic())
            result = tool.apply(exe, guard=lambda: [])
            self.assertEqual(result['action'], 'set')
            self.assertEqual(exe.read_bytes(), synthetic(laa=True))
            self.assertEqual((Path(directory) / 'X3AP.exe.x3m-pre-laa').read_bytes(), synthetic())
            self.assertEqual(tool.apply(exe, guard=lambda: [])['action'], 'none')
            self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), ['X3AP.exe', 'X3AP.exe.x3m-pre-laa'])

    def test_refuses_while_the_game_runs_or_on_a_foreign_image(self):
        tool = load_apply_laa()
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'X3AP.exe'
            exe.write_bytes(synthetic())
            with self.assertRaisesRegex(RuntimeError, 'running'):
                tool.apply(exe, guard=lambda: ['123 X3AP.exe'])
            foreign = bytearray(synthetic())
            foreign[ident.ANCHORS[0][0] - ident.IMAGE_BASE - 0x1000 + 0x400] ^= 1
            exe.write_bytes(bytes(foreign))
            with self.assertRaisesRegex(RuntimeError, 'not the known'):
                tool.apply(exe, guard=lambda: [])
            self.assertEqual(exe.read_bytes(), bytes(foreign))
            self.assertFalse((Path(directory) / 'X3AP.exe.x3m-pre-laa').exists())


X3TC = REAL.with_name('X3TC.exe')


@unittest.skipUnless(X3TC.is_file(), 'X3TC.exe not present')
class OtherGameExecutable(unittest.TestCase):
    def test_x3tc_is_refused(self):
        """X3: Terran Conflict's executable in the same directory (read only) fails five
        structural fields and every anchor."""
        checks = ident.structure(X3TC)
        self.assertEqual({k for k, ok in checks.items() if not ok},
                         {'size', 'time_date_stamp', 'image_size', 'entry_point', 'sections'})
        self.assertEqual(sum(ident.anchors(X3TC).values()), 0)
        self.assertEqual(len(ident.ANCHORS), 41)
        self.assertFalse(ident.identity_ok(X3TC))


@unittest.skipUnless(REAL.is_file(), 'installed executable not present')
class InstalledExecutable(unittest.TestCase):
    def setUp(self):
        self.data = REAL.read_bytes()

    def test_identity_and_provenance(self):
        self.assertTrue(ident.identity_ok(self.data))
        info = ident.info(self.data)
        self.assertEqual((info['sha256'], info['laa'], info['checksum'], info['identity_digest']),
                         (ident.SHIPPED_SHA256, True, '0x00000000', ident.IDENTITY_DIGEST))

    def test_variant_hashes(self):
        cleared = ident.with_laa(self.data, on=False)
        self.assertEqual(ident.raw_sha256(cleared), ident.LAA_CLEARED_SHA256)
        self.assertEqual(ident.pe_checksum(self.data), ident.NTCORE_4GB_CHECKSUM)
        ntcore = ident.with_laa(self.data, on=True, new_checksum=ident.pe_checksum(self.data))
        self.assertEqual(ident.raw_sha256(ntcore), ident.NTCORE_4GB_SHA256)
        for data in (cleared, ntcore):
            self.assertTrue(ident.identity_ok(data))
            self.assertEqual(ident.identity_digest(data), ident.IDENTITY_DIGEST)

    def test_unknown_hash_passes_and_a_changed_site_is_refused_by_its_verifier(self):
        import verify_chase_camera_site as camera
        rsrc = ident.SECTIONS[3]
        changed = bytearray(self.data)
        changed[rsrc[4] + rsrc[3] - 1] ^= 0xff
        self.assertTrue(ident.identity_ok(bytes(changed)))
        self.assertIsNone(ident.info(bytes(changed))['known'])
        self.assertEqual(camera.verify(bytes(changed))['result'], 'PASS')
        site = bytearray(self.data)
        site[camera.SITE_VA - ident.IMAGE_BASE - 0x1000 + 0x400 + 9] ^= 1
        report = camera.verify(bytes(site))
        self.assertTrue(report['checks']['exe_identity']['ok'])
        self.assertFalse(report['checks']['site_bytes']['ok'])
        self.assertEqual(report['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
