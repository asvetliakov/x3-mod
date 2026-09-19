#include "collide_narrow_census.h"
#include "collide_box_cull_core.h"   // the 300-frame p50/max/sum window (four series)
#include "engine_patch.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::collide_narrow_census::core;
namespace core = x3m::collide_narrow_census::core;
namespace engine_patch = x3m::engine_patch;
using x3m::collide_narrow_census::Addresses;
using Window = x3m::collide_box_cull::core::Window;
using WindowSummary = x3m::collide_box_cull::core::WindowSummary;
static_assert(x3m::collide_box_cull::core::counter_count == series_count, "window series");

bool patched_ = false;
const char* state_ = "disabled";
engine_patch::CallSite n5_site_{}, n6_site_{};
engine_patch::Site n7_site_{};
std::uintptr_t stubs_[3] = {0, 0, 0};   // sites 5, 6, 7
std::uint64_t qpc_frequency_ = 0;

// ---- game-thread state (pre/post handlers) ----
Entry pending_{};
std::uint32_t pending_node_ = 0, pending_mesh_ = 0;
std::uint64_t pending_qpc_ = 0;
volatile std::uint32_t dropped_ = 0, writer_thread_ = 0;   // written by the handlers only, monotonic / last value
// ---- shared under lock_ (try-lock on both sides, never waited on) ----
volatile LONG lock_ = 0;
Entry rings_[3][ring_capacity];
unsigned ring_count_[3] = {0, 0, 0};
unsigned write_ = 0, previous_ = 1, spare_ = 2;
std::uint32_t frame_accepted_ = 0, frame_overflow_ = 0;
std::uint64_t frame_ticks_ = 0;
// ---- Present-thread state ----
Window window_;
struct Sums { std::uint64_t recorded, with_previous, unchanged, memo_hits, memo_hit_visits, memo_unsafe, visits_differ, changed_position, changed_xform, changed_saved, overflow, deferred, cross_thread; } sums_{};
std::uint32_t seen_counters_[4] = {0, 0, 0, 0}, window_base_[3] = {0, 0, 0};   // dropped, nested, foreign at the window start
x3m::collide_narrow_census::FrameView last_{};

bool try_lock() { return InterlockedExchange(&lock_, 1) == 0; }
void unlock() { InterlockedExchange(&lock_, 0); }
std::uint64_t qpc() { LARGE_INTEGER v{}; return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0; }
std::uint64_t to_us(std::uint64_t ticks) { return qpc_frequency_ ? ticks * 1000000ull / qpc_frequency_ : 0; }

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[128]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool call_targets(std::uintptr_t at, std::uintptr_t target) {
    unsigned char code[5]{};
    if (!engine_patch::read_code(at, code, 5) || code[0] != 0xe8) return false;
    std::uint32_t rel = 0; std::memcpy(&rel, code + 1, 4);
    return at + 5 + rel == target;
}
// The 103 bytes of 0x0048ac80 with its one rel32 (the call to 0x0048a890) checked as a target.
bool n5_callee_matches() {
    unsigned char actual[n5_callee_length]{};
    if (!engine_patch::read_code(n5_target_va, actual, n5_callee_length)) return false;
    return !std::memcmp(actual, n5_callee, n5_callee_rel32) && !std::memcmp(actual + n5_callee_rel32 + 4, n5_callee + n5_callee_rel32 + 4, n5_callee_length - n5_callee_rel32 - 4)
        && call_targets(n5_target_va + n5_callee_rel32 - 1, n6_function_va);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN):
