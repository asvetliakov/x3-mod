#include "seta_restore.h"
#include "seta_key_latch_core.h"
#include "capture.h"
#include "chase_transition.h"
#include "config.h"
#include "cpu_state.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "fog_prefill.h"
#include "object_trace.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::seta_restore::core;
namespace engine_patch = x3m::engine_patch;

// patched_: the hook is registered for shutdown() (also after a failed rollback); armed_: a complete install (hook and
// injection consumer), the only state in which the handlers act.
bool patched_ = false, attached_ = false;
std::atomic<bool> armed_{false};
const char* state_ = "disabled";
const char* inject_state_ = "unclaimed"; // the shared injection site's status at the attach
// The known builds' expectations for the rows (story_builds.h; the run-time source is the detection), formatted once
// at install: name:build:stop_ip:gate_return per entry, and the known build ids alone for the refused row.
char stories_[160] = "", known_builds_[64] = "";
// The story sites of the VM's current object (story_detect.h), detected at the first SETA stop after the VM key changed
// and kept for the key unless the failure was transient (retried at the next SETA stop, at most detect_retry_limit times
// per key: Retries::keep); used only inside the TI case handler (the VM's thread), outside the lock.
x3m::story::SetaSites sites_{};
x3m::story::Retries retries_{};
unsigned char scratch_[x3m::story::scratch_size];
void format_stories() {
    unsigned at = 0, known = 0;
    for (const x3m::story::Build& b : x3m::story::builds) {
        const int n = std::snprintf(stories_ + at, sizeof stories_ - at, "%s%s:%lu:0x%05lx:0x%05lx", at ? "," : "", b.name,
                                    static_cast<unsigned long>(b.build), static_cast<unsigned long>(b.seta_stop_ip),
                                    static_cast<unsigned long>(b.seta_gate_return));
        if (n > 0 && unsigned(n) < sizeof stories_ - at) at += unsigned(n);
        const int k = std::snprintf(known_builds_ + known, sizeof known_builds_ - known, "%s%lu", known ? "," : "",
                                    static_cast<unsigned long>(b.build));
        if (k > 0 && unsigned(k) < sizeof known_builds_ - known) known += unsigned(k);
    }
}
engine_patch::Site hook_site_{};
SRWLOCK lock_ = SRWLOCK_INIT;
State machine_; // under lock_
std::atomic<std::uint64_t> frame_{0};
std::atomic<std::uint32_t> epoch_{1}; // the EH adapter's unwinds (lock-free)
struct Guard {
    Guard() { AcquireSRWLockExclusive(&lock_); }
    ~Guard() { ReleaseSRWLockExclusive(&lock_); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

bool read(std::uintptr_t at, void* out, unsigned n) {
    return at && x3m::engine_memory::read(at, out, n);
}
template <class T> bool load(std::uintptr_t at, T& out) {
    return read(at, &out, unsigned(sizeof out));
}
Clock now() {
    Clock c;
    c.frame = frame_.load(std::memory_order_relaxed);
    c.ms = GetTickCount64();
    c.epoch = epoch_.load(std::memory_order_acquire);
    return c;
}
// Checked engine reads for the state machine (seta_restore_core.h, "What the injection site's probe answers").
struct EngineProbe {
    bool player(std::uint32_t& ship) {
        std::uint32_t m = 0;
        return load(manager_ptr_va, m) && m && !(m & 3) && load(m + manager_player, ship);
    }
    bool parent(std::uint32_t ship, std::uint32_t& sector) {
        return ship && !(ship & 3) && load(ship + object_parent, sector);
    }
    bool sector_id(std::uint32_t sector, std::uint32_t& id) {
        return sector && !(sector & 3) && load(sector + x3m::seta_restore::core::object_id, id);
    }
    bool object_id(std::uint32_t object, std::uint32_t& id) {
        return object && !(object & 3) && load(object + x3m::seta_restore::core::object_id, id);
    }
    bool kind(std::uint32_t object, std::uint16_t& cls, std::uint16_t& marker) {
        return object && !(object & 3) && load(object + object_class, cls) && load(object + object_marker, marker);
    }
    bool speed(std::uint32_t ship, std::uint32_t& current, std::uint32_t& desired) {
        return ship && !(ship & 3) && load(ship + ship_speed, current) && load(ship + ship_desired_speed, desired);
    }
    bool flags(std::uint32_t ship, std::uint32_t& f) {
        return ship && !(ship & 3) && load(ship + ship_flags, f);
    }
    bool script(std::uint32_t ship, std::uint32_t& id) {
        return ship && !(ship & 3) && load(ship + ship_script, id);
    }
    bool paused() {
        std::uint32_t game = 0, bits = 0;
        if (!load(game_root_va, game) || !game || !load(game + game_pause, bits)) return true; // unreadable: defer
        return (bits & pause_bit) != 0;
    }
    // sector-transit-order.md section 5 (the fog hand-over's walk): the newest sector object, alive, with its scene,
    // and an id other than the departure sector's. At most once per presented frame while armed.
    bool ready(std::uint32_t departure_id, std::uint32_t& node) {
        x3m::engine_memory::revalidate(); // the stall reallocated: an epoch only (fog_prefill_poll does the same)
        auto r = [](std::uintptr_t p, void* out, std::size_t n) { return x3m::engine_memory::read(p, out, n); };
        const x3m::fog_prefill::Result result = x3m::fog_prefill::walk(r, departure_id);
        if (result.status != x3m::fog_prefill::Walk::Found) return false;
        node = result.node;
        return true;
    }
};
// ---- the SETA key stop latch (seta_key_latch_core.h; enabled by seta_manual_flight, key_latch_enable) ----
// latch_ and key_rows_ under lock_; calling_ names whose call the injection site is running (under lock_), so result()
// and a write inside the call reach the right owner. latch_debug_: the debug tier (X3M_DEBUG=1, set once before the game
// loop): the seta_factor / seta_key / seta_key_latch rows, and the prefilter bit kept set so every iteration is seen.
namespace latch = x3m::seta_key_latch::core;
latch::Latch latch_;
latch::KeyRows key_rows_;
enum class Calling : unsigned { none = 0, ticket = 1, latch = 2 };
Calling calling_ = Calling::none;
std::uint32_t latch_call_epoch_ = 0; // the EH epoch when the latch's call was handed out (under lock_)
std::atomic<bool> latch_debug_{false};
std::uint64_t qpc_frequency_ = 0;
unsigned factor_rows_ = 0, latch_rows_ = 0; // debug rows written this session (factor: VM thread; latch: under lock_)
std::uint64_t now_us() {
    LARGE_INTEGER c{};
    if (!qpc_frequency_ || !QueryPerformanceCounter(&c) || c.QuadPart < 0) return 0;
    const std::uint64_t t = std::uint64_t(c.QuadPart);
    return t / qpc_frequency_ * 1000000u + t % qpc_frequency_ * 1000000u / qpc_frequency_;
}
void publish() {
    x3m::chase_transition::inject_want(machine_.active() || latch_.active() ||
                                       latch_debug_.load(std::memory_order_relaxed));
}
// Debug rows of the latch (arm, fire, release) and its refusals; a refusal row is written in every tier (once per reason
// per session). The caller decided under lock_ whether the debug row fits the session budget.
void emit_latch(const latch::Report& r, std::uint64_t frame, unsigned due) {
    if (r.event == latch::Event::none) return;
    if (r.event == latch::Event::refused) {
        x3m::log("seta_key_latch refused reason=%s frame=%llu old=0x%08lx new=0x%08lx ip=0x%05lx ret=0x%05lx detect=%s method=%s counter=%u entries=%u calls=%u state=%s",
                 latch::reason_name(r.reason), static_cast<unsigned long long>(frame),
                 static_cast<unsigned long>(r.old_factor), static_cast<unsigned long>(r.new_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret), x3m::story::fail_name(r.fail),
                 r.where[0] ? r.where : "-", r.counter, r.entries, r.calls,
                 r.reason >= latch::refused_unreadable ? "off" : "not_armed");
        return;
    }
    if (due == 2) x3m::log("seta_key_latch rows_capped cap=%u", latch::latch_row_cap);
    if (due != 1) return;
    const unsigned long long us = now_us();
    if (r.event == latch::Event::armed)
        x3m::log("seta_key_latch armed frame=%llu us=%llu old=0x%08lx new=0x%08lx ip=0x%05lx ret=0x%05lx rearm=%u",
                 static_cast<unsigned long long>(frame), us, static_cast<unsigned long>(r.old_factor),
                 static_cast<unsigned long>(r.new_factor), static_cast<unsigned long>(r.ip),
                 static_cast<unsigned long>(r.ret), unsigned(r.rearm));
    else if (r.event == latch::Event::fire)
        x3m::log("seta_key_latch fire frame=%llu us=%llu old=0x%08lx new=0x%08lx ip=0x%05lx entries=%u held=%u zeros=%u calls=%u fires=%u",
                 static_cast<unsigned long long>(frame), us, static_cast<unsigned long>(r.old_factor),
                 static_cast<unsigned long>(r.new_factor), static_cast<unsigned long>(r.ip), r.entries, r.held, r.zeros,
                 r.calls, r.fires);
    else
        x3m::log("seta_key_latch released reason=%s frame=%llu us=%llu counter=%u entries=%u held=%u zeros=%u calls=%u fires=%u",
                 latch::reason_name(r.reason), static_cast<unsigned long long>(frame), us, r.counter, r.entries, r.held,
                 r.zeros, r.calls, r.fires);
}
// Under lock_: 1 = the debug row fits the session budget, 2 = the budget is spent now (one rows_capped row), 0 = none.
unsigned latch_row_due(const latch::Report& r) {
    if (r.event == latch::Event::none || r.event == latch::Event::refused || !latch_debug_.load(std::memory_order_relaxed) ||
        latch_rows_ > latch::latch_row_cap)
        return 0;
    ++latch_rows_;
    return latch_rows_ <= latch::latch_row_cap ? 1 : 2;
}
// One injection-site entry's key state (debug tier only; checked reads, the counter first).
struct KeySnap {
    bool counter_read = false, ring_read = false, ctx_read = false, factor_read = false;
    unsigned char counter = 0, slots[latch::ring_slot_count]{}, steer[latch::steer_count]{};
    std::uint16_t read_key = 0, ri = 0, wi = 0;
    std::uint32_t mouse = 0, joystick = 0, factor = 0;
    std::uint64_t frame = 0, us = 0;
};
KeySnap key_prev_{}; // the previous entry's snapshot (the press's first read precedes the counter's rise)
void read_snap(KeySnap& s) {
    s = KeySnap{};
    s.frame = frame_.load(std::memory_order_relaxed);
    s.us = now_us();
    s.counter_read = load(latch::seta_counter_va, s.counter);
    unsigned char ring[latch::ring_span];
    s.ring_read = load(latch::ring_read_va, s.read_key) && load(latch::ring_ri_va, s.ri) && load(latch::ring_wi_va, s.wi) &&
                  read(latch::ring_slots_va, ring, latch::ring_span) &&
                  read(latch::steer_counters_va, s.steer, latch::steer_count);
    if (s.ring_read)
        for (unsigned i = 0; i < latch::ring_slot_count; ++i) s.slots[i] = ring[i * latch::ring_slot_stride];
    std::uint32_t ctx = 0, cfg = 0;
    s.ctx_read = load(latch::input_ctx_ptr_va, ctx) && ctx && load(ctx + latch::ctx_joystick_buttons, s.joystick) &&
                 load(ctx + latch::ctx_mouse_buttons, s.mouse);
    s.factor_read = load(cfg_ptr_va, cfg) && cfg && load(cfg + cfg_factor, s.factor);
}
void emit_key(const KeySnap& s, const char* kind, std::uint32_t press, bool latched) {
    x3m::log("seta_key %s frame=%llu us=%llu press=%lu counter=%u counter_read=%u read=0x%04x ri=%u wi=%u slots=%02x,%02x,%02x,%02x ring_read=%u steer=%u,%u,%u,%u,%u,%u mouse=0x%08lx joystick=0x%08lx ctx_read=%u factor=0x%08lx factor_read=%u latch=%u",
             kind, static_cast<unsigned long long>(s.frame), static_cast<unsigned long long>(s.us),
             static_cast<unsigned long>(press), unsigned(s.counter), unsigned(s.counter_read), unsigned(s.read_key),
             unsigned(s.ri), unsigned(s.wi), unsigned(s.slots[0]), unsigned(s.slots[1]), unsigned(s.slots[2]),
             unsigned(s.slots[3]), unsigned(s.ring_read), unsigned(s.steer[0]), unsigned(s.steer[1]),
             unsigned(s.steer[2]), unsigned(s.steer[3]), unsigned(s.steer[4]), unsigned(s.steer[5]),
             static_cast<unsigned long>(s.mouse), static_cast<unsigned long>(s.joystick), unsigned(s.ctx_read),
             static_cast<unsigned long>(s.factor), unsigned(s.factor_read), unsigned(latched));
}
// One row per transition, formatted outside every lock (x3m::log runs the CRT formatter behind call_preserved). The
// speed fields (record only) are the current player ship's [+0x10]/[+0x14] as int32; speed_read=0: not read.
void emit(const Report& r) {
    const unsigned long speed_ship = r.speed_ship;
    const unsigned speed_read = r.speed_readable;
    const long speed = static_cast<std::int32_t>(r.speed), desired = static_cast<std::int32_t>(r.desired_speed);
    switch (r.row) {
    case Row::none: return;
    case Row::armed:
    case Row::skipped:
        x3m::log("seta_restore %s%s frame=%llu old=0x%08lx new=0x%08lx ip=0x%05lx ret=0x%05lx ret_name=%s ship=0x%08lx ship_id=0x%08lx script_id=0x%08lx sector=0x%08lx sector_id=%lu build=%lu autopilot=%u flags=0x%08lx speed_ship=0x%08lx speed_read=%u speed=%ld desired_speed=%ld",
                 r.row == Row::armed ? "armed" : "skipped reason=",
                 r.row == Row::armed ? "" : skip_name(r.reason), r.frame, static_cast<unsigned long>(r.old_factor),
                 static_cast<unsigned long>(r.new_factor), static_cast<unsigned long>(r.ip),
                 static_cast<unsigned long>(r.ret), r.ret_name,
                 static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.ship_id),
                 static_cast<unsigned long>(r.script_id), static_cast<unsigned long>(r.sector), static_cast<unsigned long>(r.sector_id),
                 static_cast<unsigned long>(r.build), unsigned(r.autopilot), static_cast<unsigned long>(r.flags),
                 speed_ship, speed_read, speed, desired);
        return;
    case Row::rebound:
    case Row::rebind_pending:
        // rebound: new_flags_read=0 leaves new_flags/new_autopilot 0; rebind_pending: ship_check=no_sector, no flags read
        x3m::log("seta_restore %s frame=%llu frames_since_arm=%llu ms_since_arm=%llu old_ship=0x%08lx new_ship=0x%08lx old_ship_id=0x%08lx new_ship_id=0x%08lx script_id=0x%08lx new_script_id=0x%08lx sector_id=%lu new_sector=0x%08lx new_sector_id=%lu ship_check=%s new_flags_read=%u new_flags=0x%08lx new_autopilot=%u speed_ship=0x%08lx speed_read=%u speed=%ld desired_speed=%ld",
                 r.row == Row::rebound ? "rebound" : "rebind_pending", r.frame, r.frames_since_arm, r.ms_since_arm,
                 static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.new_ship),
                 static_cast<unsigned long>(r.ship_id), static_cast<unsigned long>(r.new_ship_id),
                 static_cast<unsigned long>(r.script_id), static_cast<unsigned long>(r.new_script_id),
                 static_cast<unsigned long>(r.sector_id), static_cast<unsigned long>(r.new_sector),
                 static_cast<unsigned long>(r.new_sector_id), ship_check_name(r.ship_check),
                 unsigned(r.new_flags_readable), static_cast<unsigned long>(r.new_flags),
                 unsigned((r.new_flags & autopilot_bit) != 0), speed_ship, speed_read, speed, desired);
        return;
    case Row::cancelled:
        x3m::log("seta_restore cancelled reason=%s frame=%llu frames_since_arm=%llu ms_since_arm=%llu write_old=0x%08lx write_new=0x%08lx write_ip=0x%05lx write_ret=0x%05lx write_scan=%s write_ret_name=%s ship=0x%08lx sector_id=%lu ready_sector=0x%08lx skipped=%lu ship_check=%s new_ship=0x%08lx script_id=0x%08lx new_script_id=0x%08lx new_sector=0x%08lx new_sector_id=%lu speed_ship=0x%08lx speed_read=%u speed=%ld desired_speed=%ld",
                 cancel_name(r.reason), r.frame, r.frames_since_arm, r.ms_since_arm,
                 static_cast<unsigned long>(r.old_factor), static_cast<unsigned long>(r.new_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret), scan_name(r.scan),
                 r.reason == cancel_factor_write ? r.ret_name : "none",
                 static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.sector_id),
                 static_cast<unsigned long>(r.ready_sector), static_cast<unsigned long>(r.skipped),
                 ship_check_name(r.ship_check), static_cast<unsigned long>(r.new_ship),
                 static_cast<unsigned long>(r.script_id), static_cast<unsigned long>(r.new_script_id),
                 static_cast<unsigned long>(r.new_sector), static_cast<unsigned long>(r.new_sector_id), speed_ship,
                 speed_read, speed, desired);
        return;
    case Row::restored:
    case Row::restore_failed:
        x3m::log("seta_restore %s frame=%llu frames_after_transit=%llu frames_since_arm=%llu ms_since_arm=%llu result=%lu call=0x%08lx key=0x%lx method=%s ship=0x%08lx sector_id=%lu ready_sector=0x%08lx skipped=%lu speed_ship=0x%08lx speed_read=%u speed=%ld desired_speed=%ld",
                 r.row == Row::restored ? "restored" : "restore_failed", r.frame, r.frames_after_transit,
                 r.frames_since_arm, r.ms_since_arm, static_cast<unsigned long>(r.result), static_cast<unsigned long>(script_call_va),
                 static_cast<unsigned long>(call_key), call_method, static_cast<unsigned long>(r.ship),
                 static_cast<unsigned long>(r.sector_id), static_cast<unsigned long>(r.ready_sector),
                 static_cast<unsigned long>(r.skipped), speed_ship, speed_read, speed, desired);
        return;
    case Row::refused:
        x3m::log("seta_restore refused reason=%s frame=%llu build=%lu object=%s detect=%s method=%s known_builds=%s old=0x%08lx ip=0x%05lx ret=0x%05lx sector_id=%lu",
                 refusal_name(r.reason), r.frame, static_cast<unsigned long>(r.build), x3m::story::object_name(r.build),
                 x3m::story::fail_name(r.detect_fail), r.detect_where[0] ? r.detect_where : "-", known_builds_,
                 static_cast<unsigned long>(r.old_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret),
                 static_cast<unsigned long>(r.sector_id));
        return;
    }
}
// The row with its record-only speed fields (two checked reads, only when a row is due), outside the lock.
void finish(Report& r) {
    if (r.row == Row::none) return;
    EngineProbe probe;
    record_speed(r, probe);
    emit(r);
}

