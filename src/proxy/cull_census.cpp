#include "cull_census.h"
#include "cull_census_core.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <atomic>
#include <cstring>

// Compiled with -mno-sse -mno-mmx -mfpmath=387 and -fno-exceptions (CMake
// source property; the fixture build compiles it the same way): the two
// handlers run on the engine's render thread in the middle of its cull pass
// with no CPU-state boundary, so nothing here may touch an XMM/MMX register,
// and they contain no floating-point arithmetic, so no x87 instruction either
// (verification/probe/check_no_x87.py walks them from their symbols).
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::cull_census::core;
namespace core = x3m::cull_census::core;
namespace engine_patch = x3m::engine_patch;
namespace engine_memory = x3m::engine_memory;
bool patched_ = false;
engine_patch::Site measure_site_{}, exit_site_{};
std::uintptr_t measure_stub_ = 0, exit_stub_ = 0;
const char* state_ = "disabled";
// The ring: committed once at install (never per node), never freed (the
// module is pinned and a render thread may be inside a handler at shutdown).
Entry* ring_ = nullptr;
// Written by the render thread inside the pass, read at Present on the same
// thread (the frame routine and Present share it); relaxed atomics keep the
// loads and stores plain words without inviting the compiler to cache them.
std::atomic<std::uint32_t> count_{0}, overflow_{0}, unmeasured_{0}, exited_{0};
std::uint32_t pending_node_ = 0, pending_index_ = no_index;
AncestorStack ancestors_{};       // flag31 of the nodes whose children the pass is visiting (render thread only)
bool small_exempt_projectiles_ = false; // cull_small_parts' projectile exemption for the frame being recorded
std::int32_t small_threshold_ = 0; // cull_small_parts' threshold for the frame being recorded (0 = none)
// The LOD ladder of each model a captured frame's rows name, read at Present
// (never inside the pass) through engine_memory::read, which validates every
// span against committed readable memory, so a stale or wrong pointer yields
// "unknown", never a fault. Direct-mapped by model pointer and cleared per
// captured frame: a station's parts share a handful of models, so the reads
// are one per distinct model, not one per row.
struct Ladder { std::uint32_t model_ptr; bool known; std::int32_t count; unsigned thresholds; std::int32_t thr[ladder_cap]; };
constexpr unsigned ladder_cache_size = 256;
Ladder ladder_cache_[ladder_cache_size];

Ladder read_ladder(std::uint32_t model) {
    Ladder l{}; l.model_ptr = model;
    unsigned char head[8];   // model+0x0c record-pointer array, model+0x10 the signed LOD count word
    if (!model || !engine_memory::read(std::uintptr_t(model) + model_records_offset, head, sizeof head)) return l;
    std::uint32_t records = 0; std::int16_t count = 0;
    std::memcpy(&records, head, 4); std::memcpy(&count, head + (model_lod_count_offset - model_records_offset), 2);
    l.known = true; l.count = count;
    if (count <= 0 || !records) return l;
    const unsigned n = unsigned(count) < ladder_cap ? unsigned(count) : ladder_cap;
    std::uint32_t pointers[ladder_cap]{};
    if (!engine_memory::read(records, pointers, n * 4)) return l;
    std::int32_t thr[ladder_cap]{};
    for (unsigned i = 0; i < n; ++i)
        if (!pointers[i] || !engine_memory::read(std::uintptr_t(pointers[i]) + record_threshold_offset, &thr[i], 4)) return l;
    std::memcpy(l.thr, thr, sizeof thr); l.thresholds = n;
    return l;
}
const Ladder& ladder_of(std::uint32_t model) {
    static const Ladder none{};   // rows without a model pointer: no read, no cache slot
    if (!model) return none;
    Ladder& slot = ladder_cache_[(model >> 4) % ladder_cache_size];
    if (slot.model_ptr != model) slot = read_ladder(model);
    return slot;
}