// the stubs' absolute operands and handler calls can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
template<class Encode>
std::uintptr_t emit_stub(Encode&& encode) {
    engine_patch::Emitter e(stub_capacity);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    unsigned char code[stub_capacity];
    const unsigned length = encode(std::uint32_t(at), code);
    if (!length) return 0;
    e.bytes(code, length);
    return e.finish() ? at : 0;
}
bool windows_match(const Addresses& a) {
    unsigned char n7[n7_window_length];
    n7_expected(std::uint32_t(a.n7_hits), std::uint32_t(a.n7_mode), n7);
    return bytes_match(a.n5_site - n5_pre_length, n5_pre_window, n5_pre_length) && bytes_match(a.n5_site + call_length, n5_post_window, n5_post_length)
        && bytes_match(a.n6_site - n6_pre_length, n6_pre_window, n6_pre_length) && bytes_match(a.n6_site + call_length, n6_post_window, n6_post_length)
        && bytes_match(a.n7_site, n7, n7_window_length);
}
template<class T> std::uint32_t address_of(T* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
const char* write_kind(bool in, bool atomic) { return in ? (atomic ? "atomic" : "plain") : "none"; }
}

extern "C" {
volatile std::uint32_t x3m_collide_narrow_counters[4] = {0, 0, 0, 0};
volatile unsigned char x3m_collide_narrow_busy = 0;

// Called by the site-5 stub before the narrow phase with EFLAGS, the integer
// registers and XMM0-7 saved. No x87 opcode, no log, no allocation.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_collide_narrow_pre(const unsigned char* object_a, const unsigned char* object_b) {
    x3m::LightCallBoundary cpu;   // MXCSR + LastError: QueryPerformanceCounter may set the last error
    const std::uint32_t physics_a = load32(object_a + physics_offset), physics_b = load32(object_b + physics_offset);
    pending_.a = read_key(address_of(object_a), object_a, physics_a, reinterpret_cast<const unsigned char*>(std::uintptr_t(physics_a)));
    pending_.b = read_key(address_of(object_b), object_b, physics_b, reinterpret_cast<const unsigned char*>(std::uintptr_t(physics_b)));
    pending_mesh_ = x3m_collide_narrow_counters[0]; pending_node_ = x3m_collide_narrow_counters[1];
    writer_thread_ = GetCurrentThreadId();
    pending_qpc_ = qpc();
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_collide_narrow_post(std::int32_t result) {
    x3m::LightCallBoundary cpu;
    const std::uint64_t now = qpc();
    const std::uint64_t ticks = now > pending_qpc_ && pending_qpc_ ? now - pending_qpc_ : 0;
    pending_.result = result; pending_.flags = 0;
    pending_.mesh_pairs = x3m_collide_narrow_counters[0] - pending_mesh_; pending_.visits = x3m_collide_narrow_counters[1] - pending_node_;
    pending_.ticks = ticks > 0xffffffffull ? 0xffffffffu : std::uint32_t(ticks);
    if (!try_lock()) { dropped_ = dropped_ + 1; return; }   // Present on another thread is swapping the rings
    ++frame_accepted_; frame_ticks_ += ticks;
    if (ring_count_[write_] < ring_capacity) rings_[write_][ring_count_[write_]++] = pending_; else ++frame_overflow_;
    unlock();
}
}

