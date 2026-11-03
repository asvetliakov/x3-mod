#include "chase_transition.h"
#include "config.h"
#include "chase_transition_core.h"
#include "chase_transition_identity_core.h"
#include "chase_transition_restore_core.h"
#include "chase_camera.h"
#include "chase_lead.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "telemetry.h"
#if X3M_EDITION_FL
#include "../engine/fl/chase_camera_sites_fl.h"
#include "../engine/fl/chase_transition_sites_fl.h"
#include "../engine/fl/executable_identity_fl.h"
#endif
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Verified x86 game ABI only");
namespace x3m::chase_transition {
namespace {
#if X3M_EDITION_FL
// FL (X3FL.exe, relocatable; src/engine/fl/chase_transition_sites_fl.h, docs/reverse-engineering/fl/
// chase-transition.md "Verified 2026-11-02"): the same witness/observer/restore groups bound to the loaded base by
// fl_admit() once every identity window matches. Eight sites: the six mandatory witnesses and the script/load mode
// stores (AP's connect observer, kind 8, is a diagnostic only and is not claimed). With telemetry the event window runs
// on FL too, on FL-verified reads only (snapshot(), active_handle(), fl_store_frame()) plus a kind-9 row per finished
// restore ticket; what would need an unverified read stays off and is named in fl_diagnostics_off. Register ABI
// differences are local to handle(), restore_on_mode_store() and restore_inject(); native return addresses are
// normalized to preferred-base VAs.
namespace S = x3m::fl::chase_transition_fl;
constexpr unsigned mandatory = S::mandatory, site_count = S::site_count;
engine_patch::SiteSpec specs[site_count]{};
constexpr unsigned restore_site_count = S::restore_site_count, restore_inject_kind = 0, restore_eh_kind = 3;
engine_patch::SiteSpec restore_specs[restore_site_count]{};
std::uintptr_t fl_base = 0;                // the admitted base (0 until fl_admit succeeds)
std::uint32_t fl_native_return = 0;        // base + S::native_return_rva
const char* fl_refusal = "not_initialized"; // initialize()'s FL status
unsigned fl_failed_window = S::window_count;
// AP diagnostics not ported on FL, logged in the install and window rows: the connect observer (kind 8, not claimed),
// the VM return-record and destructor provenance, IdentityReader and geometry (+0xa8/+0x160/+0x120), whose FL
// contracts are unverified; the +0x130 boom field (unverified on FL); and the +0x58 world camera and +0x1e0 target,
// which are FL-verified (fl/chase-camera.md, fl/chase-lead.md) but kept out deliberately: the FL row carries the
// restore's field set.
constexpr const char* fl_diagnostics_off =
    "connect_observer,origin_returns,destructor_origin,identity,geometry,camera_58,target_1e0,boom_130";
#ifdef X3M_CHASE_TRANSITION_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;  // initialize()'s base instead of fl::module_base() when nonzero
unsigned fixture_fault_claim_ = ~0u; // one-shot: the n-th claim's read-back sees a foreign byte
unsigned fixture_claims_ = 0;
#endif
static_assert(S::sites[6].bytes[1] == 0x88 && S::sites[7].bytes[1] == 0x97,
              "mode stores: [eax+0x150]=ecx (script), [edi+0x150]=edx (load)");
static_assert(x3m::fl::chase_camera_fl::cockpit_view_mode == 0x150 &&
                  x3m::fl::chase_camera_fl::cockpit_connect_mode == 0x1c0 &&
                  x3m::fl::chase_camera_fl::cockpit_flags == 0x1a0 &&
                  x3m::fl::chase_camera_fl::cockpit_ref_object == 0xc &&
                  x3m::fl::chase_camera_fl::cockpit_view_object == 0x10 &&
                  x3m::fl::chase_camera_fl::cockpit_sector == 0x1fc &&
                  x3m::fl::chase_camera_fl::cockpit_sector_object == 0x54,
              "restore_read_frame's cockpit offsets are the FL-verified ones");
#else
// All spans are whole instructions, plain-copy relocations. The constructor
// has one successful exit; the updater has two. See the focused site probe.
// clang-format off
constexpr engine_patch::SiteSpec specs[] = {
    {"chase_cockpit_construct",0x0041f8d0,{0x6a,0xff,0x68,0xdd,0x07,0x53,0x00},7,0,0},
    {"chase_cockpit_complete",0x0041fe04,{0x8b,0x4c,0x24,0x14,0x5f},5,0,0},
    {"chase_cockpit_destroy",0x0041ffc0,{0x6a,0xff,0x68,0x6b,0x09,0x53,0x00},7,0,0},
    {"chase_update_begin",0x004205e0,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"chase_update_end_internal",0x004216c3,{0x5f,0x5e,0x5b,0x8b,0xe5},5,0,0},
    {"chase_update_end",0x004216d3,{0x5f,0x5e,0x5b,0x8b,0xe5},5,0,0},
    {"chase_mode_script",0x0042e742,{0x89,0x88,0x50,0x01,0x00,0x00},6,0,0},
    {"chase_mode_load",0x00419e06,{0x89,0x95,0x50,0x01,0x00,0x00},6,0,0},
    {"chase_connect",0x00422cd0,{0x83,0xec,0x08,0x83,0xf8,0x09},6,0,0}
};
// clang-format on
constexpr unsigned mandatory = 6, site_count = 9;
// X3M_CHASE_VIEW_RESTORE=1 only (docs/architecture/chase-view-restore-engine-state.md,
// section 4): the injection site at the input routine's entry (the main
// loop's input phase, outside the registry walk and every cockpit update) plus
// three program-independent VM cancel boundaries. All plain whole-instruction
// prologue copies (verify_chase_restore_sites.py proves the spans and that no
// .text branch targets their interiors).
// clang-format off
constexpr engine_patch::SiteSpec restore_specs[] = {
    {"chase_restore_inject",0x00410100,{0x55,0x8b,0xec,0x83,0xe4,0xf0},6,0,0},
    {"chase_restore_vm_clear",0x0049ea80,{0x83,0xec,0x08,0x55,0x8b,0x6c,0x24,0x10},8,0,0},
    {"chase_restore_vm_load",0x004a0880,{0x6a,0xff,0x68,0xc8,0x00,0x53,0x00},7,0,0},
    {"chase_restore_eh_adapter",0x0052f298,{0xb8,0xf4,0xe5,0x56,0x00},5,0,0}
};
// clang-format on
constexpr unsigned restore_site_count = 4, restore_inject_kind = 0, restore_eh_kind = 3;
#endif
engine_patch::Site sites[site_count], restore_sites[restore_site_count];
std::atomic<bool> enabled{false}, diagnostic{false}, restore_enabled{false};
bool initialized = false, timing = false;
std::uint64_t frequency = 0;
detail::HandlerTiming handler_timing[site_count]{};
SRWLOCK lock = SRWLOCK_INIT;
detail::State state;
struct Guard {
    Guard() { AcquireSRWLockExclusive(&lock); }
    ~Guard() { ReleaseSRWLockExclusive(&lock); }
};
using detail::Origin;
struct Snapshot {
    std::uint32_t valid = 0, mode = 0, connect = 0, ship = 0, view = 0, camera = 0, sector = 0, target = 0;
    std::int32_t boom[3]{};
#if X3M_EDITION_FL
    std::uint32_t flags = 0, sector54 = 0; // FL: +0x1a0 (valid 256), +0x54 (valid 512); camera/target/boom stay 0
#endif
};
struct Event {
    std::uint64_t sequence = 0, qpc = 0, generation = 0, update = 0;
    std::uint32_t thread = 0, kind = 0, cockpit = 0, caller = 0, requested = 0, handle = 0, valid = 0;
    Snapshot before;
    Origin origin;
    struct Geometry {
        std::uint32_t valid = 0, lock = 0;
        std::int32_t angles[3]{}, offset[3]{};
    } geometry;
    detail::Identity identity;
#if X3M_EDITION_FL
    // Kind 6: the verified dispatcher frame chain (fl_store_frame); kind 9: a finished restore ticket.
    std::uint32_t frame_ok = 0, task = 0, context = 0, script_id = 0, script_class = 0;
    std::uint32_t result = 0, cancel = 0, attempts = 0, hud = 0, mode_seen = 0, monitor = 0;
    std::uint64_t generation_from = 0;
#endif
};
struct Seen {
    std::uintptr_t cockpit = 0;
    std::uint64_t generation = 0;
    Event event;
    bool used = false;
};
Seen seen[4][detail::lifetime_capacity]{}, unknown_updates[detail::thread_capacity]{};
detail::Window<Event> window;
std::uint64_t sequence = 0, read_failures = 0, suppressed = 0;

bool bytes(std::uintptr_t base, unsigned offset, void* out, unsigned n) {
    if (!base || base > UINT32_MAX || offset > UINT32_MAX - base) return false;
    const auto at = base + offset;
    return n && n <= UINT32_MAX - at && engine_memory::read(at, out, n);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return bytes(base, offset, &out, sizeof out);
}
std::uint64_t now() {
    LARGE_INTEGER q{};
    return QueryPerformanceCounter(&q) && q.QuadPart > 0 ? std::uint64_t(q.QuadPart) : 0;
}
void snapshot(std::uintptr_t c, Snapshot& s) {
#if X3M_EDITION_FL
    // FL: the restore_read_frame field set (all verified on X3FL.exe). +0x58 camera and +0x1e0 target are verified too
    // but kept out deliberately; +0x130 boom is unverified on FL. Their valid bits stay clear.
    if (field(c, 0x150, s.mode)) s.valid |= 1;
    if (field(c, 0x1c0, s.connect)) s.valid |= 2;
    if (field(c, 0xc, s.ship)) s.valid |= 4;
    if (field(c, 0x10, s.view)) s.valid |= 8;
    if (field(c, 0x1fc, s.sector)) s.valid |= 32;
    if (field(c, 0x1a0, s.flags)) s.valid |= 256;
    if (field(c, 0x54, s.sector54)) s.valid |= 512;
#else
    if (field(c, 0x150, s.mode)) s.valid |= 1;
    if (field(c, 0x1c0, s.connect)) s.valid |= 2;
    if (field(c, 0xc, s.ship)) s.valid |= 4;
    if (field(c, 0x10, s.view)) s.valid |= 8;
    if (field(c, 0x58, s.camera)) s.valid |= 16;
    if (field(c, 0x1fc, s.sector)) s.valid |= 32;
    if (field(c, 0x1e0, s.target)) s.valid |= 64;
    if (field(c, 0x130, s.boom)) s.valid |= 128;
#endif
}
// Process roots; the CPU fixture aliases them to fixture-owned memory.
#if X3M_EDITION_FL
std::uint32_t native_registry_root = 0, cockpit_registry_root = 0; // base + slot RVA once admitted
#else
std::uint32_t native_registry_root = 0x60850c, cockpit_registry_root = 0x608504;
#endif
bool active_handle(std::uintptr_t cockpit, std::uint32_t& handle) {
    std::uint32_t registry = 0, table = 0, bucket[2]{}, link = 0;
    if (!field(cockpit_registry_root, 0, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row)) return false;
        if (row[1] == handle) return row[2] == cockpit;
        link = row[0];
    }
    return false;
}
#if !X3M_EDITION_FL // FL: no provenance reader uses it (see record())
// Runtime CODE length has not been established. Instead of inventing one,
// enforce overflow-free offsets and the same VirtualAlloc allocation as CODE
// base, then use the ordinary checked reader. This is conservative at split
// allocations and does not establish that arbitrary readable bytes are code.
bool code_address(std::uint32_t code, std::uint32_t offset, unsigned n, std::uintptr_t& at) {
    if (!code || offset > UINT32_MAX - code || !n || n > UINT32_MAX - (code + offset)) return false;
    at = code + offset;
    MEMORY_BASIC_INFORMATION a{}, b{};
    return VirtualQuery(reinterpret_cast<void*>(code), &a, sizeof a) == sizeof a &&
           VirtualQuery(reinterpret_cast<void*>(at + n - 1), &b, sizeof b) == sizeof b && a.State == MEM_COMMIT &&
           b.State == MEM_COMMIT && a.AllocationBase == b.AllocationBase;
}
#endif
#if X3M_EDITION_FL
// The script mode store's dispatcher argument frame (EBX, "Verified 2026-11-02": [EBX+4] native return base+0x0c0785,
// [EBX+0x10] command 0x30, [EBX+0xc] task; task +0x3c context; context +0 id, +8 class descriptor, [desc] class id).
// ok only for the whole chain with a dynamic (negative) object id; id and class_id are 0 otherwise.
struct StoreFrame {
    std::uint32_t task = 0, context = 0, id = 0, class_id = 0;
    bool ok = false;
};
StoreFrame fl_store_frame(std::uint32_t ebx) {
    StoreFrame f;
    std::uint32_t return_pc = 0, command = 0, desc = 0;
    f.ok = field(ebx, S::frame_return, return_pc) && return_pc == fl_native_return && fl_native_return &&
           field(ebx, S::frame_command, command) && command == S::mode_command && field(ebx, S::frame_task, f.task) &&
           f.task && !(f.task & 3) && field(f.task, 0x3c, f.context) && f.context && !(f.context & 3) &&
           field(f.context, 0, f.id) && std::int32_t(f.id) < 0 && field(f.context, 8, desc) && desc && !(desc & 3) &&
           field(desc, 0, f.class_id);
    if (!f.ok) f.id = f.class_id = 0;
    return f;
}
#endif
bool same(const Event& a, const Event& b) {
    return a.kind == b.kind && a.requested == b.requested && a.caller == b.caller && a.valid == b.valid &&
           a.handle == b.handle && !std::memcmp(&a.before, &b.before, sizeof a.before) &&
#if X3M_EDITION_FL
           a.frame_ok == b.frame_ok && a.task == b.task && a.context == b.context && a.script_id == b.script_id &&
           a.script_class == b.script_class &&
#endif
           !std::memcmp(&a.origin, &b.origin, sizeof a.origin);
}
void record(unsigned kind, std::uintptr_t cockpit, std::uint32_t thread, std::uint32_t caller = 0,
            std::uint32_t requested = 0, std::uint32_t ebp = 0) {
    if (!diagnostic.load(std::memory_order_relaxed)) return;
    Event e{};
    e.kind = kind;
    e.cockpit = std::uint32_t(cockpit);
    e.thread = thread;
    e.caller = caller;
    e.requested = requested;
    auto* life = state.find(cockpit);
    e.generation = life ? life->generation : 0;
    e.update = state.current(cockpit, thread).serial;
    // Constructor entry and partial/unknown lifetimes must not read object fields.
    if (life && life->complete) {
        snapshot(cockpit, e.before);
        if (active_handle(cockpit, e.handle))
            e.valid |= 1;
        else
            e.handle = 0;
    }
#if X3M_EDITION_FL
    // FL: the verified mode-store frame chain only (`ebp` carries EBX). AP's VM provenance (return-record pairs) and
    // destructor provenance are not ported: their dispatcher/return contracts are unverified on FL.
    if (kind == 6) {
        const StoreFrame f = fl_store_frame(ebp);
        e.frame_ok = f.ok;
        e.task = f.task;
        e.context = f.context;
        e.script_id = f.id;
        e.script_class = f.class_id;
    }
#else
    if (kind == 6) detail::provenance(ebp, e.origin, bytes, code_address);
    if (kind == 2) detail::destructor_provenance(caller, ebp, e.origin, bytes, code_address);
#endif
    if (kind == 3 && !life) {
        Seen* slot = nullptr;
        for (auto& row : unknown_updates)
            if (row.used && row.event.thread == thread) {
                slot = &row;
                break;
            }
        if (!slot)
            for (auto& row : unknown_updates)
                if (!row.used) {
                    slot = &row;
                    break;
                }
        if (!slot) {
            ++suppressed;
            return;
        }
        if (slot->used && slot->cockpit == cockpit && same(slot->event, e)) {
            ++suppressed;
            return;
        }
        *slot = {cockpit, 0, e, true};
    }
    if ((kind >= 3) && life) {
        const auto slot = unsigned(life - state.lives);
        const unsigned category = kind >= 6 ? kind - 5 : 0;
        auto& prior = seen[category][slot];
        if (prior.used && prior.cockpit == cockpit && prior.generation == e.generation && same(prior.event, e)) {
            ++suppressed;
            return;
        }
        prior = {cockpit, e.generation, e, true};
    }
#if X3M_EDITION_FL
    // FL: no geometry (+0xa8/+0x160/+0x120) or IdentityReader walk (AP-bound roots and story cells): unverified there.
    if (kind == 6 && !e.frame_ok) ++read_failures;
#else
    // Supplement only an admitted event. Animated angles and identity walks
    // neither change suppression nor add work on every updater/draw.
    if (life && life->complete) {
        if (field(cockpit, 0xa8, e.geometry.angles)) e.geometry.valid |= 1;
        if (field(cockpit, 0x160, e.geometry.offset)) e.geometry.valid |= 2;
        if (field(cockpit, 0x120, e.geometry.lock)) e.geometry.valid |= 4;
    }
    detail::IdentityReader<decltype(&bytes)> identity_reader{&bytes};
    identity_reader.capture((e.before.valid & 4) ? e.before.ship : 0, (e.origin.valid & 7) == 7 ? e.origin.context : 0,
                            e.identity);
    if (e.origin.flags & 1) ++read_failures;
#endif
    e.sequence = ++sequence;
    e.qpc = now();
    window.push(e);
}
#if X3M_EDITION_FL
// FL: a finished ticket also enters the diagnostic window as kind 9, in order with the witnesses that led to it (the
// chase_view_restore outcome row is emitted at once; the window at the next report). No engine read.
void record_outcome(const detail::RestoreOutcome& o) {
    if (!diagnostic.load(std::memory_order_relaxed)) return;
    Event e{};
    e.kind = 9;
    e.thread = GetCurrentThreadId();
    e.generation = o.generation_to;
    e.generation_from = o.generation_from;
    e.result = o.result;
    e.cancel = o.cancel;
    e.attempts = o.attempts;
    e.hud = o.hud_refresh;
    e.mode_seen = o.mode_seen;
    e.monitor = o.monitor;
    e.sequence = ++sequence;
    e.qpc = now();
    window.push(e);
}
#endif
// ---- one-use rear-chase restore ticket (X3M_CHASE_VIEW_RESTORE=1) ----
// Lock-protected state; the epoch is lock-free so the EH adapter can cancel
// without acquiring anything. The injection prefilter word is read racily by
// the site-I stub (nonzero only while a ticket is due: bit 0 this ticket,
// bit 1 the InjectConsumer's, inject_want); the handler
// re-validates everything under the lock from fresh reads.
detail::RestoreState restore;
std::atomic<std::uint32_t> restore_epoch{1};
std::atomic<std::uint32_t> restore_inject_filter{0};
std::atomic<std::uint64_t> restore_eh_bumps{0};
detail::HandlerTiming restore_timing[restore_site_count]{}, restore_frame_timing{};
std::uint64_t restore_report_frame = 0; // the last reported frame, for the outcome rows
// Engine roots the CPU fixture aliases to fixture-owned memory. The call
// target is the engine's by-name script call wrapper (cdecl, caller pops 0x1c):
// (callback slot, cookie, lookup key, name, return kind, argc, arg...). The key
// resolves through 0x0049f1e0 (negative: the dynamic object by ~id, as the
// interpreter's own DYNCALL does; non-negative: the static record 0x004b06f0
// keys, as the script's class-static calls do), the method by a content strcmp
// over the object's class chain (0x004b0740); AL 0 when object or method is
// missing. The wrapper reads argc dwords after argc (0x0049f5d0..0x0049f5fc).
#if X3M_EDITION_FL
std::uint32_t game_root = 0;               // base + S::game_root_rva once admitted
std::uintptr_t script_call_address = 0;    // base + S::script_call_rva once admitted
#else
std::uint32_t game_root = detail::restore_game_root;
std::uintptr_t script_call_address = detail::restore_script_call;
#endif
using ScriptCall0 = std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t, std::uint32_t, const char*, std::uint32_t,
                                            std::uint32_t);
