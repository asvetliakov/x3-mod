"""DLL loader of <game>/x3m/fog-families.bin (src/renderer/fog_field_assets.cpp) on the host.

The fixture (verification/probe/fog_family_file_fixture.cpp) is compiled with the host compiler
against a one-row stand-in for the generated metadata header: the compiled packets are not under
test here, only the file path. Its i686 build is the CMake target fog_family_file_fixture.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import fog_families as ff  # noqa: E402
from source_text import source_text

FIXTURE = ROOT / 'verification/probe/fog_family_file_fixture.cpp'
STUB = ('#pragma once\nnamespace x3m::renderer::fog_field {\n'
        'constexpr ProfileInfo kProfiles[] = {{Profile::Bluewell, 1u, 1560u, 1430u, 8u, 17846400u, 2.5e-6f, 0x1ull, 21101u}};\n}\n')
FNV_OFFSET, FNV_PRIME, MASK = 14695981039346656037, 1099511628211, (1 << 64) - 1
TEXELS = 1560 * 1430


def packet(profile, seed, literal=4):
    """A valid X3FOGPK v1 packet: `literal` nonzero texels, then one zero run."""
    texels = b''.join(struct.pack('<H', 0x3000 + ((seed + t) & 0x3ff)) for t in range(literal * 4))
    checksum = FNV_OFFSET
    for byte in texels:
        checksum = ((checksum ^ byte) * FNV_PRIME) & MASK
    checksum = (checksum * pow(FNV_PRIME, (TEXELS - literal) * 8, 1 << 64)) & MASK
    payload = struct.pack('<I', 0x80000000 | literal) + texels + struct.pack('<I', TEXELS - literal)
    header = ff.PACKET_HEADER.pack(b'X3FOGPK', 1, 56, profile, 1, 1560, 1430, 8, TEXELS * 8, 2, checksum, 0)
    return dict(bytes=header + payload, decoded_fnv1a=checksum, profile_id=profile, decoded_sha256='00' * 32)


def valid_file():
    rows, packets = [], []
    for index, name in enumerate(('zza', 'zzb')):
        rows.append(dict(name=name, profile_id=ff.profile_id(name), packet=index, base_sigma=2.5e-6, occupancy=.12,
                         chroma=[.2, .4, 1.0], colours=[[.5, .5, 1.0]] * 4, flags=0))
        packets.append(packet(ff.profile_id(name), 7 + 31 * index, 4 + index))
    return ff.build_file(rows, packets)


def refresh(data):
    data = bytearray(data)
    table_bytes = struct.unpack_from('<I', data, 40)[0]
    struct.pack_into('<QQ', data, 48, ff.fnv1a64(bytes(data[64:64 + table_bytes])), len(data))
    return bytes(data)


class FogFamilyFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        (cls.root / 'stub').mkdir()
        (cls.root / 'stub/fog_field_assets_metadata_inc.h').write_text(STUB)
        compiler = shutil.which('clang++') or shutil.which('c++')
        cls.exe = cls.root / 'fog-family-file-fixture'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/renderer'),
                        '-I', str(cls.root / 'stub'), str(ROOT / 'src/renderer/fog_field_assets.cpp'), str(FIXTURE),
                        '-o', str(cls.exe)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def probe(self, value):
        """The process-wide loader through X3M_FOG_FAMILIES=value: (table, rows, decodes)."""
        env = dict(os.environ, X3M_FOG_FAMILIES=str(value))
        run = subprocess.run([str(self.exe), '--probe'], env=env, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS fog_family_file probe', run.stdout)
        fields = lambda line: dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)  # noqa: E731
        table = next(fields(l) for l in run.stdout.splitlines() if l.startswith('TABLE '))
        rows = [fields(l) for l in run.stdout.splitlines() if l.startswith('ROW ')]
        decodes = {l.split()[1]: fields(l) for l in run.stdout.splitlines() if l.startswith('DECODE ')}
        return table, rows, decodes

    def write(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_self_test_cases(self):
        cases = self.root / 'cases'
        cases.mkdir()
        run = subprocess.run([str(self.exe), '--self-test', str(cases)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS fog_family_file cases=68 compiled=14', run.stdout)
        for reason in ('truncated_header', 'bad_magic', 'version', 'header_size', 'recipe', 'family_count', 'packet_count',
                       'table_past_eof', 'table_checksum', 'file_size'):
            self.assertIn(f'status=rejected reason={reason}', run.stdout)
        for reason in ('name_duplicate', 'name_compiled', 'name', 'profile_id', 'sigma', 'occupancy', 'chroma', 'colour',
                       'packet_offset', 'packet_size', 'packet_header'):
            self.assertRegex(run.stdout, rf'status=loaded row=\d reason={reason}\n')
        self.assertEqual(run.stdout.count('status=decode_failed '), 9)
        self.assertIn('CASE allocation_retry status=decode_failed_twice reason=allocation', run.stdout)
        for case in ('duplicate_after_disabled', 'packet_compiled_id', 'name_quote', 'name_backslash'):
            self.assertIn(f'CASE {case} status=loaded', run.stdout)

    def test_tool_written_file_loads_and_decodes(self):
        path = self.write('valid.bin', valid_file())
        table, rows, decodes = self.probe(path)
        self.assertEqual((table['status'], table['reason'], table['families'], table['packets'], table['rows_disabled']),
                         ('loaded', 'ok', '2', '2', '0'))
        self.assertEqual([(r['name'], int(r['profile'])) for r in rows], [('zza', ff.profile_id('zza')), ('zzb', ff.profile_id('zzb'))])
        for name in ('zza', 'zzb'):
            self.assertEqual((decodes[name]['status'], decodes[name]['bytes'], decodes[name]['match'], decodes[name]['found_after']),
                             ('ok', '17846400', '1', '1'))
        self.assertEqual(ff.read_file(path)['status'], 'loaded')

    def test_python_validator_and_dll_loader_agree(self):
        valid = valid_file()
        row0, prow0 = 64, 64 + 2 * 112

        def edit(offset, fmt, value, fresh=False):
            data = bytearray(valid)
            struct.pack_into(fmt, data, offset, value)
            return refresh(data) if fresh else bytes(data)
        quoted = bytearray(valid)
        quoted[row0:row0 + 32] = b'z"a'.ljust(32, b'\0')
        struct.pack_into('<I', quoted, row0 + 32, ff.fnv1a32(b'z"a') | 0x10000)
        named = bytearray(valid)
        named[row0:row0 + 32] = b'bluewell'.ljust(32, b'\0')
        struct.pack_into('<I', named, row0 + 32, ff.profile_id('bluewell'))
        packet0 = struct.unpack_from('<Q', valid, prow0)[0]
        cases = {
            'bad_magic': edit(0, '<8s', b'X3FOGFAX'), 'version': edit(8, '<I', 2), 'recipe': edit(16, '<I', 2),
            'family_count': edit(20, '<I', 257), 'table_checksum': edit(row0 + 40, '<f', 3e-6),
            'file_size': valid + b'\0', 'sigma': edit(row0 + 40, '<f', float('nan'), True),
            'name_compiled': refresh(named), 'name': refresh(quoted), 'packet_offset': edit(prow0, '<Q', len(valid), True),
            'packet_header': edit(packet0 + 16, '<I', 5), 'occupancy': edit(row0 + 44, '<f', .6, True),
            'profile_id': edit(row0 + 32, '<I', 3, True), 'flags': edit(row0 + 108, '<I', 8, True),
            'ok': ff.build_file([], []), 'packet_count': edit(20, '<I', 0),  # the empty table loads; 0 families need 0 packets
        }
        for reason, data in cases.items():
            with self.subTest(reason=reason):
                path = self.write(f'parity-{reason}.bin', data)
                python = ff.read_file(path)
                table, rows, _ = self.probe(path)
                self.assertEqual(table['status'], python['status'])
                self.assertEqual(table['reason'], python['reason'])
                self.assertEqual([r['disabled'] for r in rows], [r['disabled'] or '-' for r in python['rows']])
                self.assertIn(reason, [table['reason']] + [r['disabled'] for r in rows])

    def test_packet_corruption_disables_that_row_only(self):
        data = bytearray(valid_file())
        packet0 = struct.unpack_from('<Q', data, 64 + 2 * 112)[0]
        data[packet0 + 56 + 4] ^= 1  # a literal byte: the decoded checksum fails at the switch
        table, rows, decodes = self.probe(self.write('corrupt-payload.bin', bytes(data)))
        self.assertEqual((table['status'], table['rows_disabled']), ('loaded', '0'))
        self.assertEqual((decodes['zza']['status'], decodes['zza']['found_after']), ('packet_checksum', '0'))
        self.assertEqual((decodes['zzb']['status'], decodes['zzb']['found_after']), ('ok', '1'))

    def test_environment_switches_absent_directory_and_oversized(self):
        for value in ('0', 'none'):
            table, rows, _ = self.probe(value)
            self.assertEqual((table['status'], table['reason'], rows), ('disabled', 'env_disabled', []))
        self.assertEqual(self.probe(self.root / 'missing.bin')[0]['status'], 'absent')
        folder = self.root / 'a-directory'
        folder.mkdir()
        self.assertEqual(self.probe(folder)[0]['reason'], 'open_failed')
        big = self.root / 'oversized.bin'
        with big.open('wb') as stream:
            stream.write(valid_file()[:64])
            stream.truncate(ff.MAX_FILE + 1)  # sparse
        try:
            self.assertEqual(self.probe(big)[0]['reason'], 'oversized')
            self.assertEqual(ff.read_file(big)['reason'], 'oversized')
        finally:
            big.unlink()

    def test_i686_fixture_cross_compiles_when_available(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('MinGW cross compiler unavailable')
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
                        '-mincoming-stack-boundary=2', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-I', str(ROOT / 'src/renderer'),
                        '-I', str(self.root / 'stub'), str(ROOT / 'src/renderer/fog_field_assets.cpp'), str(FIXTURE), '-static',
                        '-o', str(self.root / 'fog_family_file_fixture.exe')], check=True, capture_output=True, text=True)
        self.assertTrue((self.root / 'fog_family_file_fixture.exe').stat().st_size > 0)

    def test_production_wiring(self):
        cmake = source_text(ROOT / 'CMakeLists.txt')
        self.assertIn('add_executable(fog_family_file_fixture verification/probe/fog_family_file_fixture.cpp src/renderer/fog_field_assets.cpp)', cmake)
        fog = source_text(ROOT / 'src/proxy/motion_output_fog_inc.h')
        self.assertIn('renderer::fog_field::load_family_table()', fog)
        self.assertIn('fog_enabled_ && !fog_disabled_,\n                                 renderer::fog_field::family_table());', fog)
        self.assertIn('prepared == renderer::FogPass::field_row_disabled', fog)
        passes = source_text(ROOT / 'src/renderer/fog_pass.cpp')
        self.assertIn('file_family?fog_field::decode_family(profile,atlas_bytes_):fog_field::decode_from_resource(module,profile,atlas_bytes_)', passes)
        assets = source_text(ROOT / 'src/renderer/fog_field_assets.cpp')
        for api in ('CreateFileW', 'GetFileSizeEx', 'SetFilePointerEx', 'ReadFile', 'GetModuleFileNameW', 'x3m::config::get(L"X3M_FOG_FAMILIES"'):
            self.assertIn(api, assets)


if __name__ == '__main__':
    unittest.main()