// ---- the injection consumer (chase_transition::InjectConsumer) ----
// Once per main-loop iteration while the prefilter bit is set (a ticket, an armed latch, or the debug tier): the ticket
// first; the latch's StopFastForward on an entry the ticket does not use. Under the debug tier the key state is read
// first (outside the lock) for the seta_key rows.
bool poll(std::uint32_t controlled, x3m::chase_transition::InjectCall& call) noexcept {
    if (!armed_.load(std::memory_order_acquire)) return false;
    const bool debug = latch_debug_.load(std::memory_order_relaxed);
    KeySnap snap;
    if (debug) read_snap(snap);
    Report r;
    latch::Report lr;
    bool wanted = false, latch_call = false, latched = false;
    unsigned due = 0;
    latch::KeyRows::Emit emit = latch::KeyRows::Emit::none;
    std::uint32_t press = 0;
    KeySnap pre;
    latch::Report unwound;
    unsigned unwound_due = 0;
    {
        Guard guard;
        if (calling_ == Calling::latch) { // the latch's previous call never reported back: it unwound
            unwound = latch_.on_unwound();
            unwound_due = latch_row_due(unwound);
        }
        calling_ = Calling::none; // the ticket's own state machine sees its unreported call (cancel call_unwound)
        EngineProbe probe;
        wanted = machine_.on_inject(now(), controlled, probe, r);
        if (wanted) {
            calling_ = Calling::ticket;
        } else if (latch_.active()) {
            unsigned char counter = snap.counter;
            const bool readable = debug ? snap.counter_read : load(latch::seta_counter_va, counter);
            latch_call = latch_.on_entry(readable, counter, lr);
            if (latch_call) {
                calling_ = Calling::latch;
                latch_call_epoch_ = epoch_.load(std::memory_order_acquire);
            }
            due = latch_row_due(lr);
        }
        latched = latch_.active();
        if (debug) {
            emit = key_rows_.on_entry(snap.counter_read ? snap.counter : 0);
            press = key_rows_.press;
            pre = key_prev_;
            key_prev_ = snap;
        }
        publish();
    }
    finish(r);
    emit_latch(unwound, frame_.load(std::memory_order_relaxed), unwound_due);
    emit_latch(lr, frame_.load(std::memory_order_relaxed), due);
    if (emit ==latch::KeyRows::Emit::pre_and_row) emit_key(pre, "pre", press, latched);
    if (emit == latch::KeyRows::Emit::row || emit == latch::KeyRows::Emit::pre_and_row) emit_key(snap, "row", press, latched);
    if (emit == latch::KeyRows::Emit::capped) x3m::log("seta_key rows_capped cap=%u", latch::key_row_cap);
    if (!wanted && !latch_call) return false;
    call.target = script_call_va;
    call.key = call_key;
    call.name = wanted ? call_method : latch::call_method;
    call.return_kind = call_return_kind;
    call.argc = call_argc;
    call.value = 0;
    return true;
}
void result(unsigned al) noexcept {
    Report r;
    latch::Report lr;
    unsigned due = 0;
    {
        Guard guard;
        if (calling_ == Calling::latch) {
            lr = latch_.on_result(al);
            due = latch_row_due(lr);
        } else {
            r = machine_.on_result(al, now());
        }
        calling_ = Calling::none;
        publish();
    }
    finish(r);
    emit_latch(lr, frame_.load(std::memory_order_relaxed), due);
}
void vm(unsigned kind) noexcept {
    if (!armed_.load(std::memory_order_acquire)) return;
    Report r;
    latch::Report lr;
    unsigned due = 0;
    {
        Guard guard;
        r = machine_.on_vm(kind, now());
        lr = latch_.on_vm();
        due = latch_row_due(lr);
        publish();
    }
    finish(r);
    emit_latch(lr, frame_.load(std::memory_order_relaxed), due);
}
void unwind() noexcept {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
}
const x3m::chase_transition::InjectConsumer consumer_{&poll, &result, &vm, &unwind};
}

