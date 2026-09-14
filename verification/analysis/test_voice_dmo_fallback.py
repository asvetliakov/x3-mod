"""Host checks for the voice DMO fallback hook (src/proxy/voice_dmo_fallback.cpp).

Pins the GUID constants and the failure condition the hook keys on, the
environment gate and its launcher delivery, the SiteSpec against the verifier's
independent ledger, and (when the installed EXE is present) the read-only
instruction/ABI qualification of the site. No Wine, no game.
"""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import verify_voice_dmo_site as probe
from verification.analysis.test_voice_decoder_launch import VoiceDecoderLaunchOption

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'src/proxy/voice_dmo_fallback.cpp').read_text()
REPLICA = (ROOT / 'verification/probe/voice_startup_replica.cpp').read_text()
CXX = 'i686-w64-mingw32-g++'
OBJDUMP = 'i686-w64-mingw32-objdump'
# The production compile flags (CMakeLists.txt) that matter for code generation.
FLAGS = ('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX')
GUID_RE = '{0x%s,0x%s,0x%s,{%s}}'


def guid_pattern(text):
    """dmodshow-style GUID text -> the initializer the source must contain."""
    a, b, c, d, e = text.split('-')
    tail = ','.join('0x' + (d + e)[i:i + 2] for i in range(0, 16, 2))
    return GUID_RE % (a, b, c, tail)


class Constants(unittest.TestCase):
    def test_guids_match_the_documented_values(self):
        for name, text in (('kIID_IDMOWrapperFilter', '52d6f586-9f0f-4824-8fc8-e32ca04930c2'),
                           ('kCLSID_CWMADecMediaObject', '2eeb4adf-4578-4d10-bca7-bb955f56320a'),
                           ('kDMOCATEGORY_AUDIO_DECODER', '57f2db8b-e6bb-4513-9d43-dcd2a6593125')):
            self.assertIn('constexpr GUID ' + name + '=' + guid_pattern(text) + ';', SOURCE)
        self.assertNotIn('874131cb', SOURCE.lower().replace('874131cb-4ecc-443b-8948-746b89595d20', ''), 'the speech CLSID is never retried')

    def test_condition_slot_and_site_spec(self):
        self.assertIn('kClassNotRegistered=0x80040154', SOURCE)
        self.assertIn('kDecoderSlot=0x9c', SOURCE)
        self.assertIn('if(regs[7]!=kClassNotRegistered)return;', SOURCE)
        self.assertIn('regs[7]=r.init_hr;', SOURCE)
        specs = probe.common.parse_source_specs(SOURCE)
        self.assertEqual(specs, [dict(name=probe.SITE.name, va=probe.SITE.va, bytes=probe.SITE.expected,
                                      length=len(probe.SITE.expected), rel32_offset=0, rel32_target=0)])
        self.assertEqual(probe.SITE.va, 0x4cfd46)
        self.assertEqual(probe.SITE.expected, bytes.fromhex('8bf081fe0e000780'))

    def test_env_gate_and_install_guards(self):
        self.assertIn('L"X3M_VOICE_DMO_FALLBACK"', SOURCE)
        for guard in ('install_window_open()', 'executable_verified()', 'verify_bytes(spec.address', 'engine_patch::restore(patch)'):
            self.assertIn(guard, SOURCE)
        self.assertIn('x3m::PreserveCpuState cpu;', SOURCE)
        self.assertIn('force_align_arg_pointer', SOURCE)


