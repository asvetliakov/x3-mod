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
        # Optional diagnostics: the two environment gates, the frame boundary behind one
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
        # The entry-side line: one predicate on the trace-off path, its own
        # lines_per_second limiter, written after the proceed decision (the
        # REFUSE arm returns first, so it writes none) and before the handler
        # returns into the claim tail, synchronously to the log's OS handle
        # under the full envelope; the handler itself never takes the log mutex.
        self.assertIn('if(trace_on&&enter_limit.admit(now,frequency))write_enter_line(e);', enter)
        self.assertLess(enter.index('return 0;'), enter.index('write_enter_line(e);'))
        self.assertLess(enter.index('write_enter_line(e);'), enter.rindex('return 1;'))
        self.assertEqual(enter.count('trace_on'), 1)
        self.assertNotIn('WriteFile', enter)
        writer = source[source.index('void write_enter_line(const detail::Entry& e) {'):]
        writer = writer[:writer.index('\n}')]
        self.assertIn('const HANDLE handle=log_handle();', writer)
        self.assertIn('x3m::call_preserved([&]{', writer)
        self.assertIn('"media_cue_enter frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx attempt=%lu\\n"', writer)
        self.assertIn('written_whole=n>0&&unsigned(n)<sizeof line&&WriteFile(handle,line,DWORD(n),&written,nullptr)&&written==DWORD(n);', writer)
        self.assertIn('if(!written_whole)++enter_limit.suppressed;', writer)
        self.assertIn('if (!now) { ++suppressed; return false; }', core)
        self.assertNotIn('log(', writer.replace('log_handle()', ''))
        self.assertIn('detail::RateLimit enter_limit;', source)
        self.assertIn('limit={};enter_limit={};', source)
        self.assertIn('suppressed=%llu enter_suppressed=%llu dropped=%llu', source)
        self.assertIn('limit.suppressed=0;enter_limit.suppressed=0;ring.dropped=0;', source)
        self.assertIn('inline constexpr std::uint64_t pass_dispatch_cost_ns = 280, refuse_dispatch_cost_ns = 116;', core)
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
        self.assertIn('if(!object_trace::executable_verified())status="executable_unverified";', source)
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
                      'lost return counted once and the fail-safe returned to last_return',
                      'media_cue_enter line written with the entry\'s frame, qpc, id, kind, caller, flags and attempt',
                      'media_cue_enter line was in the file before the allocator body ran',
                      'the failed build wrote its entry line; the REFUSE arm wrote none',
                      'media_cue_enter lines bounded to lines_per_second per clock second, the rest counted as enter_suppressed',
                      'foreign-thread call writes no media_cue_enter line',
                      'trace off: no media_cue_enter line written by the benchmark\'s proceeded calls',
                      'documented trace-off PASS dispatch cost within 2x of the measured cost',
                      'documented trace-off REFUSE dispatch cost within 2x of the measured cost'):
            self.assertIn(label, fixture)
        self.assertIn('HANDLE log_handle() noexcept {return media_log_handle;}', fixture)
        self.assertIn('media_cases=11', fixture)

    def test_default_id2_skip_precedes_all_diagnostic_state(self):
        source = (ROOT / 'src/proxy/media_cue.cpp').read_text()
        enter = source.split('x3m_media_cue_enter(x3m::media_cue::EnterFrame* f) {', 1)[1].split('\n}', 1)[0]
        skip = 'if(detail::refuse_id2_video(f->id,f->eax))return 0;'
        self.assertIn(skip, enter)
        before, after = enter.split(skip, 1)
        self.assertEqual(before.strip(), 'using namespace x3m::media_cue;')
        for operation in ('active.load', 'LightCallBoundary', 'gate.owned', 'qpc()', '++attempts_frame', 'pending.push'):
            self.assertIn(operation, after)
        initialize = source.split('bool initialize() {', 1)[1].split('\nnamespace detail', 1)[0]
        self.assertNotIn('if(!trace_wanted&&!cache_wanted)return false;', initialize)
        self.assertIn('else install_group(sites::kSites,status);', initialize)
        self.assertIn('if(!frequency){trace_on=false;cache_on=false;}', initialize)
        self.assertIn('active.store(installed.load(std::memory_order_acquire)&&(trace_on||cache_on)', initialize)
        self.assertNotIn('owned_eligibility', source)
        fixture = (ROOT / 'verification/probe/media_cue_skip_fixture_inc.h').read_text()
        for label in ('ID2 skipped for all callers before owner admission',
                      'ID2 skip touches no diagnostic state', 'ID2 foreign calls never enter allocator',
                      'nested ID2 does not add a return observer', 'skip rollback restores complete native span'):
            self.assertIn(label, fixture)

    def test_video_blit_witness(self):
        source = (ROOT / 'src/proxy/media_cue.cpp').read_text()
        header = (ROOT / 'src/proxy/media_cue.h').read_text()
        core = (ROOT / 'src/proxy/media_cue_core.h').read_text()
        sites = (ROOT / 'src/proxy/media_cue_sites.h').read_text()
        # The consumer's range (media-cue-playback.md, 8.3/8.6) and the cadence.
        self.assertIn('inline constexpr std::uint32_t kVideoBlitBegin = 0x004d0c40;', sites)
        self.assertIn('inline constexpr std::uint32_t kVideoBlitEnd = 0x004d14e0;', sites)  # the next function, the pump (RE note 8.6)
        self.assertIn('inline constexpr unsigned video_blit_line_interval = 60;', core)
        self.assertIn('return ret - a.blit_begin < a.blit_end - a.blit_begin;', core)
        self.assertIn('sites::kVideoBlitBegin,sites::kVideoBlitEnd};', source)
        # Off: nothing is published, so the shell pays its one predicate; on:
        # the observer classifies by the shell's return address, owner thread
        # only, restores LastError and writes through the direct handle path.
        self.assertIn('return trace_on&&active.load(std::memory_order_acquire)?&video_lock_observe:nullptr;', source)
        observer = source[source.index('void video_lock_observe(const ownership::SurfaceLockEvent& e) {'):]
        observer = observer[:observer.index('\n}')]
        self.assertIn('if(!active.load(std::memory_order_relaxed))return;', observer)
        self.assertIn('if(!detail::video_blit_caller(std::uint32_t(reinterpret_cast<std::uintptr_t>(e.return_address)),addresses))return;', observer)
        self.assertIn('const DWORD owner=gate.owner.load(std::memory_order_relaxed);', observer)
        self.assertIn('(owner?video_foreign:video_early).fetch_add(1,std::memory_order_relaxed);', observer)
        self.assertNotIn('log(', observer)
        self.assertIn('unsigned depth = 0;', core)
        self.assertIn('if (depth++) { ++reentries; return false; }', core)
        writer = source[source.index('void write_video_line(const ownership::SurfaceLockEvent& e,const char* stage) {'):]
        writer = writer[:writer.index('\n}')]
        self.assertIn('const HANDLE handle=log_handle();', writer)
        self.assertIn('"media_video_blit frame=%llu qpc=%llu texture=%p width=%u height=%u format=%u flags=0x%lx result=%s stage=%s blits=%llu unlocks=%llu\\n"', writer)
        self.assertIn('written_whole=n>0&&unsigned(n)<sizeof line&&WriteFile(handle,line,DWORD(n),&written,nullptr)&&written==DWORD(n);', writer)
        self.assertIn('if(!written_whole)++video.suppressed;', writer)
        self.assertNotIn('log(', writer.replace('log_handle()', ''))
        self.assertIn('video_blits=%llu video_unlocks=%llu video_failures=%llu video_suppressed=%llu video_reentries=%llu video_foreign=%llu video_early=%llu', source)
        self.assertIn('    video.close();\n', source)
        self.assertIn('ownership::SurfaceLockObserver video_lock_observer() noexcept;', header)
        # The ownership shell: the return address is taken at the shell's
        # entry, the observer slot is one relaxed load, the observed arm is out
        # of line, and the generator (not the generated file) is the source.
        generator = (ROOT / 'tools/ownership/generate_d3d9_forwarders.py').read_text()
        self.assertIn('body = f"return surface_lock(this, __builtin_return_address(0), {\', \'.join(args)});"', generator)
        self.assertIn('body = "return surface_unlock(this, __builtin_return_address(0));"', generator)
        forwarders = (ROOT / 'src/ownership/d3d9_forwarders_inc.h').read_text()
        self.assertIn('    return surface_lock(this, __builtin_return_address(0), locked_rect, rect, flags);', forwarders)
        self.assertIn('    return surface_unlock(this, __builtin_return_address(0));', forwarders)
        ownership = (ROOT / 'src/ownership/d3d9_ownership.cpp').read_text()
        self.assertIn('std::atomic<SurfaceLockObserver> surface_lock_observer{nullptr};', ownership)
        self.assertEqual(ownership.count('const SurfaceLockObserver observer=surface_lock_observer.load(std::memory_order_relaxed);'), 2)
        self.assertIn('if(!observer)hr=observe_result(device_of(node), static_cast<Surface*>(node)->native_->LockRect(locked_rect, rect, flags));', ownership)
        self.assertIn('else hr=observed_surface_lock(node, observer, return_address, locked_rect, rect, flags);', ownership)
        self.assertIn('__attribute__((noinline)) HRESULT observed_surface_lock(', ownership)
        # The shell owns the CPU-state envelope: incoming state restored before
        # the native call, native outgoing state restored after the second observer call.
        for name in ('observed_surface_lock', 'observed_surface_unlock'):
            arm = ownership[ownership.index(f'__attribute__((noinline)) HRESULT {name}('):]
            arm = arm[:arm.index('\n}')]
            self.assertEqual(arm.count('observer(e);'), 2)
            self.assertLess(arm.index('ExecutionState incoming;'), arm.index('observer(e);'))
            self.assertLess(arm.index('observer(e);'), arm.index('incoming.restore();'))
            self.assertLess(arm.index('incoming.restore();'), arm.index('native_->'))
            self.assertLess(arm.index('native_->'), arm.index('ExecutionState outgoing;'))
            self.assertLess(arm.index('ExecutionState outgoing;'), arm.rindex('observer(e);'))
            self.assertLess(arm.rindex('observer(e);'), arm.index('outgoing.restore();'))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('media_cue::initialize();'), capture.index('ownership::set_surface_lock_observer(observer);'))
        self.assertLess(capture.index('voice_dmo_fallback::shutdown();'), capture.index('ownership::set_surface_lock_observer(nullptr);'))
        self.assertIn('x3m::ownership::set_surface_lock_observer(nullptr);', (ROOT / 'src/proxy/loader.cpp').read_text())
        fixture = (ROOT / 'verification/probe/game_phase_cpu_fixture.cpp').read_text()
        for label in ('trace off: no video witness published',
                      "lock from outside the consumer's range writes no line and is not counted",
                      'first in-range lock writes its enter and result lines with one GetDesc each',
                      'video result line carries the native HRESULT after the lock',
                      'one enter/result pair per interval of in-range locks',
                      'first in-range unlock writes its enter and result lines',
                      'foreign-thread in-range lock writes no line and counts as video_foreign',
                      'video lock before the owner is admitted writes no line and counts as video_early',
                      're-entrant in-range lock writes no line and leaves the outer enter/result pair matched'):
            self.assertIn(label, fixture)