extern "C" {
volatile std::uint32_t x3m_seta_restore_ti_continue = 0; // the claim tail (the two displaced instructions + jmp back)

// At the TI_SetTimeWarpFactor case entry, on the VM's thread inside a native dispatch: checked reads and the state
// machine only, no VM call. The stack scan runs only for a write that can arm or cancel (SETA stops, writes while a
// ticket is active): rare.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_seta_restore_factor_write(std::uint32_t args,
                                                                                    std::uint32_t task) {
    x3m::LightCallBoundary cpu; // MXCSR + LastError; no x87 below (the row formatter runs behind call_preserved)
    if (!armed_.load(std::memory_order_acquire)) return;
    Write w;
    std::uint32_t cfg = 0, vm_root = 0;
    unsigned char tag = 0;
    w.readable = load(cfg_ptr_va, cfg) && cfg && load(cfg + cfg_factor, w.old_factor) && read(args, &tag, 1) &&
                 load(args + 1, w.new_factor) && load(task + task_ip, w.ip);
    w.tag = tag;
    const bool stop_shaped = w.readable && w.old_factor > factor_one && w.new_factor == factor_one;
    if (stop_shaped) { // the sites of the VM's current object: detected once per VM key (reads only, outside the lock)
        auto reader = [](std::uint32_t at, void* out, unsigned n) { return read(at, out, n); };
        x3m::story::VmKey key;
        if (load(vm_ptr_va, vm_root) && vm_root && x3m::story::read_key(reader, vm_root, key)) {
            if (!sites_.detected || sites_.key != key) {
                x3m::story::detect_seta(reader, vm_root, sites_, scratch_);
                sites_.key = key;
                sites_.detected = retries_.keep(key, sites_.outcome); // a transient failure: this stop refused, the next retries
            }
            w.build = key.build;
            w.sites = &sites_;
            w.build_readable = true;
        }
    }
    const bool debug = latch_debug_.load(std::memory_order_relaxed);
    Report r;
    latch::Report lr;
    unsigned due = 0;
    Calling inside = Calling::none;
    bool latched = false;
    {
        Guard guard;
        if (machine_.active() || stop_shaped || debug)
            w.ret = first_return(task, [](std::uintptr_t at, void* out, unsigned n) { return read(at, out, n); });
        EngineProbe probe;
        r = machine_.on_write(w, now(), probe);
        // inside the latch's own call only while the EH epoch has not moved since the hand-out (an unwound call leaves
        // calling_ set until the next entry; the core then refuses instead of counting a stranger's write as a fire)
        const bool inside_latch =
            calling_ == Calling::latch && epoch_.load(std::memory_order_acquire) == latch_call_epoch_;
        lr = latch_.on_write(w, frame_.load(std::memory_order_relaxed), inside_latch);
        due = latch_row_due(lr);
        inside = calling_;
        latched = latch_.active();
        publish();
    }
    finish(r);
    const std::uint64_t frame = frame_.load(std::memory_order_relaxed);
    emit_latch(lr, frame, due);
    // Debug tier: every factor write (record only), at most factor_row_cap per session. The names come from the VM's
    // current detection (sites_, this thread only); ip_name marks StopFastForward's write, the ramp step is IP 14341
    // (Mayhem, seta-manual-flight.md section 7.3).
    if (!debug || factor_rows_ > latch::factor_row_cap) return;
    if (++factor_rows_ > latch::factor_row_cap) {
        x3m::log("seta_factor rows_capped cap=%u", latch::factor_row_cap);
        return;
    }
    const x3m::story::SetaSites* names = sites_.detected && sites_.outcome.ok() ? &sites_ : nullptr;
    x3m::log("seta_factor frame=%llu us=%llu old=0x%08lx new=0x%08lx tag=%lu readable=%u ip=0x%05lx ip_name=%s ret=0x%05lx scan=%s ret_name=%s inside=%s latch=%u",
             static_cast<unsigned long long>(frame), static_cast<unsigned long long>(now_us()),
             static_cast<unsigned long>(w.old_factor), static_cast<unsigned long>(w.new_factor),
             static_cast<unsigned long>(w.tag), unsigned(w.readable), static_cast<unsigned long>(w.ip),
             names && w.ip == names->stop_ip ? "StopFastForward" : "other", static_cast<unsigned long>(w.ret.offset),
             scan_name(w.ret.scan), return_name(names, w.ret.offset, w.ret.scan == Scan::bottom),
             inside == Calling::latch ? "latch" : inside == Calling::ticket ? "ticket" : "none", unsigned(latched));
}
}