namespace x3m::collide_narrow_census {
bool install_at(const Addresses& a) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.n5_site || !a.n5_target || !a.n6_site || !a.n6_target || !a.n7_site || !a.n7_hits || !a.n7_mode) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!windows_match(a)) return done("bytes_mismatch", false);
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return done("clock_unavailable", false);
    if (!pin_self()) return done("pin_failed", false);
    const N5Operands operands{std::uint32_t(a.n5_site + call_length), std::uint32_t(a.n5_target), address_of(&x3m_collide_narrow_pre), address_of(&x3m_collide_narrow_post),
                              address_of(&x3m_collide_narrow_busy), address_of(&x3m_collide_narrow_counters[2]), address_of(&x3m_collide_narrow_counters[3])};
    const std::uintptr_t stub5 = emit_stub([&](std::uint32_t at, unsigned char* out) { return encode_n5_stub(at, operands, out); });
    const std::uintptr_t stub6 = emit_stub([&](std::uint32_t at, unsigned char* out) { return encode_n6_stub(at, address_of(&x3m_collide_narrow_counters[0]), std::uint32_t(a.n6_target), out); });
    const std::uintptr_t stub7 = emit_stub([&](std::uint32_t at, unsigned char* out) { return encode_n7_stub(at, address_of(&x3m_collide_narrow_counters[1]), std::uint32_t(a.n7_hits), std::uint32_t(a.n7_site + call_length), out); });
    if (!stub5 || !stub6 || !stub7) return done("arena_full", false);

    qpc_frequency_ = std::uint64_t(frequency.QuadPart);
    x3m_collide_narrow_busy = 0;
    ring_count_[0] = ring_count_[1] = ring_count_[2] = 0; write_ = 0; previous_ = 1; spare_ = 2;
    frame_accepted_ = frame_overflow_ = 0; frame_ticks_ = 0; sums_ = Sums{}; window_.reset(); last_ = FrameView{};
    for (unsigned i = 0; i < 4; ++i) seen_counters_[i] = x3m_collide_narrow_counters[i];
    window_base_[0] = dropped_; window_base_[1] = seen_counters_[2]; window_base_[2] = seen_counters_[3];

    // Site 7 first, then 6, then 5: an earlier counter without its consumer is
    // harmless, and a failure rolls every earlier site back. A site that is
    // live but cannot be restored keeps the module registered for shutdown().
    n7_site_ = engine_patch::Site{}; n6_site_ = engine_patch::CallSite{}; n5_site_ = engine_patch::CallSite{};
    engine_patch::SiteSpec spec{};
    spec.name = "collide_narrow_census_n7"; spec.address = a.n7_site; spec.length = call_length; spec.ret_pop = 0; spec.rel32_offset = 0;
    unsigned char n7[n7_window_length];
    n7_expected(std::uint32_t(a.n7_hits), std::uint32_t(a.n7_mode), n7);
    std::memcpy(spec.expected, n7, call_length);   // the one displaced instruction: mov eax,[hits]
    const char* reason = nullptr;
    if (!engine_patch::claim(n7_site_, spec) || !engine_patch::push_front(n7_site_, reinterpret_cast<void*>(stub7))) reason = n7_site_.patched_in ? "chain_failed" : n7_site_.status;
    else if (!engine_patch::claim_call(n6_site_, a.n6_site, a.n6_target, reinterpret_cast<void*>(stub6))) reason = n6_site_.status;
    else if (!engine_patch::claim_call(n5_site_, a.n5_site, a.n5_target, reinterpret_cast<void*>(stub5))) reason = n5_site_.status;
    if (reason) {
        const bool back5 = engine_patch::restore_call(n5_site_), back6 = engine_patch::restore_call(n6_site_), back7 = engine_patch::restore(n7_site_);
        if (!back5 || !back6 || !back7) { patched_ = true; return done("rollback_failed", false); }   // registered: shutdown() tries again
        return done(reason, false);
    }
    patched_ = true; stubs_[0] = stub5; stubs_[1] = stub6; stubs_[2] = stub7;
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_COLLIDE_NARROW_CENSUS", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    // The two callees are outside the windows install_at compares: checked here, on the real image only.
    else if (!n5_callee_matches() || !bytes_match(n6_target_va, n6_callee, n6_callee_length)
             || !call_targets(n5_site_va, n5_target_va) || !call_targets(n6_site_va, n6_target_va)) state_ = "callee_mismatch";
    else applied = install_at(Addresses{n5_site_va, n5_target_va, n6_site_va, n6_target_va, n7_site_va, n7_hits_va, n7_mode_va});
    log("collide_narrow_census requested=%u patched=%u reason=%s n5_site=0x%08lx n6_site=0x%08lx n7_site=0x%08lx write_n5=%s write_n6=%s write_n7=%s "
        "stub_n5=0x%08lx stub_n6=0x%08lx stub_n7=0x%08lx ring=%u qpc_frequency=%llu",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(n5_site_va), static_cast<unsigned long>(n6_site_va), static_cast<unsigned long>(n7_site_va),
        write_kind(n5_site_.patched_in, n5_site_.atomic_write), write_kind(n6_site_.patched_in, n6_site_.atomic_write), write_kind(n7_site_.patched_in, n7_site_.atomic_write),
        static_cast<unsigned long>(stubs_[0]), static_cast<unsigned long>(stubs_[1]), static_cast<unsigned long>(stubs_[2]), ring_capacity, qpc_frequency_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    const bool back5 = engine_patch::restore_call(n5_site_), back6 = engine_patch::restore_call(n6_site_), back7 = engine_patch::restore(n7_site_);
    patched_ = false; stubs_[0] = stubs_[1] = stubs_[2] = 0;   // the stubs stay in the arena (a thread may still be inside them)
    state_ = back5 && back6 && back7 ? "restored" : "restore_failed";
    SetLastError(error);
    return back5 && back6 && back7;
}
const char* state() { return state_; }
std::uintptr_t stub_address(unsigned site) { return patched_ && site >= 5 && site <= 7 ? stubs_[site - 5] : 0; }
FrameView last_frame() { return last_; }
std::uint32_t dropped_total() { return dropped_; }