class MediaCueLaunchOptions(unittest.TestCase):
    def test_trace_requires_telemetry_cache_default_on_and_retry_range(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--media-cue-trace')
            self.assertEqual(code, 2)
            self.assertIn('--media-cue-trace requires --telemetry', error)
            code, output, error = helper.launch(directory, '--media-cue-trace', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('1', '1', '30'))
            # The cache is on by default and does not depend on telemetry: the
            # DLL reads X3M_MEDIA_CUE_CACHE on its own (src/proxy/media_cue.cpp).
            code, output, error = helper.launch(directory)
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '1', '30'))
            code, output, error = helper.launch(directory, '--media-cue-cache', 'on', '--media-cue-retry-s', '45')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '1', '45'))
            # off still writes 0, the stock per-frame rebuild.
            code, output, error = helper.launch(directory, '--media-cue-cache', 'off')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '0', '30'))
            code, _, error = helper.launch(directory, '--media-cue-cache', 'off', '--media-cue-retry-s', '45')
            self.assertEqual(code, 2)
            self.assertIn('--media-cue-retry-s requires --media-cue-cache on', error)
            for value in ('0', '3601'):
                code, _, error = helper.launch(directory, '--media-cue-cache', 'on', '--media-cue-retry-s', value)
                self.assertEqual(code, 2, value)
                self.assertIn('between 1 and 3600', error)
            code, _, error = helper.launch(directory, '--media-cue-cache', 'maybe')
            self.assertEqual(code, 2)
            # Absent options reset inherited values: the launcher owns the three variables.
            code, output, error = helper.launch(directory, inherited={'X3M_MEDIA_CUE_TRACE': '1', 'X3M_MEDIA_CUE_CACHE': '0', 'X3M_MEDIA_CUE_RETRY_S': '5'})
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_MEDIA_CUE_TRACE'], env['X3M_MEDIA_CUE_CACHE'], env['X3M_MEDIA_CUE_RETRY_S']), ('0', '1', '30'))


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
