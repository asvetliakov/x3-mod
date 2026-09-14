"""Host checks for the game-phase patch transaction and runtime bridges.

The installer fixture brace-extracts and compiles the production
``install_group`` body.  The bridge checks bind lifecycle and ordering
requirements to the production capture/telemetry functions without starting
Wine or the game.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from verification.analysis.test_capture_bloom_lifetime import extract_function
from verification.analysis.test_chase_lead import extract_named_function


ROOT = Path(__file__).resolve().parents[2]


INSTALL_PREFIX = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

std::atomic<bool> active{true};
int verify_fail=-1,fail_index=-1,fail_stage=-1,restore_fail=-1,current=-1;
unsigned verify_calls=0,claims=0,preflight_violations=0,premature_activation=0;
bool window_open=true;
std::vector<int> restored;

namespace engine_patch {
struct Site {
    bool patched_in=false;
    const char* status="idle";
    void** entry=nullptr;
};
struct SiteSpec {
    std::uintptr_t address=0;
    unsigned char expected[10]{};
    unsigned length=5;
};
}

namespace sites {
constexpr unsigned Count=33;
constexpr unsigned PhaseCount=33;
engine_patch::SiteSpec kSites[Count];
}
unsigned site_count=sites::Count;   // production: PhaseCount, or Count with X3M_AUDIO_SITES=1
bool audio_enabled=false;

engine_patch::Site patches[sites::Count];
void* originals[sites::Count]{};
void* next_slots[sites::Count]{};
void* stubs[sites::Count]{};

void observe_inert() {
    if(active.load(std::memory_order_acquire))++premature_activation;
}

namespace engine_patch {
bool install_window_open() { observe_inert();return window_open; }
bool verify_bytes(std::uintptr_t address,const unsigned char*,unsigned) {
    observe_inert();
    const int index=int(address-0x1000);
    ++verify_calls;
    if(claims)++preflight_violations;
    return index!=verify_fail;
}
bool claim(Site& site,const SiteSpec& spec) {
    observe_inert();
    if(verify_calls!=sites::Count)++preflight_violations;
    current=int(spec.address-0x1000);++claims;
    if(current==fail_index&&fail_stage==0){site.status="claim_failed";return false;}
    site.patched_in=true;site.entry=&originals[current];site.status="active";return true;
}
bool store_pointer(void** at,void* value) {
    observe_inert();
    if(at!=&next_slots[current]||value!=originals[current])++preflight_violations;
    if(current==fail_index&&fail_stage==2)return false;
    *at=value;return true;
}
bool push_front(Site&,void* stub) {
    observe_inert();
    if(stub!=stubs[current])++preflight_violations;
    return !(current==fail_index&&fail_stage==3);
}
bool restore(Site& site) {
    observe_inert();
    const int index=int(&site-patches);restored.push_back(index);
    if(index==restore_fail)return false;
    site.patched_in=false;return true;
}
}

void* emit(unsigned index,void*** next_out) {
    observe_inert();
    if(int(index)!=current)++preflight_violations;
    if(current==fail_index&&fail_stage==1)return nullptr;
    *next_out=&next_slots[index];return stubs[index];
}
'''


INSTALL_SUFFIX = r'''
int main() {
    unsigned checks=0,failures=0;
    const auto check=[&](bool value){++checks;if(!value)++failures;};
    const auto reset=[&]{
        active.store(true);window_open=true;verify_fail=fail_index=fail_stage=restore_fail=current=-1;
        verify_calls=claims=preflight_violations=premature_activation=0;restored.clear();
        for(unsigned i=0;i<sites::Count;++i){
            patches[i]={};sites::kSites[i].address=0x1000+i;
            originals[i]=reinterpret_cast<void*>(std::uintptr_t(0x2000+i));
            next_slots[i]=nullptr;stubs[i]=reinterpret_cast<void*>(std::uintptr_t(0x3000+i));
        }
    };
    const auto all_unpatched=[&]{for(const auto& site:patches)if(site.patched_in)return false;return true;};
    const auto reverse_restored=[&](int last){
        if(restored.size()!=unsigned(last+1))return false;
        for(int i=0;i<=last;++i)if(restored[unsigned(i)]!=last-i)return false;
        return true;
    };
    const char* status=nullptr;

    reset();window_open=false;
    check(!install_group(status));check(!std::strcmp(status,"install_window_closed"));
    check(!active.load());check(verify_calls==0);check(claims==0);check(restored.empty());

    for(int index=0;index<int(sites::Count);++index){
        reset();verify_fail=index;
        check(!install_group(status));check(!std::strcmp(status,"preflight_bytes"));
        check(!active.load());check(verify_calls==unsigned(index+1));check(claims==0);
        check(restored.empty());check(all_unpatched());check(!premature_activation);
    }

    reset();
    check(install_group(status));check(!std::strcmp(status,"ok"));
    check(active.load());check(verify_calls==sites::Count);check(claims==sites::Count);
    check(!preflight_violations);check(!premature_activation);check(restored.empty());
    for(const auto& site:patches)check(site.patched_in);

    for(int index=0;index<int(sites::Count);++index)for(int stage=0;stage<4;++stage){
        reset();fail_index=index;fail_stage=stage;
        check(!install_group(status));check(!active.load());
        check(verify_calls==sites::Count);check(claims==unsigned(index+1));
        check(!preflight_violations);check(!premature_activation);
        check(!std::strcmp(status,stage==0?"claim_failed":"stub_chain_failed"));
        const int last=stage==0?index-1:index;
        check(reverse_restored(last));check(all_unpatched());
    }

    reset();fail_index=int(sites::Count)-1;fail_stage=3;restore_fail=7;
    check(!install_group(status));check(!active.load());
    check(!std::strcmp(status,"rollback_failed_inert"));
    check(reverse_restored(int(sites::Count)-1));check(patches[7].patched_in);
    for(unsigned i=0;i<sites::Count;++i)if(i!=7)check(!patches[i].patched_in);
    check(!preflight_violations);check(!premature_activation);

    std::printf("game_phase_install checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
'''


ADMISSION_PREFIX = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>

using DWORD=std::uint32_t;
DWORD fixture_thread=0;
DWORD GetCurrentThreadId() { return fixture_thread; }

namespace sites {
enum Index : unsigned {
    LoopSetup=0, Pump=2, Services=5, Render=9, Tail=13,
    DelayedBegin=15, PresentEnd=22
};
}

namespace detail {
struct Core {
    unsigned invalidations=0;
    void invalidate() { ++invalidations; }
};
}

std::atomic<DWORD> owner_thread{0},invalidation_epoch{0};
std::atomic<unsigned> foreign_hits{0},suppressed{0};
bool reporting=false;
DWORD seen_epoch=0;
detail::Core core;
'''


ADMISSION_SUFFIX = r'''
int main() {
    unsigned checks=0,failures=0;
    const auto check=[&](bool value){++checks;if(!value)++failures;};

    fixture_thread=11;
    check(!owner(sites::Pump));
    check(!owner(sites::DelayedBegin));
    check(!owner(sites::PresentEnd));
    check(owner_thread.load()==0);
    check(foreign_hits.load()==3);
    check(core.invalidations==0);

    check(owner(sites::LoopSetup));
    check(owner_thread.load()==11);
    check(foreign_hits.load()==3);
    check(seen_epoch==0);

    fixture_thread=22;
    check(!owner(sites::Pump));
    check(!owner(sites::DelayedBegin));
    check(!owner(sites::PresentEnd));
    check(!owner(sites::LoopSetup));
    check(owner_thread.load()==11);
    check(foreign_hits.load()==7);
    check(core.invalidations==0);

    // A lifecycle callback on another thread only publishes the epoch.  The
    // owner consumes it at its next endpoint without surrendering ownership.
    invalidation_epoch.fetch_add(1,std::memory_order_release);
    check(core.invalidations==0);
    check(seen_epoch==0);
    fixture_thread=11;
    check(owner(sites::Render));
    check(core.invalidations==1);
    check(seen_epoch==1);
    check(owner_thread.load()==11);
    check(owner(sites::Services));
    check(core.invalidations==1);

    // Multiple invalidations before an endpoint collapse into one Core revoke,
    // and the process-lifetime owner remains unchanged.
    invalidation_epoch.fetch_add(2,std::memory_order_release);
    check(owner(sites::Tail));
    check(core.invalidations==2);
    check(seen_epoch==3);
    check(owner_thread.load()==11);

    // Same-owner callbacks during report are suppressed.  They publish a new
    // invalidation epoch, which is deliberately consumed only after reporting.
    reporting=true;
    const DWORD before_epoch=invalidation_epoch.load();
    const DWORD before_seen=seen_epoch;
    const unsigned before_invalidations=core.invalidations;
    check(!owner(sites::PresentEnd));
    check(suppressed.load()==1);
    check(invalidation_epoch.load()==before_epoch+1);
    check(seen_epoch==before_seen);
    check(core.invalidations==before_invalidations);
    check(owner_thread.load()==11);
    reporting=false;
    check(owner(sites::Tail));
    check(core.invalidations==before_invalidations+1);
    check(seen_epoch==invalidation_epoch.load());
    check(owner_thread.load()==11);

    std::printf("game_phase_admission checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
'''


DELAYED_BEGIN_PREFIX = r'''
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace detail {
struct Stamp { std::uint64_t qpc=0; };
struct Request { std::uint32_t cockpit=0,target=0,mode=0; };
struct Core {
    unsigned invalidations=0,begins=0;
    unsigned kind=~0u;
    Stamp stamp{};
    Request request{};
    void invalidate() { ++invalidations; }
    void begin(unsigned next_kind,Stamp next_stamp,Request next_request) {
        ++begins;kind=next_kind;stamp=next_stamp;request=next_request;
    }
};
}

namespace sites { enum Index : unsigned { DelayedBegin=15 }; }

bool memory_succeeds=false;
unsigned memory_reads=0;
std::uintptr_t memory_address=0;
std::uint32_t memory_value=0;
namespace engine_memory {
bool read(std::uintptr_t at,void* output,std::size_t size) {
    ++memory_reads;memory_address=at;
    if(!memory_succeeds||size!=sizeof(std::uint32_t))return false;
    *static_cast<std::uint32_t*>(output)=memory_value;return true;
}
}

std::uint64_t read_failures=0;
detail::Core core;
'''


DELAYED_BEGIN_SUFFIX = r'''
int main() {
    unsigned checks=0,failures=0;
    const auto check=[&](bool value){++checks;if(!value)++failures;};
    const auto reset=[&]{memory_succeeds=false;memory_reads=0;memory_address=0;
        memory_value=0;read_failures=0;core={};};
    std::uint32_t regs[8]{};regs[3]=0x2000;regs[6]=0x33445566;

    reset();regs[7]=2;
    delayed_begin_under_test(sites::DelayedBegin,regs,detail::Stamp{101});
    check(memory_reads==0);check(read_failures==1);check(core.invalidations==1);
    check(core.begins==0);

    reset();regs[7]=3;
    delayed_begin_under_test(sites::DelayedBegin,regs,detail::Stamp{102});
    check(memory_reads==1);check(memory_address==0x2004);check(read_failures==1);
    check(core.invalidations==1);check(core.begins==0);

    reset();regs[7]=3;memory_succeeds=true;memory_value=0x778899aa;
    delayed_begin_under_test(sites::DelayedBegin,regs,detail::Stamp{103});
    check(memory_reads==1);check(memory_address==0x2004);check(read_failures==0);
    check(core.invalidations==0);check(core.begins==1);check(core.kind==0);
    check(core.stamp.qpc==103);check(core.request.cockpit==0x33445566);
    check(core.request.target==0x778899aa);check(core.request.mode==3);

    std::printf("game_phase_delayed_begin checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
'''


def compact(text: str) -> str:
    return re.sub(r'\s+', '', text)


def positions(text: str, *needles: str) -> list[int]:
    """Return ordered positions while giving useful failures for missing calls."""
    return [text.index(needle) for needle in needles]


def extract_delayed_begin_arm(source: str) -> str:
    """Extract only handle's stack prelude and DelayedBegin switch arm."""
    handle = extract_named_function(source, 'handle')
    prelude_start = handle.index('const std::uintptr_t esp=')
    switch_start = handle.index('switch(index){', prelude_start)
    prelude = handle[prelude_start:switch_start + len('switch(index){')]
    case_start = handle.index('case sites::DelayedBegin:', switch_start)
    case_end = handle.index('case sites::DelayedEnd:', case_start)
    return (
        'void delayed_begin_under_test(unsigned index,const std::uint32_t* regs,detail::Stamp at) noexcept {'
        + prelude + handle[case_start:case_end] + 'default:break;}}'
    )