class CallBinding(unittest.TestCase):
    """Run 13's crash: a local abstract class for IDMOWrapperFilter let GCC devirtualise
    Init to __cxa_pure_virtual, which the DLL resolved to a call into nothing."""

    def test_interface_is_an_explicit_vtable_not_a_local_abstract_class(self):
        self.assertIsNone(re.search(r'\bvirtual\b', SOURCE), 'no C++ virtual interface in the hook')
        self.assertIn('struct IDMOWrapperFilterLocal { const IDMOWrapperFilterVtbl* vtbl; };', SOURCE)
        self.assertIn('HRESULT (STDMETHODCALLTYPE* Init)(void*,REFCLSID,REFCLSID);', SOURCE)
        self.assertIn('view->vtbl->Init(view,kCLSID_CWMADecMediaObject,kDMOCATEGORY_AUDIO_DECODER)', SOURCE)
        self.assertIn('view->vtbl->Release(view);', SOURCE)
        self.assertIn('AddVectoredExceptionHandler(1,&fault_witness)', SOURCE)

    def test_fault_witness_is_lock_free_and_removed_at_shutdown(self):
        handler = SOURCE.split('LONG CALLBACK fault_witness(EXCEPTION_POINTERS* info) {', 1)[1].split('\n}\n', 1)[0]
        for forbidden in ('log(', 'log_flush', 'printf(', 'fflush', 'mutex', 'lock_guard', 'malloc', 'new '):
            self.assertNotIn(forbidden, handler, forbidden)
        self.assertIn('fault_count.fetch_add(1,std::memory_order_acq_rel)!=0)return EXCEPTION_CONTINUE_SEARCH', handler)
        self.assertIn('fault_seq.store(1,std::memory_order_release);', handler)
        self.assertIn('WriteFile(handle,line,DWORD(n),&written_bytes,nullptr)', handler)
        self.assertEqual(handler.count('return EXCEPTION_CONTINUE_SEARCH;'), 5, 'the exception always continues')
        self.assertNotIn('STACK_OVERFLOW', handler.split('default:', 1)[0], 'no formatting on the last guard page')
        # The one-shot record is taken by the execute-fault signature only; other first-chance exceptions are counted.
        self.assertIn('record->ExceptionInformation[0]==8', handler)
        self.assertIn("if(!execute){other_first_chance.fetch_add(1,std::memory_order_relaxed);return EXCEPTION_CONTINUE_SEARCH;}", handler)
        self.assertLess(handler.index('if(!execute)'), handler.index('fault_count.fetch_add'))
        self.assertIn('other_first_chance=%lu', SOURCE)
        shutdown = SOURCE.split('void shutdown() {', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('RemoveVectoredExceptionHandler(handler)', shutdown)
        self.assertIn('fault_handler=nullptr', shutdown)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('voice_dmo_fallback::shutdown();', capture)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('DLL_PROCESS_DETACH', loader)
        self.assertIn('x3m::voice_dmo_fallback::shutdown();', loader.split('DLL_PROCESS_DETACH', 1)[1])
        report = SOURCE.split('void report() {', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('fault_seq.load(std::memory_order_acquire)!=fault_reported', report)

    @unittest.skipUnless(shutil.which(CXX) and shutil.which(OBJDUMP), 'MinGW i686 toolchain unavailable')
    def test_compiled_object_has_no_pure_virtual_call(self):
        with tempfile.TemporaryDirectory() as directory:
            obj = Path(directory) / 'voice_dmo_fallback.o'
            subprocess.run([CXX, *FLAGS, '-c', str(ROOT / 'src/proxy/voice_dmo_fallback.cpp'), '-o', str(obj)],
                           check=True, cwd=ROOT, capture_output=True, text=True, timeout=120)
            # The devirtualised build carried a weak undefined __cxa_pure_virtual and a
            # DISP32 relocation against it for the Init call (the DLL's `call 0`).
            symbols = subprocess.run([OBJDUMP, '-t', str(obj)], check=True, capture_output=True, text=True, timeout=60).stdout
            self.assertNotIn('__cxa_pure_virtual', symbols)
            relocations = subprocess.run([OBJDUMP, '-r', str(obj)], check=True, capture_output=True, text=True, timeout=60).stdout
            self.assertNotIn('pure_virtual', relocations)
            listing = subprocess.run([OBJDUMP, '-d', '-Mintel', str(obj)], check=True, capture_output=True, text=True, timeout=60).stdout
            enter = listing.split('<_x3m_voice_dmo_fallback_enter>:', 1)[1].split('\n\n', 1)[0]
            calls = [line.split('call', 1)[1].strip() for line in enter.splitlines() if '\tcall ' in line]
            # Init and Release reach the wrapper only through its vtable: indirect calls, no
            # direct call except the relocated ones to this module's own functions.
            direct = [c for c in calls if not (c.startswith('DWORD PTR') or c in ('eax', 'ecx', 'edx'))]
            self.assertTrue(all('_x3m_voice_dmo_fallback_enter' in c for c in direct), calls)
            self.assertGreaterEqual(sum(c.startswith('DWORD PTR [') or c in ('eax', 'ecx', 'edx') for c in calls), 3, calls)


class ReplicaCoverage(unittest.TestCase):
    def test_replica_site_carries_the_game_bytes_and_the_hook_install_path(self):
        self.assertIn('.byte 0x8b,0xf0,0x81,0xfe,0x0e,0x00,0x07,0x80', REPLICA)
        self.assertIn('x3m::voice_dmo_fallback::fixture_site(site)&&x3m::voice_dmo_fallback::initialize()', REPLICA)
        self.assertIn('-DX3M_VOICE_DMO_FIXTURE', (ROOT / 'verification/probe/build_voice_startup_replica.sh').read_text())
        import run_voice_startup_replica as runner
        self.assertIn('game-dmo-hook', runner.MODES)
        self.assertIn('game-dmo-hook', runner.GAME_DS_MODES)
        self.assertIn('dmo_wrapper_init_hooked', runner.KEY_STEPS)


class LauncherGate(VoiceDecoderLaunchOption):
    def test_the_gate_travels_only_with_the_decoder(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            self.assertNotIn('X3M_VOICE_DMO_FALLBACK', json.loads(self.launch(directory)[1])['env'])
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_VOICE_DMO_FALLBACK'], '1')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class InstalledSite(unittest.TestCase):
    def test_site_qualifies(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertEqual([i['mnemonic'] for i in report['site']['instructions']], ['mov', 'cmp'])


if __name__ == '__main__':
    unittest.main()