bool present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return false;
    const DWORD error = GetLastError();
    if (!try_lock()) { ++sums_.deferred; SetLastError(error); return false; }   // a post handler on another thread holds it: this frame folds into the next
    const unsigned taken = write_;
    write_ = spare_; ring_count_[write_] = 0;
    const std::uint32_t accepted = frame_accepted_, overflow = frame_overflow_; const std::uint64_t ticks = frame_ticks_;
    frame_accepted_ = frame_overflow_ = 0; frame_ticks_ = 0;
    unlock();
    // Monotonic stub counters: plain aligned loads, deltas against the last Present (no write from this side).
    std::uint32_t delta[2];
    for (unsigned i = 0; i < 2; ++i) { const std::uint32_t v = x3m_collide_narrow_counters[i]; delta[i] = v - seen_counters_[i]; seen_counters_[i] = v; }
    Entry* entries = rings_[taken]; const unsigned count = ring_count_[taken];
    const MemoSummary memo = annotate(entries, count, rings_[previous_], ring_count_[previous_]);
    const std::uint64_t us = to_us(ticks);
    const std::uint32_t values[series_count] = {accepted, delta[0], delta[1], us > 0xffffffffull ? 0xffffffffu : std::uint32_t(us)};
    window_.add(frame, values);
    sums_.recorded += count; sums_.with_previous += memo.with_previous; sums_.unchanged += memo.unchanged; sums_.memo_hits += memo.memo_hits;
    sums_.memo_hit_visits += memo.memo_hit_visits; sums_.memo_unsafe += memo.memo_unsafe; sums_.visits_differ += memo.visits_differ;
    sums_.changed_position += memo.changed_position; sums_.changed_xform += memo.changed_xform; sums_.changed_saved += memo.changed_saved; sums_.overflow += overflow;
    const std::uint32_t writer = writer_thread_;
    if (writer && writer != GetCurrentThreadId()) ++sums_.cross_thread;
    last_ = FrameView{entries, count, accepted, overflow, delta[0], delta[1], ticks, memo};
    if (captured) {
        std::uint16_t order[ring_capacity];
        order_by_visits(entries, count, order);
        for (unsigned rank = 0; rank < count; ++rank) {
            const Entry& e = entries[order[rank]];
            std::uint32_t d_max = 0;
            for (unsigned k = 0; k < 3; ++k) {
                const std::uint32_t d = std::uint32_t(e.a.pos[k]) - std::uint32_t(e.b.pos[k]), m = d & 0x80000000u ? 0u - d : d;
                if (m > d_max) d_max = m;
            }
            log("collide_narrow_pair device=%llu frame=%llu rank=%u of=%u a=0x%08lx b=0x%08lx class_a=%u class_b=%u subtype_a=%u subtype_b=%u model_a=%ld model_b=%ld "
                "radius_a=%ld radius_b=%ld flags40_a=0x%08lx flags44_a=0x%08lx flags40_b=0x%08lx flags44_b=0x%08lx node_flags_a=0x%08lx node_flags_b=0x%08lx "
                "pos_a=%ld,%ld,%ld pos_b=%ld,%ld,%ld d_max=%lu r_sum=%lld visits=%lu mesh_pairs=%lu us=%llu result=%ld contact=%u "
                "previous=%u same_pos=%u same_xform=%u same_saved=%u unchanged=%u memo_hit=%u memo_unsafe=%u visits_differ=%u",
                device, frame, rank, count, (unsigned long)e.a.object, (unsigned long)e.b.object, unsigned(e.a.cls), unsigned(e.b.cls), unsigned(e.a.subtype), unsigned(e.b.subtype),
                (long)std::int32_t(e.a.model), (long)std::int32_t(e.b.model), (long)e.a.radius, (long)e.b.radius,
                (unsigned long)e.a.flags40, (unsigned long)e.a.flags44, (unsigned long)e.b.flags40, (unsigned long)e.b.flags44, (unsigned long)e.a.node_flags, (unsigned long)e.b.node_flags,
                (long)e.a.pos[0], (long)e.a.pos[1], (long)e.a.pos[2], (long)e.b.pos[0], (long)e.b.pos[1], (long)e.b.pos[2], (unsigned long)d_max,
                (long long)e.a.radius + e.b.radius, (unsigned long)e.visits, (unsigned long)e.mesh_pairs, to_us(e.ticks), (long)e.result, e.result > 0 ? 1u : 0u,
                e.flags & had_previous ? 1u : 0u, e.flags & same_position ? 1u : 0u, e.flags & same_xform ? 1u : 0u, e.flags & same_saved ? 1u : 0u,
                e.flags & core::unchanged ? 1u : 0u, e.flags & memo_hit ? 1u : 0u, e.flags & memo_unsafe ? 1u : 0u, e.flags & visits_differ ? 1u : 0u);
        }
    }
    spare_ = previous_; previous_ = taken;
    if (window_.full()) {
        WindowSummary s;
        if (window_.close(s)) {
            const std::uint32_t dropped = dropped_, nested = x3m_collide_narrow_counters[2], foreign = x3m_collide_narrow_counters[3];
            log("collide_narrow frame=%llu frames=%u accepted_p50=%llu accepted_max=%llu accepted_sum=%llu mesh_pairs_p50=%llu mesh_pairs_max=%llu mesh_pairs_sum=%llu "
                "node_pairs_p50=%llu node_pairs_max=%llu node_pairs_sum=%llu narrow_us_p50=%llu narrow_us_max=%llu narrow_us_sum=%llu "
                "recorded_sum=%llu with_previous_sum=%llu unchanged_sum=%llu memo_would_hit_sum=%llu memo_would_hit_permille=%llu memo_visits_sum=%llu memo_visits_permille=%llu "
                "memo_unsafe_sum=%llu memo_visits_differ_sum=%llu changed_pos_sum=%llu changed_xform_sum=%llu changed_saved_sum=%llu "
                "ring_overflow=%llu dropped=%lu deferred=%llu nested=%lu foreign=%lu cross_thread_frames=%llu",
                s.frame, s.frames, s.p50[0], s.max[0], s.sum[0], s.p50[1], s.max[1], s.sum[1], s.p50[2], s.max[2], s.sum[2], s.p50[3], s.max[3], s.sum[3],
                sums_.recorded, sums_.with_previous, sums_.unchanged, sums_.memo_hits, s.sum[0] ? sums_.memo_hits * 1000 / s.sum[0] : 0ull,
                sums_.memo_hit_visits, s.sum[2] ? sums_.memo_hit_visits * 1000 / s.sum[2] : 0ull,
                sums_.memo_unsafe, sums_.visits_differ, sums_.changed_position, sums_.changed_xform, sums_.changed_saved,
                sums_.overflow, (unsigned long)(dropped - window_base_[0]), sums_.deferred, (unsigned long)(nested - window_base_[1]), (unsigned long)(foreign - window_base_[2]), sums_.cross_thread);
            window_base_[0] = dropped; window_base_[1] = nested; window_base_[2] = foreign;
        }
        sums_ = Sums{};
    }
    SetLastError(error);
    return true;
}
}