// The body name of each model id a captured frame's rows name (body-format-bob1.md 6),
// read at Present through engine_memory::read like the ladder: the table header once
// per captured frame (the slot array may be reallocated between frames), then per
// distinct id the slot's name pointer and up to body_name_scan bytes of the name.
// Direct-mapped by id and cleared per captured frame; the row gets `body=-` on any
// unreadable span, an invalid id or a name without a NUL within the scan.
#ifdef X3M_CULL_CENSUS_FIXTURE
std::uintptr_t body_global_ = body_global_va;   // the fixture points it at a synthetic manager
#else
constexpr std::uintptr_t body_global_ = body_global_va;
#endif
struct BodyTable { bool read, valid; std::int32_t fixed, dynamic; std::uint32_t slots; };
BodyTable body_table_{};
struct BodyName { std::uint32_t id; bool valid; char suffix[body_suffix_size]; };
constexpr unsigned body_cache_size = 256;
BodyName body_cache_[body_cache_size];

const BodyTable& body_table() {
    if (body_table_.read) return body_table_;
    body_table_.read = true;
    std::uint32_t g = 0; std::int32_t head[3]{};   // +0xb4 fixed, +0xb8 dynamic, +0xbc slots
    static_assert(body_dynamic_count_offset == body_fixed_count_offset + 4 && body_slots_offset == body_fixed_count_offset + 8, "one 12-byte header read");
    if (!engine_memory::read(body_global_, &g, 4) || !g || !engine_memory::read(std::uintptr_t(g) + body_fixed_count_offset, head, sizeof head)) return body_table_;
    body_table_.fixed = head[0]; body_table_.dynamic = head[1]; body_table_.slots = std::uint32_t(head[2]);
    body_table_.valid = head[0] == body_fixed_count && head[1] >= 0 && head[1] < body_dynamic_limit && head[2] != 0;
    return body_table_;
}
// The name at `p`, scanned in page-bounded chunks so a short name ending just before
// an unreadable page still reads; false without a NUL within body_name_scan bytes.
bool read_name(std::uint32_t p, char* name) {
    unsigned have = 0;
    while (have < body_name_scan) {
        const std::uintptr_t at = std::uintptr_t(p) + have;
        unsigned chunk = unsigned(0x1000 - (at & 0xfff));
        if (chunk > body_name_scan - have) chunk = body_name_scan - have;
        if (!engine_memory::read(at, name + have, chunk)) return false;
        for (unsigned i = have; i < have + chunk; ++i) if (!name[i]) return true;
        have += chunk;
    }
    return false;
}
const char* body_of(std::uint32_t id) {
    BodyName& slot = body_cache_[id % body_cache_size];
    if (slot.valid && slot.id == id) return slot.suffix;
    slot.id = id; slot.valid = true;
    const BodyTable& t = body_table();
    std::uint32_t index = 0, p = 0;
    char name[body_name_scan];
    const char* shown = nullptr;
    std::uint64_t entry = 0;   // computed in 64 bits: a slot address that would wrap is refused, not read
    if (t.valid && body_slot(std::int32_t(id), t.fixed, t.dynamic, &index)
        && (entry = std::uint64_t(t.slots) + std::uint64_t(index) * body_slot_stride + body_slot_name_offset) <= 0xfffffffcu
        && engine_memory::read(std::uintptr_t(entry), &p, 4)) {
        if (!p) { body_default_name(std::int32_t(id), name); shown = name; }
        else if (read_name(p, name)) shown = name;
    }
    format_body(slot.suffix, shown);
    return slot.suffix;
}

