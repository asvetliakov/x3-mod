"""Independent checks of the media-cue gate (X3M_MEDIA_CUE_TRACE=1 /
X3M_MEDIA_CUE_CACHE=1): the site ledger against the installed EXE, refusals,
the host policy probe (cache, pending stack, ring, rate limit, window), the
production wiring (two-arm gate stub, return capture, shared install
transaction, frame boundary) and the launcher options. Twin of
test_loop_phases.py. No game, no Wine."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_media_cue_site as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]


class SourceAndPolicy(unittest.TestCase):
    def test_exact_production_spec_and_discriminators(self):
        self.assertEqual(len(probe.SITES), 1)
        self.assertEqual(probe.SITES[0].va, 0x498140)
        self.assertEqual(probe.SITES[0].expected, bytes.fromhex('538b5c2408'))
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        self.assertIn('{"media_create_enter",0x00498140,{0x53,0x8b,0x5c,0x24,0x08},5,0,0}', text)
        self.assertFalse(probe.source_checks(text.replace('},5,0,0}', '},5,4,0}', 1)))
        self.assertFalse(probe.source_checks(text.replace('0x53,0x8b,0x5c,0x24,0x08', '0x53,0x8b,0x5c,0x24,0x0c', 1)))
        self.assertIsNone(probe.source_checks(None))
        # The return-address discriminators are the verifier's call-site ledger
        # and the selector chain (0x0045c607 call -> 0x0045c60c).
        constants = {m.group(1): int(m.group(2), 16) for m in
                     re.finditer(r'inline constexpr std::uint32_t (k\w+) = (0x[0-9a-fA-F]+);', text)}
        returns = {cleanup[0] for cleanup in probe.CALL_SITES.values()}
        self.assertEqual({constants[k] for k in ('kQueryReturn', 'kSavegameReturn', 'kScriptReturn', 'kSpeechReturn', 'kHelperReturn')}, returns)
        self.assertEqual(constants['kHelperReturn'], probe.CALL_SITES[0x4f6610][0])
        self.assertEqual(constants['kSpeechReturn'], probe.CALL_SITES[0x498ef8][0])
        self.assertEqual(constants['kSelectorReturn'], 0x45c60c)
        self.assertEqual(constants['kSelectorKind'], 0x5a)
        self.assertIn((0x45c607, 'e8e49f0900', 'call 0x004f65f0'), probe.CHAIN)
        self.assertIn((0x45c605, '6a5a', 'push 0x5a: the cue kind of the sector post pass'), probe.CHAIN)

    def test_host_policy_probe(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-media-cue-') as temporary:
            exe = Path(temporary) / 'media_cue_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/media_cue_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^media_cue_host checks=\d+ failures=0 cache_bytes=\d+ pending_bytes=\d+ ring_bytes=\d+ window_bytes=\d+ cache_entries=32 pending_depth=4 lines_per_second=32\n$')

    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/media_cue.cpp').read_text()
        header = (ROOT / 'src/proxy/media_cue.h').read_text()
        core = (ROOT / 'src/proxy/media_cue_core.h').read_text()
        # Off = inert: the two environment gates, the frame boundary behind one
        # relaxed load, both handlers x87-free under LightCallBoundary, no log.
        self.assertIn('L"X3M_MEDIA_CUE_TRACE"', source)
        self.assertIn('L"X3M_MEDIA_CUE_CACHE"', source)
        self.assertIn('L"X3M_MEDIA_CUE_RETRY_S"', source)
        self.assertIn('constexpr unsigned default_retry_s=30,max_retry_s=3600;', source)
        self.assertIn('if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index);', header)
        for name in ('x3m_media_cue_enter(x3m::media_cue::EnterFrame* f)', 'x3m_media_cue_return(x3m::media_cue::ReturnFrame* f)'):
            handler = source[source.index(name):]
            handler = handler[:handler.index('\n}')]
            self.assertIn('x3m::LightCallBoundary cpu;', handler)
            self.assertNotIn('PreserveCpuState', handler)
            self.assertNotIn('log(', handler)
            self.assertEqual(handler.count('qpc()'), 1)
        enter = source[source.index('x3m_media_cue_enter(x3m::media_cue::EnterFrame* f)'):]
        enter = enter[:enter.index('\n}')]
        self.assertIn('if(!active.load(std::memory_order_relaxed))return 1;', enter)
        self.assertIn('if(!gate.owned(GetCurrentThreadId()))return 1;', enter)
        self.assertIn('if(cache_on&&scoped&&cache.refuses(f->id,now,retry_ticks)){', enter)
        self.assertIn('if(pending.push(p))f->ret=std::uint32_t(reinterpret_cast<std::uintptr_t>(return_trampoline));', enter)
        self.assertEqual(source.count('__attribute__((force_align_arg_pointer))'), 2)
        # The cache applies to the selector path alone; a success from any caller clears.
        self.assertIn('return caller == selector && kind == a.selector_kind;', core)
        self.assertIn('if (ret == a.helper_return) return slot_c == a.selector_return ? selector : other;', core)
        self.assertIn('if(f->eax)cache.success(p.id);\n    else if(cache_on&&p.scoped)cache.fail(p.id,now);', source)
        self.assertIn('inline constexpr unsigned cache_entries = 32;', core)
        self.assertIn('inline constexpr unsigned pending_depth = 4;', core)
        self.assertIn('while (depth && items[depth - 1].esp <= p.esp) { --depth; ++stale; }', core)
        self.assertIn('return pending.last_return;', source)
        self.assertIn('inline constexpr unsigned lines_per_second = 32;', core)
        self.assertIn('inline constexpr unsigned window_frames = 300, id_slots = 8;', core)
        # The gate stub: both arms restore flags/EAX/ECX/EDX/XMM0-7; REFUSE ends
        # `xor eax,eax; ret`; the return trampoline reserves the slot, writes the
        # original return address into it and `ret`s; every offset is checked.
        emit = source[source.index('void* emit_gate('):]
        emit = emit[:emit.index('\n}')]
        self.assertIn('e.byte(0x85);e.byte(0xc0);e.byte(0x74);e.byte(60);', emit)
        self.assertIn('if(e.here()!=start+64)return nullptr;', emit)
        self.assertIn('if(e.here()!=start+124)return nullptr;', emit)
        self.assertIn('restore();e.byte(0x33);e.byte(0xc0);e.byte(0xc3);', emit)
        self.assertIn('if(trampoline!=start+177)return nullptr;', emit)
        self.assertIn('e.byte(0x83);e.byte(0xec);e.byte(4);', emit)
        self.assertIn('e.byte(0x89);e.byte(0x84);e.byte(0x24);e.dword(0x90);', emit)
        self.assertIn('if(e.here()!=start+298)return nullptr;', emit)
        self.assertIn('static_assert(sizeof(x3m::media_cue::EnterFrame)==0xa4,"gate stub frame layout");', source)
        self.assertIn('static_assert(sizeof(x3m::media_cue::ReturnFrame)==0x98,"return trampoline frame layout");', source)
        # Shared install transaction, executable identity, frame boundary, telemetry gate for the trace.
        self.assertIn('return stamp::install_group(patches,specs,&emit,installed,status)&&return_trampoline;', source)
        self.assertIn('return stamp::uninstall_group(patches);', source)
        self.assertIn('else if(!object_trace::executable_verified())status="executable_unverified";', source)
        self.assertIn('trace_on=trace_wanted&&telemetry::enabled();', source)
        self.assertIn('if(trace_on&&limit.admit(e.qpc,frequency))emit_entry(e);', source)
        self.assertIn('log("media_cue frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx result=%s us=%llu attempts_frame=%lu cached=%u"', source)
        self.assertIn('log("media_cue_window qpc=%llu frame=%llu frames=%u attempts=%llu failures=%llu successes=%llu refused=%llu unobserved=%llu attempts_frame_p50=%llu attempts_frame_max=%lu ids=%s', source)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('loop_phases::initialize();'), capture.index('media_cue::initialize();'))
        self.assertLess(capture.index('frame_phases::frame(ctx.frame);'), capture.index('media_cue::frame(ctx.frame);'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/media_cue.cpp'), 2)
        build = (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text()
        self.assertIn("('src/proxy/media_cue.cpp','media')", build)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_media_cue_enter', '_x3m_media_cue_return'", audit)
        runner = (ROOT / 'verification/probe/run_game_phase_cpu.py').read_text()
        self.assertIn("'MEDIA CUE BENCH'", runner)
        fixture = (ROOT / 'verification/probe/game_phase_cpu_fixture.cpp').read_text()
        for label in ('REFUSE arm returns 0 to the caller without running the allocator',
                      'nested speech call from inside the build reached depth 2 with both spans replayed',
                      'speech caller with the cached id proceeds', 'success after the interval clears the cache entry',
                      'media install refused after the install window closed', 'foreign-thread call proceeds unobserved',
                      'lost return counted once and the fail-safe returned to last_return'):
            self.assertIn(label, fixture)


class MediaCueLaunchOptions(unittest.TestCase):
    def test_trace_requires_telemetry_cache_default_off_and_retry_range(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--media-cue-trace')
            self.assertEqual(code, 2)
            self.assertIn('--media-cue-trace requires --telemetry', error)
            code, output, error = helper.launch(directory, '--media-cue-trace', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('1', '0', '30'))
            code, output, error = helper.launch(directory, '--media-cue-cache', 'on', '--media-cue-retry-s', '45')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '1', '45'))
            code, _, error = helper.launch(directory, '--media-cue-retry-s', '45')
            self.assertEqual(code, 2)
            self.assertIn('--media-cue-retry-s requires --media-cue-cache on', error)
            for value in ('0', '3601'):
                code, _, error = helper.launch(directory, '--media-cue-cache', 'on', '--media-cue-retry-s', value)
                self.assertEqual(code, 2, value)
                self.assertIn('between 1 and 3600', error)
            code, _, error = helper.launch(directory, '--media-cue-cache', 'maybe')
            self.assertEqual(code, 2)
            # Absent options reset inherited values: the launcher owns the three variables.
            code, output, error = helper.launch(directory, inherited={'X3M_MEDIA_CUE_TRACE': '1', 'X3M_MEDIA_CUE_CACHE': '1', 'X3M_MEDIA_CUE_RETRY_S': '5'})
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '0', '30'))


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()
        cls.installed = probe.installed_spans(probe.INSTALLED.read_text())

    def report(self, image=None, decoded=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source, self.data, self.installed)

    def test_actual_executable_span_callers_and_return_contract(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertTrue(report['source_present'])
        self.assertTrue(report['checks']['no_return_slot_read'])
        self.assertTrue(report['checks']['cdecl_ret'] and report['checks']['esp_writers_known'] and report['checks']['callee_saved'])
        self.assertEqual(sorted(int(a, 16) for a in report['call_sites']['0x00498140']), sorted(probe.CALL_SITES))
        row = report['sites'][0]
        self.assertEqual(row['incoming_sources'], [])
        self.assertTrue(row['plain_copy_ok'] and row['arena_replay_ok'] and row['no_installed_conflict'])
        self.assertEqual(report['raw_interior_hits'], [])
        self.assertEqual(report['data_reference_hits'], [])

    def test_corrupted_byte_and_interior_edge_refused(self):
        site = probe.SITES[0]
        image = PatchedImage(self.image, [(site.va + 1, bytes([site.expected[1] ^ 1]))])
        row = self.report(image=image)['sites'][0]
        self.assertFalse(row['bytes_ok'] or row['ok'])
        decoded = list(self.decoded)
        decoded.append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
        row = self.report(decoded=decoded)['sites'][0]
        self.assertFalse(row['no_interior_branch'] or row['ok'])
        decoded = list(self.decoded)
        decoded.append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
        row = self.report(decoded=decoded)['sites'][0]
        self.assertFalse(row['incoming_ok'] or row['ok'])
        # A routine that read its return slot would break the return capture.
        decoded = list(self.decoded)
        decoded.append(probe.common.Instruction(0x4981a0, b'\x8b\x04\x24', 'mov', 'eax,DWORD PTR [esp]'))
        self.assertFalse(self.report(decoded=decoded)['checks']['no_return_slot_read'])
        overlapping = probe.inspect(self.image, self.decoded, self.source, self.data, [(0x498142, 5)])
        self.assertFalse(overlapping['sites'][0]['no_installed_conflict'])


if __name__ == '__main__':
    unittest.main()