using ScriptCall1 = std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t, std::uint32_t, const char*, std::uint32_t,
                                            std::uint32_t, std::uint32_t);
// Prefilter bits: the chase ticket and the second consumer (inject_attach).
constexpr std::uint32_t filter_chase = 1, filter_consumer = 2;
std::atomic<const InjectConsumer*> inject_consumer{nullptr};
bool inject_live = false, inject_chase = false; // the four sites are claimed; the chase restore holds them
void restore_filter_publish() {
    if (restore.wants_injection())
        restore_inject_filter.fetch_or(filter_chase, std::memory_order_release);
    else
        restore_inject_filter.fetch_and(~filter_chase, std::memory_order_release);
}
// Engine walks the ticket asks for only on the frames that matter (checked
// reads only; nothing is retained).
struct EngineProbe {
    std::uintptr_t cockpit = 0;
    bool active_control(std::uint32_t& handle) {
        return active_handle(cockpit, handle) && handle != 0;
    }
    // Registry membership of the ship by its native id (the resolver SA_GetEventObject 0x004607f4 uses),
    // deferred-delete flag clear, then the engine's ship-script id +0x94.
    bool ship_live(std::uint32_t ref, std::uint32_t& ship_id) {
        detail::IdentityReader<decltype(&bytes)> r{&bytes};
        r.registry_root = native_registry_root;
        std::uint32_t native_id = 0, registry = 0, table = 0, live = 0, flags = 0;
        if (!ref || (ref & 3) || !field(ref, 8, native_id) || !field(native_registry_root, 0, registry) ||
            !r.pointer(registry) || !field(registry, 0x14, table) || !r.hash(table, native_id, live) || live != ref)
            return false;
        if (!field(ref, 0x40, flags) || (flags & detail::restore_ship_delete_flag)) return false;
        return field(ref, 0x94, ship_id);
    }
    bool controller(std::uint32_t& id) {
        std::uint32_t game = 0;
        return field(game_root, 0, game) && game && !(game & 3) && field(game, 0x418, id);
    }
};
void restore_emit_sample(const detail::RestoreArmSample& s) {
    log("chase_view_restore_arm_refused reason=%u cockpit=0x%08lx generation=%llu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx handle=%lu ship_live=%u ship_id=0x%08lx armed=%u",
        s.reason, static_cast<unsigned long>(s.cockpit), s.generation, static_cast<unsigned long>(s.mode),
        static_cast<unsigned long>(s.connect), static_cast<unsigned long>(s.ship), static_cast<unsigned long>(s.view),
        static_cast<unsigned long>(s.handle), unsigned(s.ship_live), static_cast<unsigned long>(s.ship_id),
        unsigned(s.armed));
}
void restore_emit_outcome(const detail::RestoreOutcome& o) {
    log("chase_view_restore result=%s cancel=%s attempts=%u monitor=0x%08lx monitor_class=0x%lx hud_refresh=%s manager=0x%08lx manager_class=0x%lx manager_scan=%s mode_seen=%lu generation_from=%llu generation_to=%llu ship_id_from=0x%08lx ship_id_to=0x%08lx sector_from=0x%08lx sector_to=0x%08lx sector54_from=0x%08lx sector54_to=0x%08lx wait_updates=%lu frame=%llu soft_unreadable=%lu soft_mode_zero=%lu soft_connect_flags_view=%lu soft_no_handle=%lu soft_ship_dead=%lu skip_caller=%lu first_skip_caller=0x%08lx first_soft=%lu,0x%08lx,0x%08lx,%lu,%lu",
        detail::restore_result_name(o.result), detail::restore_cancel_name(o.cancel), o.attempts,
        static_cast<unsigned long>(o.monitor), static_cast<unsigned long>(o.monitor_class),
        detail::restore_hud_name(o.hud_refresh), static_cast<unsigned long>(o.manager),
        static_cast<unsigned long>(o.manager_class), detail::restore_scan_name(o.manager_scan),
        static_cast<unsigned long>(o.mode_seen), o.generation_from, o.generation_to,
        static_cast<unsigned long>(o.ship_id_from), static_cast<unsigned long>(o.ship_id_to),
        static_cast<unsigned long>(o.sector_from), static_cast<unsigned long>(o.sector_to),
        static_cast<unsigned long>(o.sector54_from), static_cast<unsigned long>(o.sector54_to),
        static_cast<unsigned long>(o.wait_updates), restore_report_frame,
        static_cast<unsigned long>(o.soft[detail::soft_unreadable]),
        static_cast<unsigned long>(o.soft[detail::soft_mode_zero]),
        static_cast<unsigned long>(o.soft[detail::soft_connect_flags_view]),
        static_cast<unsigned long>(o.soft[detail::soft_no_handle]),
        static_cast<unsigned long>(o.soft[detail::soft_ship_dead]), static_cast<unsigned long>(o.skip_caller),
        static_cast<unsigned long>(o.first_skip_caller), static_cast<unsigned long>(o.first_soft),
        static_cast<unsigned long>(o.first_soft_sector), static_cast<unsigned long>(o.first_soft_sector54),
        static_cast<unsigned long>(o.first_soft_mode), static_cast<unsigned long>(o.first_soft_handle));
}
struct RestoreRows {
    detail::RestoreArmSample sample;
    detail::RestoreOutcome outcome;
    void take() { // keeps a row an earlier take of the same handler already holds
        if (const auto s = restore.take_arm_sample(); s.wanted) sample = s;
#if X3M_EDITION_FL
        if (const auto o = restore.take_outcome(); o.wanted) {
            outcome = o;
            record_outcome(o);
        }
#else
        if (const auto o = restore.take_outcome(); o.wanted) outcome = o;
#endif
    }
    void emit() const { // never under the Guard: log() takes capture's mutex, which present() holds around report()
        if (sample.wanted) restore_emit_sample(sample);
        if (outcome.wanted) restore_emit_outcome(outcome);
    }
};
// Fresh frame of a cockpit for the injection re-check: cockpit fields through the checked reader, lifetime
// facts from the witness table (unseen or partial lifetimes read as generation 0, which the core refuses).
void restore_read_frame(std::uintptr_t cockpit, detail::RestoreFrame& f) {
    f = {};
    f.cockpit = std::uint32_t(cockpit);
    if (const auto* life = state.find(cockpit)) {
        f.generation = life->complete ? life->generation : 0;
        f.constructor_caller = life->constructor_caller;
    }
    f.readable = cockpit && !(cockpit & 3) && field(cockpit, 0x150, f.mode) && field(cockpit, 0x1c0, f.connect) &&
                 field(cockpit, 0x1a0, f.flags) && field(cockpit, 0xc, f.ref) && field(cockpit, 0x10, f.view) &&
                 field(cockpit, 0x1fc, f.sector);
    if (f.readable && !field(cockpit, 0x54, f.sector54)) f.sector54 = 0; // log-only
}
// Update-begin (kind 3): the expiry clock, every cockpit update.
void restore_on_update() {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    restore.on_update();
    restore_filter_publish();
}
void restore_on_destroy(std::uintptr_t cockpit, std::uint32_t caller) {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    const auto* life = state.find(cockpit);
    restore.on_destroy(std::uint32_t(cockpit), life ? life->generation : 0, caller);
    restore_filter_publish();
}
void restore_on_complete(std::uintptr_t cockpit) {
    if (!restore_enabled.load(std::memory_order_relaxed) || !restore.ticket_open()) return;
    const auto* life = state.find(cockpit);
    restore.on_complete(life ? life->constructor_caller : 0);
    restore_filter_publish();
}
// Kind 6 (0x0042e742: EAX cockpit, ECX mode, EBP the dispatcher frame): the
// script object making the store is read from the engine's VM task frame,
// the same chain provenance() validates: [ebp+4] == 0x4a3909 (the
// interpreter's native-call site), [ebp+0xc] task, [task+0x3c] current
// context, [context] object id, [context+8] class descriptor, [desc] class id.
// Only a dynamic object id (negative: the interpreter's own DYNCALL resolves it through the object table)
// is accepted for the monitor. Its caller frame (the monitor manager) comes from the task's return
// records (detail::restore_caller_context: one bounded checked read of at most 128 cells), read only
// for a store the ticket can capture (pending/due, a fresh cockpit from the alloc command).
void restore_on_mode_store(std::uint32_t* regs, std::uint32_t thread) {
    if (!restore_enabled.load(std::memory_order_relaxed) || restore.phase < detail::phase_pending) return;
    const std::uintptr_t cockpit = regs[7];
#if X3M_EDITION_FL
    // FL: the dispatcher's argument frame is EBX (entry ESP - 4), not EBP; the native-call return is base-relative
    // and the command word must be the mode case 0x30 (fl_store_frame, shared with the kind-6 event row).
    const std::uint32_t requested = regs[6];
    const auto* life = state.find(cockpit);
    const StoreFrame frame = fl_store_frame(regs[4]);
    const std::uint32_t task = frame.task, context = frame.context, id = frame.id, class_id = frame.class_id;
#else
    const std::uint32_t requested = regs[6], ebp = regs[2];
    const auto* life = state.find(cockpit);
    std::uint32_t return_pc = 0, task = 0, context = 0, id = 0, desc = 0, class_id = 0;
    if (!(field(ebp, 4, return_pc) && return_pc == 0x4a3909 && field(ebp, 0xc, task) && task && !(task & 3) &&
          field(task, 0x3c, context) && context && !(context & 3) && field(context, 0, id) &&
          std::int32_t(id) < 0 && field(context, 8, desc) && desc && !(desc & 3) && field(desc, 0, class_id))) {
        id = 0;
        class_id = 0;
    }
#endif
    detail::RestoreCaller caller;
    if (id && restore.phase != detail::phase_calling && life && life->constructor_caller == detail::restore_alloc_caller)
        caller = detail::restore_caller_context(task, context, bytes);
    restore.on_mode_store(std::uint32_t(cockpit), life && life->complete ? life->generation : 0, requested, thread,
                          life ? life->constructor_caller : 0, id, class_id, caller.id, caller.class_id, caller.scan);
    restore_filter_publish();
}
// Deserialization kind7 (the existing mode-load observer, 0x419e06): a cancel, no new site.
void restore_on_load() {
    if (!restore_enabled.load(std::memory_order_relaxed)) return;
    restore.on_load();
    restore_filter_publish();
}
void restore_on_vm() {
    restore_epoch.fetch_add(1, std::memory_order_acq_rel);
    restore.on_vm();
    restore_filter_publish();
}
// The engine's own by-name script calls: `SelectMode(258)` on the captured
// monitor object (the method the script's view keys end in), then
// `UpdateVisibility()` on the captured manager (what the keys run next). Each
// call runs under the engine's incoming x87/MXCSR/LastError image (the callee
// is engine code) and the handler's clean state is re-established afterwards;
// the caller's CpuState brings the engine image back at the stub boundary.
// The wrapper's AL. `target` is 0x0049f570 for the chase ticket, the consumer's own wrapper otherwise (both take
// the same arguments).
__attribute__((noinline)) unsigned script_call(std::uintptr_t target, const detail::RestoreEngineCall& c,
                                               x3m::CpuState& cpu) {
    cpu.restore();
    // Argument 0 is the completion-callback slot (task+0x30; 0 = none): Run147 passed the monitor id
    // there and the completion code 0x004a2260 -> 0x004ab170 indexed the VM callback table with -531
    // (only a signed `cmp ecx,0x20; jge` bounds it) and called through heap data. Argument 2 is the
    // lookup key, argument 4 the return kind, argument 5 argc (argc 0: six dwords).
    const std::uint32_t r =
        (c.argc ? reinterpret_cast<ScriptCall1>(target)(c.callback, c.cookie, c.key, c.name, c.return_kind, c.argc,
                                                        c.value)
                : reinterpret_cast<ScriptCall0>(target)(c.callback, c.cookie, c.key, c.name, c.return_kind, 0)) &
        0xff;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    return unsigned(r);
}
__attribute__((noinline)) bool restore_engine_call(const detail::RestoreEngineCall& c, x3m::CpuState& cpu) {
    return script_call(script_call_address, c, cpu) != 0;
}
// Site I (0x00410100 entry, once per main-loop iteration): consume a due
// ticket. No lock, RAII object or borrowed engine pointer is held across the
// engine call (a script error unwinds through this frame; the EH adapter's
// epoch bump is the cancel).
__attribute__((noinline)) void restore_inject(std::uint32_t* regs, x3m::CpuState& cpu) {
    if (!restore_enabled.load(std::memory_order_acquire)) return;
    const auto start = timing ? now() : 0;
    const auto thread = GetCurrentThreadId();
    const auto esp = regs[3] + 4;
    detail::RestoreCall call;
    RestoreRows rows;
    {
        Guard guard;
        if (restore.wants_injection()) {
            detail::RestoreFrame fresh;
            std::uint32_t controlled = 0, game = 0, pause_bits = 0;
            restore_read_frame(restore.bound_cockpit, fresh);
#if X3M_EDITION_FL
            (void)esp;
            controlled = regs[6]; // FL input worker: ECX = the controlled object (spilled at 0x0cfd0f)
#else
            field(esp, 4, controlled);
#endif
            if (field(game_root, 0, game) && game) field(game, 0x4a0, pause_bits);
            EngineProbe probe{restore.bound_cockpit};
            call = restore.on_inject_entry(thread, fresh, controlled, controlled ? pause_bits : 1, probe,
                                           restore_epoch.load(std::memory_order_acquire));
        }
        restore_filter_publish();
        rows.take();
    }
    if (call.wanted) {
        const bool ok = restore_engine_call(detail::restore_select_call(call), cpu);
        detail::RestoreRefresh refresh;
        {
            Guard guard;
            refresh = restore.on_call_result(ok, restore_epoch.load(std::memory_order_acquire));
            restore_filter_publish();
            rows.take();
        }
        if (refresh.wanted) {
            const bool shown = restore_engine_call(detail::restore_refresh_call(refresh), cpu);
            Guard guard;
            restore.on_refresh_result(shown);
            rows.take();
        }
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_timing[restore_inject_kind].add(start, end);
    }
}
// The second consumer's queued call (InjectConsumer): its poll decides under its own lock; nothing of this module is
// held across the engine call, so a script error unwinding through this frame leaves no state behind (the consumer
// sees its call unreported at the next poll and the EH adapter's unwind()).
__attribute__((noinline)) void consumer_inject(std::uint32_t* regs, x3m::CpuState& cpu) {
    const InjectConsumer* consumer = inject_consumer.load(std::memory_order_acquire);
    if (!consumer) return;
    std::uint32_t controlled = 0;
#if X3M_EDITION_FL
    // FL input worker: ECX = the controlled object (as restore_inject); the engine's named dispatchers take selector 32
    // as "no completion callback" (detail::restore_callback_none; 0 would index callback slot 0).
    controlled = regs[6];
    constexpr std::uint32_t callback = detail::restore_callback_none;
#else
    if (!field(regs[3] + 4, 4, controlled)) controlled = 0;
    constexpr std::uint32_t callback = 0;
#endif
    InjectCall call;
    if (!consumer->poll(controlled, call) || !call.target || !call.name) return;
    const detail::RestoreEngineCall engine{callback, 0, call.key, call.name, call.return_kind, call.argc, call.value};
    consumer->result(script_call(call.target, engine, cpu));
}
__attribute__((noinline)) void inject_handle(std::uint32_t* regs, x3m::CpuState& cpu) {
    const std::uint32_t filter = restore_inject_filter.load(std::memory_order_acquire);
    if (filter & filter_chase) restore_inject(regs, cpu);
    if (filter & filter_consumer) consumer_inject(regs, cpu);
}
void restore_handle(unsigned kind, std::uint32_t* regs) {
    (void)regs;
    if (kind >= restore_site_count || kind == restore_inject_kind) return;
    if (const InjectConsumer* consumer = inject_consumer.load(std::memory_order_acquire)) {
        if (kind == restore_eh_kind)
            consumer->unwind();
        else
            consumer->vm(kind);
    }
    if (!restore_enabled.load(std::memory_order_acquire)) return;
    if (kind == restore_eh_kind) {
        // Lock-free: native unwinding may pass through this adapter while
        // another observer holds its lock on the same or another thread.
        restore_epoch.fetch_add(1, std::memory_order_acq_rel);
        restore_eh_bumps.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto start = timing ? now() : 0;
    RestoreRows rows;
    {
        Guard guard;
        restore_on_vm();
        rows.take();
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_timing[kind].add(start, end);
    }
}
void handle(unsigned kind, std::uint32_t* regs) {
    if (kind >= site_count || !enabled.load(std::memory_order_acquire) ||
        (kind >= mandatory && !diagnostic.load(std::memory_order_relaxed) &&
         !restore_enabled.load(std::memory_order_relaxed)))
        return;
    const auto start = timing ? now() : 0;
    const auto thread = GetCurrentThreadId();
    const auto esp = regs[3] + 4; // PUSHFD precedes PUSHAD's saved ESP.
    std::uint32_t cockpit = 0, caller = 0;
    RestoreRows rows;
    {
        Guard guard;
#if X3M_EDITION_FL
        // FL register ABI (chase_transition_sites_fl.h): constructor, wrapper and updater take the cockpit in ECX
        // (regs[6]); the mode store has EAX = cockpit, ECX = mode; the load store EDI = cockpit, EDX = mode. Return
        // addresses are normalized to preferred-base VAs for the core. record() (telemetry only) reads the FL-verified
        // fields; the mode-store rows carry the store's preferred-base VA as their caller, as AP carries its site VA.
        switch (kind) {
        case 0:
            chase_lead::native_timing_invalidate(0, thread);
            cockpit = regs[6];
            if (field(esp, 0, caller)) caller = S::preferred(caller, fl_base);
            chase_lead::native_timing_invalidate(cockpit, thread);
            state.construct(cockpit, thread, caller);
            record(0, cockpit, thread, caller);
            break;
        case 1:
            cockpit = regs[7];
            if (state.complete(cockpit, thread)) {
                record(1, cockpit, thread);
                restore_on_complete(cockpit);
            }
            break;
        case 2:
            chase_lead::native_timing_invalidate(0, thread);
            cockpit = regs[6];
            if (cockpit) {
                if (field(esp, 0, caller)) caller = S::preferred(caller, fl_base);
                record(2, cockpit, thread, caller);
                restore_on_destroy(cockpit, caller);
                chase_lead::native_timing_invalidate(cockpit, thread);
                state.destroy(cockpit);
            }
            break;
        case 3:
            chase_lead::native_timing_invalidate(0, thread);
            cockpit = regs[6];
            state.begin(cockpit, esp, thread);
            record(3, cockpit, thread);
            restore_on_update();
            break;
        case 4:
        case 5:
            chase_lead::native_timing_invalidate(0, thread);
            state.end(regs[2] + 4, thread);
            break;
        case 6:
            record(6, regs[7], thread, S::preferred_base + S::sites[6].rva, regs[6], regs[4]);
            restore_on_mode_store(regs, thread);
            break;
        case 7:
            record(7, regs[0], thread, S::preferred_base + S::sites[7].rva, regs[5]);
            restore_on_load();
            break;
        }
#else
        switch (kind) {
        case 0:
            // Reentry retires this thread even if the constructor argument is unreadable.
            chase_lead::native_timing_invalidate(0, thread);
            if (field(esp, 4, cockpit)) {
                field(esp, 0, caller);
                chase_lead::native_timing_invalidate(cockpit, thread);
                state.construct(cockpit, thread, caller);
                record(0, cockpit, thread, caller);
            }
            break;
        case 1:
            cockpit = regs[7];
            if (state.complete(cockpit, thread)) {
                record(1, cockpit, thread);
                restore_on_complete(cockpit);
            }
            break;
        case 2:
            chase_lead::native_timing_invalidate(0, thread);
            if (field(esp, 4, cockpit)) {
                field(esp, 0, caller);
                record(2, cockpit, thread, caller, 0, regs[2]);
                restore_on_destroy(cockpit, caller);
                chase_lead::native_timing_invalidate(cockpit, thread);
                state.destroy(cockpit);
            }
            break;
        case 3:
            // Even a failed argument read must revoke this thread's previous update.
            chase_lead::native_timing_invalidate(0, thread);
            field(esp, 4, cockpit);
            state.begin(cockpit, esp, thread);
            record(3, cockpit, thread);
            restore_on_update();
            break;
        case 4:
        case 5:
            chase_lead::native_timing_invalidate(0, thread);
            state.end(regs[2] + 4, thread);
            break;
        case 6:
            record(6, regs[7], thread, 0x42e742, regs[6], regs[2]);
            restore_on_mode_store(regs, thread);
            break;
        case 7:
            record(7, regs[2], thread, 0x419e06, regs[5]);
            restore_on_load();
            break;
        case 8:
            field(esp, 0, caller);
            record(8, regs[1], thread, caller, regs[7]);
            break;
        }
#endif
        rows.take();
    }
    rows.emit(); // never under the Guard
    // Include the handler lock acquisition/release and checked reads. The
    // second lock only aggregates this sample and lies outside its interval.
    if (timing) {
        const auto end = now();
        Guard guard;
        handler_timing[kind].add(start, end);
    }
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_transition_enter(unsigned kind,
                                                                                            std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::handle(kind, regs);
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_restore_enter(unsigned kind,
                                                                                         std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::restore_handle(kind, regs);
}
// Site I: a plain CpuState (no destructor) so a script error unwinding from
// the engine call through this frame runs nothing of ours; the same capture
// and restore inventory as the other callbacks.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_inject_enter(unsigned kind,
                                                                                        std::uint32_t* regs) {
    (void)kind;
    x3m::CpuState cpu;
    cpu.capture();
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_transition::inject_handle(regs, cpu);
    cpu.restore();
}
namespace x3m::chase_transition {
namespace {
// Register-saving stub. With `filter` a one-word prefilter runs under the
// saved flags before any XMM/x87 capture: `cmp dword [filter], 0; je skip`,
// so an idle ticket costs the site one memory compare per call.
void* emit_stub(unsigned kind, const void* callback, void*** next_out, const std::atomic<std::uint32_t>* filter) {
    engine_patch::Emitter e(256);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x9c); // pushfd
    const unsigned char* skip_at = nullptr;
    if (filter) {
        constexpr unsigned prefilter = 7 + 6, full_path = 116;
        skip_at = static_cast<const unsigned char*>(e.here()) + prefilter + full_path;
        e.byte(0x83);
        e.byte(0x3d);
        e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(filter)));
        e.byte(0);
        e.byte(0x0f);
        e.byte(0x84);
        e.rel32(skip_at); // cmp dword [filter],0; je skip
        if (e.here() != skip_at - full_path) return nullptr;
    }
    e.byte(0x60);
    e.byte(0xfc); // pushad; provide the C ABI's clear DF (DF itself is in the saved flags)
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
    e.byte(0x68);
    e.dword(kind);
    e.byte(0xe8);
    e.rel32(callback);
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(8);
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
    if (filter && e.here() != skip_at) return nullptr;
    e.byte(0x9d); // popfd
    const auto next = (reinterpret_cast<std::uintptr_t>(e.here()) + 6 + 3) & ~std::uintptr_t(3);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(next));
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < next) e.byte(0xcc);
    *next_out = reinterpret_cast<void**>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
