// Exercise the actual production marker emitters and CPU callback boundary.
// Native code below is synthetic fixture code; the independent host site
// verifier binds the real game spans. This executable never launches the game.
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/game_phases.h"
#include "../../src/proxy/game_phase_sites.h"
#include "../../src/proxy/frame_phases.h"
#include "../../src/proxy/frame_phase_sites.h"
#include "../../src/proxy/pass_phases.h"
#include "../../src/proxy/pass_phase_sites.h"
#include "../../src/proxy/loop_phases.h"
#include "../../src/proxy/loop_phase_sites.h"
#include "../../src/proxy/residual_phases.h"
#include "../../src/proxy/residual_phase_sites.h"
#include "../../src/proxy/media_cue.h"
#include "../../src/proxy/media_cue_sites.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace phases = x3m::game_phases;
namespace patch = x3m::engine_patch;
namespace marker = x3m::game_phases::sites;
namespace frame = x3m::frame_phases;
namespace frame_marker = x3m::frame_phases::sites;
constexpr unsigned total_stubs = marker::Count + frame_marker::Count; // frame stamps share the emitter, indexed after
                                                                      // the phase group
static HANDLE media_log_handle = INVALID_HANDLE_VALUE; // the media-cue checks' stand-in for the session log's OS handle
namespace x3m {
LONGLONG dll_load_qpc = 0;
// The media-cue rows go through log() since the logging tiers (the session log's buffer): the stand-in writes them to
// the checks' file (trailing newline stripped as the session log does), every other row to stdout as before.
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char line[512];
    int n = std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n < 0) return;
    if (unsigned(n) >= sizeof line) n = int(sizeof line) - 1;
    if (n > 0 && line[n - 1] == '\n') line[--n] = 0;
    const bool media = !std::strncmp(line, "media_cue_enter ", 16) || !std::strncmp(line, "media_video_blit ", 17);
    if (media && media_log_handle != INVALID_HANDLE_VALUE) {
        line[n] = '\n';
        DWORD written = 0;
        WriteFile(media_log_handle, line, DWORD(n + 1), &written, nullptr);
    } else
        std::printf("%s\n", line);
}
HANDLE log_handle() noexcept {
    return media_log_handle;
}
}
namespace x3m::telemetry {
bool enabled() {
    return true;
}
std::uint64_t frequency() {
    LARGE_INTEGER f{};
    return QueryPerformanceFrequency(&f) ? std::uint64_t(f.QuadPart) : 0;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
extern "C" {
struct Snapshot {
    std::uint32_t regs[9];
    unsigned char xmm[128], x87[108];
    std::uint32_t mxcsr;
};
Snapshot* fixture_output = nullptr;
std::uint32_t fixture_entry_esp = 0, fixture_exit_esp = 0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw = 0x077f;
std::uint32_t fixture_mxcsr = 0x3f80;
std::uint32_t fixture_flags = 0x647, fixture_normal_cpu = 0;
void fixture_call(std::uint32_t site, std::uint32_t args, std::uint32_t locals, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
}
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "movl %esp,_fixture_entry_esp\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n cmpl $0,_fixture_normal_cpu\n jne 1f\n fld1\n fldpi\n1:\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
    "movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
    "movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
    "movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
    "movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
static std::uint32_t callbacks[total_stubs]{}, observed[total_stubs][9]{};
static std::uint32_t stack_args[marker::Count][6]{};
static bool capture_args = false;
static void __cdecl hostile_callback(unsigned kind, const std::uint32_t* regs) {
    check(kind < total_stubs, "callback marker ID");
    if (kind >= total_stubs) return;
    ++callbacks[kind];
    std::memcpy(observed[kind], regs, sizeof observed[kind]);
    if (capture_args && (kind == marker::DelayedBegin || kind == marker::ColdBegin || kind == marker::PresentBegin ||
                         kind == marker::PublisherBegin || kind == marker::PlaybackBegin ||
                         kind == marker::CreateBegin || kind == marker::SeekBegin)) {
        const auto* native = reinterpret_cast<const std::uint32_t*>(std::uintptr_t(regs[3]) + 4);
        const unsigned words = kind == marker::PlaybackBegin                                   ? 6
                               : kind == marker::PresentBegin                                  ? 5
                               : (kind == marker::ColdBegin || kind == marker::PublisherBegin) ? 2
                                                                                               : 1;
        std::memcpy(stack_args[kind], native, words * sizeof *native);
    }
    unsigned cw = 0, mxcsr = 0, flags = 0;
    asm volatile("fnstcw %0\n stmxcsr %1\n pushfl\n popl %2" : "=m"(cw), "=m"(mxcsr), "=r"(flags)::"memory");
    check((cw & 0xffff) == 0x037f, "callback receives default x87 control");
    check(mxcsr == 0x1f80, "callback receives default MXCSR");
    check(!(flags & 0x400), "callback C ABI has clear direction flag");
    // Volatile and computational state must be restored even when injected
    // work changes every XMM register, the x87 stack/control and LastError.
    SetLastError(0xcafebabe);
    const unsigned hostile_mxcsr = 0x5f80;
    const unsigned short hostile_cw = 0x0b7f;
    asm volatile("fninit\n fldln2\n fld1\n fldcw %0\n ldmxcsr %1\n"
                 "pxor %%xmm0,%%xmm0\n pxor %%xmm1,%%xmm1\n pxor %%xmm2,%%xmm2\n pxor %%xmm3,%%xmm3\n"
                 "pxor %%xmm4,%%xmm4\n pxor %%xmm5,%%xmm5\n pxor %%xmm6,%%xmm6\n pxor %%xmm7,%%xmm7" ::"m"(hostile_cw),
                 "m"(hostile_mxcsr)
                 : "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
}
static void invoke(void* target, Snapshot& out) {
    fixture_output = &out;
    SetLastError(0x13572468);
    fixture_call(std::uint32_t(std::uintptr_t(target)), 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc, 0x6789abcd);
    check(GetLastError() == 0x13572468, "LastError preserved");
    check(fixture_entry_esp == fixture_exit_esp, "four-byte incoming caller stack balanced");
    check(!std::memcmp(fixture_input_x87, out.x87, 108), "incoming x87 image survives actual callback");
}
static void compare(const Snapshot& before, const Snapshot& after) {
    for (unsigned r = 0; r < 9; ++r)
        if (r != 3) check(before.regs[r] == after.regs[r], "GPR and EFLAGS match native baseline");
    check(!std::memcmp(before.xmm, after.xmm, 128), "all XMM registers match native baseline");
    check(before.mxcsr == after.mxcsr, "MXCSR matches native baseline");
    check(!std::memcmp(before.x87, after.x87, 108), "x87 image matches native baseline");
}
static void* marker_stub(unsigned kind, void* next) {
    void** slot = nullptr;
    void* stub = phases::fixture_emit(kind, &slot);
    check(stub && slot, "actual production stub emitted");
    if (!stub || !slot) return nullptr;
    check(patch::store_pointer(slot, next), "continuation slot installed");
    return stub;
}
extern "C" {
std::uint32_t replay_calls = 0, replay_request = 0, replay_cockpit = 0, replay_mode = 0;
std::uint32_t replay_choice = 0, replay_result = 0x24681357, replay_context = 0x76543210;
void replay_delayed();
void replay_audio();
void replay_cold();
void replay_present();
}
// These are deliberately synthetic ABI witnesses, not extracted game bodies.
asm(".text\n.globl _replay_delayed\n_replay_delayed:\n"
    "incl _replay_calls\n movl %eax,_replay_mode\n movl %ecx,_replay_cockpit\n"
    "movl 4(%esp),%eax\n movl %eax,_replay_request\n movl $0x24681357,%eax\n ret $4\n"
    ".globl _replay_audio\n_replay_audio:\n incl _replay_calls\n movl 12(%esp),%eax\n movl %eax,_replay_request\n movl $1,%eax\n ret\n"
    ".globl _replay_cold\n_replay_cold:\n incl _replay_calls\n movl 4(%esp),%eax\n movl %eax,_replay_request\n movl _replay_result,%eax\n ret\n"
    ".globl _replay_present\n_replay_present:\n incl _replay_calls\n movl 4(%esp),%eax\n movl %eax,_replay_request\n movl $0x88760868,%eax\n ret $20\n");
static std::uintptr_t address(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p);
}
static void push(patch::Emitter& e, std::uint32_t value) {
    e.byte(0x68);
    e.dword(value);
}
static void call(patch::Emitter& e, const void* function) {
    e.byte(0xe8);
    e.rel32(function);
}
struct Replay {
    void* body = nullptr;
    void* begin = nullptr;
    void* end = nullptr;
    unsigned first = 0, last = 0;
};
static Replay make_replay(unsigned kind) {
    patch::Emitter e(256);
    Replay r;
    r.body = e.here();
    if (kind == 0) {
        push(e, 0x11112222);
        e.byte(0xb8);
        e.dword(3);
        e.byte(0xb9);
        e.dword(0x33334444);
        r.first = marker::DelayedBegin;
        r.last = marker::DelayedEnd;
        r.begin = e.here();
        call(e, reinterpret_cast<void*>(&replay_delayed));
        r.end = e.here();
        e.byte(0xe9);
        e.dword(5);
        e.byte(0xb8);
        e.dword(0xbad00001);
        e.byte(0xc3);
    } else if (kind == 1) {
        e.byte(0xbe);
        e.dword(0x33334444);
        e.byte(0xbb);
        e.dword(0x11112222);
        e.byte(0x33);
        e.byte(0xed);
        r.first = marker::AcquisitionBegin;
        r.last = marker::AcquisitionEnd;
        r.begin = e.here();
        const unsigned char arguments[] = {0x6a, 0xff, 0x6a, 0x01, 0x55, 0x55};
        e.bytes(arguments, sizeof arguments);
        call(e, reinterpret_cast<void*>(&replay_audio));
        e.byte(0x83);
        e.byte(0xc4);
        e.byte(0x10);
        e.byte(0x83);
        e.byte(0x3d);
        e.dword(std::uint32_t(address(&replay_choice)));
        e.byte(0);
        e.byte(0x74);
        e.byte(14); // skip the complete conditional cdecl call
        e.bytes(arguments, sizeof arguments);
        call(e, reinterpret_cast<void*>(&replay_audio));
        e.byte(0x83);
        e.byte(0xc4);
        e.byte(0x10);
        r.end = e.here();
        e.byte(0x8b);
        e.byte(0x15);
        e.dword(std::uint32_t(address(&replay_context)));
        e.byte(0xc3);
    } else if (kind == 2) {
        push(e, 0x20);
        push(e, 0x11112222);
        r.first = marker::ColdBegin;
        r.last = marker::ColdEnd;
        r.begin = e.here();
        call(e, reinterpret_cast<void*>(&replay_cold));
        r.end = e.here();
        const unsigned char ending[] = {0x83, 0xc4, 0x08, 0x85, 0xc0, 0x75, 0x05, 0xb8, 0x02, 0, 0xad, 0x0b, 0xc3};
        e.bytes(ending, sizeof ending);
    } else {
        static std::uint32_t vtable[18]{};
        vtable[17] = std::uint32_t(address(reinterpret_cast<void*>(&replay_present)));
        e.byte(0xba);
        e.dword(std::uint32_t(address(vtable)));
        for (unsigned i = 0; i < 4; ++i) {
            push(e, 0);
        }
        push(e, 0x11112222);
        r.first = marker::PresentBegin;
        r.last = marker::PresentEnd;
        r.begin = e.here();
        const unsigned char dispatch[] = {0x8b, 0x42, 0x44, 0xff, 0xd0};
        e.bytes(dispatch, sizeof dispatch);
        r.end = e.here();
        const unsigned char ending[] = {0x3d, 0x68, 0x08, 0x76, 0x88, 0x75, 0x05, 0xb8, 0x03, 0, 0xad, 0x0b, 0xc3};
        e.bytes(ending, sizeof ending);
    }
    if (!e.finish()) r.body = nullptr;
    return r;
}
static bool install_replay_marker(patch::Site& owned, unsigned kind, void* at) {
    auto spec = marker::kSites[kind];
    spec.address = address(at);
    // Native branch destinations/global pointers belong to this fixture.
    // Length, operand form and relocation metadata remain the actual contract.
    std::memcpy(spec.expected, at, spec.length);
    if (!patch::claim(owned, spec)) {
        std::printf("FAIL claim %u %s\n", kind, owned.status);
        check(false, "fixture span claimed");
        return false;
    }
    void* stub = marker_stub(kind, owned.tail);
    if (!stub) return false;
    patch::push_front(owned, stub);
    return true;
}
static void replay_checks(unsigned kind, unsigned variant) {
    Replay r = make_replay(kind);
    check(r.body != nullptr, "synthetic native body emitted");
    if (!r.body) return;
    replay_choice = variant;
    replay_result = variant ? 0 : 0x24681357;
    Snapshot baseline{}, hooked{};
    replay_calls = 0;
    invoke(r.body, baseline);
    const unsigned native_calls = replay_calls;
    check(native_calls == (kind == 1 ? 1 + variant : 1), "baseline executes exact native call count");
    patch::Site before{}, after{};
    if (!install_replay_marker(before, r.first, r.begin) || !install_replay_marker(after, r.last, r.end)) return;
    const unsigned begin_calls = callbacks[r.first], end_calls = callbacks[r.last];
    capture_args = true;
    replay_calls = 0;
    invoke(r.body, hooked);
    capture_args = false;
    compare(baseline, hooked);
    check(replay_calls == native_calls, "instrumented native calls execute exactly once each");
    check(callbacks[r.first] == begin_calls + 1 && callbacks[r.last] == end_calls + 1,
          "paired endpoints both observed once");
    const auto begin_esp = observed[r.first][3] + 4, end_esp = observed[r.last][3] + 4;
    check(end_esp == begin_esp + (kind == 0 ? 4 : kind == 3 ? 20 : 0), "native cdecl/custom/stdcall cleanup preserved");
    if (kind == 0) {
        check(replay_mode == 3 && replay_cockpit == 0x33334444 && replay_request == 0x11112222,
              "native custom-register inputs preserved");
        check(observed[r.first][7] == 3 && observed[r.first][6] == 0x33334444 && stack_args[r.first][0] == 0x11112222,
              "delayed marker sees original ABI inputs");
        check(hooked.regs[7] == 0x24681357, "relocated JMP bypasses dead fallthrough");
    } else if (kind == 1) {
        check(observed[r.first][1] == 0x33334444 && observed[r.first][4] == 0x11112222,
              "acquisition captures requested target and cockpit");
        check(replay_request == 1, "native sound ID argument preserved");
        check(hooked.regs[5] == replay_context, "acquisition endpoint global load replayed");
    } else if (kind == 2) {
        check(stack_args[r.first][0] == 0x11112222 && stack_args[r.first][1] == 0x20,
              "cold-load stack inputs preserved");
        check(hooked.regs[7] == (variant ? 0x0bad0002 : 0x24681357),
              "replayed TEST drives the native conditional branch");
    } else {
        check(stack_args[r.first][0] == 0x11112222, "Present begin captures original raw device");
        for (unsigned i = 1; i < 5; ++i) check(stack_args[r.first][i] == 0, "Present four null arguments preserved");
        check(observed[r.last][7] == 0x88760868, "Present end observes native HRESULT before comparison");
        check(hooked.regs[7] == 0x0bad0003, "Present HRESULT CMP replay controls native branch");
    }
    check(patch::restore(after) && patch::restore(before), "synthetic marker rollback restores both spans");
}
static inline double number(std::uint64_t value) {
    return double(std::uint32_t(value >> 32)) * 4294967296.0 + double(std::uint32_t(value));
}
extern "C" {
std::uint32_t target_native_counts[4]{}, target_play_args[6]{}, target_create_args[2]{}, target_seek_args[2]{};
std::uint32_t target_target = 0x11112222, target_cockpit = 0, target_mode = 3;
void target_play_witness();
void target_create_witness();
void target_seek_witness();
}
// Playback's helper is called by its synthetic callee: its native arguments
// are two return PCs above ESP. The create/seek helpers are the direct callees.
asm(".text\n.globl _target_play_witness\n_target_play_witness:\n"
    "incl _target_native_counts+4\n"
    "movl 8(%esp),%eax\n movl %eax,_target_play_args\n movl 12(%esp),%eax\n movl %eax,_target_play_args+4\n"
    "movl 16(%esp),%eax\n movl %eax,_target_play_args+8\n movl 20(%esp),%eax\n movl %eax,_target_play_args+12\n"
    "movl 24(%esp),%eax\n movl %eax,_target_play_args+16\n movl 28(%esp),%eax\n movl %eax,_target_play_args+20\n ret\n"
    ".globl _target_create_witness\n_target_create_witness:\n incl _target_native_counts+8\n"
    "movl %eax,_target_create_args+4\n movl 4(%esp),%eax\n movl %eax,_target_create_args\n movl $0x24681357,%eax\n ret\n"
    ".globl _target_seek_witness\n_target_seek_witness:\n incl _target_native_counts+12\n"
    "movl %eax,_target_seek_args+4\n movl 4(%esp),%eax\n movl %eax,_target_seek_args\n movl _replay_result,%eax\n ret\n");
static constexpr std::uint32_t playback_args[6] = {7, 0, 0, 1, 0xffffffff, 0};
struct TargetReplay {
    void* body = nullptr;
    void* publisher_return = nullptr;
    void* spans[8]{};
    patch::Site owned[8]{};
    unsigned installed = 0;
};
static TargetReplay make_target_replay() {
    TargetReplay r;
    patch::Emitter create(128);
    void* create_body = create.here();
    create.byte(0xbf);
    create.dword(2); // native shared join decrements EDI
    create.byte(0x83);
    create.byte(0x3d);
    create.dword(std::uint32_t(address(&replay_choice)));
    create.byte(1);
    create.byte(0x74);
    create.byte(15); // bypass push, EAX setup, CALL and cleanup
    push(create, 7);
    create.byte(0x33);
    create.byte(0xc0);
    r.spans[4] = create.here();
    call(create, reinterpret_cast<void*>(&target_create_witness));
    const unsigned char create_cleanup[] = {0x83, 0xc4, 0x04};
    create.bytes(create_cleanup, sizeof create_cleanup);
    r.spans[5] = create.here();
    const unsigned char create_end[] = {0x83, 0xef, 0x01, 0x66, 0x85, 0xff, 0x75,
                                        0x05, 0xb8, 0x05, 0,    0xad, 0x0b, 0xc3};
    create.bytes(create_end, sizeof create_end);
    if (!create.finish()) return r;
    patch::Emitter seek(64);
    void* seek_body = seek.here();
    push(seek, 23);
    seek.byte(0xb8);
    seek.dword(0x33334444);
    r.spans[6] = seek.here();
    call(seek, reinterpret_cast<void*>(&target_seek_witness));
    r.spans[7] = seek.here();
    const unsigned char seek_end[] = {0x83, 0xc4, 0x04, 0x85, 0xc0, 0x75, 0x05, 0xb8, 0x04, 0, 0xad, 0x0b, 0xc3};
    seek.bytes(seek_end, sizeof seek_end);
    if (!seek.finish()) return r;
    patch::Emitter media(64);
    void* media_body = media.here();
    call(media, reinterpret_cast<void*>(&target_play_witness));
    call(media, create_body);
    call(media, seek_body);
    media.byte(0xc3);
    if (!media.finish()) return r;
    patch::Emitter playback(128);
    void* playback_body = playback.here();
    playback.byte(0x57); // native dispatcher saves EDI below its six cdecl args
    for (unsigned i = 6; i; --i) push(playback, playback_args[i - 1]);
    r.spans[2] = playback.here();
    call(playback, media_body);
    r.spans[3] = playback.here();
    const unsigned char playback_end[] = {0x83, 0xc4, 0x18, 0x5f, 0xb8, 0x01, 0, 0, 0, 0xc3};
    playback.bytes(playback_end, sizeof playback_end);
    if (!playback.finish()) return r;
    patch::Emitter publisher(128);
    void* publisher_body = publisher.here();
    r.spans[0] = publisher_body;
    // Exact 425a10 prologue, remaining three saves, and exact common epilogue.
    const unsigned char publisher_start[] = {0x53, 0x8b, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x8b, 0xf1};
    publisher.bytes(publisher_start, sizeof publisher_start);
    publisher.byte(0xff);
    publisher.byte(0x05);
    publisher.dword(std::uint32_t(address(target_native_counts)));
    const unsigned char gates[] = {0x85, 0xdb, 0x74, 0x0a, 0x83, 0xf8, 0x03, 0x75, 0x05};
    publisher.bytes(gates, sizeof gates);
    call(publisher, playback_body);
    r.spans[1] = publisher.here();
    const unsigned char publisher_end[] = {0x5f, 0x5e, 0x5d, 0x5b, 0xc2, 0x04, 0};
    publisher.bytes(publisher_end, sizeof publisher_end);
    if (!publisher.finish()) return r;
    patch::Emitter wrapper(64);
    void* body = wrapper.here();
    wrapper.byte(0xff);
    wrapper.byte(0x35);
    wrapper.dword(std::uint32_t(address(&target_target)));
    wrapper.byte(0xa1);
    wrapper.dword(std::uint32_t(address(&target_mode)));
    wrapper.byte(0x8b);
    wrapper.byte(0x0d);
    wrapper.dword(std::uint32_t(address(&target_cockpit)));
    call(wrapper, publisher_body);
    r.publisher_return = wrapper.here();
    wrapper.byte(0xc3);
    if (wrapper.finish()) r.body = body;
    return r;
}
static bool span_contract(unsigned kind, const void* at) {
    const auto& spec = marker::kSites[kind];
    const auto* bytes = static_cast<const unsigned char*>(at);
    bool okay = true;
    for (unsigned i = 0; i < spec.length; ++i) {
        if (spec.rel32_offset && i >= spec.rel32_offset && i < spec.rel32_offset + 4) continue;
        okay = okay && bytes[i] == spec.expected[i];
    }
    check(okay, "synthetic span matches proved native opcodes except relocated call destination");
    return okay;
}
static bool install_target_replay(TargetReplay& r) {
    for (unsigned i = 0; i < 8; ++i) {
        if (!span_contract(marker::PublisherBegin + i, r.spans[i])) return false;
        if (!install_replay_marker(r.owned[i], marker::PublisherBegin + i, r.spans[i])) return false;
        ++r.installed;
    }
    return true;
}
static void restore_target_replay(TargetReplay& r) {
    while (r.installed) check(patch::restore(r.owned[--r.installed]), "target synthetic span rollback");
}
static void target_variant(unsigned variant) {
    target_target = (variant == 1 || variant == 2) ? 0 : 0x11112222;
    target_mode = variant == 2 ? 2 : 3;
    replay_choice = variant == 3 ? 1 : 0;
    replay_result = variant == 4 ? 0 : 0x24681357;
    std::memset(target_native_counts, 0, sizeof target_native_counts);
}
static void target_replay_checks() {
    TargetReplay r = make_target_replay();
    check(r.body != nullptr, "nested target synthetic body emitted");
    if (!r.body) return;
    // The executable arena is monotonic. Capture every native variant before
    // one installation, then reuse those eight stubs for every hooked variant.
    Snapshot baselines[5]{};
    std::uint32_t native_counts[5][4]{};
    for (unsigned variant = 0; variant < 5; ++variant) {
        target_variant(variant);
        invoke(r.body, baselines[variant]);
        std::memcpy(native_counts[variant], target_native_counts, sizeof native_counts[variant]);
    }
    if (!install_target_replay(r)) {
        restore_target_replay(r);
        return;
    }
    for (unsigned variant = 0; variant < 5; ++variant) {
        Snapshot hooked{};
        std::uint32_t before[8]{};
        for (unsigned i = 0; i < 8; ++i) before[i] = callbacks[marker::PublisherBegin + i];
        target_variant(variant);
        capture_args = true;
        invoke(r.body, hooked);
        capture_args = false;
        compare(baselines[variant], hooked);
        const bool nested = variant != 1 && variant != 2;
        for (unsigned i = 0; i < 4; ++i)
            check(target_native_counts[i] == native_counts[variant][i] &&
                      native_counts[variant][i] == (i == 0                   ? 1
                                                    : !nested                ? 0
                                                    : i == 2 && variant == 3 ? 0
                                                                             : 1),
                  "nested native calls and bypass execute exact count");
        for (unsigned i = 0; i < 8; ++i)
            check(callbacks[marker::PublisherBegin + i] - before[i] == (i < 2                    ? 1
                                                                        : !nested                ? 0
                                                                        : i == 4 && variant == 3 ? 0
                                                                                                 : 1),
                  "nested endpoint or common join reached exact count");
        const auto pub = marker::PublisherBegin;
        check(observed[pub][7] == target_mode && observed[pub][6] == target_cockpit &&
                  stack_args[pub][1] == target_target,
              "publisher entry EAX ECX and target at ESP+4 preserved");
        check(stack_args[pub][0] == address(r.publisher_return), "publisher native return PC at entry ESP preserved");
        check(observed[pub + 1][3] + 16 == observed[pub][3],
              "publisher end ESP equals entry ESP minus four saved registers");
        check(hooked.regs[0] == 0x98765432 && hooked.regs[1] == 0x12345678 && hooked.regs[2] == 0x3456789a &&
                  hooked.regs[4] == 0x23456789,
              "publisher POP x4 RET4 preserves native callee saves");
        if (nested) {
            const auto play = marker::PlaybackBegin;
            check(!std::memcmp(stack_args[play], playback_args, sizeof playback_args) &&
                      !std::memcmp(target_play_args, playback_args, sizeof playback_args),
                  "MOV6 six cdecl arguments reach marker and native callee");
            check(observed[play][3] == observed[play + 1][3] && hooked.regs[7] == 1,
                  "MOV6 end precedes cleanup24 and replays POP EDI MOV EAX 1");
            if (variant != 3) {
                const auto create = marker::CreateBegin;
                check(stack_args[create][0] == 7 && observed[create][7] == 0 && target_create_args[0] == 7 &&
                          target_create_args[1] == 0,
                      "create direct-call stack argument and EAX zero preserved");
                check(observed[create + 1][3] == observed[create][3] + 4, "create endpoint follows caller cleanup4");
            }
            const auto seek = marker::SeekBegin;
            check(stack_args[seek][0] == 23 && observed[seek][7] == 0x33334444 && target_seek_args[0] == 23 &&
                      target_seek_args[1] == 0x33334444,
                  "seek direct-call stack argument and stream register preserved");
            check(observed[seek][3] == observed[seek + 1][3] && observed[seek + 1][7] == replay_result,
                  "seek endpoint precedes cleanup and sees original result");
            check(observed[play + 1][7] == (variant == 4 ? 0x0bad0004 : 0x24681357),
                  "seek TEST zero and nonzero outcomes control downstream native branch");
        }
    }
    restore_target_replay(r);
}
static void input_replay_checks(unsigned variant) {
    std::uint32_t control[0x500 / 4]{};
    control[0x4d8 / 4] = variant;
    control[0x4a0 / 4] = variant ? 4 : 0;
    patch::Emitter e(96);
    void* body = e.here();
    e.byte(0xbe);
    e.dword(std::uint32_t(address(control)));
    e.byte(0x33);
    e.byte(0xed);
    e.byte(0x33);
    e.byte(0xc0);
    void* before = e.here();
    const unsigned char cmp[] = {0x39, 0xae, 0xd8, 0x04, 0, 0, 0x75, 0x05, 0xb8, 1, 0, 0, 0};
    e.bytes(cmp, sizeof cmp);
    void* after = e.here();
    const unsigned char test[] = {0xf6, 0x86, 0xa0, 0x04, 0, 0, 4, 0x74, 0x05, 0xb8, 2, 0, 0, 0, 0xc3};
    e.bytes(test, sizeof test);
    const bool emitted = e.finish() != nullptr;
    check(emitted, "input CMP TEST synthetic body emitted");
    if (!emitted) return;
    if (!span_contract(marker::InputBody, before) || !span_contract(marker::InputAfter, after)) return;
    Snapshot baseline{}, hooked{};
    invoke(body, baseline);
    patch::Site first{}, last{};
    if (!install_replay_marker(first, marker::InputBody, before)) return;
    if (!install_replay_marker(last, marker::InputAfter, after)) {
        check(patch::restore(first), "partial input marker rollback");
        return;
    }
    const auto body_calls = callbacks[marker::InputBody], after_calls = callbacks[marker::InputAfter];
    invoke(body, hooked);
    compare(baseline, hooked);
    check(callbacks[marker::InputBody] == body_calls + 1 && callbacks[marker::InputAfter] == after_calls + 1,
          "both input partition endpoints execute exactly once");
    check(hooked.regs[7] == (variant ? 2 : 1), "input original CMP and TEST drive both branch outcomes");
    check(patch::restore(last) && patch::restore(first), "input original spans restored");
}
static constexpr std::uint32_t benchmark_raw_device = 0x11112222;
static void* benchmark_begin_adapter(void* next) {
    patch::Emitter e(64);
    void* start = e.here();
    for (unsigned i = 0; i < 4; ++i) {
        push(e, 0);
    }
    push(e, benchmark_raw_device);
    e.byte(0xe9);
    e.rel32(next);
    return e.finish() ? start : nullptr;
}
static bool timed_loops(void* continuation, void* const* stubs, void* const* present_begin, unsigned mode,
                        unsigned loops, std::uint64_t& ticks) {
    phases::fixture_enable(mode == 2);
    Snapshot output{};
    fixture_output = &output;
    const auto one_loop = [&](unsigned frame) {
        // The motion route advances this epoch once per frame. Include the
        // common advance in all three modes; enabled metadata reads then pay
        // their ordinary first-touch region validation in every loop.
        x3m::engine_memory::next_frame();
        for (unsigned marker_id = 0; marker_id < 13; ++marker_id) {
            void* target = mode ? stubs[marker_id] : continuation;
            fixture_call(std::uint32_t(address(target)), 0, 0, 0, 0, 0);
            if (marker_id == marker::Input) {
                fixture_call(std::uint32_t(address(mode ? stubs[marker::InputBody] : continuation)), 0, 0, 0, 0, 0);
                fixture_call(std::uint32_t(address(mode ? stubs[marker::InputAfter] : continuation)), 0, 0, 0, 0, 0);
            }
        }
        // The native caller has pushed device plus four null arguments before
        // PresentBegin. Both adapters reproduce those pushes and cleanup; only
        // the hooked adapter dispatches the actual marker callback between them.
        fixture_call(std::uint32_t(address(present_begin[mode ? 1 : 0])), 0, 0, 0, 0, 0);
        LARGE_INTEGER endpoint{};
        if (!QueryPerformanceCounter(&endpoint) || endpoint.QuadPart <= 0) return false;
        phases::present_endpoint(benchmark_raw_device, 1, 1, frame, false, std::uint64_t(endpoint.QuadPart), 0);
        fixture_call(std::uint32_t(address(mode ? stubs[marker::PresentEnd] : continuation)), 0, 0, 0, 0, 0);
        fixture_call(std::uint32_t(address(mode ? stubs[marker::Tail] : continuation)), 0, 0, 0, 0, 0);
        return true;
    };
    // Prime one completed dispatch outside the paired timing window. Every
    // measured loop now has a real anchor, appends finite normal tape segments,
    // stages its owned endpoint, and closes the dispatch with matching HRESULT.
    // Runtime report totals include this unmeasured warm-up loop per batch.
    if (!one_loop(0)) return false;
    LARGE_INTEGER start{}, end{};
    if (!QueryPerformanceCounter(&start) || start.QuadPart <= 0) return false;
    for (unsigned loop = 0; loop < loops; ++loop)
        if (!one_loop(loop + 1)) return false;
    if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
    ticks = std::uint64_t(end.QuadPart - start.QuadPart);
    return true;
}
static void benchmark(void* continuation, void* const* stubs) {
    constexpr unsigned loops = 4, batches = 128, trials = 3;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "benchmark QPC frequency");
    if (frequency.QuadPart <= 0) return;
    phases::fixture_set_callback(nullptr);
    fixture_normal_cpu = 1;
    fixture_cw = 0x037f;
    fixture_mxcsr = 0x1f80;
    fixture_flags = 0x247;
    // Native stdcall has consumed all five words before the next observation.
    patch::Emitter cleanup(8);
    void* clean = cleanup.here();
    const unsigned char pop_arguments[] = {0x83, 0xc4, 0x14, 0xc3};
    cleanup.bytes(pop_arguments, sizeof pop_arguments);
    check(cleanup.finish() != nullptr, "benchmark Present argument cleanup emitted");
    void* begin_stub = marker_stub(marker::PresentBegin, clean);
    void* present_begin[2] = {benchmark_begin_adapter(clean), benchmark_begin_adapter(begin_stub)};
    check(begin_stub && present_begin[0] && present_begin[1], "paired Present benchmark adapters emitted");
    if (!begin_stub || !present_begin[0] || !present_begin[1]) return;
    // Ask Windows for an unused region. A fixture-only address seam keeps the
    // actual checked reads/cache path without depending on free game addresses.
    void* const pump_globals = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(pump_globals != nullptr, "dynamic synthetic pump-global mapping allocated");
    if (!pump_globals) return;
    auto* const bytes = static_cast<unsigned char*>(pump_globals);
    *reinterpret_cast<std::uint32_t*>(bytes + 0x8adc) = 1; // active window
    *reinterpret_cast<std::uint32_t*>(bytes + 0x6f3c) = std::uint32_t(address(bytes + 0x9000));
    *reinterpret_cast<std::uint32_t*>(bytes + 0x9000) = 0; // ordinary active-mode flags
    const bool configured = phases::fixture_pump_region(address(pump_globals), 0x10000);
    check(configured, "bounded fixture pump address region configured");
    if (!configured) {
        VirtualFree(pump_globals, 0, MEM_RELEASE);
        return;
    }
    std::uint64_t totals[3]{};
    bool valid = true;
    for (unsigned trial = 0; trial < trials && valid; ++trial)
        for (unsigned batch = 0; batch < batches && valid; ++batch)
            for (unsigned position = 0; position < 3 && valid; ++position) {
                // Balanced order; initialization and mode switching are excluded.
                const unsigned mode = (position + trial + 1) % 3;
                std::uint64_t ticks = 0;
                valid = timed_loops(continuation, stubs, present_begin, mode, loops, ticks);
                totals[mode] += ticks;
            }
    check(valid, "paired baseline disabled enabled QPC samples");
    const double scale = 1e6 / number(std::uint64_t(frequency.QuadPart)) / double(loops * batches * trials);
    std::printf(
        "GAME PHASE BENCH trials=%u batches=%u loops_per_batch=%u marker_calls_per_loop=18 owned_bridges_per_loop=1 cpu_queries_per_enabled_loop=17 baseline_loop_us=%.6f disabled_loop_us=%.6f enabled_loop_us=%.6f disabled_added_loop_us=%.6f enabled_added_loop_us=%.6f enabled_added_per_marker_equivalent_us=%.6f scope=actual_emit_callback_core_owned_bridge_GetThreadTimes harness=paired_same_snapshot normal_tape=yes pump_metadata=valid engine_read_epoch=per_loop warmup_loops_per_batch=1 runtime_report_includes_warmup=yes game_fps=unmeasured\n",
        trials, batches, loops, number(totals[0]) * scale, number(totals[1]) * scale, number(totals[2]) * scale,
        (number(totals[1]) - number(totals[0])) * scale, (number(totals[2]) - number(totals[0])) * scale,
        (number(totals[2]) - number(totals[0])) * scale / 18.0);
    // This is outside the measured batches. The production report supplies raw
    // GetThreadTimes query-sandwich totals/maxima and actual handler costs.
    phases::report(0);
    phases::fixture_enable(false);
    check(phases::fixture_pump_region(0, 0), "fixture pump address region cleared before free");
    x3m::engine_memory::reset();
    check(VirtualFree(pump_globals, 0, MEM_RELEASE) != FALSE, "synthetic pump-global mapping released");
}
struct TargetState {
    std::uint64_t completed[4]{}, ignored = 0, reads = 0;
    unsigned depth = 0;
};
static bool target_state(TargetState& state) {
    const bool okay = phases::fixture_target_state(state.completed, &state.ignored, &state.reads, &state.depth);
    check(okay, "owned actual target handler state available");
    return okay;
}
static void admit_target_phase(void* const* stubs, unsigned mode, void* continuation) {
    phases::fixture_enable(mode == 2);
    for (unsigned i = 0; i <= marker::Input; ++i)
        fixture_call(std::uint32_t(address(mode ? stubs[i] : continuation)), 0, 0, 0, 0, 0);
    fixture_call(std::uint32_t(address(mode ? stubs[marker::InputBody] : continuation)), 0, 0, 0, 0, 0);
}
static void check_target_delta(const TargetState& before, const TargetState& after, unsigned variant,
                               unsigned repetitions) {
    const bool nested = variant != 1 && variant != 2;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned expected = i == 0                   ? (variant == 2 ? 0 : repetitions)
                                  : !nested                ? 0
                                  : i == 2 && variant == 3 ? 0
                                                           : repetitions;
        check(after.completed[i] - before.completed[i] == expected,
              "actual handler matches expected target stack tokens");
    }
    check(after.ignored - before.ignored == ((variant == 2 || variant == 3) ? repetitions : 0),
          "only native untracked shared joins are ignored");
    check(after.reads == before.reads, "actual target checked reads have zero new failures");
    check(before.depth == 0 && after.depth == 0, "all actual target handler tokens close at native ESP");
}
static void targeted_benchmark(void* continuation, void* const* stubs) {
    // The same synthetic native bodies execute in all modes. Two copies allow
    // a true unpatched baseline without patching or generating code in a clock
    // window. The hooked copy always traverses the actual production emitter.
    TargetReplay native = make_target_replay(), hooked = make_target_replay();
    check(native.body && hooked.body, "paired targeted burst native bodies emitted");
    if (!native.body || !hooked.body) return;
    if (!install_target_replay(hooked)) {
        restore_target_replay(hooked);
        return;
    }
    auto* bytes = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(bytes != nullptr, "dynamic target cockpit and pump witness allocated");
    if (!bytes) {
        restore_target_replay(hooked);
        return;
    }
    *reinterpret_cast<std::uint32_t*>(bytes + 0x8adc) = 1;
    *reinterpret_cast<std::uint32_t*>(bytes + 0x6f3c) = std::uint32_t(address(bytes + 0x9000));
    *reinterpret_cast<std::uint32_t*>(bytes + 0x9000) = 0;
    target_cockpit = std::uint32_t(address(bytes + 0x1000));
    *reinterpret_cast<std::uint32_t*>(bytes + 0x1010) = 0x22224444; // raw view witness only
    *reinterpret_cast<std::uint32_t*>(bytes + 0x11e0) = 0x55556666;
    *reinterpret_cast<std::uint32_t*>(bytes + 0x11e4) = 0xabcd0002;
    phases::fixture_enable(false);
    const bool configured = phases::fixture_pump_region(address(bytes), 0x10000);
    check(configured, "dynamic target benchmark pump region configured");
    if (!configured) {
        VirtualFree(bytes, 0, MEM_RELEASE);
        restore_target_replay(hooked);
        return;
    }
    phases::fixture_set_callback(nullptr);
    Snapshot output{};
    fixture_output = &output;
    // Functional checks use the real checked-read handler, including the short
    // mode3 null interval (no cockpit dereference) and rejected mode2 epilogue.
    for (unsigned variant = 0; variant < 5; ++variant) {
        x3m::engine_memory::next_frame();
        admit_target_phase(stubs, 2, continuation);
        target_variant(variant);
        // Deliberately unreadable cockpit proves that rejected null/mode2
        // entries do not touch the nonnull mode3 cockpit witness fields.
        target_cockpit = (variant == 1 || variant == 2) ? 1 : std::uint32_t(address(bytes + 0x1000));
        TargetState before{}, after{};
        if (!target_state(before)) break;
        invoke(hooked.body, output);
        if (target_state(after)) check_target_delta(before, after, variant, 1);
    }
    constexpr unsigned requests = 16, batches = 32, trials = 3;
    static const char* const variants[] = {"nonnull_mode3", "null_mode3", "rejected_null_mode2"};
    LARGE_INTEGER frequency{};
    bool valid = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0;
    for (unsigned variant = 0; variant < 3 && valid; ++variant) {
        const bool nested = variant == 0;
        target_cockpit = nested ? std::uint32_t(address(bytes + 0x1000)) : 1;
        std::uint64_t totals[3]{};
        for (unsigned trial = 0; trial < trials && valid; ++trial)
            for (unsigned batch = 0; batch < batches && valid; ++batch)
                for (unsigned position = 0; position < 3 && valid; ++position) {
                    const unsigned mode = (position + trial + 1) % 3;
                    admit_target_phase(stubs, mode, continuation);
                    target_variant(variant);
                    TargetState before{}, after{};
                    if (mode == 2 && !target_state(before)) {
                        valid = false;
                        break;
                    }
                    void* body = mode ? hooked.body : native.body;
                    LARGE_INTEGER start{}, end{};
                    valid = QueryPerformanceCounter(&start) && start.QuadPart > 0;
                    if (!valid) break;
                    for (unsigned request = 0; request < requests; ++request) {
                        // Conservative first-touch validation once per request,
                        // shared by all modes, instead of perpetual cache hits.
                        x3m::engine_memory::next_frame();
                        fixture_call(std::uint32_t(address(body)), 0, 0, 0, 0, 0);
                    }
                    valid = QueryPerformanceCounter(&end) && end.QuadPart >= start.QuadPart;
                    if (valid) totals[mode] += std::uint64_t(end.QuadPart - start.QuadPart);
                    if (mode == 2 && target_state(after)) check_target_delta(before, after, variant, requests);
                    for (unsigned i = 0; i < 4; ++i)
                        check(target_native_counts[i] == ((i == 0 || nested) ? requests : 0),
                              "target benchmark executes exactly the native bodies admitted by this variant");
                }
        if (valid) {
            const double scale = 1e6 / number(std::uint64_t(frequency.QuadPart)) / double(requests * batches * trials);
            std::printf(
                "GAME PHASE TARGET BENCH variant=%s trials=%u batches=%u requests_per_batch=%u marker_calls_per_request=%u cpu_queries_per_request=0 native_request_us=%.6f disabled_request_us=%.6f enabled_request_us=%.6f disabled_added_request_us=%.6f enabled_added_request_us=%.6f scope=actual_emit_checked_reads_ESP_tokens witness=dynamic_cockpit_and_native_stack engine_read_epoch=per_request admission_outside_clock=yes game_fps=unmeasured\n",
                variants[variant], trials, batches, requests, nested ? 8u : 2u, number(totals[0]) * scale,
                number(totals[1]) * scale, number(totals[2]) * scale, (number(totals[1]) - number(totals[0])) * scale,
                (number(totals[2]) - number(totals[0])) * scale);
        }
        phases::report(0);
    }
    check(valid, "paired native disabled enabled targeted burst QPC samples for all three variants");
    phases::fixture_enable(false);
    check(phases::fixture_pump_region(0, 0), "target pump witness cleared before free");
    x3m::engine_memory::reset();
    target_cockpit = 0;
    check(VirtualFree(bytes, 0, MEM_RELEASE) != FALSE, "dynamic target witness released");
    restore_target_replay(hooked);
}
extern "C" {
std::uint32_t frame_native_calls = 0;
void frame_callee();
void frame_callee_ret4();
}
asm(".text\n.globl _frame_callee\n_frame_callee:\n incl _frame_native_calls\n ret\n"
    ".globl _frame_callee_ret4\n_frame_callee_ret4:\n incl _frame_native_calls\n ret $4\n");
// One synthetic render frame: the ten real spans in the game's order with a
// two-iteration view loop whose back edge lands on the view_setup_begin span
// start (as 0x472387 does on the game's loop). Call/disp32 fields point at
// fixture objects; opcodes, lengths and relocation metadata are the contract.
struct FrameBody {
    void* body = nullptr;
    void* spans[frame_marker::Count]{};
    unsigned length = 0;
};
static std::uint32_t frame_view[0x20]{}, frame_layers[0x20]{}, frame_global_a = 0x1111, frame_global_b = 0x2222;
static FrameBody make_frame_body() {
    frame_view[0x1c / 4] = std::uint32_t(address(frame_layers));
    patch::Emitter e(128);
    FrameBody r;
    r.body = e.here();
    e.byte(0xbe);
    e.dword(std::uint32_t(address(frame_view)));
    for (unsigned i = 1; i <= 4; ++i) push(e, i); // the qsort arguments Views' add esp,0x10 removes
    r.spans[0] = e.here();
    call(e, reinterpret_cast<void*>(&frame_callee));
    r.spans[1] = e.here();
    call(e, reinterpret_cast<void*>(&frame_callee));
    r.spans[2] = e.here();
    e.byte(0xa1);
    e.dword(std::uint32_t(address(&frame_global_a)));
    r.spans[3] = e.here();
    const unsigned char views[] = {0x33, 0xdb, 0x83, 0xc4, 0x10};
    e.bytes(views, sizeof views);
    e.byte(0xb9);
    e.dword(2); // two views
    r.spans[7] = e.here();
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&frame_callee_ret4));
    r.spans[8] = e.here();
    const unsigned char submit[] = {0x8b, 0x46, 0x1c, 0x8b, 0x68, 0x4c};
    e.bytes(submit, sizeof submit);
    r.spans[9] = e.here();
    e.byte(0x8b);
    e.byte(0x15);
    e.dword(std::uint32_t(address(&frame_global_b)));
    e.byte(0x49);
    e.byte(0x75);
    e.byte(static_cast<unsigned char>(-(6 + 6 + 6 + 1 + 2))); // dec ecx (1 byte); jnz view_setup_begin
    r.spans[4] = e.here();
    const unsigned char overlays[] = {0x33, 0xdb, 0x39, 0x5c, 0x24, 0x18};
    e.bytes(overlays, sizeof overlays);
    r.spans[5] = e.here();
    e.byte(0xa1);
    e.dword(std::uint32_t(address(&frame_global_b)));
    r.spans[6] = e.here();
    call(e, reinterpret_cast<void*>(&frame_callee));
    e.byte(0xc3);
    r.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(r.body));
    if (!e.finish()) r.body = nullptr;
    return r;
}
// Offset of the one fixture-relocated field per span (call rel32 or absolute
// disp32); 0 = the span is byte-exact.
static constexpr unsigned frame_field[frame_marker::Count] = {1, 1, 1, 0, 0, 1, 1, 2, 0, 2};
static bool frame_specs(const FrameBody& r, patch::SiteSpec* specs) {
    bool okay = true;
    for (unsigned k = 0; k < frame_marker::Count; ++k) {
        specs[k] = frame_marker::kSites[k];
        specs[k].address = address(r.spans[k]);
        const auto* bytes = static_cast<const unsigned char*>(r.spans[k]);
        std::memcpy(specs[k].expected, bytes, specs[k].length);
        for (unsigned i = 0; i < specs[k].length; ++i) {
            if (frame_field[k] && i >= frame_field[k] && i < frame_field[k] + 4) continue;
            okay = okay && bytes[i] == frame_marker::kSites[k].expected[i];
        }
    }
    check(okay, "synthetic frame spans match proved native opcodes except relocated fields");
    return okay;
}
static void frame_replay_checks() {
    FrameBody r = make_frame_body();
    check(r.body != nullptr, "synthetic frame body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[frame_marker::Count];
    if (!frame_specs(r, specs)) return;
    unsigned char original[128];
    std::memcpy(original, r.body, r.length);
    Snapshot baseline{}, hooked{}, after{};
    frame_native_calls = 0;
    invoke(r.body, baseline);
    check(frame_native_calls == 5, "frame baseline executes five native calls");
    const char* status = nullptr;
    check(frame::fixture_install(specs, &status), "frame group installed on the synthetic spans");
    check(status && !std::strcmp(status, "ok"), "frame install status ok");
    check(frame::active, "frame group active after install");
    check(std::memcmp(original, r.body, r.length) != 0, "frame spans carry the patch jumps");
    phases::fixture_set_callback(nullptr); // the real stamp handler, through the real CPU boundary
    frame::present_begin();
    frame::present_end(); // admits this thread and starts frame 1
    frame_native_calls = 0;
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(frame_native_calls == 5, "instrumented frame executes exactly the native calls");
    frame::present_begin();
    frame::present_end();
    frame::frame(1);
    frame::detail::Sample s{};
    check(frame::fixture_last_sample(&s), "closed frame sample readable by the owner thread");
    check(s.frame == 1 && s.complete && s.views == 2, "scripted frame is complete with two views");
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < frame::detail::phase_count; ++i) sum += s.phase_us[i];
    check(sum == s.dt_us, "phase intervals partition the frame");
    const auto* tracker = frame::fixture_tracker();
    check(tracker->order_errors == 0 && tracker->clock_errors == 0 && tracker->unmatched == 0 && tracker->dropped == 0,
          "scripted frame has no order, clock or unmatched errors");
    // Order: after a full frame body the tracker sits in scene_end; entering the
    // begin_scene span again is a backward core stamp, which drops the frame so
    // the next Present return restarts it without a sample.
    invoke(r.body, after);
    compare(baseline, after);
    {
        // Enter the body at the begin_scene span with its ESI and the four
        // pushed words in place; the remaining stamps of that pass are unmatched.
        patch::Emitter e(40);
        void* entry = e.here();
        e.byte(0xbe);
        e.dword(std::uint32_t(address(frame_view)));
        for (unsigned i = 1; i <= 4; ++i) push(e, i);
        e.byte(0xe9);
        e.rel32(r.spans[2]);
        check(e.finish() != nullptr, "begin_scene entry trampoline emitted");
        fixture_call(std::uint32_t(address(entry)), 0, 0, 0, 0, 0);
    }
    check(tracker->order_errors == 1 && tracker->dropped == 1 && tracker->unmatched == 10,
          "backward core stamp is an order error that drops the frame; later stamps unmatched");
    frame::present_begin();
    frame::present_end();
    frame::frame(2);
    frame::detail::Sample dropped{};
    frame::fixture_last_sample(&dropped);
    check(dropped.frame == 1, "dropped frame produced no sample");
    check(frame::fixture_uninstall(), "frame group rollback restores every span");
    check(!std::memcmp(original, r.body, r.length), "frame spans byte-identical after rollback");
    check(!frame::active, "frame group inactive after rollback");
    phases::fixture_set_callback(&hostile_callback);
    frame_native_calls = 0;
    invoke(r.body, after);
    compare(baseline, after);
    check(frame_native_calls == 5, "restored frame body runs natively");
    // Byte mismatch: one corrupted opcode in the overlays span refuses the whole
    // group before any claim; no span changes.
    FrameBody c = make_frame_body();
    check(c.body != nullptr, "second synthetic frame body emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[frame_marker::Count];
    if (!frame_specs(c, corrupt)) return;
    unsigned char corrupted[128];
    std::memcpy(corrupted, c.body, c.length);
    corrupt[4].expected[0] ^= 1;
    check(!frame::fixture_install(corrupt, &status), "frame install refused on a byte mismatch");
    check(status && !std::strcmp(status, "preflight_bytes"), "byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted, c.body, c.length), "no span patched after the preflight refusal");
    check(!frame::active, "frame group stays inactive after refusal");
    frame::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[4].expected[0] ^= 1;
    patch::SiteSpec partial[frame_marker::Count];
    std::memcpy(partial, corrupt, sizeof partial);
    partial[1] = partial[0];
    check(!frame::fixture_install(partial, &status), "frame install refused on a duplicate claim");
    check(status && !std::strcmp(status, "bytes_mismatch"), "duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted, c.body, c.length), "partial install rolled back to original bytes");
    check(!frame::active, "frame group inactive after partial rollback");
    frame::fixture_uninstall();
    // Late window: after the first Present closes the window no claim is made.
    patch::close_install_window("fixture_first_present");
    check(!frame::fixture_install(corrupt, &status), "frame install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"), "late install reported as install_window_closed");
    check(!std::memcmp(corrupted, c.body, c.length), "no span touched by the late refusal");
    frame::fixture_uninstall();
}
// Pass phases (X3M_PASS_PHASES=1): the four exact effect-pass spans (plain
// copies, byte-identical to the EXE) executed once per body call at a frame
// depth of 0x90, with the operands they dereference supplied by fixture
// objects: [esp+0x28] -> a geometry object, [esp+0x74] the pass index, EBX ->
// the effect object whose first word is a vtable of >= 0x10c bytes. The body
// keeps the cdecl contract (EBX saved) so the benchmark can call it directly.
namespace pass = x3m::pass_phases;
namespace pass_marker = x3m::pass_phases::sites;
static std::uint32_t pass_effect_vtable[0x50]{}, pass_effect_object[4]{}, pass_geometry[8]{};
struct PassBody {
    void* body = nullptr;
    void* spans[pass_marker::Count]{};
    unsigned length = 0;
};
static PassBody make_pass_body() {
    pass_effect_object[0] = std::uint32_t(address(pass_effect_vtable));
    patch::Emitter e(96);
    PassBody r;
    r.body = e.here();
    e.byte(0x53);
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x90); // push ebx; sub esp,0x90
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x28);
    e.dword(std::uint32_t(address(pass_geometry))); // mov [esp+0x28],&geometry
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x74);
    e.dword(5); // mov [esp+0x74],5
    e.byte(0xbb);
    e.dword(std::uint32_t(address(pass_effect_object))); // mov ebx,&effect
    for (unsigned k = 0; k < pass_marker::Count; ++k) {
        r.spans[k] = e.here();
        e.bytes(pass_marker::kSites[k].expected, pass_marker::kSites[k].length);
    }
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x90);
    e.byte(0x5b);
    e.byte(0xc3); // add esp,0x90; pop ebx; ret
    r.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(r.body));
    if (!e.finish()) r.body = nullptr;
    return r;
}
static void pass_specs(const PassBody& r, patch::SiteSpec* specs) {
    for (unsigned k = 0; k < pass_marker::Count; ++k) {
        specs[k] = pass_marker::kSites[k];
        specs[k].address = address(r.spans[k]);
    }
}
// Entry at the pass_applied span with the body's frame in place: the chain is
// entered mid-pass, so the closing stamp has no open interval (an orphan).
static void* make_pass_entry(const PassBody& r, unsigned span) {
    patch::Emitter e(48);
    void* entry = e.here();
    e.byte(0x53);
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x90);
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x28);
    e.dword(std::uint32_t(address(pass_geometry)));
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x74);
    e.dword(5);
    e.byte(0xbb);
    e.dword(std::uint32_t(address(pass_effect_object)));
    e.byte(0xe9);
    e.rel32(r.spans[span]);
    return e.finish() ? entry : nullptr;
}
static DWORD WINAPI pass_foreign_thread(LPVOID body) {
    reinterpret_cast<void (*)()>(body)();
    return 0;
}
static void pass_replay_checks() {
    PassBody r = make_pass_body();
    check(r.body != nullptr, "synthetic pass body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[pass_marker::Count];
    pass_specs(r, specs);
    unsigned char original[96];
    std::memcpy(original, r.body, r.length);
    Snapshot baseline{}, hooked{}, after{};
    invoke(r.body, baseline);
    check(baseline.regs[7] == 6 && baseline.regs[5] == std::uint32_t(address(pass_effect_vtable)),
          "pass body baseline reads the pass index and the effect vtable");
    const char* status = nullptr;
    check(pass::fixture_install(specs, &status), "pass group installed on the synthetic spans");
    check(status && !std::strcmp(status, "ok"), "pass install status ok");
    check(pass::active, "pass group active after install");
    check(std::memcmp(original, r.body, r.length) != 0, "pass spans carry the patch jumps");
    const auto* gate = pass::fixture_gate();
    const auto* accumulator = pass::fixture_accumulator();
    // Before the first frame boundary no thread is admitted: early, ignored.
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(gate->early.load() == pass_marker::Count && gate->foreign.load() == 0 && accumulator->passes == 0,
          "stamps before admission are early and ignored");
    pass::frame(1, false, 0); // admits this thread; no frame-phase sample yet, so the accumulation is discarded
    check(pass::fixture_dropped() == 1, "frame boundary without a frame-phase sample is a dropped frame");
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(accumulator->passes == 1 && accumulator->last == 0, "one body pass counted and the interval chain closed");
    check(accumulator->orphans == 0 && accumulator->clock_errors == 0 && accumulator->clock_failures == 0 &&
              accumulator->unmatched == 0,
          "scripted pass has no orphan, clock or unmatched errors");
    pass::frame(2, true, 4321);
    pass::detail::Sample s{};
    check(pass::fixture_last_sample(&s), "closed pass sample readable by the owner thread");
    check(s.frame == 2 && s.passes == 1 && s.view_submit_us == 4321,
          "pass sample carries the frame, the pass count and the joined view_submit");
    check(s.sum_us == s.interval_us[0] + s.interval_us[1] + s.interval_us[2], "pass intervals sum to sum_us");
    check(s.self_us == pass_marker::Count * pass::detail::dispatch_cost_ns / 1000,
          "self cost is passes x sites x the fixture-measured dispatch cost");
    check(accumulator->passes == 0, "take resets the per-frame accumulation");
    // A stamp from another thread is foreign, counted and ignored.
    HANDLE thread = CreateThread(nullptr, 0, &pass_foreign_thread, r.body, 0, nullptr);
    check(thread != nullptr, "foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(gate->foreign.load() == pass_marker::Count && accumulator->passes == 0,
          "foreign-thread stamps are counted and ignored");
    // Entering the chain at pass_applied: the closing stamp is an orphan, the
    // draw and EndPass intervals still accumulate and the pass is counted.
    void* entry = make_pass_entry(r, pass_marker::PassApplied);
    check(entry != nullptr, "pass_applied entry trampoline emitted");
    if (entry) {
        invoke(entry, after);
        compare(baseline, after);
        check(accumulator->orphans == 1 && accumulator->passes == 1 && accumulator->last == 0,
              "mid-pass entry is one orphan, still one pass");
    }
    pass::frame(3, true, 0);
    check(pass::fixture_uninstall(), "pass group rollback restores every span");
    check(!std::memcmp(original, r.body, r.length), "pass spans byte-identical after rollback");
    check(!pass::active, "pass group inactive after rollback");
    invoke(r.body, after);
    compare(baseline, after);
    // Byte mismatch: one corrupted opcode refuses the whole group before any claim.
    PassBody c = make_pass_body();
    check(c.body != nullptr, "second synthetic pass body emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[pass_marker::Count];
    pass_specs(c, corrupt);
    unsigned char corrupted[96];
    std::memcpy(corrupted, c.body, c.length);
    corrupt[2].expected[0] ^= 1;
    check(!pass::fixture_install(corrupt, &status), "pass install refused on a byte mismatch");
    check(status && !std::strcmp(status, "preflight_bytes"), "pass byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted, c.body, c.length), "no pass span patched after the preflight refusal");
    check(!pass::active, "pass group stays inactive after refusal");
    pass::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[2].expected[0] ^= 1;
    patch::SiteSpec partial[pass_marker::Count];
    std::memcpy(partial, corrupt, sizeof partial);
    partial[1] = partial[0];
    check(!pass::fixture_install(partial, &status), "pass install refused on a duplicate claim");
    check(status && !std::strcmp(status, "bytes_mismatch"),
          "pass duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted, c.body, c.length), "pass partial install rolled back to original bytes");
    check(!pass::active, "pass group inactive after partial rollback");
    pass::fixture_uninstall();
}
static void pass_late_window_checks() { // after the frame checks closed the install window
    PassBody r = make_pass_body();
    check(r.body != nullptr, "late-window pass body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[pass_marker::Count];
    pass_specs(r, specs);
    unsigned char original[96];
    std::memcpy(original, r.body, r.length);
    const char* status = nullptr;
    check(!pass::fixture_install(specs, &status), "pass install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"),
          "late pass install reported as install_window_closed");
    check(!std::memcmp(original, r.body, r.length), "no pass span touched by the late refusal");
    pass::fixture_uninstall();
}
// Per-dispatch cost of the lean stub: the body (four spans) called directly,
// unhooked against hooked, best of `trials`; the difference over four
// dispatches is the stub envelope + owner check + QPC + accumulate.
static void pass_benchmark() {
    constexpr unsigned loops = 20000, trials = 7;
    PassBody r = make_pass_body();
    check(r.body != nullptr, "benchmark pass body emitted");
    if (!r.body) return;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "pass benchmark QPC frequency");
    const auto body = reinterpret_cast<void (*)()>(r.body);
    const auto timed = [&](std::uint64_t& best) {
        best = ~std::uint64_t(0);
        for (unsigned t = 0; t < trials; ++t) {
            LARGE_INTEGER start{}, end{};
            if (!QueryPerformanceCounter(&start)) return false;
            for (unsigned i = 0; i < loops; ++i) body();
            if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
            const auto ticks = std::uint64_t(end.QuadPart - start.QuadPart);
            if (ticks < best) best = ticks;
        }
        return true;
    };
    std::uint64_t baseline = 0, hooked = 0;
    FrameBody f = make_frame_body();
    patch::SiteSpec fs[frame_marker::Count];
    if (!f.body || !frame_specs(f, fs)) return;
    const char* frame_status = nullptr;
    check(frame::fixture_install(fs, &frame_status), "pass benchmark frame group installed");
    phases::fixture_set_callback(nullptr);
    frame::present_begin();
    frame::present_end();
    frame::frame(1);
    frame::stamp(frame_marker::ViewSetupBegin);
    frame::stamp(frame_marker::ViewSubmitBegin);
    check(timed(baseline), "pass benchmark baseline timed");
    patch::SiteSpec specs[pass_marker::Count];
    pass_specs(r, specs);
    const char* status = nullptr;
    check(pass::fixture_install(specs, &status), "pass benchmark group installed");
    pass::frame(1, false, 0);
    check(timed(hooked), "pass benchmark hooked timed");
    frame::stamp(frame_marker::ViewSubmitEnd);
    const auto submit_us = frame::shared_tracker()->submit_ticks * 1000000ull / std::uint64_t(frequency.QuadPart);
    pass::frame(2, true, submit_us);
    pass::detail::Sample s{};
    check(pass::fixture_last_sample(&s) && s.passes == loops * trials, "hooked benchmark loop counted every pass");
    check(s.scoped_us > 0 && s.outside_us == 0 && s.outside_passes == 0 && s.crossing_passes == 0,
          "pass benchmark measures successful inside-view attribution");
    check(pass::fixture_accumulator()->scope_errors == 0 && pass::fixture_accumulator()->complement_underflow == 0,
          "pass benchmark attribution health is clean");
    check(pass::fixture_uninstall(), "pass benchmark group rolled back");
    check(frame::fixture_uninstall(), "pass benchmark frame group rolled back");
    const double ns_per_tick = 1e9 / number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns = number(baseline) * ns_per_tick / loops, hooked_ns = number(hooked) * ns_per_tick / loops;
    const double dispatch_ns = (hooked_ns - baseline_ns) / pass_marker::Count;
    const double busy_us = dispatch_ns * 4024 / 1000; // ~1,006 passes x 4 stamps per busy frame (effect-pass-loop.md
                                                      // section 4)
    std::printf(
        "PASS PHASE BENCH loops=%u trials=%u baseline_ns_per_loop=%.1f hooked_ns_per_loop=%.1f dispatch_ns=%.1f implied_busy_frame_us=%.0f budget_us=1500 within_budget=%u documented_dispatch_ns=%llu\n",
        loops, trials, baseline_ns, hooked_ns, dispatch_ns, busy_us, unsigned(busy_us <= 1500.0),
        static_cast<unsigned long long>(pass::detail::dispatch_cost_ns));
    check(busy_us <= 1500.0, "lean stub within the 1.5 ms busy-frame budget");
    // The documented constant behind self_p50_us must stay within a factor of
    // two of the measurement; a drift beyond that is a stale ledger, not noise.
    const double documented = number(pass::detail::dispatch_cost_ns);
    check(dispatch_ns > 0 && documented >= dispatch_ns * 0.5 && documented <= dispatch_ns * 2.0,
          "documented dispatch cost within 2x of the measured cost");
}
// Loop phases (X3M_LOOP_PHASES=1): the per-sector update driver 0x0043a360
// mirrored instruction for instruction (same opcodes and lengths, so the
// routine's own rel8 gate and back-edge displacements are reused unchanged;
// docs/reverse-engineering/main-loop-input-region.md section 2), with the
// universe root, the container list and the five `ret 4` callees supplied by
// fixture objects. Four containers: node0 active, node1 class 1 with the skip
// bit (the `test` gate edge lands on the pass-end span start), node2 class 2
// (the `cmp` gate edge lands there too), node3 active; node4 is the list
// terminator the driver never processes (`cmp [esi],0` on the new esi). One
// body call therefore fires 16 stamps: 3 x 2 pass-A calls, 4 pass-A ends,
// 2 economy, 4 pass-B ends.
namespace loop = x3m::loop_phases;
namespace loop_marker = x3m::loop_phases::sites;
extern "C" {
std::uint32_t loop_native_calls[5]{}, loop_native_args[5][4]{}, loop_spin_iterations = 0;
void loop_callee_collide();
void loop_callee_simulate();
void loop_callee_post();
void loop_callee_economy();
void loop_callee_attach();
__attribute__((force_align_arg_pointer)) void __cdecl loop_record(unsigned callee, std::uint32_t argument) {
    if (callee < 5) {
        if (loop_native_calls[callee] < 4) loop_native_args[callee][loop_native_calls[callee]] = argument;
        ++loop_native_calls[callee];
    }
    if (callee == 1)
        for (volatile std::uint32_t i = 0; i < loop_spin_iterations; ++i) {} // simulate is the pathological routine of
                                                                             // the replay check
}
}
// Each callee is `stdcall` of one argument (`ret 4`) like the five real ones,
// records its argument and keeps EAX/ECX/EDX deterministic for the snapshot.
#define LOOP_CALLEE(name, index)                                                                                       \
    ".globl _" #name "\n_" #name ":\n pushl %eax\n pushl %ecx\n pushl %edx\n pushl 16(%esp)\n pushl $" #index          \
    "\n call _loop_record\n addl $8,%esp\n popl %edx\n popl %ecx\n popl %eax\n ret $4\n"
asm(".text\n" LOOP_CALLEE(loop_callee_collide, 0) LOOP_CALLEE(loop_callee_simulate, 1) LOOP_CALLEE(loop_callee_post, 2)
        LOOP_CALLEE(loop_callee_economy, 3) LOOP_CALLEE(loop_callee_attach, 4));
static std::uint32_t loop_root[4]{}, loop_root_global = 0; // the root object ([+4] pass-A gate, [+8] list head) and the
                                                           // global that points at it (0x0060850c in the game)
alignas(16) static unsigned char loop_nodes[5][0x150]{};
struct LoopBody {
    void* body = nullptr;
    void* spans[loop_marker::Count]{};
    void* loop_a = nullptr;
    void* loop_b = nullptr;
    void* pass_b = nullptr;
    unsigned length = 0;
};
static void loop_nodes_setup() {
    std::memset(loop_nodes, 0, sizeof loop_nodes);
    for (unsigned i = 0; i < 4; ++i)
        *reinterpret_cast<std::uint32_t*>(loop_nodes[i]) = std::uint32_t(address(loop_nodes[i + 1]));
    const std::uint16_t classes[5] = {1, 1, 2, 1, 1};
    for (unsigned i = 0; i < 5; ++i) *reinterpret_cast<std::uint16_t*>(loop_nodes[i] + 0x48) = classes[i];
    loop_nodes[1][0x148] = 1; // skipped
    loop_root[1] = 1;
    loop_root[2] = std::uint32_t(address(loop_nodes[0]));
    loop_root_global = std::uint32_t(address(loop_root));
}
static LoopBody make_loop_body() {
    loop_nodes_setup();
    patch::Emitter e(160);
    LoopBody r;
    r.body = e.here();
    e.byte(0x53);
    e.byte(0x56);
    e.byte(0x57); // push ebx; push esi; push edi
    e.byte(0x8b);
    e.byte(0x3d);
    e.dword(std::uint32_t(address(&loop_root_global))); // mov edi,ds:[root_global]  (0x0060850c in the game)
    e.byte(0x83);
    e.byte(0x7f);
    e.byte(0x04);
    e.byte(0x00); // cmp [edi+4],0
    e.byte(0xbb);
    e.dword(1); // mov ebx,1
    e.byte(0x74);
    e.byte(0x33); // je pass_b
    e.byte(0x8b);
    e.byte(0x77);
    e.byte(0x08); // mov esi,[edi+8]
    e.byte(0x83);
    e.byte(0x3e);
    e.byte(0x00); // cmp [esi],0
    e.byte(0x74);
    e.byte(0x2b); // je pass_b
    e.byte(0x8d);
    e.byte(0x64);
    e.byte(0x24);
    e.byte(0x00); // lea esp,[esp+0]
    r.loop_a = e.here();
    const unsigned char gate_type[] = {0x66, 0x39, 0x5e, 0x48}, gate_skip[] = {0x84, 0x9e, 0x48, 0x01, 0x00, 0x00},
                        pass_end[] = {0x8b, 0x36, 0x83, 0x3e, 0x00};
    e.bytes(gate_type, sizeof gate_type);
    e.byte(0x75);
    e.byte(0x1a); // cmp WORD PTR [esi+0x48],bx; jne pass_a_end
    e.bytes(gate_skip, sizeof gate_skip);
    e.byte(0x75);
    e.byte(0x12); // test BYTE PTR [esi+0x148],bl; jne pass_a_end
    r.spans[0] = e.here();
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&loop_callee_collide));
    r.spans[1] = e.here();
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&loop_callee_simulate));
    r.spans[2] = e.here();
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&loop_callee_post));
    r.spans[3] = e.here();
    e.bytes(pass_end, sizeof pass_end); // mov esi,[esi]; cmp [esi],0
    e.byte(0x75);
    e.byte(0xd9); // jne loop_a
    r.pass_b = e.here();
    e.byte(0x8b);
    e.byte(0x77);
    e.byte(0x08);
    e.byte(0x83);
    e.byte(0x3e);
    e.byte(0x00);
    e.byte(0x74);
    e.byte(0x22);
    e.byte(0x90); // mov esi,[edi+8]; cmp [esi],0; je done; nop
    r.loop_b = e.here();
    e.bytes(gate_type, sizeof gate_type);
    e.byte(0x75);
    e.byte(0x14);
    e.bytes(gate_skip, sizeof gate_skip);
    e.byte(0x75);
    e.byte(0x0c);
    r.spans[4] = e.here();
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&loop_callee_economy));
    e.byte(0x56);
    call(e, reinterpret_cast<void*>(&loop_callee_attach));
    r.spans[5] = e.here();
    e.bytes(pass_end, sizeof pass_end);
    e.byte(0x75);
    e.byte(0xdf); // jne loop_b
    e.byte(0x5f);
    e.byte(0x5e);
    e.byte(0x5b);
    e.byte(0xc3); // pop edi; pop esi; pop ebx; ret
    r.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(r.body));
    if (!e.finish()) r.body = nullptr;
    return r;
}
// The layout must reproduce the routine's offsets exactly, or the reused rel8
// displacements would land elsewhere; every span keeps the proved opcodes and
// only the four call rel32 fields (offset 2) are fixture-relocated.
static bool loop_specs(const LoopBody& r, patch::SiteSpec* specs) {
    const auto offset = [&](const void* p) { return unsigned(address(p) - address(r.body)); };
    bool okay = r.length == 0x75 && offset(r.loop_a) == 0x20 && offset(r.pass_b) == 0x47 && offset(r.loop_b) == 0x50;
    for (unsigned k = 0; k < loop_marker::Count; ++k) {
        specs[k] = loop_marker::kSites[k];
        specs[k].address = address(r.spans[k]);
        okay = okay && offset(r.spans[k]) == loop_marker::kSites[k].address - 0x0043a360;
        const auto* bytes = static_cast<const unsigned char*>(r.spans[k]);
        std::memcpy(specs[k].expected, bytes, specs[k].length);
        const unsigned field = loop_marker::kSites[k].rel32_offset;
        for (unsigned i = 0; i < specs[k].length; ++i) {
            if (field && i >= field && i < field + 4) continue;
            okay = okay && bytes[i] == loop_marker::kSites[k].expected[i];
        }
    }
    check(
        okay,
        "synthetic driver reproduces the routine layout and the proved span opcodes except the relocated call fields");
    return okay;
}
// Entry into the driver at one span with its frame in place (the three pushes,
// EDI = root, EBX = 1, ESI = the first container): at pass_a_end this is the
// gate edge landing on the stub entry, at simulate it is a mid-chain entry.
static void* make_loop_entry(const LoopBody& r, unsigned span) {
    patch::Emitter e(40);
    void* entry = e.here();
    e.byte(0x53);
    e.byte(0x56);
    e.byte(0x57);
    e.byte(0xbf);
    e.dword(std::uint32_t(address(loop_root)));
    e.byte(0xbb);
    e.dword(1);
    e.byte(0xbe);
    e.dword(std::uint32_t(address(loop_nodes[0])));
    e.byte(0xe9);
    e.rel32(r.spans[span]);
    return e.finish() ? entry : nullptr;
}
static void loop_reset_calls() {
    std::memset(loop_native_calls, 0, sizeof loop_native_calls);
    std::memset(loop_native_args, 0, sizeof loop_native_args);
}
static bool loop_calls_are(std::uint32_t collide, std::uint32_t simulate, std::uint32_t post, std::uint32_t economy,
                           std::uint32_t attach) {
    return loop_native_calls[0] == collide && loop_native_calls[1] == simulate && loop_native_calls[2] == post &&
           loop_native_calls[3] == economy && loop_native_calls[4] == attach;
}
static DWORD WINAPI loop_foreign_thread(LPVOID body) {
    reinterpret_cast<void (*)()>(body)();
    return 0;
}
static void loop_replay_checks() {
    LoopBody r = make_loop_body();
    check(r.body != nullptr, "synthetic driver body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[loop_marker::Count];
    if (!loop_specs(r, specs)) return;
    unsigned char original[160];
    std::memcpy(original, r.body, r.length);
    void* entry_end = make_loop_entry(r, loop_marker::SectorPassAEnd);
    void* entry_mid = make_loop_entry(r, loop_marker::SectorSimulate);
    check(entry_end && entry_mid, "driver entry trampolines emitted");
    if (!entry_end || !entry_mid) return;
    Snapshot baseline{}, baseline_end{}, baseline_mid{}, hooked{}, after{};
    loop_reset_calls();
    invoke(r.body, baseline);
    check(loop_calls_are(2, 2, 2, 2, 2), "driver baseline runs pass A and pass B for the two active containers");
    check(loop_native_args[0][0] == address(loop_nodes[0]) && loop_native_args[0][1] == address(loop_nodes[3]) &&
              loop_native_args[3][0] == address(loop_nodes[0]) && loop_native_args[3][1] == address(loop_nodes[3]),
          "callees receive the active containers, skipped and terminator nodes excluded");
    check(baseline.regs[8] & 0x40, "driver exits with the terminator's cmp flags (ZF) live");
    loop_reset_calls();
    invoke(entry_end, baseline_end);
    check(loop_calls_are(1, 1, 1, 2, 2), "pass_a_end entry baseline continues the walk from the second container");
    loop_reset_calls();
    invoke(entry_mid, baseline_mid);
    check(loop_calls_are(1, 2, 2, 2, 2), "simulate entry baseline runs the rest of the chain");
    const char* status = nullptr;
    check(loop::fixture_install(specs, &status), "loop group installed on the synthetic spans");
    check(status && !std::strcmp(status, "ok"), "loop install status ok");
    check(loop::active, "loop group active after install");
    check(std::memcmp(original, r.body, r.length) != 0, "loop spans carry the patch jumps");
    const auto* gate = loop::fixture_gate();
    const auto* accumulator = loop::fixture_accumulator();
    // Before the first frame boundary no thread is admitted: early, ignored.
    loop_reset_calls();
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(loop_calls_are(2, 2, 2, 2, 2), "instrumented driver executes exactly the native calls");
    check(gate->early.load() == 16 && gate->foreign.load() == 0 && accumulator->dispatches == 0,
          "stamps before admission are early and ignored");
    loop::frame(1, false, 0, 0); // admits this thread; no frame-phase sample yet, so the accumulation is discarded
    check(loop::fixture_dropped() == 1, "frame boundary without a frame-phase sample is a dropped frame");
    loop_spin_iterations = 200000;
    loop_reset_calls();
    invoke(r.body, hooked);
    compare(baseline, hooked);
    loop_spin_iterations = 0;
    check(loop_calls_are(2, 2, 2, 2, 2), "instrumented driver with a slow simulate executes exactly the native calls");
    check(accumulator->sectors == 2 && accumulator->containers == 4 && accumulator->dispatches == 16 &&
              accumulator->open == loop::detail::none,
          "two sectors, four containers, sixteen dispatches and a closed chain");
    check(accumulator->orphans == 0 && accumulator->clock_errors == 0 && accumulator->clock_failures == 0 &&
              accumulator->unmatched == 0,
          "gate edges onto pass_a_end/pass_b_end are the container walk, not orphans");
    check(accumulator->max_owner == 1 && accumulator->ticks[1] > accumulator->ticks[0] &&
              accumulator->ticks[1] > accumulator->ticks[2] && accumulator->ticks[1] > accumulator->ticks[3],
          "the slow simulate owns the frame's largest interval");
    loop::frame(2, true, 777, 4321);
    loop::detail::Sample s{};
    check(loop::fixture_last_sample(&s), "closed loop sample readable by the owner thread");
    check(s.frame == 2 && s.sectors == 2 && s.containers == 4 && s.dispatches == 16 && s.dt_us == 777 &&
              s.input_us == 4321,
          "loop sample carries frame, counts, the joined dt and pre_render");
    check(s.sum_us == s.interval_us[0] + s.interval_us[1] + s.interval_us[2] + s.interval_us[3] && s.interval_us[1] > 0,
          "loop intervals sum to sum_us");
    check(s.max_owner == 1 && s.max_interval_us <= s.interval_us[1] && s.max_interval_us > 0,
          "sample keeps the largest interval and its owner");
    check(s.self_us == 16 * loop::detail::dispatch_cost_ns / 1000,
          "self cost is dispatches x the fixture-measured dispatch cost");
    check(accumulator->dispatches == 0 && accumulator->max_owner == loop::detail::none,
          "take resets the per-frame accumulation");
    // A stamp from another thread is foreign, counted and ignored.
    HANDLE thread = CreateThread(nullptr, 0, &loop_foreign_thread, r.body, 0, nullptr);
    check(thread != nullptr, "loop foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(gate->foreign.load() == 16 && accumulator->dispatches == 0,
          "foreign-thread loop stamps are counted and ignored");
    // The gate edge onto pass_a_end lands on the stub entry: the replayed
    // `mov esi,[esi]; cmp [esi],0` decides the `jne` back edge in the tail.
    loop_reset_calls();
    invoke(entry_end, after);
    compare(baseline_end, after);
    check(loop_calls_are(1, 1, 1, 2, 2), "entry on the pass_a_end stub continues the walk with the replayed cmp flags");
    check(accumulator->orphans == 0 && accumulator->sectors == 2 && accumulator->containers == 4 &&
              accumulator->dispatches == 13,
          "pass_a_end entry with no open interval is not an orphan");
    // Entering the chain at simulate: the collide close has no open interval.
    loop_reset_calls();
    invoke(entry_mid, after);
    compare(baseline_mid, after);
    check(loop_calls_are(1, 2, 2, 2, 2), "entry on the simulate stub runs the rest of the chain");
    check(accumulator->orphans == 1 && accumulator->sectors == 4, "mid-chain entry is one orphan");
    loop::frame(3, true, 0, 0);
    check(loop::fixture_uninstall(), "loop group rollback restores every span");
    check(!std::memcmp(original, r.body, r.length), "loop spans byte-identical after rollback");
    check(!loop::active, "loop group inactive after rollback");
    loop_reset_calls();
    invoke(r.body, after);
    compare(baseline, after);
    check(loop_calls_are(2, 2, 2, 2, 2), "restored driver runs natively");
    // Byte mismatch: one corrupted opcode refuses the whole group before any claim.
    LoopBody c = make_loop_body();
    check(c.body != nullptr, "second synthetic driver body emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[loop_marker::Count];
    if (!loop_specs(c, corrupt)) return;
    unsigned char corrupted[160];
    std::memcpy(corrupted, c.body, c.length);
    corrupt[3].expected[0] ^= 1;
    check(!loop::fixture_install(corrupt, &status), "loop install refused on a byte mismatch");
    check(status && !std::strcmp(status, "preflight_bytes"), "loop byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted, c.body, c.length), "no loop span patched after the preflight refusal");
    check(!loop::active, "loop group stays inactive after refusal");
    loop::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[3].expected[0] ^= 1;
    patch::SiteSpec partial[loop_marker::Count];
    std::memcpy(partial, corrupt, sizeof partial);
    partial[1] = partial[0];
    check(!loop::fixture_install(partial, &status), "loop install refused on a duplicate claim");
    check(status && !std::strcmp(status, "bytes_mismatch"),
          "loop duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted, c.body, c.length), "loop partial install rolled back to original bytes");
    check(!loop::active, "loop group inactive after partial rollback");
    loop::fixture_uninstall();
}
static void loop_late_window_checks() { // after the frame checks closed the install window
    LoopBody r = make_loop_body();
    check(r.body != nullptr, "late-window driver body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[loop_marker::Count];
    if (!loop_specs(r, specs)) return;
    unsigned char original[160];
    std::memcpy(original, r.body, r.length);
    const char* status = nullptr;
    check(!loop::fixture_install(specs, &status), "loop install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"),
          "late loop install reported as install_window_closed");
    check(!std::memcmp(original, r.body, r.length), "no loop span touched by the late refusal");
    loop::fixture_uninstall();
}
// Per-dispatch cost of the lean stub on the driver: the body (16 dispatches
// over two active and two skipped containers) unhooked against hooked, best of
// `trials`. A frame costs 6 dispatches per active sector plus 2 per skipped
// container; the row states 1 and 200 active sectors.
static void loop_benchmark() {
    constexpr unsigned loops = 20000, trials = 7, dispatches = 16;
    LoopBody r = make_loop_body();
    check(r.body != nullptr, "benchmark driver body emitted");
    if (!r.body) return;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "loop benchmark QPC frequency");
    const auto body = reinterpret_cast<void (*)()>(r.body);
    const auto timed = [&](std::uint64_t& best) {
        best = ~std::uint64_t(0);
        for (unsigned t = 0; t < trials; ++t) {
            LARGE_INTEGER start{}, end{};
            if (!QueryPerformanceCounter(&start)) return false;
            for (unsigned i = 0; i < loops; ++i) body();
            if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
            const auto ticks = std::uint64_t(end.QuadPart - start.QuadPart);
            if (ticks < best) best = ticks;
        }
        return true;
    };
    std::uint64_t baseline = 0, hooked = 0;
    check(timed(baseline), "loop benchmark baseline timed");
    patch::SiteSpec specs[loop_marker::Count];
    if (!loop_specs(r, specs)) return;
    const char* status = nullptr;
    check(loop::fixture_install(specs, &status), "loop benchmark group installed");
    loop::frame(1, false, 0, 0);
    check(timed(hooked), "loop benchmark hooked timed");
    loop::frame(2, true, 0, 0);
    loop::detail::Sample s{};
    check(loop::fixture_last_sample(&s) && s.dispatches == loops * trials * dispatches &&
              s.sectors == loops * trials * 2,
          "hooked benchmark loop counted every dispatch and sector");
    check(loop::fixture_uninstall(), "loop benchmark group rolled back");
    const double ns_per_tick = 1e9 / number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns = number(baseline) * ns_per_tick / loops, hooked_ns = number(hooked) * ns_per_tick / loops;
    const double dispatch_ns = (hooked_ns - baseline_ns) / dispatches;
    std::printf(
        "LOOP PHASE BENCH loops=%u trials=%u dispatches_per_loop=%u baseline_ns_per_loop=%.1f hooked_ns_per_loop=%.1f dispatch_ns=%.1f implied_frame_us_1_sector=%.2f implied_frame_us_200_sectors=%.0f documented_dispatch_ns=%llu arena_used=%u arena_capacity=%u\n",
        loops, trials, dispatches, baseline_ns, hooked_ns, dispatch_ns, dispatch_ns * 6 / 1000,
        dispatch_ns * 1200 / 1000, static_cast<unsigned long long>(loop::detail::dispatch_cost_ns), patch::arena_used(),
        patch::arena_capacity());
    const double documented = number(loop::detail::dispatch_cost_ns);
    check(dispatch_ns > 0 && documented >= dispatch_ns * 0.5 && documented <= dispatch_ns * 2.0,
          "documented loop dispatch cost within 2x of the measured cost");
}

// Residual phases (X3M_RESIDUAL_PHASES=1): the two exact residual spans on a
// synthetic material-plus-view body at the pass body's frame depth: the
// material_setup span (`mov edx,[ebx]; mov ecx,[edx+0xfc]`, EBX -> the pass
// fixture's effect object), a gate that skips the four pass spans when
// residual_skip_passes is set (a material whose geometry guard skipped the
// pass loop), `push 0` and the view_particles span (`mov edx,[global];
// add esp,4`, its disp32 relocated to a fixture global; the add esp,4 removes
// the pushed word in the claim tail at the game's ESP). The pass group is
// installed on the same body's pass spans and the frame group on the frame
// body, so the residual stamps pair with the real retained clocks and the
// frame boundary runs through frame_phases::frame as in production.
namespace residual = x3m::residual_phases;
namespace residual_marker = x3m::residual_phases::sites;
static std::uint32_t residual_global = 0x3333, residual_skip_passes = 0;
struct ResidualBody {
    void* body = nullptr;
    void* spans[residual_marker::Count]{};
    void* pass_spans[pass_marker::Count]{};
    unsigned length = 0;
};
static ResidualBody make_residual_body() {
    pass_effect_object[0] = std::uint32_t(address(pass_effect_vtable));
    patch::Emitter e(128);
    ResidualBody r;
    r.body = e.here();
    e.byte(0x53);
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x90); // push ebx; sub esp,0x90
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x28);
    e.dword(std::uint32_t(address(pass_geometry))); // mov [esp+0x28],&geometry
    e.byte(0xc7);
    e.byte(0x44);
    e.byte(0x24);
    e.byte(0x74);
    e.dword(5); // mov [esp+0x74],5
    e.byte(0xbb);
    e.dword(std::uint32_t(address(pass_effect_object))); // mov ebx,&effect
    r.spans[0] = e.here();
    e.bytes(residual_marker::kSites[0].expected, residual_marker::kSites[0].length);
    e.byte(0x83);
    e.byte(0x3d);
    e.dword(std::uint32_t(address(&residual_skip_passes)));
    e.byte(0x00); // cmp dword [skip],0
    unsigned pass_bytes = 0;
    for (unsigned k = 0; k < pass_marker::Count; ++k) pass_bytes += pass_marker::kSites[k].length;
    e.byte(0x75);
    e.byte(static_cast<unsigned char>(pass_bytes)); // jne over the pass spans
    for (unsigned k = 0; k < pass_marker::Count; ++k) {
        r.pass_spans[k] = e.here();
        e.bytes(pass_marker::kSites[k].expected, pass_marker::kSites[k].length);
    }
    e.byte(0x6a);
    e.byte(0x00); // push 0: the particles argument the span's add esp,4 removes
    r.spans[1] = e.here();
    e.byte(0x8b);
    e.byte(0x15);
    e.dword(std::uint32_t(address(&residual_global)));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x90);
    e.byte(0x5b);
    e.byte(0xc3); // add esp,0x90; pop ebx; ret
    r.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(r.body));
    if (!e.finish()) r.body = nullptr;
    return r;
}
// The material span is byte-exact; the view span keeps the proved opcodes and
// only its disp32 (offset 2) is fixture-relocated.
static bool residual_specs(const ResidualBody& r, patch::SiteSpec* specs) {
    bool okay = true;
    for (unsigned k = 0; k < residual_marker::Count; ++k) {
        specs[k] = residual_marker::kSites[k];
        specs[k].address = address(r.spans[k]);
        const auto* bytes = static_cast<const unsigned char*>(r.spans[k]);
        std::memcpy(specs[k].expected, bytes, specs[k].length);
        for (unsigned i = 0; i < specs[k].length; ++i) {
            if (k == residual_marker::ViewParticles && i >= 2 && i < 6) continue;
            okay = okay && bytes[i] == residual_marker::kSites[k].expected[i];
        }
    }
    check(okay, "synthetic residual spans match the proved native opcodes except the relocated view field");
    return okay;
}
static void residual_pass_specs(const ResidualBody& r, patch::SiteSpec* specs) {
    for (unsigned k = 0; k < pass_marker::Count; ++k) {
        specs[k] = pass_marker::kSites[k];
        specs[k].address = address(r.pass_spans[k]);
    }
}
static DWORD WINAPI residual_foreign_thread(LPVOID body) {
    reinterpret_cast<void (*)()>(body)();
    return 0;
}
static void residual_replay_checks() {
    ResidualBody r = make_residual_body();
    check(r.body != nullptr, "synthetic residual body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[residual_marker::Count];
    if (!residual_specs(r, specs)) return;
    patch::SiteSpec pass_specs_on_body[pass_marker::Count];
    residual_pass_specs(r, pass_specs_on_body);
    unsigned char original[128];
    std::memcpy(original, r.body, r.length);
    Snapshot baseline{}, baseline_skip{}, hooked{}, after{};
    residual_skip_passes = 0;
    invoke(r.body, baseline);
    check(baseline.regs[7] == 6 && baseline.regs[5] == 0x3333,
          "residual body baseline reads the pass index and the view global");
    residual_skip_passes = 1;
    invoke(r.body, baseline_skip);
    residual_skip_passes = 0;
    // The frame group on the frame body supplies the view_submit_end clock and
    // the frame boundary; the pass group on this body's pass spans the
    // pass_end/pass_begin clocks and the pass count.
    FrameBody f = make_frame_body();
    check(f.body != nullptr, "residual frame body emitted");
    if (!f.body) return;
    patch::SiteSpec frame_specs_on_body[frame_marker::Count];
    if (!frame_specs(f, frame_specs_on_body)) return;
    const char* status = nullptr;
    check(frame::fixture_install(frame_specs_on_body, &status) && status && !std::strcmp(status, "ok"),
          "frame group installed for the residual checks");
    check(pass::fixture_install(pass_specs_on_body, &status) && status && !std::strcmp(status, "ok"),
          "pass group installed on the residual body's pass spans");
    check(residual::fixture_install(specs, &status), "residual group installed on the synthetic spans");
    check(status && !std::strcmp(status, "ok"), "residual install status ok");
    check(residual::active, "residual group active after install");
    check(std::memcmp(original, r.body, r.length) != 0, "residual spans carry the patch jumps");
    phases::fixture_set_callback(nullptr); // the frame stamps through the real handler
    const auto* gate = residual::fixture_gate();
    const auto* accumulator = residual::fixture_accumulator();
    const auto* pass_accumulator = pass::fixture_accumulator();
    // Before the first frame boundary no thread is admitted: early, ignored.
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(gate->early.load() == residual_marker::Count && gate->foreign.load() == 0 && accumulator->materials == 0,
          "residual stamps before admission are early and ignored");
    frame::present_begin();
    frame::present_end();
    frame::frame(1); // admits the frame owner and, through the frame boundary, this group and the pass group; no sample
                     // yet
    check(residual::fixture_dropped() == 1 && pass::fixture_dropped() == 1,
          "frame boundary without a frame-phase sample is a dropped frame for both groups");
    frame_native_calls = 0;
    invoke(f.body, hooked);
    check(frame_native_calls == 5 && frame::shared_tracker()->submit_end != 0,
          "frame body stamped two views and retained the view_submit_end clock");
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(accumulator->materials == 1 && accumulator->particle_views == 1 && accumulator->prepare_skipped == 1 &&
              accumulator->setup_skipped == 0,
          "first material has no pass_end to pair with; its setup is pending");
    check(pass_accumulator->begin_armed == false && pass_accumulator->begin_clock >= accumulator->p_clock &&
              pass_accumulator->end_clock > pass_accumulator->begin_clock,
          "pass group retained the first pass_begin and the pass_end for this group");
    check(accumulator->ticks[2] > 0 && accumulator->view_skipped == 0,
          "particles paired with the frame's view_submit_end");
    // These synthetic materials run after both view submissions have closed:
    // all preparation/setup must land in their outside-view buckets.
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(accumulator->materials == 2 && accumulator->ticks[0] == 0 && accumulator->ticks[1] == 0 &&
              accumulator->ticks[4] > 0 && accumulator->ticks[5] > 0 && accumulator->prepare_skipped == 1 &&
              accumulator->setup_skipped == 0,
          "second material closes the first setup and pairs prepare with the last pass_end");
    check(accumulator->view_skipped == 1 && accumulator->particle_views == 2,
          "a second view stamp against the same view_submit_end is view_skipped");
    // A material whose pass loop is skipped: its setup never closes and the
    // following material has no fresh pass_end.
    residual_skip_passes = 1;
    invoke(r.body, hooked);
    compare(baseline_skip, hooked);
    residual_skip_passes = 0;
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(accumulator->materials == 4 && accumulator->setup_skipped == 1 && accumulator->prepare_skipped == 2,
          "skipped pass loop leaves its setup and the next prepare unpaired");
    check(accumulator->clock_errors == 0 && accumulator->clock_failures == 0 && accumulator->unmatched == 0,
          "scripted materials have no clock or unmatched errors");
    check(pass_accumulator->passes == 3 && pass_accumulator->orphans == 0,
          "pass group still counts every pass with no orphan");
    LARGE_INTEGER qf{};
    check(QueryPerformanceFrequency(&qf) && qf.QuadPart > 0, "residual QPC frequency");
    const std::uint64_t qpc_hz = std::uint64_t(qf.QuadPart),
                        ticks_before[3] = {accumulator->ticks[0], accumulator->ticks[1], accumulator->ticks[2]};
    frame::present_begin();
    frame::present_end();
    frame::frame(2);
    residual::detail::Sample s{};
    check(residual::fixture_last_sample(&s), "closed residual sample readable by the owner thread");
    check(s.frame == 2 && s.materials == 4 && s.particle_views == 4 && s.passes == 3 && s.views == 2,
          "residual sample carries the frame, the counts, the pass count and the view count");
    // Sub-microsecond intervals truncate to 0 us at a 10 MHz clock: compare the
    // conversion, not the magnitude (setup grows at take by the pending close).
    check(s.interval_us[0] == ticks_before[0] * 1000000ull / qpc_hz &&
              s.interval_us[2] == ticks_before[2] * 1000000ull / qpc_hz &&
              s.interval_us[1] >= ticks_before[1] * 1000000ull / qpc_hz && s.interval_us[2] > 0,
          "prepare, setup and particles converted to microseconds");
    check(accumulator->setup_skipped == 1,
          "the frame boundary closed the last material's pending setup without skipping it");
    check(s.prepare_per_pass_ns == s.interval_us[0] * 1000 / 3 && s.setup_per_pass_ns == s.interval_us[1] * 1000 / 3,
          "per-pass figures divide by the frame's pass count");
    check((s.views_us >= s.view_setup_us + s.view_submit_us + s.interval_us[2] &&
           s.interval_us[3] == s.views_us - s.view_setup_us - s.view_submit_us - s.interval_us[2] &&
           accumulator->other_underflow == 0) ||
              (s.interval_us[3] == 0 && accumulator->other_underflow == 1),
          "other is views minus setup, submit and particles, or zero with the underflow counted");
    check(s.self_us == 8 * residual::detail::dispatch_cost_ns / 1000,
          "self cost is stamps x the fixture-measured dispatch cost");
    check(accumulator->materials == 0 && accumulator->p_clock == 0 && pass_accumulator->begin_armed == false,
          "take resets the per-frame accumulation and disarms the pass capture");
    pass::detail::Sample ps{};
    check(pass::fixture_last_sample(&ps) && ps.frame == 2 && ps.passes == 3,
          "the pass group's own sample closed after this group read it");
    // Real handler clocks inside a new synthetic view exercise successful
    // cumulative intersections, in addition to the outside-view bodies above.
    frame::stamp(frame_marker::ViewSetupBegin);
    frame::stamp(frame_marker::ViewSubmitBegin);
    invoke(r.body, hooked);
    compare(baseline, hooked);
    invoke(r.body, hooked);
    compare(baseline, hooked);
    check(accumulator->ticks[0] > 0 && accumulator->ticks[1] > 0 && accumulator->ticks[4] == 0 &&
              accumulator->ticks[5] == 0,
          "open submission preparation and setup stay inside the view");
    check(pass_accumulator->scoped_ticks > 0 && pass_accumulator->outside_ticks == 0 &&
              pass_accumulator->outside_passes == 0 && pass_accumulator->crossing_passes == 0,
          "open submission passes have no outside attribution");
    check(accumulator->scope_errors == 0 && pass_accumulator->scope_errors == 0,
          "active-view attribution has zero scope errors");
    frame::stamp(frame_marker::ViewSubmitEnd);
    frame::present_begin();
    frame::present_end();
    frame::frame(3);
    // A stamp from another thread is foreign, counted and ignored.
    HANDLE thread = CreateThread(nullptr, 0, &residual_foreign_thread, r.body, 0, nullptr);
    check(thread != nullptr, "residual foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(gate->foreign.load() == residual_marker::Count && accumulator->materials == 0,
          "foreign-thread residual stamps are counted and ignored");
    check(residual::fixture_uninstall(), "residual group rollback restores both spans");
    check(pass::fixture_uninstall() && frame::fixture_uninstall(),
          "pass and frame groups rolled back after the residual checks");
    check(!std::memcmp(original, r.body, r.length), "residual body byte-identical after rollback");
    check(!residual::active, "residual group inactive after rollback");
    phases::fixture_set_callback(&hostile_callback);
    invoke(r.body, after);
    compare(baseline, after);
    // Byte mismatch: one corrupted opcode refuses the whole group before any claim.
    ResidualBody c = make_residual_body();
    check(c.body != nullptr, "second synthetic residual body emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[residual_marker::Count];
    if (!residual_specs(c, corrupt)) return;
    unsigned char corrupted[128];
    std::memcpy(corrupted, c.body, c.length);
    corrupt[1].expected[0] ^= 1;
    check(!residual::fixture_install(corrupt, &status), "residual install refused on a byte mismatch");
    check(status && !std::strcmp(status, "preflight_bytes"), "residual byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted, c.body, c.length), "no residual span patched after the preflight refusal");
    check(!residual::active, "residual group stays inactive after refusal");
    residual::fixture_uninstall();
    // Partial install: the second spec duplicates the first address, so its
    // claim reads the fresh jump and fails; the first site is rolled back.
    corrupt[1].expected[0] ^= 1;
    patch::SiteSpec partial[residual_marker::Count];
    std::memcpy(partial, corrupt, sizeof partial);
    partial[1] = partial[0];
    check(!residual::fixture_install(partial, &status), "residual install refused on a duplicate claim");
    check(status && !std::strcmp(status, "bytes_mismatch"),
          "residual duplicate claim reported with the claim's own reason");
    check(!std::memcmp(corrupted, c.body, c.length), "residual partial install rolled back to original bytes");
    check(!residual::active, "residual group inactive after partial rollback");
    residual::fixture_uninstall();
}
static void residual_late_window_checks() { // after the frame checks closed the install window
    ResidualBody r = make_residual_body();
    check(r.body != nullptr, "late-window residual body emitted");
    if (!r.body) return;
    patch::SiteSpec specs[residual_marker::Count];
    if (!residual_specs(r, specs)) return;
    unsigned char original[128];
    std::memcpy(original, r.body, r.length);
    const char* status = nullptr;
    check(!residual::fixture_install(specs, &status), "residual install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"),
          "late residual install reported as install_window_closed");
    check(!std::memcmp(original, r.body, r.length), "no residual span touched by the late refusal");
    residual::fixture_uninstall();
}
// Per-dispatch cost of the two residual spans with pass stamps hooked in both
// arms inside a live view: residual-unhooked against hooked, best of
// `trials`; a busy frame fires about 1,000 material stamps and a few view stamps.
static void residual_benchmark() {
    constexpr unsigned loops = 20000, trials = 7;
    ResidualBody r = make_residual_body();
    check(r.body != nullptr, "benchmark residual body emitted");
    if (!r.body) return;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "residual benchmark QPC frequency");
    const auto body = reinterpret_cast<void (*)()>(r.body);
    const auto timed = [&](std::uint64_t& best) {
        best = ~std::uint64_t(0);
        for (unsigned t = 0; t < trials; ++t) {
            LARGE_INTEGER start{}, end{};
            if (!QueryPerformanceCounter(&start)) return false;
            for (unsigned i = 0; i < loops; ++i) body();
            if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
            const auto ticks = std::uint64_t(end.QuadPart - start.QuadPart);
            if (ticks < best) best = ticks;
        }
        return true;
    };
    std::uint64_t baseline = 0, hooked = 0;
    residual_skip_passes = 0;
    FrameBody f = make_frame_body();
    patch::SiteSpec fs[frame_marker::Count];
    if (!f.body || !frame_specs(f, fs)) return;
    patch::SiteSpec ps[pass_marker::Count];
    residual_pass_specs(r, ps);
    const char* sibling_status = nullptr;
    check(frame::fixture_install(fs, &sibling_status), "residual benchmark frame group installed");
    check(pass::fixture_install(ps, &sibling_status), "residual benchmark pass group installed in both arms");
    phases::fixture_set_callback(nullptr);
    frame::present_begin();
    frame::present_end();
    frame::frame(1);
    frame::stamp(frame_marker::ViewSetupBegin);
    frame::stamp(frame_marker::ViewSubmitBegin);
    check(timed(baseline), "residual benchmark baseline timed");
    pass::frame(1, false, 0); // discard baseline; hooked arm starts with no pending pass

    patch::SiteSpec specs[residual_marker::Count];
    if (!residual_specs(r, specs)) return;
    const char* status = nullptr;
    check(residual::fixture_install(specs, &status), "residual benchmark group installed");
    residual::frame(1, false, 0, 0, 0, 0);
    check(timed(hooked), "residual benchmark hooked timed");
    const auto* paired = residual::fixture_accumulator();
    check(paired->ticks[0] > 0 && paired->ticks[1] > 0 && paired->ticks[4] == 0 && paired->ticks[5] == 0 &&
              paired->prepare_skipped == 1 && paired->setup_skipped == 0,
          "residual benchmark measures successful inside-view pairing");
    check(paired->scope_errors == 0 && paired->clock_errors == 0 && paired->clock_failures == 0,
          "residual benchmark attribution health is clean");
    frame::stamp(frame_marker::ViewSubmitEnd);
    const auto submit_us = frame::shared_tracker()->submit_ticks * 1000000ull / std::uint64_t(frequency.QuadPart);
    residual::frame(2, true, submit_us, 0, submit_us, 1);
    residual::detail::Sample s{};
    check(residual::fixture_last_sample(&s) && s.materials == loops * trials && s.particle_views == loops * trials,
          "hooked benchmark loop counted every material and view stamp");
    check(residual::fixture_uninstall(), "residual benchmark group rolled back");
    check(pass::fixture_uninstall() && frame::fixture_uninstall(), "residual benchmark siblings rolled back");
    const double ns_per_tick = 1e9 / number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns = number(baseline) * ns_per_tick / loops, hooked_ns = number(hooked) * ns_per_tick / loops;
    const double dispatch_ns = (hooked_ns - baseline_ns) / residual_marker::Count;
    std::printf(
        "RESIDUAL PHASE BENCH loops=%u trials=%u baseline_ns_per_loop=%.1f hooked_ns_per_loop=%.1f dispatch_ns=%.1f implied_busy_frame_us=%.0f documented_dispatch_ns=%llu arena_used=%u arena_capacity=%u\n",
        loops, trials, baseline_ns, hooked_ns, dispatch_ns, dispatch_ns * 1010 / 1000,
        static_cast<unsigned long long>(residual::detail::dispatch_cost_ns), patch::arena_used(),
        patch::arena_capacity());
    const double documented = number(residual::detail::dispatch_cost_ns);
    check(dispatch_ns > 0 && documented >= dispatch_ns * 0.5 && documented <= dispatch_ns * 2.0,
          "documented residual dispatch cost within 2x of the measured cost");
}

// Media-cue gate (media_cue.cpp): the two-arm gate stub on a synthetic
// allocator whose first five bytes are the proved span `push ebx;
// mov ebx,[esp+8]`, called through emitted cdecl callers that reproduce the
// game's frames: the play helper (`push ecx; push [id]; mov eax,[flags];
// call; add esp,4; pop ecx; ret`) called by a selector (`push 0x5a; call;
// add esp,4; ret`) or by another kind, and direct speech/script callers.
namespace media = x3m::media_cue;
namespace media_marker = x3m::media_cue::sites;
extern "C" {
std::uint32_t media_fixture_id = 0, media_fixture_flags = 0, media_fixture_result = 0;
std::uint32_t media_body_calls = 0, media_body_ebx = 0, media_body_arg = 0, media_body_flags = 0, media_body_ebx_ok = 0;
std::uint32_t media_reenter = 0, media_reenter_result = 0, media_reenter_depth = 0, media_lost_trigger = 0;
std::uint32_t media_enter_probe = 0, media_enter_bytes_in_body = 0;
void (*media_reenter_call)() = nullptr;
__attribute__((force_align_arg_pointer)) void __cdecl media_body_record(std::uint32_t ebx, std::uint32_t argument,
                                                                        std::uint32_t flags) {
    ++media_body_calls;
    media_body_ebx = ebx;
    media_body_arg = argument;
    media_body_flags = flags;
    media_body_ebx_ok += ebx == argument; // the replayed `mov ebx,[esp+8]` read the argument at the game's exact ESP
    if (media_enter_probe) {
        media_enter_probe = 0;
        media_enter_bytes_in_body = GetFileSize(media_log_handle, nullptr);
    } // the entry line must already be in the file while the build runs
    if (media_lost_trigger) {
        media_lost_trigger = 0;
        media::fixture_drop_pending();
    } // the entry vanishes while the build runs
    if (media_reenter && media_reenter_call) { // models COM apartment dispatch re-entering the allocator from inside
                                               // the build
        media_reenter = 0;
        const std::uint32_t outer_result = media_fixture_result, outer_id = media_fixture_id;
        media_fixture_result = media_reenter_result;
        media_fixture_id = outer_id + 1;
        media_reenter_call();
        media_reenter_depth = media::fixture_pending()->max_depth;
        media_fixture_result = outer_result;
        media_fixture_id = outer_id;
    }
}
}
struct MediaBody {
    void* body = nullptr;
    unsigned length = 0;
    void* helper = nullptr;
    void* selector = nullptr;
    void* other = nullptr;
    void* speech = nullptr;
    void* script = nullptr;
    void* query = nullptr;
    void* savegame = nullptr;
    media::detail::Addresses addresses{};
};
static MediaBody make_media_body() {
    MediaBody r;
    {
        patch::Emitter e(64);
        r.body = e.here();
        e.byte(0x53);
        e.byte(0x8b);
        e.byte(0x5c);
        e.byte(0x24);
        e.byte(0x08); // push ebx; mov ebx,[esp+8]   (the proved span)
        e.byte(0x50);
        e.byte(0x51);
        e.byte(0x52); // push eax; push ecx; push edx
        e.byte(0x8b);
        e.byte(0x44);
        e.byte(0x24);
        e.byte(0x08);
        e.byte(0x50); // mov eax,[esp+8] (the flags word); push eax
        e.byte(0x8b);
        e.byte(0x44);
        e.byte(0x24);
        e.byte(0x18);
        e.byte(0x50); // mov eax,[esp+0x18] (the argument); push eax
        e.byte(0x53);
        call(e, reinterpret_cast<void*>(&media_body_record)); // push ebx; call media_body_record(ebx,argument,flags)
        e.byte(0x83);
        e.byte(0xc4);
        e.byte(0x0c); // add esp,12
        e.byte(0x5a);
        e.byte(0x59);
        e.byte(0x58); // pop edx; pop ecx; pop eax
        e.byte(0xa1);
        e.dword(std::uint32_t(address(&media_fixture_result))); // mov eax,[result]: the record or 0
        e.byte(0x85);
        e.byte(0xc0); // test eax,eax
        e.byte(0x5b);
        e.byte(0xc3); // pop ebx; ret (cdecl)
        r.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(r.body));
        if (!e.finish()) r.body = nullptr;
    }
    patch::Emitter e(192);
    r.helper = e.here();
    e.byte(0x51);
    e.byte(0xff);
    e.byte(0x35);
    e.dword(std::uint32_t(address(&media_fixture_id))); // push ecx; push [id]
    e.byte(0xa1);
    e.dword(std::uint32_t(address(&media_fixture_flags)));
    call(e, r.body); // mov eax,[flags]; call body
    r.addresses.helper_return = std::uint32_t(address(e.here()));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0x59);
    e.byte(0xc3); // add esp,4; pop ecx; ret
    r.selector = e.here();
    e.byte(0x6a);
    e.byte(0x5a);
    call(e, r.helper); // push 0x5a; call helper
    r.addresses.selector_return = std::uint32_t(address(e.here()));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0xc3); // add esp,4; ret
    r.other = e.here();
    e.byte(0x6a);
    e.byte(0x11);
    call(e, r.helper);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0xc3); // another helper caller, kind 0x11
    r.speech = e.here();
    e.byte(0xff);
    e.byte(0x35);
    e.dword(std::uint32_t(address(&media_fixture_id)));
    e.byte(0xa1);
    e.dword(std::uint32_t(address(&media_fixture_flags)));
    call(e, r.body);
    r.addresses.speech_return = std::uint32_t(address(e.here()));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0xc3);
    r.script = e.here();
    e.byte(0xff);
    e.byte(0x35);
    e.dword(std::uint32_t(address(&media_fixture_id)));
    e.byte(0xa1);
    e.dword(std::uint32_t(address(&media_fixture_flags)));
    call(e, r.body);
    r.addresses.script_return = std::uint32_t(address(e.here()));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04);
    e.byte(0xc3);
    const auto direct = [&](void*& caller, std::uint32_t& ret) {
        caller = e.here();
        e.byte(0xff);
        e.byte(0x35);
        e.dword(std::uint32_t(address(&media_fixture_id)));
        e.byte(0xa1);
        e.dword(std::uint32_t(address(&media_fixture_flags)));
        call(e, r.body);
        ret = std::uint32_t(address(e.here()));
        e.byte(0x83);
        e.byte(0xc4);
        e.byte(0x04);
        e.byte(0xc3);
    };
    direct(r.query, r.addresses.query_return);
    direct(r.savegame, r.addresses.savegame_return);
    r.addresses.selector_kind = media_marker::kSelectorKind;
    if (!e.finish()) r.body = nullptr;
    return r;
}
static bool media_specs(const MediaBody& r, patch::SiteSpec* specs) {
    specs[0] = media_marker::kSites[0];
    specs[0].address = address(r.body);
    const bool okay = r.body && !std::memcmp(r.body, media_marker::kSites[0].expected, media_marker::kSites[0].length);
    check(okay, "synthetic allocator opens with the proved span bytes");
    return okay;
}
static void media_reset() {
    media_body_calls = media_body_ebx = media_body_arg = media_body_flags = media_body_ebx_ok = 0;
}
// The stand-in session log: a delete-on-close file in the working directory.
// `media_log_take` reads everything written so far into `out` (NUL-terminated,
// truncated to the buffer) and empties the file; `media_log_lines` counts the
// `media_cue_enter` lines in a taken buffer.
static bool media_log_open() {
    if (media_log_handle != INVALID_HANDLE_VALUE) return true;
    media_log_handle = CreateFileW(L"media_cue_enter_fixture.log", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    return media_log_handle != INVALID_HANDLE_VALUE;
}
static unsigned media_log_take(char* out, unsigned capacity) {
    out[0] = 0;
    if (media_log_handle == INVALID_HANDLE_VALUE) return 0;
    const DWORD size = GetFileSize(media_log_handle, nullptr);
    DWORD read = 0;
    if (SetFilePointer(media_log_handle, 0, nullptr, FILE_BEGIN) != 0) return 0;
    if (size && !ReadFile(media_log_handle, out, size < capacity - 1 ? size : capacity - 1, &read, nullptr)) read = 0;
    out[read] = 0;
    SetFilePointer(media_log_handle, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(media_log_handle);
    return size;
}
static unsigned media_log_lines(const char* text) {
    unsigned lines = 0;
    for (const char* p = text; *p;) {
        const char* nl = std::strchr(p, '\n');
        if (!nl) break;
        lines += !std::strncmp(p, "media_cue_enter ", 16);
        p = nl + 1;
    }
    return lines;
}
static DWORD WINAPI media_foreign_thread(LPVOID caller) {
    reinterpret_cast<void (*)()>(caller)();
    return 0;
}
static constexpr std::uint32_t media_record = 0x40001000, media_id_a = 812, media_id_b = 245;
static void media_replay_checks() {
    MediaBody r = make_media_body();
    check(r.body != nullptr, "synthetic allocator and callers emitted");
    if (!r.body) return;
    patch::SiteSpec specs[media_marker::Count];
    if (!media_specs(r, specs)) return;
    check(media_log_open(), "media stand-in log opened");
    static char lines[8192];
    char expected[192]; // the 40-call bound case writes up to 32 lines of ~90 B
    unsigned char original[64];
    std::memcpy(original, r.body, r.length);
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    const std::uint64_t retry_ticks = std::uint64_t(f.QuadPart) / 10; // 100 ms
    Snapshot baseline_ok{}, baseline_fail{}, baseline_speech{}, baseline_other{}, hooked{}, after{};
    media_fixture_id = media_id_a;
    media_fixture_flags = 0;
    media_reset();
    media_fixture_result = media_record;
    invoke(r.selector, baseline_ok);
    check(media_body_calls == 1 && media_body_ebx_ok == 1 && media_body_arg == media_id_a && media_body_flags == 0 &&
              baseline_ok.regs[7] == media_record,
          "native selector chain reaches the allocator body with ebx == [esp+8] and returns the record");
    media_reset();
    media_fixture_result = 0;
    invoke(r.selector, baseline_fail);
    check(media_body_calls == 1 && baseline_fail.regs[7] == 0,
          "native failed build returns 0 through the selector chain");
    media_reset();
    invoke(r.speech, baseline_speech);
    media_reset();
    invoke(r.other, baseline_other);
    const char* status = nullptr;
    const bool media_installed = media::fixture_install(specs, r.addresses, true, true, retry_ticks, &status);
    check(media_installed, "media gate installed on the synthetic span");
    if (!media_installed)
        std::printf("media install status=%s site_status=%s\n", status ? status : "null", media::fixture_site_status());
    check(status && !std::strcmp(status, "ok"), "media install status ok");
    check(media::active, "media gate active after install");
    check(std::memcmp(original, r.body, r.length) != 0, "media span carries the patch jump");
    const auto* gate = media::fixture_gate();
    const auto* cache = media::fixture_cache();
    const auto* pending = media::fixture_pending();
    media::detail::Entry e{};
    // Before the frame boundary admits a thread, a call passes unobserved.
    media_reset();
    media_fixture_result = media_record;
    invoke(r.selector, hooked);
    compare(baseline_ok, hooked);
    check(media_body_calls == 1 && media_body_ebx_ok == 1,
          "PASS arm before admission replays the span at the game's ESP");
    check(gate->early.load() == 1 && pending->depth == 0 && pending->max_depth == 0 && !media::fixture_pop_trace(&e),
          "call before admission is early, unobserved and untraced");
    check(media_log_take(lines, sizeof lines) == 0, "early call writes no media_cue_enter line");
    media::frame(1);
    media_enter_probe = 1;
    media_enter_bytes_in_body = 0;
    // Observed success.
    media_reset();
    media_fixture_result = media_record;
    invoke(r.selector, hooked);
    compare(baseline_ok, hooked);
    check(media_body_calls == 1 && media_body_ebx_ok == 1 && media_body_arg == media_id_a,
          "PASS arm with return capture replays the span and reaches the body once");
    check(media::fixture_pop_trace(&e) && e.outcome == media::detail::observed && e.result == media_record &&
              e.id == media_id_a && e.kind == 0x5a && e.caller == media::detail::selector && e.scoped && e.frame == 1 &&
              e.attempt == 1,
          "trace entry names the selector, the id, kind 0x5a, the record and the frame");
    std::snprintf(expected, sizeof expected,
                  "media_cue_enter frame=1 qpc=%llu id=%lu kind=0x5a caller=selector flags=0x0 attempt=1\n",
                  static_cast<unsigned long long>(e.qpc), static_cast<unsigned long>(media_id_a));
    const unsigned enter_bytes = media_log_take(lines, sizeof lines);
    check(!std::strcmp(lines, expected),
          "media_cue_enter line written with the entry's frame, qpc, id, kind, caller, flags and attempt");
    if (std::strcmp(lines, expected)) std::printf("media_cue_enter got=%s expected=%s", lines, expected);
    check(enter_bytes > 0 && media_enter_bytes_in_body == enter_bytes,
          "media_cue_enter line was in the file before the allocator body ran");
    check(pending->depth == 0 && pending->max_depth == 1 && pending->stale == 0 && pending->lost == 0 &&
              pending->mismatched == 0 && cache->used == 0,
          "return trampoline popped its pending entry; a success is not cached");
    // Observed failure: cached.
    media_reset();
    media_fixture_result = 0;
    invoke(r.selector, hooked);
    compare(baseline_fail, hooked);
    check(media_body_calls == 1 && media::fixture_pop_trace(&e) && e.outcome == media::detail::observed &&
              e.result == 0 && e.attempt == 2,
          "observed failed build traced with result 0");
    check(cache->used == 1 && cache->slots[0].used && cache->slots[0].id == media_id_a && cache->slots[0].failures == 1,
          "selector failure enters the negative cache");
    // Refused inside the retry interval: REFUSE arm, EAX 0, the body never runs.
    media_reset();
    invoke(r.selector, after);
    compare(baseline_fail, after);
    check(media_body_calls == 0 && after.regs[7] == 0,
          "REFUSE arm returns 0 to the caller without running the allocator");
    check(media::fixture_pop_trace(&e) && e.outcome == media::detail::refused && e.id == media_id_a && e.attempt == 3,
          "refusal traced as cached");
    media_log_take(lines, sizeof lines);
    check(media_log_lines(lines) == 1 && std::strstr(lines, "media_cue_enter frame=1 qpc=") &&
              std::strstr(lines, " id=812 kind=0x5a caller=selector flags=0x0 attempt=2\n"),
          "the failed build wrote its entry line; the REFUSE arm wrote none");
    check(media::fixture_refused() == 1 && cache->slots[0].refusals == 1 && pending->depth == 0,
          "refusal counted, no pending entry");
    check(media::fixture_attempts_frame() == 3,
          "per-frame attempt count covers observed and refused calls, not the early one");
    // Speech, script and another helper kind are never refused, and their failures are not cached.
    media_reset();
    invoke(r.speech, after);
    compare(baseline_speech, after);
    check(media_body_calls == 1 && media::fixture_pop_trace(&e) && e.caller == media::detail::speech && !e.scoped &&
              e.outcome == media::detail::observed && e.result == 0,
          "speech caller with the cached id proceeds");
    media_reset();
    invoke(r.script, after);
    check(media_body_calls == 1 && media::fixture_pop_trace(&e) && e.caller == media::detail::script && !e.scoped,
          "script caller proceeds");
    media_reset();
    invoke(r.other, after);
    compare(baseline_other, after);
    check(media_body_calls == 1 && media::fixture_pop_trace(&e) && e.caller == media::detail::other && e.kind == 0x11 &&
              !e.scoped,
          "another helper caller (kind 0x11) proceeds as other");
    media_log_take(lines, sizeof lines);
    check(media_log_lines(lines) == 3 && std::strstr(lines, " kind=none caller=speech ") &&
              std::strstr(lines, " kind=none caller=script ") && std::strstr(lines, " kind=0x11 caller=other "),
          "speech, script and other entry lines carry their caller and kind text");
    check(cache->used == 1 && cache->slots[0].failures == 1, "non-selector failures leave the cache unchanged");
    // Re-entrant call from inside the body (COM dispatch): the inner return pops first.
    media_fixture_id = media_id_b;
    media_fixture_result = media_record;
    media_reenter = 1;
    media_reenter_result = 0;
    media_reenter_call = reinterpret_cast<void (*)()>(r.speech);
    media_reset();
    invoke(r.selector, after);
    compare(baseline_ok, after);
    check(media_body_calls == 2 && media_body_ebx_ok == 2 && media_reenter_depth == 2,
          "nested speech call from inside the build reached depth 2 with both spans replayed");
    check(media::fixture_pop_trace(&e) && e.caller == media::detail::speech && e.id == media_id_b + 1 && e.result == 0,
          "inner call traced first");
    check(media::fixture_pop_trace(&e) && e.caller == media::detail::selector && e.id == media_id_b &&
              e.result == media_record,
          "outer call traced with its own id and result");
    check(pending->depth == 0 && pending->stale == 0 && pending->lost == 0 && pending->mismatched == 0 &&
              cache->used == 1 && cache->slots[0].id == media_id_a,
          "re-entrancy leaves the pending stack empty and the cache untouched");
    media_reenter_call = nullptr;
    media_fixture_id = media_id_a;
    // Retry after the interval, refusal again, then success clears the entry.
    Sleep(150);
    media_reset();
    media_fixture_result = 0;
    invoke(r.selector, after);
    compare(baseline_fail, after);
    check(media_body_calls == 1 && cache->slots[0].failures == 2,
          "retry after the interval reaches the allocator and refreshes the failure");
    media_reset();
    invoke(r.selector, after);
    check(media_body_calls == 0 && media::fixture_refused() == 2, "refused again inside the new interval");
    Sleep(150);
    media_reset();
    media_fixture_result = media_record;
    invoke(r.selector, after);
    compare(baseline_ok, after);
    check(media_body_calls == 1 && cache->used == 0, "success after the interval clears the cache entry");
    media_reset();
    media_fixture_result = 0;
    invoke(r.selector, after);
    check(media_body_calls == 1 && cache->used == 1, "next call after a success proceeds and re-caches its failure");
    while (media::fixture_pop_trace(&e)) {}
    media_log_take(lines, sizeof lines);
    media::fixture_reset_enter_limit();
    media_fixture_id = media_id_b;
    media_fixture_result = media_record;
    for (unsigned i = 0; i < 40; ++i) invoke(r.selector, after);
    while (media::fixture_pop_trace(&e)) {}
    media_log_take(lines, sizeof lines);
    const unsigned bounded = media_log_lines(lines);
    const auto* enter_limit = media::fixture_enter_limit();
    check(bounded <= media::detail::lines_per_second && bounded + unsigned(enter_limit->suppressed) == 40,
          "media_cue_enter lines bounded to lines_per_second per clock second, the rest counted as enter_suppressed");
    media_fixture_id = media_id_a;
    // A foreign thread passes unobserved and untraced.
    media_reset();
    media_fixture_result = media_record;
    media::fixture_reset_enter_limit(); // the limiter must be open for the no-line check to mean anything
    HANDLE thread = CreateThread(nullptr, 0, &media_foreign_thread, r.selector, 0, nullptr);
    check(thread != nullptr, "media foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(media_body_calls == 1 && gate->foreign.load() == 1 && !media::fixture_pop_trace(&e) && pending->depth == 0,
          "foreign-thread call proceeds unobserved");
    check(media_log_take(lines, sizeof lines) == 0, "foreign-thread call writes no media_cue_enter line");
    // Forced lost return (unreachable by construction: the entry is dropped while the
    // build runs); the trampoline returns to the last substituted real return address.
    media_fixture_id = media_id_b;
    media_reset();
    media_fixture_result = media_record;
    media_lost_trigger = 1;
    invoke(r.selector, after);
    compare(baseline_ok, after);
    check(media_body_calls == 1 && pending->lost == 1 && pending->depth == 0 && !media::fixture_pop_trace(&e),
          "lost return counted once and the fail-safe returned to last_return");
    media_fixture_id = media_id_a;
    media::frame(2);
    check(media::fixture_attempts_frame() == 0, "frame boundary resets the per-frame attempt count");
    check(media::fixture_uninstall(), "media gate rollback restores the span");
    check(!std::memcmp(original, r.body, r.length), "media span byte-identical after rollback");
    check(!media::active, "media gate inactive after rollback");
    media_reset();
    media_fixture_result = media_record;
    invoke(r.selector, after);
    compare(baseline_ok, after);
    check(media_body_calls == 1, "restored allocator runs natively");
    // Cache off: failures are traced, never refused.
    check(media::fixture_install(specs, r.addresses, true, false, retry_ticks, &status),
          "media gate installed with the cache off");
    media::frame(1);
    media_reset();
    media_fixture_result = 0;
    invoke(r.selector, after);
    invoke(r.selector, after);
    invoke(r.selector, after);
    check(media_body_calls == 3 && media::fixture_refused() == 0 && cache->used == 0, "cache off never refuses");
    check(media::fixture_uninstall(), "cache-off gate rolled back");
    // Byte mismatch refuses before any claim.
    MediaBody c = make_media_body();
    check(c.body != nullptr, "second synthetic allocator emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[media_marker::Count];
    if (!media_specs(c, corrupt)) return;
    unsigned char corrupted[64];
    std::memcpy(corrupted, c.body, c.length);
    corrupt[0].expected[1] ^= 1;
    check(!media::fixture_install(corrupt, c.addresses, true, true, retry_ticks, &status),
          "media install refused on a byte mismatch");
    check(status && !std::strcmp(status, "preflight_bytes"), "media byte mismatch reported as preflight_bytes");
    check(!std::memcmp(corrupted, c.body, c.length) && !media::active,
          "no media span patched after the preflight refusal");
    media::fixture_uninstall();
}
// The video blit witness (media_cue.cpp video_lock_observe): the observer the
// ownership shell would call, driven here with synthetic events. A stand-in
// IDirect3DSurface9 answers GetDesc only; every other method is unreachable.
struct FakeSurface final : IDirect3DSurface9 {
    unsigned desc_calls = 0;
    HRESULT WINAPI QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG WINAPI AddRef() override { return 1; }
    ULONG WINAPI Release() override { return 1; }
    HRESULT WINAPI GetDevice(IDirect3DDevice9**) override { return E_NOTIMPL; }
    HRESULT WINAPI SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI GetPrivateData(REFGUID, void*, DWORD*) override { return E_NOTIMPL; }
    HRESULT WINAPI FreePrivateData(REFGUID) override { return E_NOTIMPL; }
    DWORD WINAPI SetPriority(DWORD) override { return 0; }
    DWORD WINAPI GetPriority() override { return 0; }
    void WINAPI PreLoad() override {}
    D3DRESOURCETYPE WINAPI GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT WINAPI GetContainer(REFIID, void**) override { return E_NOTIMPL; }
    HRESULT WINAPI GetDesc(D3DSURFACE_DESC* d) override {
        ++desc_calls;
        *d = D3DSURFACE_DESC{};
        d->Format = D3DFMT_X8R8G8B8;
        d->Type = D3DRTYPE_SURFACE;
        d->Pool = D3DPOOL_DEFAULT;
        d->Width = 512;
        d->Height = 256;
        return S_OK;
    }
    HRESULT WINAPI LockRect(D3DLOCKED_RECT*, const RECT*, DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI UnlockRect() override { return E_NOTIMPL; }
    HRESULT WINAPI GetDC(HDC*) override { return E_NOTIMPL; }
    HRESULT WINAPI ReleaseDC(HDC) override { return E_NOTIMPL; }
};
static unsigned media_video_lines(const char* text) {
    unsigned lines = 0;
    for (const char* p = text; *p;) {
        const char* nl = std::strchr(p, '\n');
        if (!nl) break;
        lines += !std::strncmp(p, "media_video_blit ", 17);
        p = nl + 1;
    }
    return lines;
}
namespace own = x3m::ownership;
static own::SurfaceLockObserver media_video_observer = nullptr;
static own::SurfaceLockEvent media_video_event{};
static DWORD WINAPI media_video_foreign_thread(LPVOID) {
    media_video_observer(media_video_event);
    return 0;
}
// One lock or unlock as the shell reports it: the enter phase, then the result phase.
static void media_video_call(own::SurfaceLockObserver observer, IDirect3DSurface9* surface, std::uint32_t ret,
                             bool unlock, HRESULT result) {
    own::SurfaceLockEvent e{surface,
                            surface,
                            reinterpret_cast<const void*>(ret),
                            nullptr,
                            0,
                            S_FALSE,
                            unlock ? own::SurfaceLockPhase::UnlockEnter : own::SurfaceLockPhase::LockEnter};
    observer(e);
    e.result = result;
    e.phase = unlock ? own::SurfaceLockPhase::UnlockResult : own::SurfaceLockPhase::LockResult;
    observer(e);
}
static void media_video_checks() {
    MediaBody r = make_media_body();
    check(r.body != nullptr, "video witness allocator emitted");
    if (!r.body) return;
    patch::SiteSpec specs[media_marker::Count];
    if (!media_specs(r, specs)) return;
    check(media_log_open(), "video witness stand-in log opened");
    static char lines[8192];
    char expected[192];
    media::detail::Addresses addresses = r.addresses;
    addresses.blit_begin = media_marker::kVideoBlitBegin;
    addresses.blit_end = media_marker::kVideoBlitEnd;
    const char* status = nullptr;
    const std::uint32_t in_range = 0x004d0d27, unlock_ret = 0x004d14ba, outside = 0x00401000; // the LockRect/UnlockRect
                                                                                              // call sites' return
                                                                                              // addresses and a foreign
                                                                                              // caller
    check(media::fixture_install(specs, addresses, false, true, 1, &status),
          "video witness gate installed with the trace off");
    check(media::video_lock_observer() == nullptr, "trace off: no video witness published");
    check(media::fixture_uninstall(), "trace-off video witness gate rolled back");
    check(media::fixture_install(specs, addresses, true, true, 1, &status),
          "video witness gate installed with the trace on");
    own::SurfaceLockObserver observer = media::video_lock_observer();
    check(observer != nullptr, "trace on: video witness published");
    if (!observer) {
        media::fixture_uninstall();
        return;
    }
    FakeSurface surface;
    media_log_take(lines, sizeof lines);
    media_video_call(observer, &surface, in_range, false, S_OK); // before the owner is admitted: nothing
    check(media_log_take(lines, sizeof lines) == 0 && media::fixture_video()->locks_total == 0 &&
              media::fixture_video_dropped(false) == 1 && media::fixture_video_dropped(true) == 0,
          "video lock before the owner is admitted writes no line and counts as video_early");
    media::frame(1);
    media_video_call(observer, &surface, outside, false, S_OK);
    check(media_log_take(lines, sizeof lines) == 0 && media::fixture_video()->locks_total == 0,
          "lock from outside the consumer's range writes no line and is not counted");
    media_video_call(observer, &surface, media_marker::kVideoBlitEnd, false, S_OK);
    check(media_log_take(lines, sizeof lines) == 0 && media::fixture_video()->locks_total == 0,
          "lock returning to the range's end bound is outside");
    media_video_call(observer, &surface, in_range, false, S_OK);
    media_log_take(lines, sizeof lines);
    check(media_video_lines(lines) == 2 && surface.desc_calls == 2,
          "first in-range lock writes its enter and result lines with one GetDesc each");
    std::snprintf(
        expected, sizeof expected,
        " texture=%p width=512 height=256 format=22 flags=0x0 result=pending stage=lock_enter blits=1 unlocks=0\n",
        static_cast<void*>(&surface));
    const char* second = std::strchr(lines, '\n');
    second = second ? second + 1 : lines;
    check(!std::strncmp(lines, "media_video_blit frame=1 qpc=", 29) && std::strstr(lines, expected) &&
              std::strstr(lines, expected) < second,
          "video enter line carries frame, qpc, texture, size, format, flags, result=pending and the blit count");
    std::snprintf(
        expected, sizeof expected,
        " texture=%p width=512 height=256 format=22 flags=0x0 result=0x00000000 stage=lock blits=1 unlocks=0\n",
        static_cast<void*>(&surface));
    check(!std::strncmp(second, "media_video_blit frame=1 qpc=", 29) && std::strstr(second, expected),
          "video result line carries the native HRESULT after the lock");
    if (!std::strstr(second, expected)) std::printf("media_video_blit got=%s", lines);
    for (unsigned i = 1; i < media::detail::video_blit_line_interval * 2; ++i)
        media_video_call(observer, &surface, in_range, false, i == 5 ? E_FAIL : S_OK);
    media_log_take(lines, sizeof lines);
    check(media_video_lines(lines) == 2 && std::strstr(lines, "stage=lock_enter blits=61 unlocks=0\n") &&
              std::strstr(lines, "stage=lock blits=61 unlocks=0\n"),
          "one enter/result pair per interval of in-range locks");
    check(media::fixture_video()->locks_total == media::detail::video_blit_line_interval * 2 &&
              media::fixture_video()->failures == 1,
          "in-range locks and the failed lock counted");
    media_video_call(observer, &surface, unlock_ret, true, S_OK);
    media_log_take(lines, sizeof lines);
    std::snprintf(
        expected, sizeof expected,
        " texture=%p width=512 height=256 format=22 flags=0x0 result=0x00000000 stage=unlock blits=120 unlocks=1\n",
        static_cast<void*>(&surface));
    check(media_video_lines(lines) == 2 &&
              std::strstr(lines, "result=pending stage=unlock_enter blits=120 unlocks=1\n") &&
              std::strstr(lines, expected),
          "first in-range unlock writes its enter and result lines");
    media_video_call(observer, &surface, unlock_ret, true, S_OK);
    check(media_log_take(lines, sizeof lines) == 0 && media::fixture_video()->unlocks == 2,
          "later unlocks are counted without a line");
    { // a re-entrant in-range lock between the enter and the result of blit 121 (on the interval): counted, no line of
      // its own, the outer pair matched
        own::SurfaceLockEvent outer{&surface, &surface, reinterpret_cast<const void*>(in_range), nullptr,
                                    0,        S_FALSE,  own::SurfaceLockPhase::LockEnter};
        observer(outer);
        media_video_call(observer, &surface, in_range, false, E_FAIL);
        outer.result = S_OK;
        outer.phase = own::SurfaceLockPhase::LockResult;
        observer(outer);
        media_log_take(lines, sizeof lines);
        check(media_video_lines(lines) == 2 && std::strstr(lines, "stage=lock_enter blits=121 unlocks=2\n") &&
                  std::strstr(lines, "result=0x00000000 stage=lock blits=121 unlocks=2\n"),
              "re-entrant in-range lock writes no line and leaves the outer enter/result pair matched");
        check(media::fixture_video()->reentries == 1 && media::fixture_video()->depth == 0 &&
                  media::fixture_video()->failures == 2,
              "re-entry counted once, its failure counted, depth back to zero");
    }
    media_video_observer = observer;
    media_video_event = own::SurfaceLockEvent{&surface, &surface, reinterpret_cast<const void*>(in_range), nullptr,
                                              0,        S_FALSE,  own::SurfaceLockPhase::LockEnter};
    HANDLE thread = CreateThread(nullptr, 0, &media_video_foreign_thread, nullptr, 0, nullptr);
    check(thread != nullptr, "video foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(media_log_take(lines, sizeof lines) == 0 &&
              media::fixture_video()->locks_total == media::detail::video_blit_line_interval * 2 + 1 &&
              media::fixture_video_dropped(true) == 1,
          "foreign-thread in-range lock writes no line and counts as video_foreign");
    check(media::fixture_video()->suppressed == 0, "every admitted video line reached the file whole");
    check(media::fixture_uninstall(), "video witness gate rolled back");
    check(media::video_lock_observer() == nullptr, "video witness withdrawn after rollback");
}
static void media_late_window_checks() { // after the frame checks closed the install window
    MediaBody r = make_media_body();
    check(r.body != nullptr, "late-window allocator emitted");
    if (!r.body) return;
    patch::SiteSpec specs[media_marker::Count];
    if (!media_specs(r, specs)) return;
    unsigned char original[64];
    std::memcpy(original, r.body, r.length);
    const char* status = nullptr;
    check(!media::fixture_install(specs, r.addresses, true, true, 1, &status),
          "media install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"),
          "late media install reported as install_window_closed");
    check(!std::memcmp(original, r.body, r.length), "no media span touched by the late refusal");
    media::fixture_uninstall();
}
// Per-call cost of the gate: the selector chain unhooked against hooked on
// the PASS arm (entry handler, return capture) and on the REFUSE arm (entry
// handler only) with the trace off, best of `trials`, checked within 2x of the
// documented costs; then the trace-on PASS arm (the entry limiter admits 32
// lines per second, the rest pay the predicate) and the cost of one admitted
// media_cue_enter line (32 calls after a limiter reset, formatter and
// unbuffered write included).
static void media_benchmark() {
    constexpr unsigned loops = 20000, trials = 7;
    MediaBody r = make_media_body();
    check(r.body != nullptr, "benchmark allocator emitted");
    if (!r.body) return;
    check(media_log_open(), "benchmark stand-in log opened");
    static char lines[32768];
    media_log_take(lines, sizeof lines); // trials x lines_per_second entry lines of ~90 B
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "media benchmark QPC frequency");
    const auto caller = reinterpret_cast<void (*)()>(r.selector);
    const auto timed = [&](std::uint64_t& best) {
        best = ~std::uint64_t(0);
        for (unsigned t = 0; t < trials; ++t) {
            LARGE_INTEGER start{}, end{};
            if (!QueryPerformanceCounter(&start)) return false;
            for (unsigned i = 0; i < loops; ++i) caller();
            if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
            const auto ticks = std::uint64_t(end.QuadPart - start.QuadPart);
            if (ticks < best) best = ticks;
        }
        return true;
    };
    media_fixture_id = media_id_a;
    media_fixture_flags = 0;
    media_fixture_result = media_record;
    std::uint64_t baseline = 0, pass = 0, refuse = 0;
    check(timed(baseline), "media benchmark baseline timed");
    patch::SiteSpec specs[media_marker::Count];
    if (!media_specs(r, specs)) return;
    const char* status = nullptr;
    check(media::fixture_install(specs, r.addresses, false, true, std::uint64_t(frequency.QuadPart) * 3600, &status),
          "media benchmark gate installed");
    media::frame(1);
    media_reset();
    check(timed(pass), "media benchmark PASS arm timed");
    check(media_body_calls == loops * trials && media_body_ebx_ok == loops * trials,
          "PASS arm benchmark replayed the span every call");
    media_fixture_result = 0;
    caller(); // one observed failure primes the cache
    media_reset();
    check(timed(refuse), "media benchmark REFUSE arm timed");
    check(media_body_calls == 0 && media::fixture_refused() == loops * trials,
          "REFUSE arm benchmark never reached the allocator");
    check(media::fixture_uninstall(), "media benchmark gate rolled back");
    check(media_log_take(lines, sizeof lines) == 0,
          "trace off: no media_cue_enter line written by the benchmark's proceeded calls");
    std::uint64_t trace_pass = 0, enter_lines = ~std::uint64_t(0);
    media_fixture_result = media_record;
    check(media::fixture_install(specs, r.addresses, true, true, std::uint64_t(frequency.QuadPart) * 3600, &status),
          "media benchmark gate installed with the trace on");
    media::frame(1);
    media_reset();
    check(timed(trace_pass), "media benchmark trace-on PASS arm timed");
    check(media_body_calls == loops * trials, "trace-on PASS arm benchmark reached the allocator every call");
    media_log_take(lines, sizeof lines);
    for (unsigned t = 0; t < trials; ++t) {
        media::fixture_reset_enter_limit();
        LARGE_INTEGER start{}, end{};
        if (!QueryPerformanceCounter(&start)) break;
        for (unsigned i = 0; i < media::detail::lines_per_second; ++i) caller();
        if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) break;
        const auto ticks = std::uint64_t(end.QuadPart - start.QuadPart);
        if (ticks < enter_lines) enter_lines = ticks;
    }
    media_log_take(lines, sizeof lines);
    check(media_log_lines(lines) == trials * media::detail::lines_per_second,
          "each limiter reset admitted exactly lines_per_second entry lines");
    check(media::fixture_uninstall(), "media trace-on benchmark gate rolled back");
    const double ns_per_tick = 1e9 / number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns = number(baseline) * ns_per_tick / loops, pass_ns = number(pass) * ns_per_tick / loops,
                 refuse_ns = number(refuse) * ns_per_tick / loops;
    const double trace_pass_ns = number(trace_pass) * ns_per_tick / loops,
                 enter_line_ns = number(enter_lines) * ns_per_tick / media::detail::lines_per_second - pass_ns;
    std::printf(
        "MEDIA CUE BENCH loops=%u trials=%u baseline_ns_per_call=%.1f pass_ns_per_call=%.1f refuse_ns_per_call=%.1f pass_dispatch_ns=%.1f refuse_dispatch_ns=%.1f documented_pass_ns=%llu documented_refuse_ns=%llu trace_pass_ns_per_call=%.1f trace_pass_dispatch_ns=%.1f enter_line_ns=%.1f stub_bytes=300 arena_used=%u arena_capacity=%u\n",
        loops, trials, baseline_ns, pass_ns, refuse_ns, pass_ns - baseline_ns, refuse_ns - baseline_ns,
        static_cast<unsigned long long>(media::detail::pass_dispatch_cost_ns),
        static_cast<unsigned long long>(media::detail::refuse_dispatch_cost_ns), trace_pass_ns,
        trace_pass_ns - baseline_ns, enter_line_ns, patch::arena_used(), patch::arena_capacity());
    check(pass_ns > baseline_ns && refuse_ns > 0, "media dispatch costs measured");
    const double pass_dispatch = pass_ns - baseline_ns, refuse_dispatch = refuse_ns - baseline_ns;
    const double documented_pass = number(media::detail::pass_dispatch_cost_ns),
                 documented_refuse = number(media::detail::refuse_dispatch_cost_ns);
    check(documented_pass >= pass_dispatch * 0.5 && documented_pass <= pass_dispatch * 2.0,
          "documented trace-off PASS dispatch cost within 2x of the measured cost");
    check(documented_refuse >= refuse_dispatch * 0.5 && documented_refuse <= refuse_dispatch * 2.0,
          "documented trace-off REFUSE dispatch cost within 2x of the measured cost");
    check(enter_lines != ~std::uint64_t(0) && trace_pass_ns >= pass_ns * 0.5, "trace-on costs measured");
}
#include "media_cue_skip_fixture_inc.h"
int main(int argc, char** argv) {
    for (unsigned i = 0; i < sizeof fixture_xmm_seed; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(i * 37 + 9);
    if (argc == 2 && !std::strcmp(argv[1], "--media-only")) {
        media_skip_checks();
        media_replay_checks();
        media_video_checks();
        media_benchmark();
        patch::close_install_window("media_fixture");
        media_late_window_checks();
        std::printf("MEDIA CUE CPU checks=%u failures=%u arena_used=%u arena_capacity=%u\n", checks, failures,
                    patch::arena_used(), patch::arena_capacity());
        return failures ? 1 : 0;
    }
    patch::Emitter tail(8);
    void* continuation = tail.here();
    tail.byte(0xc3);
    if (!tail.finish()) return 2;
    phases::fixture_enable(false);
    phases::fixture_set_callback(&hostile_callback);
    void* stubs[marker::Count]{};
    for (unsigned kind = 0; kind < marker::Count; ++kind) {
        stubs[kind] = marker_stub(kind, continuation);
        if (!stubs[kind]) return 2;
        Snapshot baseline{}, hooked{};
        invoke(continuation, baseline);
        invoke(stubs[kind], hooked);
        compare(baseline, hooked);
        check(callbacks[kind] == 1, "every actual emitter reaches redirected callback once");
        check(observed[kind][0] == 0x98765432 && observed[kind][1] == 0x12345678 && observed[kind][2] == 0x3456789a &&
                  observed[kind][4] == 0x23456789 && observed[kind][5] == 0x6789abcd &&
                  observed[kind][6] == 0x56789abc && observed[kind][7] == 0x456789ab,
              "callback PUSHAD frame preserves all incoming register values");
    }
    replay_checks(0, 0);
    replay_checks(1, 0);
    replay_checks(1, 1);
    replay_checks(2, 0);
    replay_checks(2, 1);
    replay_checks(3, 0);
    target_replay_checks();
    input_replay_checks(0);
    input_replay_checks(1);
    benchmark(continuation, stubs);
    targeted_benchmark(continuation, stubs);
    pass_replay_checks();
    pass_benchmark();
    loop_replay_checks();
    loop_benchmark();
    residual_replay_checks();
    residual_benchmark();
    media_skip_checks();
    media_replay_checks();
    media_video_checks();
    media_benchmark();
    frame_replay_checks(); // closes the install window
    pass_late_window_checks();
    loop_late_window_checks();
    residual_late_window_checks();
    media_late_window_checks();
    std::printf(
        "GAME PHASE CPU stubs=%u frame_sites=%u pass_sites=%u loop_sites=%u residual_sites=%u media_sites=%u replay_cases=13 actual_target_handler_cases=5 frame_cases=4 pass_cases=5 loop_cases=7 residual_cases=6 media_cases=11 arena_used=%u arena_capacity=%u checks=%u failures=%u\n",
        unsigned(marker::Count), unsigned(frame_marker::Count), unsigned(pass_marker::Count),
        unsigned(loop_marker::Count), unsigned(residual_marker::Count), unsigned(media_marker::Count),
        patch::arena_used(), patch::arena_capacity(), checks, failures);
    return failures ? 1 : 0;
}
