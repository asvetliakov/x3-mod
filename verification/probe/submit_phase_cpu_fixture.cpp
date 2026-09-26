// Exercise the production submit-phase stamps (src/proxy/submit_phases.cpp):
// the context lean stub, the install transaction and the handler, on a
// synthetic body that holds all twenty-two proved spans byte for byte (only
// the declared rel32 fields are fixture-relocated). The independent host site
// verifier (verify_submit_phase_sites.py) binds the real game spans. This
// executable never launches the game.
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/submit_phases.h"
#include "../../src/proxy/submit_phase_sites.h"
#include "../../src/proxy/frame_phases.h"
#include "../../src/proxy/lean_stub.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace patch = x3m::engine_patch;
namespace submit = x3m::submit_phases;
namespace site = x3m::submit_phases::sites;
namespace detail = x3m::submit_phases::detail;
// The production unit's link-time neighbours, reduced to what it reads.
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
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
namespace x3m::frame_phases {
std::atomic<bool> active{true};
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
std::uint32_t fixture_flags = 0x647;
void fixture_call(std::uint32_t target, std::uint32_t ebx, std::uint32_t ebp, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
void fixture_nop_ret();
void fixture_pop_ret();
}
// Four-byte caller stack, no 16-byte alignment promise; hostile live state:
// two values on the x87 stack (the st(0) the engine may hold across a site),
// a non-default x87 control word and MXCSR, seeded XMM0-7, DF and the
// arithmetic flags set. The harness of game_phase_cpu_fixture.cpp.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "movl %esp,_fixture_entry_esp\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
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
    "addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n"
    // Stand-ins for the relocated call targets: D3DXMatrixInverse / _malloc (the
    // body balances their arguments itself) and the queue walk 0x0047e6e0 (one
    // pushed argument, popped here so the body stays linear).
    ".globl _fixture_nop_ret\n_fixture_nop_ret:\n ret\n"
    ".globl _fixture_pop_ret\n_fixture_pop_ret:\n ret $4\n");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
static inline double number(std::uint64_t value) {
    return double(std::uint32_t(value >> 32)) * 4294967296.0 + double(std::uint32_t(value));
}
static std::uintptr_t address(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p);
}
static void invoke(void* target, Snapshot& out) {
    fixture_output = &out;
    SetLastError(0x13572468);
    fixture_call(std::uint32_t(address(target)), 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc, 0x6789abcd);
    check(GetLastError() == 0x13572468, "LastError preserved");
    check(fixture_entry_esp == fixture_exit_esp, "four-byte incoming caller stack balanced");
    check(!std::memcmp(fixture_input_x87, out.x87, 108),
          "incoming x87 image (two live stack values, control word) survives every stamp");
}
static void compare(const Snapshot& before, const Snapshot& after) {
    for (unsigned r = 0; r < 9; ++r)
        if (r != 3) check(before.regs[r] == after.regs[r], "GPR and EFLAGS match native baseline");
    check(!std::memcmp(before.xmm, after.xmm, 128), "all XMM registers match native baseline");
    check(before.mxcsr == after.mxcsr, "MXCSR matches native baseline");
    check(!std::memcmp(before.x87, after.x87, 108), "x87 image matches native baseline");
}