// LOD-switch log state (X3M_LOD_SWITCH_LOG, core::track_observe): 0 = off, the stubs
// then stay armed on capture frames only. The table is committed once when the
// option is set (never per frame or per node, never freed: same rule as the ring).
unsigned lod_switch_cap_ = 0;
TrackSlot* track_ = nullptr;
std::uint32_t track_frame_ = 0;   // armed frames since the last clear; 0 = cleared
void clear_tracks() {
    if (track_) std::memset(track_, 0, sizeof(TrackSlot) * track_size);
    track_frame_ = 0;
}
// Compares every kept, exited entry of the ended frame with its (node, view)
// state and logs up to lod_switch_cap_ switch rows, one overflow row and the frame
// row (only on frames with a switch). `fresh`: the ladder and body caches were
// already reset for this frame by the captured-frame rows.
void scan_lod_switches(unsigned long long frame, std::uint32_t entries, bool fresh) {
    if (++track_frame_ == 0) { clear_tracks(); track_frame_ = 1; }   // wrap: re-seed everything
    std::uint32_t switches = 0, nodes = 0;
    for (std::uint32_t i = 0; i < entries; ++i) {
        const Entry& e = ring_[i];
        if (!e.exited || !(e.flags_out & 2u)) continue;   // only nodes the pass kept: a culled node's +0x14c is the entry 0
        ++nodes;
        std::int32_t from = 0;
        if (track_observe(track_, e.node, e.view, e.model, e.lod, track_frame_, &from) != Observed::switched) continue;
        if (switches++ >= lod_switch_cap_) continue;
        if (!fresh) {
            std::memset(ladder_cache_, 0, sizeof ladder_cache_);
            std::memset(body_cache_, 0, sizeof body_cache_); body_table_ = BodyTable{};
            engine_memory::revalidate();
            fresh = true;
        }
        // T_pad: the last record's threshold (merged-lod-feasibility.md; the pad of a merged body), '-' when unknown.
        const Ladder& l = ladder_of(e.model_ptr);
        char pad[12] = "-";
        if (e.model_ptr && l.known && l.count > 0 && l.thresholds == unsigned(l.count))
            format_int(pad, sizeof pad, l.thr[l.thresholds - 1]);
        x3m::log("lod_switch frame=%llu node=%08lx%s from=%ld to=%ld s=%ld D=%ld T_pad=%s%s view=%08lx",
            frame, (unsigned long)e.node, body_of(e.model), (long)from, (long)e.lod, (long)e.s, (long)e.d, pad, flag31_suffix(e.flag31), (unsigned long)e.view);
    }
    if (switches > lod_switch_cap_)
        x3m::log("lod_switch_overflow frame=%llu dropped=%lu cap=%u", frame, (unsigned long)(switches - lod_switch_cap_), lod_switch_cap_);
    if (switches) x3m::log("lod_switch_frame frame=%llu switches=%lu nodes=%lu", frame, (unsigned long)switches, (unsigned long)nodes);
}

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[measure_window_length]{};
    return length <= measure_window_length && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the stubs' CALLs and the enabled byte can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
engine_patch::SiteSpec spec_for(const char* name, std::uintptr_t address, const unsigned char* bytes) {
    engine_patch::SiteSpec spec{};
    spec.name = name; spec.address = address; spec.length = site_length; spec.ret_pop = ret_pop; spec.rel32_offset = 0;
    std::memcpy(spec.expected, bytes, site_length);
    return spec;
}
// Emits one stub followed by its 4-aligned continuation slot; returns the
// stub start and the slot, or 0 when the arena is full.
template<unsigned Length, class Encode>
std::uintptr_t emit_stub(Encode&& encode, void*** slot_out) {
    engine_patch::Emitter e(Length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + Length + 3) & ~std::uintptr_t(3);
    unsigned char code[Length];
    encode(std::uint32_t(at), std::uint32_t(slot), code);
    e.bytes(code, Length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// Claims one site and chains a stub in front of its tail. On any failure the
// site is left as claim() left it (restored, or registered when rollback failed).
bool hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, std::uintptr_t stub, void** slot, const char** reason) {
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) { *reason = site.status; return false; }
    if (!stub || !slot || !engine_patch::store_pointer(slot, *site.entry) || !engine_patch::push_front(site, reinterpret_cast<void*>(stub))) {
        engine_patch::restore(site);
        *reason = "chain_failed"; return false;
    }
    return true;
}
}

volatile unsigned char x3m_cull_census_enabled = 0; // declared extern "C" in the header