// Integer-only stub at 0x004b148c (entered by the claim's dispatcher): saves EFLAGS, the eight general registers and
// XMM0..7 (the handler is SSE2 code), passes EBX (argument cells) and EBP (task), clears DF for the C ABI, restores
// everything and continues at the claim tail, which replays `mov eax,[0x00606f34]; mov edx,[ebx+1]` and jumps to
// 0x004b1494. ESP leaves exactly as it entered.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_seta_restore_ti_stub
_x3m_seta_restore_ti_stub:
    pushfd
    pushad
    sub esp, 0x80
    movdqu [esp+0x00], xmm0
    movdqu [esp+0x10], xmm1
    movdqu [esp+0x20], xmm2
    movdqu [esp+0x30], xmm3
    movdqu [esp+0x40], xmm4
    movdqu [esp+0x50], xmm5
    movdqu [esp+0x60], xmm6
    movdqu [esp+0x70], xmm7
    cld
    push ebp
    push ebx
    call _x3m_seta_restore_factor_write
    add esp, 8
    movdqu xmm0, [esp+0x00]
    movdqu xmm1, [esp+0x10]
    movdqu xmm2, [esp+0x20]
    movdqu xmm3, [esp+0x30]
    movdqu xmm4, [esp+0x40]
    movdqu xmm5, [esp+0x50]
    movdqu xmm6, [esp+0x60]
    movdqu xmm7, [esp+0x70]
    add esp, 0x80
    popad
    popfd
    jmp dword ptr [_x3m_seta_restore_ti_continue]
    .att_syntax
)");
extern "C" void x3m_seta_restore_ti_stub();