class GamePhaseInstallTests(unittest.TestCase):
    def test_fixture_pump_region_bounds_and_disabled_lifetime(self):
        source = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        body = extract_named_function(source, 'fixture_pump_region')
        self.assertIn('#else\nconstexpr std::uintptr_t pump_active_address=0x608adc,pump_flags_address=0x606f3c;\n#endif', source)
        handle = extract_named_function(source, 'handle')
        self.assertIn('engine_memory::read(pump_active_address,&p.active,4)', handle)
        self.assertIn('engine_memory::read(pump_flags_address,&flags,4)', handle)
        self.assertIn('engine_memory::read(flags,&p.flags,4)', handle)
        program = r'''
#include <atomic>
#include <cstdint>
#include <cassert>
std::atomic<bool> active{false};
std::uintptr_t pump_active_address=0,pump_flags_address=0;
''' + body + r'''
int main(){
    assert(fixture_pump_region(0,0));
    assert(!fixture_pump_region(0,0x10000));
    assert(!fixture_pump_region(0x100003,0x10000));
    assert(!fixture_pump_region(0x100000,0x9003));
    assert(!fixture_pump_region(0xffff0000,0x10000));
    assert(!fixture_pump_region(4,UINT32_MAX));
    assert(pump_active_address==0&&pump_flags_address==0);
    assert(fixture_pump_region(0x100000,0x9004));
    assert(pump_active_address==0x108adc&&pump_flags_address==0x106f3c);
    assert(!fixture_pump_region(0x200000,0x9003));
    assert(pump_active_address==0x108adc&&pump_flags_address==0x106f3c);
    active.store(true);
    assert(!fixture_pump_region(0x200000,0x10000));
    assert(!fixture_pump_region(0,0));
    assert(pump_active_address==0x108adc&&pump_flags_address==0x106f3c);
    active.store(false);
    assert(fixture_pump_region(0xffff0000,0xfffc));
    assert(pump_active_address==0xffff8adc&&pump_flags_address==0xffff6f3c);
    assert(fixture_pump_region(0,0));
    assert(pump_active_address==0&&pump_flags_address==0);
}
'''
        with tempfile.TemporaryDirectory(prefix='x3-game-phase-pump-region-') as temporary:
            cpp = Path(temporary) / 'fixture.cpp'
            executable = Path(temporary) / 'fixture'
            cpp.write_text(program)
            build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_actual_install_group_is_atomic_across_all_sites_and_failures(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        body = extract_named_function(source, 'install_group')
        with tempfile.TemporaryDirectory(prefix='x3-game-phase-install-') as temporary:
            cpp = Path(temporary) / 'fixture.cpp'
            executable = Path(temporary) / 'fixture'
            cpp.write_text(INSTALL_PREFIX + body + INSTALL_SUFFIX)
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'game_phase_install checks=1538 failures=0\n')
            self.assertEqual(run.stderr, '')

    def test_actual_owner_and_synchronize_admission(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        bodies = ''.join([
            extract_named_function(source, 'synchronize'),
            extract_named_function(source, 'owner'),
        ])
        with tempfile.TemporaryDirectory(prefix='x3-game-phase-admission-') as temporary:
            cpp = Path(temporary) / 'fixture.cpp'
            executable = Path(temporary) / 'fixture'
            cpp.write_text(ADMISSION_PREFIX + bodies + ADMISSION_SUFFIX)
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'game_phase_admission checks=39 failures=0\n')
            self.assertEqual(run.stderr, '')

    def test_targeted_handler_reads_tokens_rejected_requests_and_failures(self):
        source = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        handle = extract_named_function(source, 'handle')
        start = handle.index('const std::uintptr_t esp=')
        switch = handle.index('switch(index){', start)
        arms = handle[handle.index('case sites::InputBody:', switch):handle.index('default:++read_failures;', switch)]
        code = r'''
#include "game_phases_core.h"
#include <cassert>
#include <cstring>
#include <map>
namespace detail=x3m::game_phases::detail;
namespace sites {enum {InputBody=23,InputAfter,PublisherBegin,PublisherEnd,PlaybackBegin,PlaybackEnd,CreateBegin,CreateEnd,SeekBegin,SeekEnd};}
detail::Core core;unsigned read_failures=0,reads=0,publisher_entries=0,publisher_filtered=0;
std::map<std::uintptr_t,std::uint32_t> memory;
namespace engine_memory {
bool read(std::uintptr_t at,void* out,unsigned count){
    ++reads;
    for(unsigned i=0;i<count;i+=4){auto it=memory.find(at+i);if(it==memory.end())return false;std::memcpy(static_cast<char*>(out)+i,&it->second,4);}
    return true;
}}
''' + extract_named_function(source, 'read_word') + (
            'void target(unsigned index,const std::uint32_t* regs,detail::Stamp at) noexcept {'
            + handle[start:switch+len('switch(index){')] + arms + 'default:break;}}') + r'''
void reset(){core={};core.frequency=1000000;core.phase_live=true;core.phase=6;core.phase_begin.qpc=1;memory.clear();read_failures=reads=0;}
int main(){
    std::uint32_t r[8]{};r[3]=0x1ffc;r[6]=0x3000;r[7]=2;
    reset();target(sites::PublisherBegin,r,{10});assert(!core.depth&&!reads&&publisher_entries==1&&publisher_filtered==1); // nonmode3 fastgate
    r[7]=3;memory[0x2000]=0x42dd6e;memory[0x2004]=0; // native nulltarget does not touchcockpit
    target(sites::PublisherBegin,r,{20});assert(core.depth==1&&reads==2&&core.stack[0].request.target==0);
    r[3]=0x1fec;target(sites::PublisherEnd,r,{30});assert(!core.depth&&core.first_calls[0].witness.end_esp==0x1ff0);
    reset();r[3]=0x1ffc;memory[0x2000]=0x42a462;memory[0x2004]=0x4444;
    memory[0x31e0]=0x5555;memory[0x31e4]=0xabcd0002;memory[0x3010]=0x6666;
    target(sites::PublisherBegin,r,{40});assert(core.depth==1&&reads==5);
    assert(core.stack[0].witness.previous_mode==2&&core.stack[0].witness.previous_target==0x5555&&core.stack[0].witness.view==0x6666);
    r[3]=0x17fc;for(unsigned i=0;i<6;++i)memory[0x1800+4*i]=100+i;
    target(sites::PlaybackBegin,r,{50});assert(core.depth==2&&core.stack[1].witness.args[5]==105);
    r[3]=0x13fc;memory[0x1400]=9;r[7]=0;target(sites::CreateBegin,r,{60});assert(core.depth==3);
    r[3]=0x1400;r[7]=0x7777;target(sites::CreateEnd,r,{70});assert(core.depth==2&&core.first_calls[2].result==0x7777);
    r[3]=0x13fc;r[7]=0x8888;target(sites::SeekBegin,r,{80});assert(core.depth==3&&core.stack[2].witness.args[1]==0x8888);
    r[7]=0;target(sites::SeekEnd,r,{90});assert(core.depth==2&&core.first_calls[3].result==0);
    r[3]=0x17fc;target(sites::PlaybackEnd,r,{100});assert(core.depth==1);
    r[3]=0x1fec;target(sites::PublisherEnd,r,{110});assert(!core.depth&&core.first_calls[0].witness.caller==0x42a462);
    reset();r[3]=0x1ffc;r[7]=3;memory[0x2000]=0x42dd6e;memory[0x2004]=0x4444;
    target(sites::PublisherBegin,r,{120});assert(read_failures==1&&core.invalidated==1&&!core.depth);
    reset();r[3]=0xfffffffc;target(sites::PublisherBegin,r,{130});assert(read_failures==1&&core.invalidated==1&&!reads);
    reset();r[3]=0x17fc;target(sites::PlaybackBegin,r,{140});assert(read_failures==1&&core.invalidated==1);
    reset();r[3]=0x13fc;target(sites::CreateBegin,r,{150});target(sites::SeekBegin,r,{160});assert(!reads&&!core.depth); // no playbackparent
}
'''
        with tempfile.TemporaryDirectory(prefix='x3-game-phase-targeted-') as temporary:
            cpp = Path(temporary) / 'fixture.cpp'
            executable = Path(temporary) / 'fixture'
            cpp.write_text(code)
            build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT / 'src/proxy'), str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_actual_delayed_begin_validation_and_word_read_fail_once(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        bodies = ''.join([
            extract_named_function(source, 'read_word'),
            extract_delayed_begin_arm(source),
        ])
        with tempfile.TemporaryDirectory(prefix='x3-game-phase-delayed-begin-') as temporary:
            cpp = Path(temporary) / 'fixture.cpp'
            executable = Path(temporary) / 'fixture'
            cpp.write_text(DELAYED_BEGIN_PREFIX + bodies + DELAYED_BEGIN_SUFFIX)
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'game_phase_delayed_begin checks=19 failures=0\n')
            self.assertEqual(run.stderr, '')

    def test_present_publishes_exact_metadata_before_telemetry_and_frame_advance(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        present = compact(extract_function(
            source,
            'HRESULT WINAPI present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r)',
        ))
        expected = (
            'game_phases::present_endpoint(reinterpret_cast<std::uintptr_t>(d),'
            'ctx.id,ctx.reset_generation,ctx.frame,ctx.capture,end,static_cast<std::uint32_t>(hr));'
        )
        order = positions(
            present,
            'constHRESULThr=fn(d,a,b,w,r);',
            'constautoend=telemetry::now();',
            expected,
            'telemetry::present(ctx.stats,ctx.frame,ctx.capture,begin,end,hr);',
            '++ctx.frame;',
        )
        self.assertEqual(order, sorted(order))
        self.assertEqual(present.count('game_phases::present_endpoint('), 1)

    def test_every_reset_entry_invalidates_and_accepted_attempts_advance_generation(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        reset_common = compact(extract_function(
            source,
            'HRESULT reset_common(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode,bool extended)',
        ))
        order = positions(
            reset_common,
            'game_phases::invalidate_device();',
            'if(ctx.bloom_busy||ctx.motion_output.composition_operation_active())returnD3DERR_INVALIDCALL;',
            '++ctx.reset_generation;',
            'constHRESULThr=extended',
        )
        self.assertEqual(order, sorted(order))
        self.assertEqual(reset_common.count('++ctx.reset_generation;'), 1)
        self.assertEqual(reset_common.count('game_phases::invalidate_device();'), 1)

        reset = compact(extract_function(
            source, 'HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p)'))
        reset_ex = compact(extract_function(
            source,
            'HRESULT WINAPI reset_ex(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode)',
        ))
        self.assertIn('returnreset_common(d,p,nullptr,false);', reset)
        self.assertIn('returnreset_common(d,p,mode,true);', reset_ex)

    def test_final_release_invalidates_before_metadata_retirement(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        release = compact(extract_function(
            source, 'ULONG WINAPI release_device(IDirect3DDevice9* d)'))
        final = compact(extract_function(release, 'if(!refs)'))
        order = positions(
            final,
            'game_phases::invalidate_device();',
            'telemetry::summary(devices.at(d)->stats,"device_destroy",devices.at(d)->frame);',
            'devices.erase(d);',
        )
        self.assertEqual(order, sorted(order))
        self.assertEqual(release.count('game_phases::invalidate_device();'), 1)

    def test_initialize_and_report_are_confined_to_existing_safe_boundaries(self):
        capture_source = (ROOT / 'src/proxy/capture.cpp').read_text()
        initialize = compact(extract_function(capture_source, 'void initialize_log(HMODULE module)'))
        init_order = positions(
            initialize,
            'telemetry::initialize([]{if(logfile)fflush(logfile);});',
            'game_phases::initialize();',
            'loading_trace::initialize();',
        )
        self.assertEqual(init_order, sorted(init_order))

        production_sources = list((ROOT / 'src').rglob('*.cpp'))
        initialize_calls = [
            path for path in production_sources
            if 'game_phases::initialize();' in path.read_text()
        ]
        self.assertEqual(initialize_calls, [ROOT / 'src/proxy/capture.cpp'])
        self.assertEqual(capture_source.count('game_phases::initialize();'), 1)

        telemetry_source = (ROOT / 'src/proxy/telemetry.cpp').read_text()
        summary = compact(extract_function(
            telemetry_source, 'void summary(State& state,const char* reason,uint64_t frame)'))
        report_order = positions(
            summary,
            'engine_memory_line("summary",state.device,frame);',
            'if(state.device)game_phases::report(frame);',
            'if(flush_output)',
        )
        self.assertEqual(report_order, sorted(report_order))
        self.assertEqual(telemetry_source.count('game_phases::report(frame);'), 1)


if __name__ == '__main__':
    unittest.main()
