"""Session identity header (docs/architecture/platform-portability.md): the
`proxy_identity` / `proxy_options` parser and the CMake commit fragment.
Host only; never executes Wine and never builds the DLL."""
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import proxy_identity as identity  # noqa: E402

DIGEST = 'a' * 64
OTHER = 'b' * 64
COMMIT = 'e15d4990' + '0' * 32
LINE = (f'proxy_identity sha256={DIGEST} bytes=3211264 path=C:\\X3\\d3d9.dll '
        f'manifest_sha256={OTHER} source_commit={COMMIT} attach_us=4210')


class SessionStartLines(unittest.TestCase):
    """Format and placement of the two session-start lines that carry the
    session's clocks and loaded-module identity. Source text only; the values
    come from documented Win32 calls at attach and at the first device creation."""

    def test_clock_anchor_precedes_the_identity_header(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('log("clock_anchor utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ qpc=%llu '
                      'qpc_frequency=%llu local_offset_min=%ld"', capture)
        self.assertLess(capture.index('log("clock_anchor'), capture.index('proxy_identity::log_identity(module)'))
        # One UTC reading next to one counter reading, with the documented
        # fallback when the precise call is missing.
        anchor = capture[capture.index('log("capture_dir=%s'):capture.index('proxy_identity::log_identity(module)')]
        self.assertIn('GetSystemTimePreciseAsFileTime', anchor)
        self.assertIn('GetSystemTimeAsFileTime(&utc)', anchor)
        self.assertIn('GetTimeZoneInformation(&zone)', anchor)

    def test_loaded_module_line_and_its_two_call_sites(self):
        source = (ROOT / 'src/proxy/proxy_identity.cpp').read_text()
        self.assertIn('log("loaded_module name=%s path=%s size=%llu sha256=%s hash_us=%llu '
                      'image_size=%lu stamp=%08lx exports=%lu wine_builtin=%d"', source)
        # The hash prefix is the first 16 hex digits of the file's SHA-256, and
        # the module reference is taken and released around the file read.
        self.assertIn('hex=hash_file(path,full,bytes)&&full.size()==64?full.substr(0,16):std::string("unavailable")', source)
        self.assertIn('GetModuleHandleExW(0,name,&module)', source)
        self.assertIn('if(module) FreeLibrary(module);', source)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('x3m::proxy_identity::log_loaded_module(backend, "d3d9.dll");', loader)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('if(!helper_module_logged.exchange(true)){LightCallBoundary boundary;'
                      'proxy_identity::log_loaded_module(L"d3dx9_37.dll");}', capture)
        self.assertEqual(capture.count('proxy_identity::log_loaded_module('), 1)
        # The 3.8 MB hash runs before the hook mutex and before the HookGuard's
        # frame-timing scope, so it blocks no other hook and lands in no window.
        body = capture[capture.index('HRESULT WINAPI create_device('):]
        self.assertLess(body.index('proxy_identity::log_loaded_module(L"d3dx9_37.dll")'), body.index('HookGuard lock;'))
        self.assertLess(body.index('proxy_identity::log_loaded_module(L"d3dx9_37.dll")'), body.index('CpuCallBoundary cpu;'))


class MappedImageFields(unittest.TestCase):
    """The `loaded_module` line also reports what the mapped image says about
    itself, because under Wine a builtin module keeps the native file's
    FullDllName and on-disk size: path and size alone cannot tell them apart
    (measured with a probe EXE, docs/architecture/effect-pass-replay.md).
    Source text only; the values come from documented PE structures read at the
    module base, never from the file on disk."""

    def setUp(self):
        self.source = (ROOT / 'src/proxy/proxy_identity.cpp').read_text()
        start = self.source.index('ModuleImage module_image(HMODULE module)')
        self.reader = self.source[start:self.source.index('void log_module(', start)]

    def test_field_order_after_the_existing_fields(self):
        line = re.search(r'log\("loaded_module ([^"]*)"', self.source).group(1)
        self.assertEqual(line.split(),
                         ['name=%s', 'path=%s', 'size=%llu', 'sha256=%s', 'hash_us=%llu',
                          'image_size=%lu', 'stamp=%08lx', 'exports=%lu', 'wine_builtin=%d'])
        # The values are passed in the same order as the fields.
        arguments = self.source[self.source.index('wine_builtin=%d"'):self.source.index('image.wine_builtin);')]
        self.assertLess(arguments.index('image.size'), arguments.index('image.stamp'))
        self.assertLess(arguments.index('image.stamp'), arguments.index('image.exports'))

    def test_every_header_read_is_bounds_checked_before_it_is_dereferenced(self):
        self.assertIn('if(!module) return info;', self.reader)
        self.assertIn("dos->e_magic!=IMAGE_DOS_SIGNATURE", self.reader)
        self.assertIn('nt->Signature!=IMAGE_NT_SIGNATURE', self.reader)
        self.assertIn('nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC', self.reader)
        # e_lfanew is checked on both sides before the NT headers are formed.
        self.assertIn('lfanew<static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))', self.reader)
        self.assertIn('lfanew>static_cast<LONG>(0x1000u-sizeof(IMAGE_NT_HEADERS32))', self.reader)
        self.assertLess(self.reader.index('e_lfanew'), self.reader.index('base+lfanew'))
        # The export directory is read only when NumberOfRvaAndSizes covers the
        # entry and the whole structure lies inside SizeOfImage.
        self.assertIn('nt->OptionalHeader.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_EXPORT', self.reader)
        self.assertIn('directory.VirtualAddress<=info.size-sizeof(IMAGE_EXPORT_DIRECTORY)', self.reader)
        self.assertIn('NumberOfFunctions', self.reader)

    def test_marker_rule_stays_inside_the_dos_header_area(self):
        self.assertIn('static const char marker[]="Wine builtin DLL";', self.reader)
        self.assertIn('const std::size_t marker_size=sizeof(marker)-1;', self.reader)
        self.assertIn('for(std::size_t at=0x40;at+marker_size<=0x80;++at)', self.reader)
        self.assertEqual(len('Wine builtin DLL'), 16)
        # The rule the loop implements, on synthetic buffers: the 16-byte marker
        # anywhere in [0x40, 0x80) sets the flag; a marker that starts before
        # 0x40 or would run past 0x80 does not.
        def scan(buffer):
            return any(buffer[at:at + 16] == b'Wine builtin DLL' for at in range(0x40, 0x80 - 16 + 1))
        self.assertTrue(scan(bytearray(0x40) + b'Wine builtin DLL' + bytearray(0x40)))
        self.assertTrue(scan(bytearray(0x70) + b'Wine builtin DLL' + bytearray(0x10)))
        self.assertFalse(scan(bytearray(0x71) + b'Wine builtin DLL' + bytearray(0x10)))
        self.assertFalse(scan(bytearray(0x30) + b'Wine builtin DLL' + bytearray(0x50)))
        self.assertFalse(scan(bytearray(0x100)))

    def test_no_file_is_opened_for_the_image_fields(self):
        for call in ('CreateFileW', 'ReadFile', 'IsBadReadPtr', '__try'):
            self.assertNotIn(call, self.reader)
        # The image is read while the caller still holds the module reference.
        body = self.source[self.source.index('void log_module('):self.source.index('std::string options()')]
        self.assertLess(body.index('image=module_image(module);'), body.index('GetModuleFileNameW'))


class ProxyEnvironmentLine(unittest.TestCase):
    """`proxy_environment`: the emulation and Wine/CrossOver variables the
    launcher sets for the child, recorded in-process so a run can show which of
    them arrived (run 37 set FEX_TSOENABLED and WINE_D3D_CONFIG with nothing to
    confirm them). Source text plus the filter/format rule on a synthetic
    environment block; the values come from GetEnvironmentStringsW only."""

    LIMIT = 200
    ELLIPSIS = '\u2026'

    def setUp(self):
        self.source = (ROOT / 'src/proxy/proxy_identity.cpp').read_text()

    @staticmethod
    def select(name):
        upper = name.upper()
        return upper.startswith('FEX_') or upper.startswith('WINE') or upper.startswith('CX_')

    @classmethod
    def build(cls, block):
        """The rule collect()/environment() implements in
        src/proxy/proxy_identity.cpp, on a synthetic NUL-separated block."""
        entries = []
        for entry in block.split('\0'):
            if not entry or entry.startswith('='):
                continue
            name, _, value = entry.partition('=')
            if not cls.select(name):
                continue
            value = ''.join(c if 0x21 <= ord(c) <= 0x7e else '_' for c in value)
            if len(value) > cls.LIMIT:
                value = value[:cls.LIMIT] + cls.ELLIPSIS
            entries.append(f'{name}={value}')
        entries.sort()
        return 'proxy_environment' + ''.join(' ' + e for e in entries) + f' count={len(entries)}'

    def test_filter_sort_and_count_on_a_synthetic_block(self):
        block = '\0'.join(['WINEMSYNC=1', 'PATH=C:\\windows', 'FEX_TSOENABLED=0',
                           'X3M_TAA=1', 'CX_ROOT=/opt/cxoffice', 'WINE_D3D_CONFIG=csmt=0x0',
                           '=C:=C:\\X3', 'HOME=/Users/x'])
        self.assertEqual(self.build(block),
                         'proxy_environment CX_ROOT=/opt/cxoffice FEX_TSOENABLED=0 '
                         'WINEMSYNC=1 WINE_D3D_CONFIG=csmt=0x0 count=4')
        # X3M_* stays on proxy_options; nothing unrelated is listed.
        self.assertEqual(self.build('X3M_TAA=1\0PATH=/bin'), 'proxy_environment count=0')

    def test_case_insensitive_names_and_empty_value(self):
        self.assertEqual(self.build('wine_d3d_config=\0Fex_X87ReducedPrecision=1'),
                         'proxy_environment Fex_X87ReducedPrecision=1 wine_d3d_config= count=2')

    def test_long_value_truncated_with_an_ellipsis(self):
        overrides = 'd3d9=n,b;' * 40  # 360 characters
        value = self.build(f'WINEDLLOVERRIDES={overrides}').split(' ')[1].split('=', 1)[1]
        self.assertEqual(len(value), self.LIMIT + 1)
        self.assertTrue(value.endswith(self.ELLIPSIS))
        self.assertEqual(value[:self.LIMIT], overrides[:self.LIMIT])
        # Exactly at the limit nothing is appended.
        self.assertEqual(self.build('WINEDEBUG=' + 'a' * self.LIMIT),
                         'proxy_environment WINEDEBUG=' + 'a' * self.LIMIT + ' count=1')

    def test_non_printable_becomes_underscore_so_no_field_splits(self):
        self.assertEqual(self.build('WINEDEBUG=warn +tid\0WINEESYNC=\u00e9'),
                         'proxy_environment WINEDEBUG=warn_+tid WINEESYNC=_ count=2')

    def test_source_emits_the_line_once_after_proxy_options(self):
        self.assertEqual(self.source.count('log("proxy_environment%s"'), 1)
        self.assertEqual(self.source.count('log("proxy_options%s"'), 1)
        self.assertLess(self.source.index('log("proxy_options%s"'),
                        self.source.index('log("proxy_environment%s"'))
        # Both lines come from one filtered pass over the documented block.
        self.assertIn('return _wcsnicmp(entry,L"FEX_",4)==0||_wcsnicmp(entry,L"WINE",4)==0||'
                      '_wcsnicmp(entry,L"CX_",3)==0;', self.source)
        self.assertIn('constexpr std::size_t kValueLimit=200;', self.source)
        self.assertIn(r'value+="\xe2\x80\xa6"', self.source)
        self.assertIn('LPWCH block=GetEnvironmentStringsW();', self.source)
        self.assertEqual(self.source.count('GetEnvironmentStringsW()'), 1)
        self.assertEqual(self.source.count('FreeEnvironmentStringsW(block);'), 1)
        self.assertIn('line+=" count="+std::to_string(entries.size());', self.source)
        # X3M_* option values are never truncated.
        self.assertIn('collect(is_option,std::string::npos)', self.source)
        self.assertIn('collect(is_environment,kValueLimit)', self.source)

    def test_a_failure_still_yields_a_count(self):
        body = self.source[self.source.index('void log_identity('):]
        self.assertIn('environment_line=" count=0"', body[:body.index('catch')])
        self.assertIn('environment_line=" count=0";', body[body.index('catch'):])


class ParseIdentity(unittest.TestCase):
    def test_full_line(self):
        fields = identity.parse_identity(LINE)
        self.assertEqual(fields['sha256'], DIGEST)
        self.assertEqual(fields['bytes'], 3211264)
        self.assertEqual(fields['path'], 'C:\\X3\\d3d9.dll')
        self.assertEqual(fields['manifest_sha256'], OTHER)
        self.assertEqual(fields['source_commit'], COMMIT)
        self.assertEqual(fields['attach_us'], 4210)
        self.assertFalse(fields['dirty'])

    def test_attach_us_optional_and_checked(self):
        without = identity.parse_identity(LINE.replace(' attach_us=4210', ''))
        self.assertNotIn('attach_us', without)
        with self.assertRaises(identity.ProxyIdentityError):
            identity.parse_identity(LINE.replace('attach_us=4210', 'attach_us=soon'))

    def test_unavailable_none_and_unknown(self):
        fields = identity.parse_identity(
            'proxy_identity sha256=unavailable bytes=0 path=unknown manifest_sha256=none source_commit=unknown')
        self.assertEqual(fields['sha256'], 'unavailable')
        self.assertEqual(fields['manifest_sha256'], 'none')
        self.assertEqual(fields['bytes'], 0)
        self.assertFalse(fields['dirty'])

    def test_dirty_suffix(self):
        fields = identity.parse_identity(LINE.replace(COMMIT, COMMIT + '-dirty'))
        self.assertTrue(fields['dirty'])
        self.assertEqual(fields['source_commit'], COMMIT + '-dirty')

    def test_other_lines_ignored(self):
        for line in ('screen_emission_mode requested=1 enabled=1 hdr=1',
                     'capture_dir=C:\\X3\\x3-modern-captures source=game',
                     'proxy_identity_extra sha256=x'):
            self.assertIsNone(identity.parse_identity(line))
            self.assertIsNone(identity.parse_options(line))

    def test_malformed(self):
        for line in (LINE.replace('bytes=3211264', 'bytes=many'),
                     LINE.replace(f'sha256={DIGEST}', 'sha256=deadbeef'),
                     LINE.replace(f'manifest_sha256={OTHER}', 'manifest_sha256=nope'),
                     LINE.replace(f'source_commit={COMMIT}', 'source_commit=zzzz'),
                     LINE.replace(f'manifest_sha256={OTHER} ', ''),
                     'proxy_identity sha256'):
            with self.assertRaises(identity.ProxyIdentityError):
                identity.parse_identity(line)


class ParseOptions(unittest.TestCase):
    def test_variables(self):
        options = identity.parse_options('proxy_options X3M_MOTION_OUTPUT=1 X3M_OWNERSHIP=1 X3M_TAA_K=0.9')
        self.assertEqual(options, {'X3M_MOTION_OUTPUT': '1', 'X3M_OWNERSHIP': '1', 'X3M_TAA_K': '0.9'})

    def test_empty_line_and_empty_value(self):
        self.assertEqual(identity.parse_options('proxy_options'), {})
        self.assertEqual(identity.parse_options('proxy_options X3M_CAMERA='), {'X3M_CAMERA': ''})

    def test_rejects_unrelated_or_duplicate(self):
        for line in ('proxy_options PATH=/usr/bin', 'proxy_options X3M_TAA', 'proxy_options X3M_TAA=1 X3M_TAA=0'):
            with self.assertRaises(identity.ProxyIdentityError):
                identity.parse_options(line)

    def test_case_insensitive_names(self):
        self.assertEqual(identity.parse_options('proxy_options x3m_taa=1'), {'x3m_taa': '1'})

    def test_sorted_names_preserved(self):
        options = identity.parse_options('proxy_options X3M_A=1 X3M_B=2 X3M_C=3')
        self.assertEqual(list(options), sorted(options))


class ScanLog(unittest.TestCase):
    def test_scan_session_log(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'session.log'
            log.write_text('capture_dir=C:\\X3\\x3-modern-captures source=game\n'
                           + LINE + '\nproxy_options X3M_MOTION_OUTPUT=1\n'
                           'motion_capture_mode requested=0 enabled=0\n', encoding='utf-8')
            result = identity.scan_log(log)
            self.assertEqual(result.identity['sha256'], DIGEST)
            self.assertEqual(result.options, {'X3M_MOTION_OUTPUT': '1'})
            self.assertEqual(result.malformed, [])

    def test_scan_log_without_header(self):
        self.assertEqual(identity.scan(['motion_capture_mode requested=0 enabled=0']), (None, None, []))

    def test_scan_does_not_raise_on_malformed_lines(self):
        result = identity.scan(['proxy_identity sha256=deadbeef bytes=1 path=x manifest_sha256=none source_commit=unknown',
                                'proxy_options X3M_TAA'])
        self.assertIsNone(result.identity)
        self.assertIsNone(result.options)
        self.assertEqual(len(result.malformed), 2)


@unittest.skipUnless(shutil.which('cmake') and shutil.which('i686-w64-mingw32-g++'),
                     'requires cmake and the MinGW i686 cross compiler')
class CMakeCommitFragment(unittest.TestCase):
    """Configure a fresh build directory and check the generated fragment."""

    def test_generated_header_carries_head(self):
        head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
        dirty = bool(subprocess.run(['git', 'status', '--porcelain', '--untracked-files=normal', '--',
                                     'src', 'cmake', 'tools', 'CMakeLists.txt'],
                                    cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip())
        with tempfile.TemporaryDirectory() as directory:
            # This test configures only to inspect commit metadata; it never
            # builds the media consumer. Supply named SDK placeholders for
            # CMake's required path check, with a hard error on any accidental
            # compilation. Production still requires the reviewed real SDK.
            lav_headers = Path(directory) / 'configure-only-lav'
            lav_headers.mkdir()
            for name in ('LAVVideoSettings.h', 'LAVSplitterSettings.h'):
                (lav_headers / name).write_text('#error Configuration-only SDK placeholder must not be compiled\n')
            configure = subprocess.run(
                ['cmake', '-S', str(ROOT), '-B', directory,
                 f'-DCMAKE_TOOLCHAIN_FILE={ROOT / "cmake/mingw-i686.cmake"}', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                 f'-DX3M_LAV_INCLUDE_DIR={lav_headers}',
                 f'-DPython3_EXECUTABLE={sys.executable}'],
                capture_output=True, text=True)
            self.assertEqual(configure.returncode, 0, configure.stderr[-2000:])
            header = Path(directory) / 'generated/x3m_source_commit_inc.h'
            self.assertTrue(header.is_file(), 'x3m_source_commit_inc.h was not generated')
            text = header.read_text(encoding='utf-8')
        match = re.search(r'#define X3M_SOURCE_COMMIT "([^"]*)"', text)
        self.assertIsNotNone(match, text)
        self.assertEqual(match.group(1), head + ('-dirty' if dirty else ''))
        self.assertTrue(identity.COMMIT.match(match.group(1)))

    def test_writer_only_rewrites_on_change(self):
        """The build-time refresh must not touch the header when the value is
        unchanged, otherwise every build relinks the DLL."""
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'x3m_source_commit_inc.h'
            command = [sys.executable, str(ROOT / 'tools/build/write_source_commit.py'),
                       '--root', str(ROOT), '--output', str(header)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            first = header.stat().st_mtime_ns
            text = header.read_text(encoding='utf-8')
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(header.stat().st_mtime_ns, first)
            self.assertEqual(header.read_text(encoding='utf-8'), text)

    def test_writer_reports_unknown_without_git(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'x3m_source_commit_inc.h'
            completed = subprocess.run([sys.executable, str(ROOT / 'tools/build/write_source_commit.py'),
                                        '--root', directory, '--output', str(header)], capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr[-500:])
            self.assertIn('"unknown"', header.read_text(encoding='utf-8'))


class BuiltDllMarker(unittest.TestCase):
    """tools/manage.py reads the DLL's own compiled-in commit for the manifest."""

    def setUp(self):
        spec = importlib.util.spec_from_file_location('x3m_manage', ROOT / 'tools/manage.py')
        self.manage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.manage)

    def test_marker_scan_on_build_dll(self):
        dll = ROOT / 'build/d3d9.dll'
        if not dll.is_file():
            self.skipTest('build/d3d9.dll not present')
        commit = self.manage.dll_source_commit(dll)
        self.assertIsNotNone(commit, 'built DLL carries no X3M_SOURCE_COMMIT marker')
        self.assertTrue(identity.COMMIT.match(commit), commit)
        self.assertEqual(self.manage.source_commit(dll), (commit, 'dll'))

    def test_missing_marker_falls_back_to_launcher(self):
        with tempfile.TemporaryDirectory() as directory:
            other = Path(directory) / 'd3d9.dll'
            other.write_bytes(b'MZ' + b'\0' * 64)
            commit, origin = self.manage.source_commit(other)
            self.assertEqual(origin, 'launcher')
            self.assertTrue(identity.COMMIT.match(commit), commit)


if __name__ == '__main__':
    unittest.main()