namespace {
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into the stub.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
// Every window the hook, its register contract and the call depend on (core::windows; verify_seta_restore_sites.py
// pins the same), each read whole through ReadProcessMemory; mismatch_ names the first one that differs.
Mismatch mismatch_{};
bool windows_match() {
    return compare_windows(
        [](std::uint32_t at, void* out, unsigned n) {
            return engine_patch::read_code(at, static_cast<unsigned char*>(out), n);
        },
        mismatch_);
}
// Lower-case hex of n <= shown_bytes bytes into out (2 * shown_bytes + 1 chars), "-" for none.
void hex(const unsigned char* bytes, unsigned n, char* out) {
    static const char digits[] = "0123456789abcdef";
    if (!n) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * n] = 0;
}
const char* write_kind(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
// Hook, then the shared injection site; a later failure puts the hook back. A hook that cannot be restored keeps the
// module registered (patched_) so shutdown() tries again; armed_ stays false (the stub then only forwards).
bool install(const char** reason) {
    mismatch_ = Mismatch{}; // a retried initialize() must not print a stale window on a non-byte refusal
    if (!x3m::object_trace::executable_verified()) {
        *reason = "executable_mismatch";
        return false;
    }
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!windows_match()) {
        *reason = "bytes_mismatch";
        return false;
    }
    if (!pin_self()) {
        *reason = "pin_failed";
        return false;
    }
    engine_patch::SiteSpec spec{};
    spec.name = "seta_restore_factor";
    spec.address = hook_va;
    spec.length = hook_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, hook_bytes, hook_length);
    hook_site_ = engine_patch::Site{};
    if (!engine_patch::claim(hook_site_, spec)) {
        *reason = hook_site_.status;
        if (hook_site_.patched_in) patched_ = true; // rollback_failed: still registered
        return false;
    }
    x3m_seta_restore_ti_continue = address_of(*hook_site_.entry);
    bool ok = engine_patch::push_front(hook_site_, reinterpret_cast<void*>(&x3m_seta_restore_ti_stub)) != nullptr;
    if (!ok) *reason = "chain_failed";
    if (ok) {
        ok = x3m::chase_transition::inject_attach(&consumer_, &inject_state_);
        if (!ok) *reason = inject_state_;
        attached_ = ok;
    }
    if (!ok) {
        if (!engine_patch::restore(hook_site_)) {
            patched_ = true;
            *reason = "rollback_failed";
        }
        return false;
    }
    patched_ = true;
    *reason = "ok";
    return true;
}
}