void* emit(unsigned kind, void*** next_out) {
    return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_transition_enter), next_out, nullptr);
}
void* emit_restore(unsigned kind, void*** next_out) {
    if (kind == restore_inject_kind)
        return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_inject_enter), next_out,
                         &restore_inject_filter);
    return emit_stub(kind, reinterpret_cast<const void*>(&x3m_chase_restore_enter), next_out, nullptr);
}
bool restore_group(unsigned first, unsigned end) {
    bool okay = true;
    for (unsigned i = end; i-- > first;)
        if (sites[i].patched_in && !engine_patch::restore(sites[i])) okay = false;
    return okay;
}
#if X3M_EDITION_FL
// The FL read-back after a successful claim (the chase_camera/engine_effects FL convention): the site must hold this
// module's jump before a stub is chained. Otherwise, inside the install window, the original first five bytes go back
// over whatever is there; the site is released only when the whole span then reads back as the original (else it
// stays registered and inert: nothing was chained, so a jump that is still ours reaches the plain tail).
bool fl_confirm(engine_patch::Site& s) {
#ifdef X3M_CHASE_TRANSITION_FL_FIXTURE
    if (fixture_claims_++ == fixture_fault_claim_) *reinterpret_cast<volatile unsigned char*>(s.spec.address + 1) ^= 0xff;
#endif
    unsigned char now[5]{};
    if (engine_patch::read_code(s.spec.address, now, 5) && !std::memcmp(now, s.patched, 5)) return true;
    // The restore is a plain write when the span crosses a qword: only while the install window is still open (no
    // engine thread can be inside the span). claim() just checked it on this thread, but re-check rather than rely on
    // that; a closed window leaves the site registered and inert (readback_rollback_failed).
    auto* code = reinterpret_cast<void*>(s.spec.address);
    DWORD writable = 0, unused = 0;
    if (engine_patch::install_window_open() && VirtualProtect(code, s.spec.length, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(s.spec.address, s.original, 5);
        FlushInstructionCache(GetCurrentProcess(), code, s.spec.length);
        VirtualProtect(code, s.spec.length, s.protection, &unused);
    }
    unsigned char back[engine_patch::max_prologue]{};
    if (engine_patch::read_code(s.spec.address, back, s.spec.length) && !std::memcmp(back, s.original, s.spec.length)) {
        s.patched_in = false;
        s.status = "readback_restored";
    } else
        s.status = "readback_rollback_failed";
    return false;
}
#endif
bool install_group(unsigned first, unsigned end) {
    for (unsigned i = first; i < end; ++i) {
        if (!engine_patch::claim(sites[i], specs[i])) {
            restore_group(first, end);
            return false;
        }
#if X3M_EDITION_FL
        if (!fl_confirm(sites[i])) {
            restore_group(first, end);
            return false;
        }
#endif
        void** next = nullptr;
        void* stub = emit(i, &next);
        if (!stub || !next || !engine_patch::store_pointer(next, *sites[i].entry) ||
            !engine_patch::push_front(sites[i], stub)) {
            restore_group(first, end);
            return false;
        }
    }
    return true;
}
// Partial-install rollback: a refused/failed site restores every earlier one.
bool restore_sites_restore() {
    bool okay = true;
    for (unsigned i = restore_site_count; i-- > 0;)
        if (restore_sites[i].patched_in && !engine_patch::restore(restore_sites[i])) okay = false;
    return okay;
}
bool restore_sites_install(const engine_patch::SiteSpec* table) {
    for (unsigned i = 0; i < restore_site_count; ++i) {
        if (!engine_patch::claim(restore_sites[i], table[i])) {
            restore_sites_restore();
            return false;
        }
#if X3M_EDITION_FL
        if (!fl_confirm(restore_sites[i])) {
            restore_sites_restore();
            return false;
        }
#endif
        void** next = nullptr;
        void* stub = emit_restore(i, &next);
        if (!stub || !next || !engine_patch::store_pointer(next, *restore_sites[i].entry) ||
            !engine_patch::push_front(restore_sites[i], stub)) {
            restore_sites_restore();
            return false;
        }
    }
    return true;
}
// The four sites once for every user (the chase restore, the InjectConsumer): the first claim inside the install
// window, every later user attaches to the live sites. *status: ok, late_claim or the refusing site's status.
bool inject_acquire(const char** status) {
    if (inject_live) {
        *status = "ok";
        return true;
    }
    if (!engine_patch::install_window_open()) {
        *status = "late_claim";
        return false;
    }
    if (!restore_sites_install(restore_specs)) {
        *status = "claim_failed";
        for (const auto& s : restore_sites) // rolled-back sites read "restored": the refusing one has its own status
            if (s.status && std::strcmp(s.status, "unclaimed") != 0 && std::strcmp(s.status, "restored") != 0 &&
                std::strcmp(s.status, "active") != 0) {
                *status = s.status;
                break;
            }
        return false;
    }
    inject_live = true;
    *status = "ok";
    return true;
}
bool inject_release_if_unused() {
    if (!inject_live || inject_chase || inject_consumer.load(std::memory_order_acquire)) return true;
    if (!restore_sites_restore()) return false;
    inject_live = false;
    return true;
}
#if X3M_EDITION_FL
std::uintptr_t fl_module_base() {
#ifdef X3M_CHASE_TRANSITION_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
// Every identity window at base + RVA, before any write: nullptr when all are exactly the engine's (then the twelve
// specs, the roots, the call target and the native return are bound to this base), else the refusal.
const char* fl_admit(std::uintptr_t base) {
    auto read = [](std::uintptr_t at, void* out, unsigned n) {
        return engine_patch::read_code(at, static_cast<unsigned char*>(out), n);
    };
    if (const char* refusal = S::plan(base, read, &fl_failed_window)) return refusal;
    auto bind = [base](const S::SiteRow& row, engine_patch::SiteSpec& spec) {
        spec = engine_patch::SiteSpec{};
        spec.name = row.name;
        spec.address = base + row.rva;
        S::expected(row, base, spec.expected);
        spec.length = row.length;
    };
    for (unsigned i = 0; i < site_count; ++i) bind(S::sites[i], specs[i]);
    for (unsigned i = 0; i < restore_site_count; ++i) bind(S::restore_sites[i], restore_specs[i]);
    native_registry_root = std::uint32_t(base + S::sim_registry_rva);
    cockpit_registry_root = std::uint32_t(base + S::cockpit_registry_rva);
    game_root = std::uint32_t(base + S::game_root_rva);
    script_call_address = base + S::script_call_rva;
    fl_native_return = std::uint32_t(base + S::native_return_rva);
    fl_base = base;
    return nullptr;
}
#endif
bool restore_wanted() {
    wchar_t setting[8]{};
    return x3m::config::get(L"X3M_CHASE_VIEW_RESTORE", setting, 8) == 1 && setting[0] == L'1';
}
// X3M_CHASE_VIEW_RESTORE_RETRIES: 0..5 (default 3); anything else refuses the install (fail closed).
bool restore_retries(unsigned& out) {
    wchar_t setting[8]{};
    const DWORD n = x3m::config::get(L"X3M_CHASE_VIEW_RESTORE_RETRIES", setting, 8);
    if (n == 0) {
        out = detail::restore_default_retries;
        return true;
    }
    if (n != 1 || setting[0] < L'0' || setting[0] > L'0' + detail::restore_retry_cap) return false;
    out = unsigned(setting[0] - L'0');
    return true;
}
}
#if X3M_EDITION_FL
// FL: the same three groups as AP (mandatory witnesses with the chase camera; the two mode-store observers and the
// four restore sites only with X3M_CHASE_VIEW_RESTORE=1), bound to the admitted base after every identity window
// matched, each group both-or-none. With telemetry the event window is on (FL-verified reads only; diagnostics_off names
// what is not ported). Rows carry edition=fl.
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    const bool wanted = chase_camera::wanted() && chase_camera::installed();
    const std::uintptr_t base = wanted ? fl_module_base() : 0;
    if (!wanted)
        fl_refusal = "camera_not_installed";
    else if (!base)
        fl_refusal = "executable_mismatch";
    else if (!engine_patch::install_window_open())
        fl_refusal = "late_claim";
    else if (const char* refusal = fl_admit(base))
        fl_refusal = refusal;
    else if (!install_group(0, mandatory))
        fl_refusal = "claim_failed";
    else
        fl_refusal = "active";
    const bool okay = !std::strcmp(fl_refusal, "active");
    LARGE_INTEGER f{};
    timing = okay && telemetry::enabled() && QueryPerformanceFrequency(&f) && f.QuadPart > 0;
    frequency = timing ? std::uint64_t(f.QuadPart) : 0;
    enabled.store(okay, std::memory_order_release);
    const bool restore_requested = restore_wanted();
    unsigned retries = detail::restore_default_retries;
    const bool retries_ok = restore_retries(retries);
    // Both or none: the two observers serve only the restore here, so a refused restore group takes them back too.
    const bool observers_claimed = okay && restore_requested && retries_ok && install_group(mandatory, site_count);
    const char* inject_status = "unclaimed";
    const bool restore_okay = observers_claimed && inject_acquire(&inject_status);
    if (observers_claimed && !restore_okay) restore_group(mandatory, site_count);
    const bool observers = restore_okay;
    inject_chase = restore_okay;
    // The event window, as on AP with telemetry; the mode-store kinds 6/7 appear only when the restore claimed them.
    const bool diag = okay && telemetry::enabled();
    diagnostic.store(diag, std::memory_order_release);
    log("chase_transition installed=%u status=%s edition=fl base=0x%08lx window=%s window_closed_by=%s diagnostics=%u diagnostics_reason=%s diagnostics_off=%s observers=%u mandatory_sites=6 observer_sites=2 lifetime_capacity=64 thread_capacity=8 timing=%u",
        unsigned(okay), fl_refusal, static_cast<unsigned long>(base),
        fl_failed_window < S::window_count ? S::window_names[fl_failed_window] : "-",
        engine_patch::install_window_reason() ? engine_patch::install_window_reason() : "-", unsigned(diag),
        diag ? "fl_verified_reads" : okay ? "telemetry_off" : "not_installed", fl_diagnostics_off, unsigned(observers),
        unsigned(timing));
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].patched_in || (okay && (i < mandatory || restore_requested)))
            log("chase_transition_site index=%u site=0x%08lx rva=0x%06lx patched=%u atomic_write=%u status=%s edition=fl",
                i, static_cast<unsigned long>(specs[i].address), static_cast<unsigned long>(S::sites[i].rva),
                unsigned(sites[i].patched_in), unsigned(sites[i].atomic_write), sites[i].status);
    {
        Guard guard;
        restore = {};
        restore.retries = retries;
        restore_filter_publish();
    }
    restore_enabled.store(restore_okay, std::memory_order_release);
    if (restore_requested || restore_okay) {
        const char* reason = !okay                ? fl_refusal
                             : !retries_ok        ? "invalid_retries"
                             : !observers_claimed ? "observers_failed"
                             : !restore_okay      ? inject_status
                                                  : "ok";
        log("chase_view_restore installed=%u requested=%u status=%s edition=fl path=engine sites=1 cancel_sites=%u inject_site=0x%08lx inject_rva=0x%06lx call=0x%08lx call_rva=0x%06lx callback=%u method=SelectMode mode=%u refresh=UpdateVisibility refresh_scan_cells=%u observers=%u retries=%u retries_valid=%u expiry_updates=%u due_frames=%u retry_spacing=%u",
            unsigned(restore_okay), unsigned(restore_requested), reason, restore_site_count - 1,
            static_cast<unsigned long>(restore_specs[0].address), static_cast<unsigned long>(S::restore_sites[0].rva),
            static_cast<unsigned long>(script_call_address), static_cast<unsigned long>(S::script_call_rva),
            detail::restore_callback_none, detail::restore_rear_mode, detail::restore_caller_scan_cells,
            unsigned(observers), retries, unsigned(retries_ok), detail::restore_pending_update_expiry,
            detail::restore_due_frames, detail::restore_retry_spacing);
        for (unsigned i = 0; i < restore_site_count; ++i)
            if (restore_sites[i].patched_in || okay)
                log("chase_view_restore_site index=%u site=0x%08lx rva=0x%06lx patched=%u atomic_write=%u status=%s edition=fl",
                    i, static_cast<unsigned long>(restore_specs[i].address),
                    static_cast<unsigned long>(S::restore_sites[i].rva), unsigned(restore_sites[i].patched_in),
                    unsigned(restore_sites[i].atomic_write), restore_sites[i].status);
    }
    SetLastError(error);
    return okay;
}
#else
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    const bool wanted = chase_camera::wanted() && chase_camera::installed();
    const bool okay = wanted && object_trace::executable_verified() && engine_patch::install_window_open() &&
                      install_group(0, mandatory);
    LARGE_INTEGER f{};
    timing = okay && telemetry::enabled() && QueryPerformanceFrequency(&f) && f.QuadPart > 0;
    frequency = timing ? std::uint64_t(f.QuadPart) : 0;
    enabled.store(okay, std::memory_order_release);
    // The three mode/load/connect observers are installed with telemetry (the
    // diagnostic window) or with the restore ticket, which needs the mode
    // store (monitor capture, call result) and the deserializer (cancel).
    const bool restore_requested = restore_wanted();
    unsigned retries = detail::restore_default_retries;
    const bool retries_ok = restore_retries(retries);
    const bool observers = okay && (telemetry::enabled() || restore_requested) && install_group(mandatory, site_count);
    const bool diag = observers && telemetry::enabled();
    diagnostic.store(diag, std::memory_order_release);
    log("chase_transition installed=%u diagnostics=%u observers=%u mandatory_sites=6 diagnostic_sites=3 mode_writes=0 lifetime_capacity=64 thread_capacity=8 timing=%u qpc_frequency=%llu identity_version=1 origin_pairs=6 identity_scope=admitted_events",
        unsigned(okay), unsigned(diag), unsigned(observers), unsigned(timing), frequency);
    for (unsigned i = 0; i < site_count; ++i)
        if (sites[i].patched_in || wanted)
            log("chase_transition_site index=%u site=0x%08lx patched=%u status=%s", i,
                static_cast<unsigned long>(specs[i].address), unsigned(sites[i].patched_in), sites[i].status);
    // With the option unset nothing below is claimed. The engine path needs
    // the observers (kind 6/7); the call target and roots are the EXE's own
    // (object_trace::executable_verified() bound the image above).
    // The four sites may already be live for the second consumer (inject_attach): then the ticket attaches to them.
    const char* inject_status = "unclaimed";
    const bool restore_okay = restore_requested && okay && observers && retries_ok && inject_acquire(&inject_status);
    inject_chase = restore_okay;
    {
        Guard guard;
        restore = {};
        restore.retries = retries;
        restore_filter_publish();
    }
    restore_enabled.store(restore_okay, std::memory_order_release);
    if (restore_requested || restore_okay) {
        log("chase_view_restore installed=%u requested=%u path=engine sites=1 cancel_sites=%u inject_site=0x%08lx call=0x%08lx method=SelectMode mode=%u refresh=UpdateVisibility refresh_scan_cells=%u observers=%u retries=%u retries_valid=%u expiry_updates=%u due_frames=%u retry_spacing=%u",
            unsigned(restore_okay), unsigned(restore_requested), restore_site_count - 1,
            static_cast<unsigned long>(detail::restore_inject_site),
            static_cast<unsigned long>(detail::restore_script_call), detail::restore_rear_mode,
            detail::restore_caller_scan_cells, unsigned(observers),
            retries, unsigned(retries_ok), detail::restore_pending_update_expiry, detail::restore_due_frames,
            detail::restore_retry_spacing);
        for (unsigned i = 0; i < restore_site_count; ++i)
            log("chase_view_restore_site index=%u site=0x%08lx patched=%u status=%s", i,
                static_cast<unsigned long>(restore_specs[i].address), unsigned(restore_sites[i].patched_in),
                restore_sites[i].status);
    }
    SetLastError(error);
    return okay;
}
#endif
bool inject_attach(const InjectConsumer* consumer, const char** status) noexcept {
    const DWORD error = GetLastError();
    const char* reason = "invalid_consumer";
#if X3M_EDITION_FL
    // FL (the SETA restore, src/engine/fl/seta_restore_fl.cpp): the consumer's edition is admitted before it attaches.
    // The four sites are bound to the loaded base by fl_admit() (every identity window of chase_transition_sites_fl.h
    // checked first), here when the chase restore has not admitted it (camera off or not yet initialized); the call
    // target and selector are the consumer's (consumer_inject), the controlled object ECX.
    if (!fl_base && consumer) {
        const std::uintptr_t base = fl_module_base();
        if (!base)
            reason = "executable_mismatch";
        else if (const char* refusal = fl_admit(base))
            reason = refusal;
    }
    bool ok = consumer && consumer->poll && consumer->result && consumer->vm && consumer->unwind && fl_base &&
              !inject_consumer.load(std::memory_order_acquire) && inject_acquire(&reason);
#else
    bool ok = consumer && consumer->poll && consumer->result && consumer->vm && consumer->unwind &&
              !inject_consumer.load(std::memory_order_acquire) && inject_acquire(&reason);
#endif
    if (ok) inject_consumer.store(consumer, std::memory_order_release);
    *status = reason;
    SetLastError(error);
    return ok;
}
bool inject_detach() noexcept {
    const DWORD error = GetLastError();
    restore_inject_filter.fetch_and(~filter_consumer, std::memory_order_release);
    inject_consumer.store(nullptr, std::memory_order_release);
    const bool ok = inject_release_if_unused();
    SetLastError(error);
    return ok;
}
void inject_want(bool wanted) noexcept {
    if (wanted)
        restore_inject_filter.fetch_or(filter_consumer, std::memory_order_release);
    else
        restore_inject_filter.fetch_and(~filter_consumer, std::memory_order_release);
}
bool restore_installed() {
    return restore_enabled.load(std::memory_order_acquire);
}
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
bool diagnostics_active() {
    return diagnostic.load(std::memory_order_acquire);
}
std::uint64_t generation(std::uintptr_t cockpit) noexcept {
    if (!installed()) return 0;
    const DWORD error = GetLastError();
    std::uint64_t result;
    {
        Guard guard;
        result = state.generation(cockpit);
    }
    SetLastError(error);
    return result;
}
Update current_update(std::uintptr_t cockpit) noexcept {
    if (!installed()) return {};
    const DWORD error = GetLastError();
    Update result;
    {
        Guard guard;
        result = state.current(cockpit, GetCurrentThreadId());
    }
    SetLastError(error);
    return result;
}
bool active_control_cockpit(std::uintptr_t cockpit) noexcept {
    if (!installed() || !cockpit || (cockpit & 3)) return false;
    const DWORD error = GetLastError();
    std::uint64_t gen = 0;
    std::uint32_t handle = 0;
    // The generation is sampled under the lock; the bounded registry walk then
    // runs unlocked (checked engine reads only). A destroy between the two is
    // excluded only because both run on the game thread, inside the cockpit
    // update whose lifetime witness produced the generation.
    {
        Guard guard;
        gen = state.generation(cockpit);
    }
    // handle != 0: a registry with no active control (slot +0x10 zero) must not
    // match a stale row whose id is 0. chase_fire::active_cockpit keeps its
    // older walk without this guard; noted, not changed here.
    const bool result = gen != 0 && active_handle(cockpit, handle) && handle != 0;
    SetLastError(error);
    return result;
}
// Chase hook visit (0x00420e06, inside the cockpit update on the game thread).
// One atomic load when the option is off; otherwise the ticket's arm/due
// logic under the lock, with the registry walks only on frames that matter.
// Unseen or partial lifetimes read as generation 0 and never arm.
void restore_observe_frame(const AppliedFrame& frame) noexcept {
    if (!restore_enabled.load(std::memory_order_acquire)) return;
    const DWORD error = GetLastError();
    const auto start = timing ? now() : 0;
    detail::RestoreFrame f;
    f.cockpit = std::uint32_t(frame.cockpit);
    f.mode = frame.mode;
    f.connect = frame.connect;
    f.flags = frame.flags;
    f.ref = frame.ref;
    f.view = frame.view;
    f.sector = frame.sector;
    f.sector54 = frame.sector54;
    f.readable = frame.cockpit != 0 && !(frame.cockpit & 3);
    RestoreRows rows;
    {
        Guard guard;
        if (const auto* life = state.find(frame.cockpit)) {
            f.generation = life->complete ? life->generation : 0;
            f.constructor_caller = life->constructor_caller;
        }
        if (f.generation) {
            EngineProbe probe{frame.cockpit};
            restore.on_frame(f, probe, restore_epoch.load(std::memory_order_acquire));
            restore_filter_publish();
        }
        rows.take();
    }
    rows.emit();
    if (timing) {
        const auto end = now();
        Guard guard;
        restore_frame_timing.add(start, end);
    }
    SetLastError(error);
}
void report(std::uint64_t frame) {
    if (!installed()) return;
    detail::Window<Event> out;
    std::uint64_t over = 0, threads = 0, failed = 0, skipped = 0;
    detail::HandlerTiming durations[site_count]{};
    {
        Guard guard;
        out = window;
        window = {};
        over = state.lifetime_overflow;
        threads = state.thread_overflow;
        failed = read_failures;
        skipped = suppressed;
        for (unsigned i = 0; i < site_count; ++i) {
            durations[i] = handler_timing[i];
            handler_timing[i] = {};
        }
    }
    // Split conversion avoids the i386 uint64 -> double x87 CRT sequence.
    const auto as_double = [](std::uint64_t v) {
        return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
    };
    const double micros = frequency ? 1e6 / as_double(frequency) : 0;
    if (timing)
        for (unsigned i = 0; i < site_count; ++i) {
            const auto& c = durations[i];
            log("chase_transition_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_admission_guards includes=thread_lookup_lock_wait_reads_lock_release excludes=stub_cpu_boundary_first_qpc_timing_aggregation native_work=excluded",
                frame, i, c.calls, c.samples, c.invalid, as_double(c.ticks) * micros, as_double(c.max_ticks) * micros);
        }
    if (restore_installed()) {
        detail::RestoreState r;
        detail::HandlerTiming rt[restore_site_count]{}, ft{};
        {
            Guard guard;
            restore_report_frame = frame;
            r = restore;
            for (unsigned i = 0; i < restore_site_count; ++i) {
                rt[i] = restore_timing[i];
                restore_timing[i] = {};
            }
            ft = restore_frame_timing;
            restore_frame_timing = {};
        }
        if (timing)
            log("chase_view_restore_state frame=%llu phase=%u arms=%llu arm_refusals=%llu arm_refusal_reasons=%llu,%llu,%llu,%llu,%llu,%llu,%llu pendings=%llu dues=%llu calls=%llu monitors_captured=%llu injections_skipped=%llu rearm_frames=%llu eh_bumps=%llu epoch=%lu results=%llu,%llu,%llu,%llu,%llu,%llu cancels=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu managers_captured=%llu hud=%llu,%llu,%llu,%llu,%llu monitor=0x%08lx attempts=%u",
                frame, r.phase, r.arms, r.arm_refusals, r.arm_refusal_reasons[0], r.arm_refusal_reasons[1],
                r.arm_refusal_reasons[2], r.arm_refusal_reasons[3], r.arm_refusal_reasons[4],
                r.arm_refusal_reasons[5], r.arm_refusal_reasons[6], r.pendings, r.dues, r.calls, r.monitors_captured,
                r.injections_skipped, r.rearm_frames, restore_eh_bumps.load(std::memory_order_relaxed),
                static_cast<unsigned long>(restore_epoch.load(std::memory_order_relaxed)), r.results[0],
                r.results[1], r.results[2], r.results[3], r.results[4], r.results[5], r.cancels[0], r.cancels[1],
                r.cancels[2], r.cancels[3], r.cancels[4], r.cancels[5], r.cancels[6], r.cancels[7], r.cancels[8],
                r.cancels[9], r.cancels[10], r.cancels[11], r.cancels[12], r.cancels[13], r.managers_captured,
                r.hud_refreshes[0], r.hud_refreshes[1], r.hud_refreshes[2], r.hud_refreshes[3], r.hud_refreshes[4],
                static_cast<unsigned long>(r.monitor),
                r.attempts);
        if (timing)
            for (unsigned i = 0; i < restore_site_count; ++i)
                if (rt[i].calls)
                    log("chase_view_restore_timing frame=%llu kind=%u calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=handler_after_prefilter native_work=%s",
                        frame, i, rt[i].calls, rt[i].samples, rt[i].invalid, as_double(rt[i].ticks) * micros,
                        as_double(rt[i].max_ticks) * micros,
                        i == restore_inject_kind ? "included_on_call_iterations" : "excluded");
        if (timing && ft.calls)
            log("chase_view_restore_timing frame=%llu kind=frame calls=%llu samples=%llu invalid_qpc=%llu total_us=%.3f max_us=%.3f scope=chase_hook_observer native_work=excluded",
                frame, ft.calls, ft.samples, ft.invalid, as_double(ft.ticks) * micros, as_double(ft.max_ticks) * micros);
    }
    if (!diagnostics_active()) return;
#if X3M_EDITION_FL
    log("chase_transition_window frame=%llu first=%u last=%u dropped=%llu lifetime_overflow=%llu thread_overflow=%llu frame_refusals_total=%llu suppressed_total=%llu detail=off diagnostics_off=%s edition=fl",
        frame, out.first_used, out.last_used, out.dropped, over, threads, failed, skipped, fl_diagnostics_off);
    // FL rows: the witness kinds 0-3, 6, 7 with the verified snapshot (snapshot_valid bits 1 mode, 2 connect, 4 ref,
    // 8 view, 32 sector, 256 flags, 512 sector54) and, for kind 6, the dispatcher frame chain; kind 9 a finished
    // restore ticket. No chase_transition_detail row: all its fields are unverified on FL.
    auto print = [](const Event& e) {
        static const char* const names[10] = {"construct", "complete", "destroy", "update", "end_internal",
                                               "end", "mode_script", "mode_load", "connect", "restore"};
        if (e.kind == 9) {
            log("chase_transition_event event=%llu qpc=%llu generation=%llu update=0 thread=%lu kind=9 kind_name=restore result=%s cancel=%s attempts=%lu hud_refresh=%s mode_seen=%lu monitor=0x%08lx generation_from=%llu edition=fl",
                e.sequence, e.qpc, e.generation, static_cast<unsigned long>(e.thread), detail::restore_result_name(e.result),
                detail::restore_cancel_name(e.cancel), static_cast<unsigned long>(e.attempts),
                detail::restore_hud_name(e.hud), static_cast<unsigned long>(e.mode_seen),
                static_cast<unsigned long>(e.monitor), e.generation_from);
            return;
        }
        log("chase_transition_event event=%llu qpc=%llu generation=%llu update=%llu thread=%lu kind=%lu kind_name=%s cockpit=0x%08lx caller=0x%08lx requested=%lu valid=%lu active_handle=%lu snapshot_valid=%lu mode=%lu connect=%lu flags=0x%08lx ship=0x%08lx view=0x%08lx sector=0x%08lx sector54=0x%08lx frame=%s task=0x%08lx context=0x%08lx script_id=0x%08lx script_class=0x%lx edition=fl",
            e.sequence, e.qpc, e.generation, e.update, static_cast<unsigned long>(e.thread),
            static_cast<unsigned long>(e.kind), e.kind < 10 ? names[e.kind] : "?", static_cast<unsigned long>(e.cockpit),
            static_cast<unsigned long>(e.caller), static_cast<unsigned long>(e.requested),
            static_cast<unsigned long>(e.valid), static_cast<unsigned long>(e.handle),
            static_cast<unsigned long>(e.before.valid), static_cast<unsigned long>(e.before.mode),
            static_cast<unsigned long>(e.before.connect), static_cast<unsigned long>(e.before.flags),
            static_cast<unsigned long>(e.before.ship), static_cast<unsigned long>(e.before.view),
            static_cast<unsigned long>(e.before.sector), static_cast<unsigned long>(e.before.sector54),
            e.kind != 6 ? "-" : e.frame_ok ? "ok" : "refused", static_cast<unsigned long>(e.task),
            static_cast<unsigned long>(e.context), static_cast<unsigned long>(e.script_id),
            static_cast<unsigned long>(e.script_class));
    };
#else
    log("chase_transition_window frame=%llu first=%u last=%u dropped=%llu lifetime_overflow=%llu thread_overflow=%llu origin_refusals_total=%llu suppressed_total=%llu",
        frame, out.first_used, out.last_used, out.dropped, over, threads, failed, skipped);
    auto print = [](const Event& e) {
        log("chase_transition_event event=%llu qpc=%llu generation=%llu update=%llu thread=%lu kind=%lu cockpit=0x%08lx caller=0x%08lx requested=%lu valid=%lu active_handle=%lu snapshot_valid=%lu mode=%lu connect=%lu ship=0x%08lx view=0x%08lx camera=0x%08lx sector=0x%08lx target=0x%08lx boom=%ld,%ld,%ld origin_valid=%lu origin_flags=%lu task=0x%08lx next_pc=0x%08lx context=0x%08lx context_word0=0x%08lx context_return_count=%u context_returns=%08lx:%08lx,%08lx:%08lx,%08lx:%08lx,%08lx:%08lx",
            e.sequence, e.qpc, e.generation, e.update, e.thread, e.kind, e.cockpit, e.caller, e.requested, e.valid,
            e.handle, e.before.valid, e.before.mode, e.before.connect, e.before.ship, e.before.view, e.before.camera,
            e.before.sector, e.before.target, e.before.boom[0], e.before.boom[1], e.before.boom[2], e.origin.valid,
            e.origin.flags, e.origin.task, e.origin.pc, e.origin.context, e.origin.context_word0, e.origin.count,
            e.origin.contexts[0], e.origin.returns[0], e.origin.contexts[1], e.origin.returns[1], e.origin.contexts[2],
            e.origin.returns[2], e.origin.contexts[3], e.origin.returns[3]);
        log("chase_transition_detail event=%llu geometry_valid=%lu angles_a8=%ld,%ld,%ld offset_160=%ld,%ld,%ld view_lock_120=%lu context_returns_extra=%08lx:%08lx,%08lx:%08lx identity_valid=%lu identity_refused=%lu vm=0x%08lx native_id=0x%08lx native_script=0x%08lx player_script=0x%08lx controller_script=0x%08lx monitor_script=0x%08lx script_mode=%lu monitor_ref=0x%08lx warp_phase=%lu killed=%lu cell_tags=%lu,%lu,%lu,%lu,%lu,%lu script_classes=%08lx,%08lx,%08lx,%08lx",
            e.sequence, e.geometry.valid, e.geometry.angles[0], e.geometry.angles[1], e.geometry.angles[2],
            e.geometry.offset[0], e.geometry.offset[1], e.geometry.offset[2], e.geometry.lock, e.origin.contexts[4],
            e.origin.returns[4], e.origin.contexts[5], e.origin.returns[5], e.identity.valid, e.identity.refused,
            e.identity.vm, e.identity.native_id, e.identity.native_script, e.identity.player, e.identity.controller,
            e.identity.monitor, e.identity.mode, e.identity.ref, e.identity.warp, e.identity.killed, e.identity.tags[0],
            e.identity.tags[1], e.identity.tags[2], e.identity.tags[3], e.identity.tags[4], e.identity.tags[5],
            e.identity.class_ids[0], e.identity.class_ids[1], e.identity.class_ids[2], e.identity.class_ids[3]);
    };
#endif
    for (unsigned i = 0; i < out.first_used; ++i) print(out.first[i]);
    const unsigned start = out.last_used == 32 ? out.next : 0;
    for (unsigned i = 0; i < out.last_used; ++i) print(out.last[(start + i) % 32]);
}
void shutdown() {
    restore_enabled.store(false, std::memory_order_release);
    diagnostic.store(false, std::memory_order_release);
    enabled.store(false, std::memory_order_release);
    const bool view_restore = restore_sites_restore();
    const bool diag = restore_group(mandatory, site_count), base = restore_group(0, mandatory);
    {
        Guard guard;
        for (auto& t : state.threads) t = {};
        for (auto& l : state.lives) l = {};
        restore = {};
        restore_filter_publish();
    }
    log("chase_transition_shutdown mandatory_restored=%u diagnostic_restored=%u view_restore_restored=%u",
        unsigned(base), unsigned(diag), unsigned(view_restore));
}
}
