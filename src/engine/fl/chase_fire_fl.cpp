// FL edition of the chase cursor fire (src/proxy/chase_fire.h; the AP module is src/proxy/chase_fire.cpp). Contract:
// docs/reverse-engineering/fl/chase-fire-aim.md, "Verified 2026-11-03"; site and windows:
// src/engine/fl/chase_fire_sites_fl.h. The policy is the AP module's (shared portable core chase_fire_core.h): in the
// applied external chase view, a player main-group cursor fire call whose native cursor-active compare found the cursor
// inactive takes the native cursor ray (unprojection through the chase camera at the real cursor pixel, the native
// range/cone checks and gun transport) instead of skipping it. Only the saved ZF of the one relocated JE changes; no
// cursor, steering, fire argument, ray, range, gun or camera state is written.
// FL differences: base + RVA addresses admitted by five identity windows; the fire ABI (ECX = ship saved at [EBP-0x3c],
// stack words at EBX+8: target, group, flags, mask); the cursor cells in x/active/y order; a zero active handle refused.
// The witness is published by the chase camera's pose seam (camera_context with that update's QPC), as on AP.
#include "../../proxy/chase_fire.h"
#include "../../proxy/chase_fire_core.h"
#include "../../proxy/chase_camera.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/capture.h"
#include "../../proxy/telemetry.h"
#include "executable_identity_fl.h"
#include "chase_fire_sites_fl.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Verified x86 engine ABI");
namespace x3m::chase_fire {
namespace {
namespace S = x3m::fl::chase_fire_fl;
using core::Context;
using core::Result;
std::uintptr_t base_ = 0;
#ifdef X3M_CHASE_FIRE_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
unsigned fixture_fault_offset_[2] = {6, 6};
bool fixture_fault_ = false;
#endif
engine_patch::SiteSpec spec{};
engine_patch::Site site;
std::atomic<bool> enabled{false};
std::atomic<std::uint32_t> player{0};
bool initialized = false, timing = false;
std::uint64_t frequency = 0;
SRWLOCK lock = SRWLOCK_INIT;
Context context;
struct Counts {
    std::uint64_t result[unsigned(Result::Count)]{};
    std::uint64_t timed = 0, ticks = 0, max_ticks = 0;
};
Counts counts;
// The early filters (before the lock, the QPC or the registry walk), counted by reason so a flight can tell a natively
// active cursor from a call that never reached the decision. `native_active` is incremented by the stub itself (lock
// inc on its saved-ZF-clear path: the cursor-active compare found the cursor active, the native cursor ray runs anyway).
enum Early : unsigned { NoWitness, Group, Flags, OtherShip, EarlyRead, EarlyCount };
std::atomic<std::uint32_t> early[EarlyCount]{};
volatile LONG native_active = 0;
void note_early(Early e) {
    early[e].fetch_add(1, std::memory_order_relaxed);
}
inline double as_double(std::uint64_t v) {
    return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
}
bool read(std::uintptr_t base, unsigned offset, void* out, unsigned size) {
    return base && !(base & 3) && offset <= UINT32_MAX - base && size <= UINT32_MAX - base - offset &&
           engine_memory::read(base + offset, out, size);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return read(base, offset, &out, sizeof out);
}
// Image globals (base + RVA); the RVAs are relocated dwords of the admitted windows.
template <class T> bool global(std::uint32_t rva, T& out) {
    return base_ && engine_memory::read(base_ + rva, &out, sizeof out);
}
std::uint64_t now() {
    LARGE_INTEGER stamp{};
    return QueryPerformanceCounter(&stamp) && stamp.QuadPart > 0 ? std::uint64_t(stamp.QuadPart) : 0;
}
// The registry's active control cockpit (resolver base + 0x01e420's walk, bounded, no engine call): a nonzero active
// handle whose row maps to an aligned cockpit. The native fire path resolves it again itself.
bool active_cockpit(std::uintptr_t& cockpit) {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0;
    if (!global(S::registry_slot_rva, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !handle || !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row)) return false;
        if (row[1] == handle) {
            cockpit = row[2];
            return cockpit && !(cockpit & 3);
        }
        link = row[0];
    }
    return false;
}
// X3FL.exe bindings of the shared policy.
struct Env {
    static bool active_cockpit(std::uintptr_t& cockpit) { return chase_fire::active_cockpit(cockpit); }
    template <class T> static bool field(std::uintptr_t base, unsigned offset, T& out) {
        return chase_fire::field(base, offset, out);
    }
    // One 12-byte read of x, active, y (base + 0x1aa180), permuted to the canonical active, x, y.
    static bool cursor(std::int32_t (&cursor)[3]) {
        std::int32_t cells[3]{};
        if (!global(S::cursor_x_rva, cells)) return false;
        cursor[0] = cells[1];
        cursor[1] = cells[0];
        cursor[2] = cells[2];
        return true;
    }
    static std::uintptr_t defaults_slot() { return base_ ? base_ + S::defaults_slot_rva : 0; }
    static std::uint32_t thread() { return GetCurrentThreadId(); }
    static std::uint64_t frequency() { return chase_fire::frequency; }
};
// regs: the pushad block (EDI ESI EBP ESP EBX EDX ECX EAX), then EFLAGS.
void handle(std::uint32_t* regs) {
    // The stub already left for a disabled module or a natively active cursor (ZF clear). Almost every other call
    // leaves at the two bounded argument reads, before the lock, the QPC or the registry walk.
    if (!enabled.load(std::memory_order_acquire) || !(regs[8] & core::zero_flag)) return;
    const auto expected_ship = player.load(std::memory_order_relaxed);
    if (!expected_ship) return note_early(NoWitness);
    std::uint32_t call[2]{}, ship = 0; // [EBX+0xc] gun group, [EBX+0x10] fire flags; [EBP-0x3c] ship
    if (!field(regs[4], S::arg_group, call) || regs[2] < S::ship_below_ebp ||
        !field(regs[2] - S::ship_below_ebp, 0, ship))
        return note_early(EarlyRead);
    if (core::call_refused(call[0], call[1])) return note_early(call[0] != core::main_group ? Group : Flags);
    if (ship != expected_ship) return note_early(OtherShip);
    const auto stamp = now();
    Env env;
    AcquireSRWLockExclusive(&lock);
    const Result result = core::decide(context, ship, stamp, env);
    if (result == Result::Override) regs[8] &= ~core::zero_flag;
    ++counts.result[unsigned(result)];
    if (timing) {
        const auto end = now(), ticks = end > stamp ? end - stamp : 0;
        ++counts.timed;
        counts.ticks += ticks;
        if (ticks > counts.max_ticks) counts.max_ticks = ticks;
    }
    ReleaseSRWLockExclusive(&lock);
}
} // namespace
} // namespace x3m::chase_fire
// cdecl from the generated stub with the pushad block. The game's stack is 4-byte aligned mid-function
// (force_align_arg_pointer). Full CPU boundary: the handler runs under a clean masked x87 and MXCSR 0x1f80; the native
// x87 image, MXCSR and LastError come back before the relocated JE replays.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_fire_fl_enter(std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_fire::handle(regs);
}
namespace x3m::chase_fire {
namespace {
// Stub (entry after its aligned continuation slot):
//   pushfd; cmp byte [enabled],0; je pass; test byte [esp],0x40; jnz main
//   lock inc dword [native_active]                    <- cursor natively active (saved ZF clear)
//   pass: popfd; jmp [next]                           <- disabled or cursor natively active: flags/stack restored
//   main: pushad; cld; sub esp,0x80; movups [esp+16*i],xmm_i; lea eax,[esp+0x80]; push eax; call enter; add esp,4;
//   movups xmm_i,[esp+16*i]; add esp,0x80; popad; popfd; jmp [next]
// The saved EFLAGS sit above the pushad block, as in the AP stub; popfd restores DF and every flag but a cleared ZF.
void* emit(void*** next_out) {
    engine_patch::Emitter e(200);
    if (!e.ok()) return nullptr;
    while (e.ok() && (reinterpret_cast<std::uintptr_t>(e.here()) & 3)) e.byte(0xcc);
    auto* slot = static_cast<void**>(e.here());
    e.dword(0);
    void* start = e.here();
    static_assert(sizeof(std::atomic<bool>) == 1, "one-byte flag");
    e.byte(0x9c);
    e.byte(0x80);
    e.byte(0x3d);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(&enabled)));
    e.byte(0x00);
    e.byte(0x74); // je pass
    e.byte(0x0d);
    e.byte(0xf6); // test byte [esp],0x40
    e.byte(0x04);
    e.byte(0x24);
    e.byte(0x40);
    e.byte(0x75); // jnz main
    e.byte(0x0e);
    e.byte(0xf0); // lock inc dword [native_active]
    e.byte(0xff);
    e.byte(0x05);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(&native_active)));
    e.byte(0x9d); // pass
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(slot)));
    e.byte(0x60); // main
    e.byte(0xfc);
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x80);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x11);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x8d);
    e.byte(0x84);
    e.byte(0x24);
    e.dword(0x80);
    e.byte(0x50);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&x3m_chase_fire_fl_enter));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(4);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x10);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x80);
    e.byte(0x61);
    e.byte(0x9d);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(slot)));
    const bool complete = e.ok();
    *next_out = slot;
    return e.finish() && complete ? start : nullptr;
}
std::uintptr_t fl_base() {
#ifdef X3M_CHASE_FIRE_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
bool read_code(std::uintptr_t address, void* out, unsigned size) {
    return engine_patch::read_code(address, static_cast<unsigned char*>(out), size);
}
// The FL read-back rollback (the chase_lead / chase_camera FL convention): a site that does not read back as this
// module's jump is not owned, so engine_patch::restore() would refuse it. Inside the install window the original first
// five bytes go back over whatever is there; restored only when all six bytes then read back original.
bool force_restore() {
    auto* code = reinterpret_cast<void*>(site.spec.address);
    DWORD writable = 0, unused = 0;
    if (VirtualProtect(code, 6, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(site.spec.address, site.original, 5);
        FlushInstructionCache(GetCurrentProcess(), code, 6);
        VirtualProtect(code, 6, site.protection, &unused);
    }
    unsigned char back[6]{};
    if (!engine_patch::read_code(site.spec.address, back, 6) || std::memcmp(back, site.original, 6)) return false;
    site.patched_in = false;
    return true;
}
// Claim, read back, chain the stub: all or nothing. *rollback says what happened to the site's bytes on failure
// ("restored", "failed", or "none" when nothing was written).
bool install(const char*& status, const char*& rollback) {
    rollback = "none";
    if (!engine_patch::claim(site, spec)) {
        status = site.status;
        if (site.patched_in) rollback = "failed";
        else if (!std::strcmp(site.status, "patch_rolled_back")) rollback = "restored";
        return false;
    }
#ifdef X3M_CHASE_FIRE_FL_FIXTURE
    if (fixture_fault_) { // a foreign write between the claim and the read-back
        for (const unsigned offset : fixture_fault_offset_)
            if (offset < 6) *reinterpret_cast<unsigned char*>(site.spec.address + offset) ^= 0xff;
        fixture_fault_ = false;
    }
#endif
    unsigned char now[5]{};
    if (!engine_patch::read_code(site.spec.address, now, 5) || std::memcmp(now, site.patched, 5)) {
        status = "readback_mismatch";
        rollback = force_restore() ? "restored" : "failed";
        return false;
    }
    void** next = nullptr;
    void* stub = emit(&next);
    const char* failure = !stub || !next                                    ? "stub_failed"
                          : !engine_patch::store_pointer(next, *site.entry) ? "chain_write_failed"
                          : !engine_patch::push_front(site, stub)           ? "chain_failed"
                                                                            : nullptr;
    if (failure) {
        status = failure;
        rollback = engine_patch::restore(site) ? "restored" : "failed";
        return false;
    }
    status = "active";
    return true;
}
} // namespace
#ifdef X3M_CHASE_FIRE_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_reset() {
    initialized = false;
    site = engine_patch::Site{};
    enabled.store(false);
    player.store(0);
    AcquireSRWLockExclusive(&lock);
    context = {};
    counts = {};
    ReleaseSRWLockExclusive(&lock);
    for (auto& e : early) e.store(0);
    InterlockedExchange(&native_active, 0);
    base_ = 0;
}
void fixture_fault_readback(unsigned offset, unsigned second) {
    fixture_fault_ = true;
    fixture_fault_offset_[0] = offset;
    fixture_fault_offset_[1] = second;
}
void fixture_age_witness(std::int64_t ticks) {
    AcquireSRWLockExclusive(&lock);
    context.qpc = std::uint64_t(std::int64_t(context.qpc) + ticks);
    ReleaseSRWLockExclusive(&lock);
}
void fixture_shift_thread() {
    AcquireSRWLockExclusive(&lock);
    ++context.thread;
    ReleaseSRWLockExclusive(&lock);
}
FixtureCounts fixture_counts() {
    FixtureCounts f;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < unsigned(Result::Count) && i < 6; ++i) f.result[i] = counts.result[i];
    f.timed = counts.timed;
    f.frequency = frequency;
    ReleaseSRWLockExclusive(&lock);
    f.player = player.load();
    for (unsigned i = 0; i < EarlyCount && i < 5; ++i) f.early[i] = early[i].load();
    f.native_active = std::uint32_t(native_active);
    return f;
}
void fixture_timing(bool on) {
    timing = on;
}
#endif
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    timing = telemetry::enabled();
    LARGE_INTEGER f{};
    frequency = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    const std::uintptr_t base = fl_base();
    const char* status = nullptr;
    const char* window = "-";
    const char* rollback = "none";
    if (!chase_camera::installed())
        status = "chase_camera_unavailable";
    else if (!frequency)
        status = "prerequisite_failed";
    else if (!base)
        status = "executable_mismatch";
    else if (!engine_patch::install_window_open())
        status = "late_claim";
    else
        status = S::plan(base, read_code, &window);
    bool ok = status == nullptr;
    if (ok) {
        base_ = base;
        spec = engine_patch::SiteSpec{};
        spec.name = S::admission.name;
        spec.address = base + S::admission.rva;
        std::memcpy(spec.expected, S::admission.bytes, 6);
        spec.length = 6;
        spec.ret_pop = 0;
        spec.rel32_offset = S::admission.rel32_offset;
        ok = install(status, rollback);
    }
    enabled.store(ok, std::memory_order_release);
    log("chase_fire installed=%u status=%s scope=applied_chase_258_player_main_cursor lifetime=process "
        "telemetry_required=0 native_input_writes=0 edition=fl base=0x%08lx rva=0x%06lx window=%s rollback=%s",
        unsigned(ok), ok ? "active" : status, static_cast<unsigned long>(base),
        static_cast<unsigned long>(S::admission.rva), window, rollback);
    SetLastError(error);
    return ok;
}
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
void invalidate_camera(std::uintptr_t cockpit) {
    if (!enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&lock);
    if (context.cockpit == cockpit) {
        context = {};
        player.store(0, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&lock);
}
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, std::uint32_t mode,
                    std::uint32_t connect, std::uint32_t flags, bool applied, std::uint64_t stamp) {
    if (!enabled.load(std::memory_order_acquire)) return;
    Context next;
    if (core::publishable(applied, mode, connect, flags, stamp, cockpit, ship, camera) &&
        field(ship, 8, next.ship_id)) {
        next.cockpit = cockpit;
        next.ship = ship;
        next.camera = camera;
        next.qpc = stamp;
        next.thread = GetCurrentThreadId();
    }
    AcquireSRWLockExclusive(&lock);
    context = next;
    player.store(std::uint32_t(next.ship), std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&lock);
}
void report(std::uint64_t frame) {
    if (!enabled.load(std::memory_order_acquire) || !timing) return;
    Counts c;
    AcquireSRWLockExclusive(&lock);
    c = counts;
    counts = {};
    ReleaseSRWLockExclusive(&lock);
    std::uint32_t e[EarlyCount]{};
    for (unsigned i = 0; i < EarlyCount; ++i) e[i] = early[i].exchange(0, std::memory_order_relaxed);
    const auto active = std::uint32_t(InterlockedExchange(&native_active, 0));
    // An effective branch override count, not proof that later native gun checks admitted a ray. native_active and
    // early_* count seam visits of every caller (the seam is reached only with both cursor flag bits set).
    const double micros = frequency ? 1000000.0 / as_double(frequency) : 0;
    log("chase_fire_window frame=%llu native_inactive_override=%llu refused_stale=%llu refused_identity=%llu "
        "refused_view=%llu refused_cursor=%llu refused_read=%llu timed_calls=%llu total_us=%.3f max_us=%.3f "
        "timing_scope=eligible_handler_after_first_qpc excludes=stub_cpu_boundary_first_qpc_early_filter "
        "native_input_writes=0 downstream_constraints=native native_active=%lu early_no_witness=%lu early_group=%lu "
        "early_flags=%lu early_other_ship=%lu early_read=%lu edition=fl",
        frame, c.result[0], c.result[1], c.result[2], c.result[3], c.result[4], c.result[5], c.timed,
        as_double(c.ticks) * micros, as_double(c.max_ticks) * micros, static_cast<unsigned long>(active),
        static_cast<unsigned long>(e[NoWitness]), static_cast<unsigned long>(e[Group]),
        static_cast<unsigned long>(e[Flags]), static_cast<unsigned long>(e[OtherShip]),
        static_cast<unsigned long>(e[EarlyRead]));
}
void shutdown() {
    enabled.store(false, std::memory_order_release);
    player.store(0, std::memory_order_relaxed);
    if (site.patched_in) {
        const bool restored = engine_patch::restore(site);
        log("chase_fire_shutdown restored=%u status=%s edition=fl", unsigned(restored), site.status);
    }
}
} // namespace x3m::chase_fire