namespace x3m::seta_restore {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_ || attached_) {
        SetLastError(error);
        return armed_.load(std::memory_order_acquire);
    }
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_SETA_RESTORE", setting, 4);
    if (!(length == 1 && setting[0] == L'1')) {
        state_ = "disabled";
        if (length) log("seta_restore installed=0 requested=0 reason=disabled");
        SetLastError(error);
        return false;
    }
    {
        Guard guard;
        machine_ = State{};
        latch_ = latch::Latch{}; // enabled later by seta_manual_flight (key_latch_enable)
        key_rows_ = latch::KeyRows{};
        calling_ = Calling::none;
        latch_rows_ = 0;
    }
    factor_rows_ = 0;
    sites_ = x3m::story::SetaSites{};
    retries_ = x3m::story::Retries{};
    format_stories();
    const bool ok = install(&state_);
    armed_.store(ok, std::memory_order_release);
    if (ok)
        log("seta_restore installed=1 hook=0x%08lx write=%s stub=0x%08lx inject_site=0x%08lx inject=%s call=0x%08lx key=0x%lx method=%s detect=runtime expected=%s due_frames=%llu expiry_ms=%llu",
            static_cast<unsigned long>(hook_va), write_kind(hook_site_),
            static_cast<unsigned long>(address_of(reinterpret_cast<const void*>(&x3m_seta_restore_ti_stub))),
            static_cast<unsigned long>(inject_site_va), inject_state_, static_cast<unsigned long>(script_call_va),
            static_cast<unsigned long>(call_key), call_method, stories_,
            static_cast<unsigned long long>(due_frames), static_cast<unsigned long long>(expiry_ms));
    else {
        const Window* w = mismatch_.window;
        char expected[2 * shown_bytes + 1], got[2 * shown_bytes + 1];
        hex(w ? w->bytes : nullptr, w ? mismatch_.shown : 0, expected);
        if (w && !mismatch_.readable)
            std::memcpy(got, "unreadable", sizeof "unreadable");
        else
            hex(mismatch_.got, w ? mismatch_.shown : 0, got);
        log("seta_restore refused reason=%s window=0x%08lx window_name=%s expected=%s got=%s hook=0x%08lx hook_status=%s inject=%s patched=%u",
            state_, static_cast<unsigned long>(w ? w->va : 0), w ? w->name : "-", expected, got,
            static_cast<unsigned long>(hook_va), hook_site_.status, inject_state_, unsigned(patched_));
    }
    SetLastError(error);
    return ok;
}
const char* key_latch_enable(bool debug) {
    const DWORD error = GetLastError();
    const char* status = "ok";
    if (!armed_.load(std::memory_order_acquire)) {
        status = std::strcmp(state_, "disabled") == 0 ? "seta_restore_disabled" : "seta_restore_refused";
    } else {
        LARGE_INTEGER f{};
        qpc_frequency_ = debug && QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
        Guard guard;
        latch_ = latch::Latch{};
        latch_.enabled = true;
        key_rows_ = latch::KeyRows{};
        key_prev_ = KeySnap{};
        latch_rows_ = 0;
        latch_debug_.store(debug, std::memory_order_relaxed);
        publish();
    }
    SetLastError(error);
    return status;
}
bool shutdown() {
    const DWORD error = GetLastError();
    armed_.store(false, std::memory_order_release);
    latch_debug_.store(false, std::memory_order_relaxed);
    {
        Guard guard;
        latch_.enabled = false;
        latch_.armed = false;
    }
    bool ok = true;
    if (attached_) {
        ok = x3m::chase_transition::inject_detach() && ok;
        attached_ = false;
    }
    if (patched_) {
        const bool back = engine_patch::restore(hook_site_);
        if (back) patched_ = false;
        ok = back && ok;
    }
    SetLastError(error);
    return ok;
}
const char* state() {
    return state_;
}
void present(unsigned long long frame) {
    if (armed_.load(std::memory_order_relaxed)) frame_.store(frame, std::memory_order_relaxed);
}
}