// Engine stand-ins. `object` serves as view, effect, node and material block:
// its first word points at itself (the vtable loads), +0x10 too (world
// return b), +0x2a0 is the node-cache list head. The cache list is four
// records, the last one the null-linked terminator the engine never compares;
// the queue is three entries ending at the root's sentinel (+0x44).
static std::uint32_t object[0x100]{};
static std::uint32_t records[4][4]{}, queue_root[0x20]{}, entries[3][8]{};
static std::uint32_t view_root_word = 0;
extern "C" {
std::uint32_t body_miss = 0, body_skip_block = 0, body_hit_record = 0;
}
static void prepare_engine() {
    std::memset(object, 0, sizeof object);
    object[0] = std::uint32_t(address(object));
    object[0x10 / 4] = std::uint32_t(address(object));
    for (unsigned i = 0; i < 4; ++i) {
        records[i][0] = i < 3 ? std::uint32_t(address(records[i + 1])) : 0;
        records[i][3] = 0x1000 + i;
    }
    object[0x2a0 / 4] = std::uint32_t(address(records[0]));
    const std::uint32_t sentinel = std::uint32_t(address(queue_root)) + 0x44;
    for (unsigned i = 0; i < 3; ++i) entries[i][0] = i < 2 ? std::uint32_t(address(entries[i + 1])) : sentinel;
    queue_root[0x40 / 4] = std::uint32_t(address(entries[0]));
    view_root_word = std::uint32_t(address(queue_root));
    body_hit_record = std::uint32_t(address(records[2]));
}
struct Body {
    void* body = nullptr;
    void* spans[site::Count]{};
    unsigned length = 0;
};
static Body make_body() {
    patch::Emitter e(512);
    Body b;
    b.body = e.here();
    const auto span = [&](unsigned k, void (*callee)() = nullptr) {
        const void* target = reinterpret_cast<const void*>(callee);
        const auto& s = site::kSites[k];
        b.spans[k] = e.here();
        if (s.rel32_offset) {
            e.bytes(s.expected, s.rel32_offset);
            e.rel32(target);
        } else
            e.bytes(s.expected, s.length);
    };
    const auto load = [&](unsigned char opcode) {
        e.byte(opcode);
        e.dword(std::uint32_t(address(object)));
    }; // mov reg,&object
    e.byte(0x53);
    e.byte(0x56);
    e.byte(0x57);
    e.byte(0x55); // push ebx; push esi; push edi; push ebp
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x400); // sub esp,0x400
    load(0xbb);
    load(0xbe);
    load(0xbf);
    load(0xbd);
    load(0xb8); // ebx, esi, edi, ebp, eax = &object
    span(site::MaterialEnter);
    e.byte(0x8b);
    e.byte(0xe5);
    e.byte(0x5d); // push ebp; mov ebp,esp; and esp,-16 | mov esp,ebp; pop ebp
    span(site::TechniqueBegin);
    span(site::TechniqueEnd);
    span(site::BlockBegin);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04); // push 1; lea | add esp,4
    span(site::InverseWorldBegin, &fixture_nop_ret);
    span(site::InverseWorldEnd);
    span(site::InverseViewBegin, &fixture_nop_ret);
    span(site::InverseViewEnd);
    e.byte(0x83);
    e.byte(0x3d);
    e.dword(std::uint32_t(address(&body_skip_block)));
    e.byte(0x00); // cmp dword [skip],0
    e.byte(0x75);
    e.byte(static_cast<unsigned char>(site::kSites[site::BlockEnd].length)); // jne over block_end: the geometry guard
    span(site::BlockEnd);
    span(site::EndBegin);
    span(site::EndEnd);
    e.byte(0x56);
    e.byte(0x57); // push esi; push edi for material_return's pops
    span(site::MaterialReturn);
    load(0xb8);
    span(site::WorldEnter);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x08); // sub esp,8; cmp | add esp,8
    span(site::WorldReturnA);
    span(site::WorldReturnB);
    load(0xb8);
    span(site::SortEnter);
    span(site::SortReturnA, &fixture_pop_ret);
    span(site::SortReturnB, &fixture_pop_ret);
    span(site::SortReturnC, &fixture_pop_ret);
    span(site::WalkBegin);
    e.byte(0x83);
    e.byte(0x3d);
    e.dword(std::uint32_t(address(&body_miss)));
    e.byte(0x00); // cmp dword [miss],0
    e.byte(0x74);
    e.byte(static_cast<unsigned char>(site::kSites[site::WalkMiss].length + 3)); // je over the miss path: the hit
    span(site::WalkMiss, &fixture_nop_ret);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(0x04); // push 0x70; call | add esp,4
    e.byte(0x8b);
    e.byte(0x35);
    e.dword(std::uint32_t(address(&body_hit_record))); // mov esi,[hit]: the engine's `mov esi,eax`
    span(site::WalkJoin);
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x400);
    e.byte(0x5d);
    e.byte(0x5f);
    e.byte(0x5e);
    e.byte(0x5b);
    e.byte(0xc3);
    b.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(b.body));
    if (!e.finish()) b.body = nullptr;
    return b;
}
// Every span is byte-exact except the four bytes of a declared rel32 field.
static bool body_specs(const Body& b, patch::SiteSpec* specs) {
    bool okay = true;
    for (unsigned k = 0; k < site::Count; ++k) {
        specs[k] = site::kSites[k];
        specs[k].address = address(b.spans[k]);
        const auto* bytes = static_cast<const unsigned char*>(b.spans[k]);
        std::memcpy(specs[k].expected, bytes, specs[k].length);
        const unsigned rel = site::kSites[k].rel32_offset;
        for (unsigned i = 0; i < specs[k].length; ++i) {
            if (rel && i >= rel && i < rel + 4) continue;
            okay = okay && bytes[i] == site::kSites[k].expected[i];
        }
    }
    check(okay, "synthetic spans match the proved native bytes except the relocated rel32 fields");
    return okay;
}
static DWORD WINAPI foreign_thread(LPVOID body) {
    reinterpret_cast<void (*)()>(body)();
    return 0;
}
static void stub_shape_checks() {
    void** next = nullptr;
    void* stub = submit::fixture_emit(7, &next);
    check(stub && next, "production context stub emitted");
    if (!stub || !next) return;
    const auto* code = static_cast<const unsigned char*>(stub);
    const unsigned used = unsigned(reinterpret_cast<const unsigned char*>(next) + 4 - code);
    check(used <= x3m::lean_stub::context_emitted + 3 && used <= x3m::lean_stub::context_reserve,
          "context stub fits its documented size and reservation");
    check(code[0] == 0x9c && code[1] == 0x60 && code[2] == 0xfc, "context stub opens with pushfd; pushad; cld");
    // Fixed layout: 3 + sub esp (6) + 8 saves (40) + lea/push (8) + push index (5) + call (5) + add esp (3) + 8 loads
    // (40) + add esp (6).
    check(code[49] == 0x8d && code[56] == 0x50 && code[57] == 0x68 && code[58] == 7 && code[62] == 0xe8,
          "context stub passes the pushad frame pointer and the site index");
    check(code[116] == 0x61 && code[117] == 0x9d && code[118] == 0xff && code[119] == 0x25,
          "context stub closes with popad; popfd; jmp [next], no x87 or fxsave save anywhere");
}
static void replay_checks() {
    prepare_engine();
    Body b = make_body();
    check(b.body != nullptr, "synthetic submit body emitted");
    if (!b.body) return;
    patch::SiteSpec specs[site::Count];
    if (!body_specs(b, specs)) return;
    unsigned char original[512];
    std::memcpy(original, b.body, b.length);
    Snapshot baseline{}, baseline_miss{}, baseline_skip{}, hooked{}, after{};
    invoke(b.body, baseline);
    body_miss = 1;
    invoke(b.body, baseline_miss);
    body_miss = 0;
    body_skip_block = 1;
    invoke(b.body, baseline_skip);
    body_skip_block = 0;
    const char* status = nullptr;
    check(submit::fixture_install(specs, &status, &view_root_word), "submit group installed on the synthetic spans");
    check(status && !std::strcmp(status, "ok"), "submit install status ok");
    check(submit::active, "submit group active after install");
    check(std::memcmp(original, b.body, b.length) != 0, "submit spans carry the patch jumps");
    const auto* gate = submit::fixture_gate();
    const auto* a = submit::fixture_accumulator();
    // Before the first frame boundary no thread is admitted: early, ignored.
    // The hit path runs every site but walk_miss.
    invoke(b.body, hooked);
    compare(baseline, hooked);
    check(gate->early.load() == site::Count - 1 && gate->foreign.load() == 0 && a->stamps == 0,
          "stamps before admission are early and ignored");
    submit::frame(1, false); // admits this thread; no frame-phase sample: dropped
    check(submit::fixture_dropped() == 1, "frame boundary without a frame-phase sample is a dropped frame");
    // One hit-path run: every pair closes once; the two surplus sort returns and
    // the second world return (one entry, both callers' returns in line) are idle.
    invoke(b.body, hooked);
    compare(baseline, hooked);
    bool once = true;
    for (unsigned i = 0; i < detail::interval_count; ++i) once = once && a->calls[i] == 1 && a->open_clock[i] == 0;
    check(once, "hit path closes each of the nine pairs exactly once and leaves none open");
    check(a->stamps == site::Count - 1 && a->idle == 3 && a->reopened == 0 && a->unmatched == 0 &&
              a->clock_errors == 0 && a->clock_failures == 0,
          "21 stamps, the surplus sort and world returns idle, no error counter");
    check(a->sort_nodes == 3 && a->sort_nodes_max == 3, "queue length read from the root global, head and sentinel");
    check(a->walk_lookups == 1 && a->walk_misses == 0 && a->walk_sampled_iterations == 0,
          "an unsampled lookup reads no engine memory");
    // technique, inverse_world, inverse_view, block_end, end_begin and end_end
    // run inside material; technique..inverse_view (6 stamps) inside block.
    check(a->material_inner == 10 && a->block_inner == 4,
          "dispatches nested in material and block are counted for the net figures");
    // Miss path: walk_miss closes the pair, the join is idle.
    body_miss = 1;
    invoke(b.body, hooked);
    compare(baseline_miss, hooked);
    body_miss = 0;
    check(a->calls[detail::Walk] == 2 && a->walk_misses == 1 && a->idle == 7 && a->stamps == 2 * site::Count - 1,
          "miss closes the walk at the malloc path and the join stamp is idle");
    // Geometry-guard skip: block_end never runs, end_begin closes the block.
    body_skip_block = 1;
    invoke(b.body, hooked);
    compare(baseline_skip, hooked);
    body_skip_block = 0;
    check(a->calls[detail::Block] == 3 && a->block_skipped == 1 && a->open_clock[detail::Block] == 0,
          "a skipped pass-loop guard closes the block at the End dispatch");
    // Lookups 4..16: the sixteenth is the sampled one, a hit on the third record.
    for (unsigned i = 0; i < 13; ++i) {
        invoke(b.body, hooked);
        compare(baseline, hooked);
    }
    check(a->walk_lookups == 16 && a->walk_sampled_iterations == 3,
          "the sampled hit counts the records up to the one in the saved ESI, through the saved EDI view");
    // Lookup 32 sampled on the miss path: every linked record.
    for (unsigned i = 0; i < 15; ++i) invoke(b.body, hooked);
    body_miss = 1;
    invoke(b.body, hooked);
    compare(baseline_miss, hooked);
    body_miss = 0;
    check(a->walk_lookups == 32 && a->walk_sampled_iterations == 6 && a->walk_misses == 2,
          "the sampled miss counts every linked record");
    LARGE_INTEGER qf{};
    check(QueryPerformanceFrequency(&qf) && qf.QuadPart > 0, "QPC frequency");
    const std::uint64_t hz = std::uint64_t(qf.QuadPart);
    std::uint64_t ticks[detail::interval_count];
    std::memcpy(ticks, a->ticks, sizeof ticks);
    const std::uint32_t stamps = a->stamps, material_inner = a->material_inner;
    submit::frame(2, true);
    detail::Sample s{};
    check(submit::fixture_last_sample(&s), "closed sample readable by the owner thread");
    bool converted = true, counted = true;
    for (unsigned i = 0; i < detail::interval_count; ++i) {
        converted = converted && s.interval_us[i] == ticks[i] * 1000000ull / hz;
        counted = counted && s.calls[i] == 32;
    }
    check(s.frame == 2 && converted && counted,
          "sample carries the frame, 32 closed pairs per interval and the tick-to-microsecond conversion");
    check(s.stamps == stamps && s.self_us == std::uint64_t(stamps) * detail::dispatch_cost_ns / 1000,
          "self cost is stamps x the fixture-measured dispatch cost");
    const std::uint64_t inner_us = std::uint64_t(material_inner) * detail::dispatch_cost_ns / 1000;
    check(s.material_net_us ==
              (s.interval_us[detail::Material] > inner_us ? s.interval_us[detail::Material] - inner_us : 0),
          "material net subtracts the nested dispatches");
    check(s.sort_nodes == 32 * 3 && s.sort_nodes_max == 3 && s.walk_misses == 2 &&
              s.walk_iterations == 6 * detail::walk_sample_period,
          "queue length, misses and the scaled walk iterations reach the sample");
    check(a->stamps == 0 && a->calls[0] == 0 && a->ticks[detail::Material] == 0 && a->walk_lookups == 32,
          "take resets the frame accumulation and keeps the sampling phase");
    // A stamp from another thread is foreign, counted and ignored.
    HANDLE thread = CreateThread(nullptr, 0, &foreign_thread, b.body, 0, nullptr);
    check(thread != nullptr, "foreign thread started");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    check(gate->foreign.load() == site::Count - 1 && a->stamps == 0, "foreign-thread stamps are counted and ignored");
    check(submit::fixture_uninstall(), "group rollback restores every span");
    check(!std::memcmp(original, b.body, b.length), "body byte-identical after rollback");
    check(!submit::active, "group inactive after rollback");
    invoke(b.body, after);
    compare(baseline, after);
    // Byte mismatch: one corrupted opcode refuses the whole group before any claim.
    Body c = make_body();
    check(c.body != nullptr, "second synthetic body emitted");
    if (!c.body) return;
    patch::SiteSpec corrupt[site::Count];
    if (!body_specs(c, corrupt)) return;
    unsigned char untouched[512];
    std::memcpy(untouched, c.body, c.length);
    corrupt[site::Count - 1].expected[0] ^= 1;
    check(!submit::fixture_install(corrupt, &status, &view_root_word),
          "install refused on a byte mismatch at the last site");
    check(status && !std::strcmp(status, "preflight_bytes"), "byte mismatch reported as preflight_bytes");
    check(!std::memcmp(untouched, c.body, c.length), "no span patched after the preflight refusal");
    check(!submit::active, "group stays inactive after refusal");
    submit::fixture_uninstall();
    // Partial install: site 12 duplicates site 3's address, so its claim reads
    // the fresh jump and fails; the twelve patched sites are rolled back.
    corrupt[site::Count - 1].expected[0] ^= 1;
    patch::SiteSpec partial[site::Count];
    std::memcpy(partial, corrupt, sizeof partial);
    partial[12] = partial[3];
    check(!submit::fixture_install(partial, &status, &view_root_word), "install refused on a duplicate claim");
    check(status && !std::strcmp(status, "bytes_mismatch"), "duplicate claim reported with the claim's own reason");
    check(!std::memcmp(untouched, c.body, c.length), "partial install rolled back to original bytes");
    check(!submit::active, "group inactive after partial rollback");
    submit::fixture_uninstall();
    invoke(c.body, after);
    compare(baseline, after);
}
// Per-dispatch cost of the context stub: the body unhooked against hooked,
// best of `trials`, over the 21 stamps of the hit path.
static void benchmark() {
    constexpr unsigned loops = 20000, trials = 7;
    prepare_engine();
    Body b = make_body();
    check(b.body != nullptr, "benchmark body emitted");
    if (!b.body) return;
    LARGE_INTEGER frequency{};
    check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "benchmark QPC frequency");
    const auto body = reinterpret_cast<void (*)()>(b.body);
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
    check(timed(baseline), "benchmark baseline timed");
    patch::SiteSpec specs[site::Count];
    if (!body_specs(b, specs)) return;
    const char* status = nullptr;
    check(submit::fixture_install(specs, &status, &view_root_word), "benchmark group installed");
    submit::frame(1, false);
    check(timed(hooked), "benchmark hooked timed");
    submit::frame(2, true);
    detail::Sample s{};
    check(submit::fixture_last_sample(&s) && s.stamps == loops * trials * (site::Count - 1) &&
              s.calls[detail::Material] == loops * trials,
          "hooked benchmark loop counted every stamp");
    check(submit::fixture_uninstall(), "benchmark group rolled back");
    const double ns_per_tick = 1e9 / number(std::uint64_t(frequency.QuadPart));
    const double baseline_ns = number(baseline) * ns_per_tick / loops, hooked_ns = number(hooked) * ns_per_tick / loops;
    const double dispatch_ns = (hooked_ns - baseline_ns) / (site::Count - 1);
    // A busy frame: ~12 stamps per draw at 510 draws, 2 per world matrix at
    // ~1,400 calls, 2-3 per cache lookup at ~900 nodes.
    std::printf(
        "SUBMIT PHASE BENCH loops=%u trials=%u stamps_per_loop=%u baseline_ns_per_loop=%.1f hooked_ns_per_loop=%.1f dispatch_ns=%.1f implied_busy_frame_us=%.0f documented_dispatch_ns=%llu arena_used=%u arena_capacity=%u\n",
        loops, trials, unsigned(site::Count - 1), baseline_ns, hooked_ns, dispatch_ns, dispatch_ns * 10800 / 1000,
        static_cast<unsigned long long>(detail::dispatch_cost_ns), patch::arena_used(), patch::arena_capacity());
    const double documented = number(detail::dispatch_cost_ns);
    check(dispatch_ns > 0 && documented >= dispatch_ns * 0.5 && documented <= dispatch_ns * 2.0,
          "documented submit dispatch cost within 2x of the measured cost");
}
static void late_window_checks() {
    prepare_engine();
    Body b = make_body();
    check(b.body != nullptr, "late-window body emitted");
    if (!b.body) return;
    patch::SiteSpec specs[site::Count];
    if (!body_specs(b, specs)) return;
    unsigned char original[512];
    std::memcpy(original, b.body, b.length);
    patch::close_install_window("fixture_first_present");
    const char* status = nullptr;
    check(!submit::fixture_install(specs, &status, &view_root_word), "install refused after the install window closed");
    check(status && !std::strcmp(status, "install_window_closed"), "late install reported as install_window_closed");
    check(!std::memcmp(original, b.body, b.length), "no span touched by the late refusal");
    check(!submit::active, "group inactive after the late refusal");
    submit::fixture_uninstall();
}
int main() {
    for (unsigned i = 0; i < 128; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(0xa5 ^ i * 7);
    stub_shape_checks();
    replay_checks();
    benchmark();
    late_window_checks();
    std::printf("SUBMIT PHASE CPU sites=%u intervals=%u arena_used=%u arena_capacity=%u checks=%u failures=%u\n",
                unsigned(site::Count), detail::interval_count, patch::arena_used(), patch::arena_capacity(), checks,
                failures);
    return failures ? 1 : 0;
}