extern "C" void x3m_cull_census_measure(std::uint32_t node, std::int32_t measure, std::int32_t s, std::int32_t d, std::uint32_t view) {
    const std::uint32_t index = count_.load(std::memory_order_relaxed);
    pending_node_ = node;
    if (!ring_ || index >= ring_size) { overflow_.fetch_add(1, std::memory_order_relaxed); pending_index_ = no_index; return; }
    // Every field below was dereferenced by the pass itself on this node
    // before the site (0x0047cfed +0x130, 0x0047d08b radius, 0x0047d19b model,
    // 0x0047d1af flags, 0x0047d258/0x0047d2a7 thresholds) and the parent link
    // is read at 0x0047d2a2..0x0047d2af with the same null test.
    const auto* n = reinterpret_cast<const std::uint32_t*>(node);
    Entry& e = ring_[index];
    e.node = node; e.model = n[model_offset / 4]; e.view = view;
    e.s = s; e.measure = measure; e.d = d; e.radius = std::int32_t(n[radius_offset / 4]);
    e.thr_1dc = std::int32_t(n[threshold_1dc_offset / 4]); e.thr_1d8 = std::int32_t(n[threshold_1d8_offset / 4]);
    const std::uint32_t parent = n[parent_offset / 4];
    e.parent = parent;
    e.flag31 = ancestors_.resolve(parent, n[flags12c_offset / 4]);
    e.limit = size_limit(e.thr_1d8, parent != 0, parent ? std::int32_t(reinterpret_cast<const std::uint32_t*>(parent)[threshold_1d8_offset / 4]) : 0);
    e.flags_in = n[flags12c_offset / 4]; e.flags130 = n[flags130_offset / 4]; e.flags_out = 0; e.lod = 0; e.exited = 0; e.model_ptr = 0;
    pending_index_ = index;
    count_.store(index + 1, std::memory_order_relaxed);
}
extern "C" void x3m_cull_census_exit(std::uint32_t node, std::uint32_t ebx, std::uint32_t model_slot, std::uint32_t d_slot) {
    // The child loop follows this site: the node becomes the ancestor of the rows measured next
    // (+0x18 and +0x12c of the node the pass holds in EDI, read as at the measure site).
    {
        const auto* n = reinterpret_cast<const std::uint32_t*>(node);
        ancestors_.push(node, ancestors_.resolve(n[parent_offset / 4], n[flags12c_offset / 4]));
    }
    if (pending_node_ != node) { unmeasured_.fetch_add(1, std::memory_order_relaxed); return; }
    pending_node_ = 0;
    const std::uint32_t index = pending_index_;
    pending_index_ = no_index;
    if (index == no_index || !ring_) return; // dropped at the measure site: counted in overflow
    const auto* n = reinterpret_cast<const std::uint32_t*>(node);
    Entry& e = ring_[index];
    e.flags_out = n[flags12c_offset / 4]; e.lod = std::int32_t(n[lod_offset / 4]); e.exited = 1;
    e.model_ptr = exit_model_pointer(ebx, model_slot, d_slot); // a register and two stack slots: no engine read here
    exited_.fetch_add(1, std::memory_order_relaxed);
}

namespace x3m::cull_census {
bool install_at(std::uintptr_t measure_site, std::uintptr_t exit_site) {
    if (patched_) { state_ = "already_installed"; return false; }
    const char* reason = nullptr;
    if (!measure_site || !exit_site || measure_site < measure_site_offset || exit_site < exit_site_offset) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!bytes_match(measure_site - measure_site_offset, measure_window, measure_window_length)
             || !bytes_match(exit_site - exit_site_offset, exit_window, exit_window_length)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    else if (!ring_ && !(ring_ = static_cast<Entry*>(VirtualAlloc(nullptr, sizeof(Entry) * ring_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))) reason = "ring_alloc_failed";
    std::uintptr_t measure_stub = 0, exit_stub = 0; void** measure_slot = nullptr; void** exit_slot = nullptr;
    if (!reason) {
        const std::uint32_t enabled = std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_enabled));
        measure_stub = emit_stub<measure_stub_length>([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            encode_measure_stub(at, enabled, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_measure)), slot, out); }, &measure_slot);
        exit_stub = emit_stub<exit_stub_length>([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            encode_exit_stub(at, enabled, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_exit)), slot, out); }, &exit_slot);
        if (!measure_stub || !exit_stub) reason = "arena_full";
    }
    if (!reason) {
        x3m_cull_census_enabled = 0;
        if (!hook_site(measure_site_, spec_for("cull_census_measure", measure_site, core::measure_site), measure_stub, measure_slot, &reason)) {
            if (measure_site_.patched_in) { patched_ = true; state_ = "measure_rollback_failed"; return false; } // registered for shutdown()
        } else if (!hook_site(exit_site_, spec_for("cull_census_exit", exit_site, core::exit_site), exit_stub, exit_slot, &reason)) {
            // Partial install: the measure site goes back; a live but unrestorable
            // exit site keeps both registered so shutdown() tries again.
            if (exit_site_.patched_in) { patched_ = true; state_ = "exit_rollback_failed"; return false; }
            if (!engine_patch::restore(measure_site_)) { patched_ = true; state_ = "measure_rollback_failed"; return false; }
            state_ = reason; return false;
        } else {
            patched_ = true; measure_stub_ = measure_stub; exit_stub_ = exit_stub; reason = "ok";
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_CULL_CENSUS", setting, 4);
    if (length == 0) {
        state_ = "disabled";
        if (GetEnvironmentVariableW(L"X3M_LOD_SWITCH_LOG", setting, 4)) log("cull_census_lod_switch state=census_off cap=0 table=0");
        SetLastError(error); return false;
    }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else applied = install_at(measure_site_va, exit_site_va);
    // X3M_LOD_SWITCH_LOG=N (rows per frame): only with the census live.
    const char* lod_switch = "off";
    wchar_t cap_text[8]{};
    const DWORD cap_length = GetEnvironmentVariableW(L"X3M_LOD_SWITCH_LOG", cap_text, 8);
    unsigned cap = 0;
    if (cap_length && !applied) lod_switch = "census_off";
    else if (cap_length && !parse_lod_switch_cap(cap_text, cap_length < 8 ? unsigned(cap_length) : 8u, &cap)) lod_switch = "invalid";
    else if (cap && !set_lod_switch_log(cap)) lod_switch = "alloc_failed";
    else if (cap) lod_switch = "on";
    log("cull_census requested=%u patched=%u reason=%s measure_site=0x%08lx exit_site=0x%08lx write_measure=%s write_exit=%s stub_measure=0x%08lx stub_exit=0x%08lx ring=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(measure_site_va), static_cast<unsigned long>(exit_site_va),
        measure_site_.patched_in ? (measure_site_.atomic_write ? "atomic" : "plain") : "none", exit_site_.patched_in ? (exit_site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(measure_stub_), static_cast<unsigned long>(exit_stub_), ring_size);
    log("cull_census_lod_switch state=%s cap=%u table=%u", lod_switch, lod_switch_cap_, lod_switch_cap_ ? track_size : 0u);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    x3m_cull_census_enabled = 0;
    const bool exit_ok = engine_patch::restore(exit_site_), measure_ok = engine_patch::restore(measure_site_);
    clear_tracks();
    patched_ = false; measure_stub_ = exit_stub_ = 0; // the stubs stay in the arena (a thread may still be inside them)
    state_ = exit_ok && measure_ok ? "restored" : "restore_failed";
    SetLastError(error);
    return exit_ok && measure_ok;
}
const char* state() { return state_; }
std::uintptr_t measure_stub_address() { return patched_ ? measure_stub_ : 0; }
std::uintptr_t exit_stub_address() { return patched_ ? exit_stub_ : 0; }
void begin_frame(bool capture) {
    if (!patched_) return;
    x3m_cull_census_enabled = 0;
    count_.store(0, std::memory_order_relaxed); overflow_.store(0, std::memory_order_relaxed);
    unmeasured_.store(0, std::memory_order_relaxed); exited_.store(0, std::memory_order_relaxed);
    pending_node_ = 0; pending_index_ = no_index; ancestors_.clear();
    x3m_cull_census_enabled = capture || lod_switch_cap_ ? 1 : 0;   // the LOD-switch log compares every frame
}
void reset() {
    begin_frame(false);
    x3m_cull_census_enabled = 0;
    clear_tracks();   // a Reset re-seeds: the first frames after it report no switch
}
bool set_lod_switch_log(unsigned cap) {
    if (cap > lod_switch_max_cap) return false;
    if (cap && !track_) {
        track_ = static_cast<TrackSlot*>(VirtualAlloc(nullptr, sizeof(TrackSlot) * track_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!track_) { lod_switch_cap_ = 0; return false; }
    }
    clear_tracks();
    lod_switch_cap_ = cap;
    return true;
}
#ifdef X3M_CULL_CENSUS_FIXTURE
void set_body_table_global(std::uintptr_t va) { body_global_ = va; }
#endif
void note_small_threshold(std::int32_t threshold, bool exempt_projectiles) {
    small_threshold_ = threshold; small_exempt_projectiles_ = exempt_projectiles;
}
Stats stats() {
    Stats s{};
    s.entries = count_.load(std::memory_order_relaxed); s.overflow = overflow_.load(std::memory_order_relaxed);
    s.unmeasured = unmeasured_.load(std::memory_order_relaxed); s.exited = exited_.load(std::memory_order_relaxed);
    s.armed = x3m_cull_census_enabled != 0;
    return s;
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    const DWORD error = GetLastError();
    const bool armed = x3m_cull_census_enabled != 0;
    x3m_cull_census_enabled = 0; // the ring is read below on the same thread that fills it
    if (captured) {
        const Stats s = stats();
        const std::uint32_t entries = s.entries < ring_size ? s.entries : ring_size;
        // Rows below the small-parts threshold that the stub let through as projectiles (the
        // stub's own per-frame count is exempt_bullet= in cull_small_parts_frame).
        std::uint32_t exempt = 0;
        for (std::uint32_t i = 0; i < entries && ring_; ++i) if (small_exempt(ring_[i], small_threshold_, small_exempt_projectiles_)) ++exempt;
        log("cull_census_frame device=%llu frame=%llu entries=%lu overflow=%lu unmeasured=%lu exited=%lu ring=%u culled_small_exempt_bullet=%lu",
            device, frame, (unsigned long)entries, (unsigned long)s.overflow, (unsigned long)s.unmeasured, (unsigned long)s.exited, ring_size, (unsigned long)exempt);
        std::memset(ladder_cache_, 0, sizeof ladder_cache_);
        std::memset(body_cache_, 0, sizeof body_cache_); body_table_ = BodyTable{};
        // A fresh validation epoch for this frame's ladder reads: on a census-only run no motion
        // route advances it, so region checks would otherwise be trusted for up to 100 ms.
        engine_memory::revalidate(); // an epoch only: not a Present (engine_memory.h)
        for (std::uint32_t i = 0; i < entries && ring_; ++i) {
            const Entry& e = ring_[i];
            // A culled_small row names the scope that culled it, then the model's LOD ladder
            // and the body name of its id, then flag31 (appended: the row parsers anchor on the fields before them).
            const Verdict verdict = classify(e, small_threshold_, small_exempt_projectiles_);
            char ladder[8 + 12 + 5 + ladder_cap * 12 + 1];
            const Ladder& l = ladder_of(e.model_ptr);
            format_ladder(ladder, sizeof ladder, e.model_ptr && l.known, l.count, l.thresholds, l.thr);
            log("cull_census device=%llu frame=%llu view=%08lx node=%08lx model=%08lx s=%ld measure=%ld d=%ld radius=%ld thr_1dc=%ld thr_1d8=%ld limit=%ld flags_in=%08lx flags_out=%08lx lod=%ld verdict=%s%s%s%s%s",
                device, frame, (unsigned long)e.view, (unsigned long)e.node, (unsigned long)e.model, (long)e.s, (long)e.measure, (long)e.d, (long)e.radius,
                (long)e.thr_1dc, (long)e.thr_1d8, (long)e.limit, (unsigned long)e.flags_in, (unsigned long)e.flags_out, (long)e.lod, verdict_name(verdict),
                verdict == Verdict::culled_small ? " scope=all" : "", ladder, body_of(e.model), flag31_suffix(e.flag31));
        }
    }
    if (lod_switch_cap_ && track_ && ring_ && armed) {
        const std::uint32_t entries = count_.load(std::memory_order_relaxed);
        scan_lod_switches(frame, entries < ring_size ? entries : ring_size, captured);
    }
    count_.store(0, std::memory_order_relaxed); overflow_.store(0, std::memory_order_relaxed);
    unmeasured_.store(0, std::memory_order_relaxed); exited_.store(0, std::memory_order_relaxed);
    pending_node_ = 0; pending_index_ = no_index; ancestors_.clear();
    SetLastError(error);
}
}
