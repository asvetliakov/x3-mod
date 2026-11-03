// FL edition of the ship avoidance (X3M_SHIP_AVOIDANCE; the AP twin is the step-2 part of src/proxy/ship_steering_decide.cpp,
// _warn.cpp and _install.cpp): the lazy sector snapshot (wk::fill_snapshot with the FL TShips slot), the target adjustment at
// site A (av::resolve), and site E, the native warn test base+0x057600 claimed separately with the suppression stub
// (ship_steering_fl.cpp asm). Facts: docs/reverse-engineering/fl/ship-avoidance.md sections 1-5, "Port (2026-11-03)"; sites
// and windows: ship_avoidance_sites_fl.h. The gate, traffic, lane and dock rows of the AP verdict stay off (their groups are
// not ported): E answers the resolved pair, the capital's small-ship and formation-member rows, exactly as the AP verdict with
// those groups off; every other pair runs the native test.
//
// Install (from ship_steering::initialize, after the steering claimed its four sites, inside the install window): the part-2
// windows (the snapshot's native contracts) or the avoidance stays off with a refusal row; then the part-3 windows and the E
// claim (read back, write path checked), or E stays unclaimed and the target adjustment runs with the native warn untouched.
// warn_live_ is published only after the stub is chained; a failed restore keeps the stub passing through.
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "ship_steering_fl_internal.h"
#include <cstdio>

using namespace x3m_fl_ship_steering_rt;
namespace engine_patch = x3m::engine_patch;

extern "C" {
void* x3m_fl_ship_avoid_next_e = nullptr;
}

namespace {
engine_patch::Site warn_site_{};
unsigned failed_window_ = FA::window_count;
std::uint32_t rep_av_[13] = {}, rep_av_why_[unsigned(av::Unresolved::count)] = {}, rep_av_ep_[4] = {};
unsigned long long last_detour_tick_ = 0;
unsigned detour_rows_this_second_ = 0;
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
bool fixture_fault_e_ = false; // one-shot: E's read-back sees a foreign byte
#endif

const av::Snapshot* snapshot_for(std::uint32_t sector, std::uint32_t now) noexcept {
    unsigned slot = 0;
    for (unsigned i = 0; i < FA::snapshot_slots; ++i) {
        if (snaps_[i].sector == sector && snaps_[i].epoch == now && snaps_[i].count + snaps_[i].overflow) return &snaps_[i];
        if (snaps_[i].sector == sector) slot = i;
        else if (snaps_[slot].sector != sector && snaps_[i].epoch < snaps_[slot].epoch) slot = i;
    }
    av::Snapshot& s = snaps_[slot];
    bool want_census = debug_;
    for (unsigned i = 0; i < census_logged_count_ && want_census; ++i)
        if (census_logged_sectors_[i] == sector) want_census = false;
    Census* census = nullptr;
    Census local{};
    if (want_census && census_logged_count_ < 8) {
        census_logged_sectors_[census_logged_count_++] = sector;
        census = &local;
        census->sector = sector;
    }
    RawMem mem;
    wk::Stats stats;
    wk::fill_snapshot(mem, sector, now, s, part_cache_, stats, census, &members_, &member_stats_);
    av_snapshots_.add();
    av_walk_objects_.add(stats.visited);
    if (s.overflow) av_overflow_.add();
    part_built_.add(stats.part_built);
    part_fallback_.add(stats.part_fallback);
    if (stats.part_pinned_full) part_pinned_full_.add(stats.part_pinned_full);
    part_invalid_.add(stats.part_invalid);
    part_dirty_.add(stats.part_dirty);
    part_scaled_.add(stats.part_scaled);
    if (census) {
        const std::uint32_t w = census_write_.load(std::memory_order_relaxed);
        if (w - census_read_ < 4) {
            census_[w & 3] = local;
            census_write_.store(w + 1, std::memory_order_release);
        }
    }
    return &s;
}
__attribute__((always_inline)) inline float pair_distance_m(std::uint32_t a, std::uint32_t b) noexcept { // centre distance, m (-1: unreadable)
    const std::uint32_t pa = rd32(a + F::obj_phys), pb = rd32(b + F::obj_phys);
    if (!pa || !pb) return -1.0f;
    float d2 = 0.0f;
    for (unsigned i = 0; i < 3; ++i) {
        const float d = float(rds32(pa + F::phys_position + 4 * i)) / core::kUnitsPerMetre -
                        float(rds32(pb + F::phys_position + 4 * i)) / core::kUnitsPerMetre;
        d2 += d * d;
    }
    return core::sqrt_ss(d2);
}
using EV = av::EVerdict;
// Site E's verdict: the AP e_verdict with the gate, traffic, lane and dock groups off (their state never set on FL), plus the
// FL lifetime guards (a recycled A or B is never answered from the old object's decision).
inline int e_verdict(std::uint32_t a, std::uint32_t b, std::uint32_t now) noexcept {
    if (!a || !b || rd16(a + F::obj_class) != F::class_ship) return 0;
    const std::uint32_t fl = rd32(a + F::obj_flight);
    if (!fl) return 0;
    av::SiteE in;
    in.a_order = rd16(fl + F::flight_order);
    in.a_target = rd32(fl + F::flight_target);
    const core::Ship* s = table_.find(a);
    if (s && table_id_[unsigned(s - table_.entries)] != rd32(a + F::obj_id)) { // the entry is a previous object's
        s = nullptr;
        e_recycled_a_.add();
    }
    in.a_in_table = s != nullptr;
    const std::uint16_t cls_b = rd16(b + F::obj_class);
    in.b_player = cls_b == F::class_ship && (rd32(b + F::obj_flags) & F::flag_player) != 0;
    if (!s && !in.a_arrival) return 0; // pass_not_fresh
    const unsigned slot = s ? unsigned(s - table_.entries) : 0u;
    in.a_age_ms = s ? now - s->stamp_ms : 0xffffffffu;
    in.frame_ms = frame_step_ms();
    in.avoidance_on = avoidance_on_;
    if (cls_b == F::class_gate && s) { // the gate rows: no gate state on FL (the gate group is not ported)
        in.b_gate = true;
        in.gates_live = false;
        in.capital_policy = false;
        in.small_gap_ms = 0u;
        in.gate_state = false;
        in.gate_capital = false;
        in.gate_join = false;
        in.gate_handover = false;
        in.p2_age_ms = 0xffffffffu;
        in.lead_age_ms = 0xffffffffu;
    } else if (cls_b == F::class_ship) {
        in.b_ship = true;
        const std::uint32_t table = rd32(table_slot_);
        const std::int32_t type_a = std::int16_t(rd16(a + F::obj_type)), type_b = std::int16_t(rd16(b + F::obj_type));
        in.codes_readable = table && type_a >= 0 && type_b >= 0;
        if (in.codes_readable) {
            const std::uint32_t code_a = rd32(table + std::uint32_t(type_a) * F::record_stride + F::record_class_code);
            in.a_capital = code_a < core::kClassCount && ((core::kCapitalMask >> code_a) & 1u);
            if (in.a_capital) {
                const std::uint32_t code_b = rd32(table + std::uint32_t(type_b) * F::record_stride + F::record_class_code);
                in.b_readable = code_b < core::kClassCount;
                in.b_capital_code = in.b_readable && ((core::kCapitalMask >> code_b) & 1u);
                in.b_class_masked = in.b_readable && ((class_mask_ >> code_b) & 1u) != 0;
                in.small_dock_legs = false; // (AP: the capital gate key)
                in.small_any_age = false;
                in.r_a = float(rds32(a + F::obj_radius)) / core::kUnitsPerMetre;
                in.r_b = float(rds32(b + F::obj_radius)) / core::kUnitsPerMetre;
                in.b_flags = rd32(b + F::obj_flags);
                in.b_docked = rd32(b + F::obj_docked);
                const std::uint32_t fl_b = rd32(b + F::obj_flight);
                in.b_order = fl_b ? rd16(fl_b + F::flight_order) : std::uint16_t(0);
                in.b_target = fl_b ? rd32(fl_b + F::flight_target) : 0u;
                const std::uint32_t id_a = rd32(a + F::obj_id), id_b = rd32(b + F::obj_id);
                if (in.b_readable && av::small_pair(true, in.r_a, true, in.b_capital_code, in.r_b)) {
                    in.b_is_a_leader = (av::member_order(in.a_order) && in.a_target == b) || members_.current(a, b, id_a, id_b, now) != nullptr;
                } else if (in.b_readable && in.a_order != core::kOrderAvoid && avoidance_on_) {
                    in.b_member_current = members_.current(b, a, id_b, id_a, now) != nullptr;
                    if (in.b_member_current) in.d = pair_distance_m(a, b);
                }
            }
        }
    }
    in.b_resolved = s && in.a_order != core::kOrderAvoid &&
                    av::suppressed(avoid_[slot], a, b, now, av::fresh_window_ms(av::kResolvedAgeMs, in.frame_ms));
    if (in.b_resolved) { // the resolved B is the object the decision saw: its native id then
        const av::ShipAvoid& st = avoid_[slot];
        const std::uint32_t id_b = rd32(b + F::obj_id);
        bool same = false;
        for (unsigned i = 0; i < st.n_resolved && i < av::kMaxResolved; ++i)
            if (st.resolved[i] == b && resolved_id_[slot][i] == id_b) same = true;
        if (!same) {
            in.b_resolved = false;
            e_resolved_id_mismatch_.add();
        }
    }
    const EV v = av::site_e_verdict(in);
    switch (v) {
    case EV::suppress_small:
        e_suppressed_small_.add();
        if (s && in.a_age_ms > av::fresh_window_ms(av::kSteeredFreshMs, in.frame_ms, av::kSteeredReplanMs)) e_small_gap_.add();
        return 1;
    case EV::suppress_member: e_suppressed_member_.add(); return 1;
    case EV::suppress_gate: e_suppressed_gate_.add(); return 1;
    case EV::suppress_gate_small: e_suppressed_gate_small_.add(); return 1;
    case EV::suppress_resolved: e_suppressed_.add(); return 1;
    case EV::pass_no_autopilot: case EV::pass_not_steerable: case EV::pass_class: case EV::pass_b_dock_leg: case EV::pass_own_leader: {
        e_small_pass_.add();
        const unsigned k = v == EV::pass_no_autopilot ? unsigned(av::SmallPair::no_autopilot) : v == EV::pass_not_steerable ? unsigned(av::SmallPair::not_steerable)
                           : v == EV::pass_class ? unsigned(av::SmallPair::class_masked) : v == EV::pass_b_dock_leg ? unsigned(av::SmallPair::b_dock_leg)
                                                                                     : unsigned(av::SmallPair::own_leader);
        e_small_pass_reason_[k].add();
        return 0;
    }
    default: return 0;
    }
}
bool straddles(std::uintptr_t at, unsigned n) {
    return (at & 7u) + n > 8u;
}
const char* write_name(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
bool windows_ok(unsigned part) {
    return !x3m::fl::seta_windows_fl::plan(
        FA::windows, part, base_,
        [](std::uintptr_t at, void* out, unsigned n) { return engine_patch::read_code(at, static_cast<unsigned char*>(out), n); },
        &failed_window_);
}
const char* claim_e() {
    if (!engine_patch::install_window_open()) return "late_claim";
    if (!windows_ok(3)) return "window_mismatch";
    engine_patch::SiteSpec spec{};
    spec.name = FA::warn_name;
    spec.address = base_ + FA::warn_rva;
    spec.length = FA::warn_length;
    spec.ret_pop = FA::warn_ret_pop;
    std::memcpy(spec.expected, FA::warn_bytes, FA::warn_length);
    warn_site_ = engine_patch::Site{};
    if (!engine_patch::claim(warn_site_, spec)) return warn_site_.status;
    unsigned char now[5]{};
    const bool read = engine_patch::read_code(spec.address, now, 5);
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
    if (fixture_fault_e_) {
        fixture_fault_e_ = false;
        now[1] ^= 0xff;
    }
#endif
    const char* why = nullptr;
    if (!read || std::memcmp(now, warn_site_.patched, 5))
        why = "readback_failed";
    else if (warn_site_.atomic_write == straddles(spec.address, 5))
        why = "write_path_mismatch";
    else {
        x3m_fl_ship_avoid_next_e = *warn_site_.entry;
        std::atomic_thread_fence(std::memory_order_release);
        if (!engine_patch::push_front(warn_site_, reinterpret_cast<void*>(&x3m_fl_ship_avoid_e_stub))) why = "stub_chain_failed";
    }
    if (!why) return nullptr;
    if (!engine_patch::restore(warn_site_)) return "rollback_failed_live_site_passes_through";
    warn_site_ = engine_patch::Site{};
    return why;
}
} // namespace

extern "C" __attribute__((force_align_arg_pointer)) int __cdecl x3m_fl_ship_avoid_suppress(std::uint32_t a, std::uint32_t b) {
    if (!installed_.load(std::memory_order_relaxed) || !warn_live_.load(std::memory_order_relaxed)) return 0;
    Envelope env;
    e_calls_.add();
    return e_verdict(a, b, game_time_ms());
}

namespace x3m_fl_ship_steering_rt {
// The avoidance adjustment of the order's point for one steering call (av::resolve over the sector snapshot): the AP
// avoid_adjust with the dock and traffic rows off.
void avoid_adjust(std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t code, unsigned slot,
                  std::uint32_t now, float P[3], std::uint32_t* v_emergency_out) noexcept {
    const std::uint32_t sector = rd32(obj + F::obj_sector);
    av_calls_.add();
    if (!sector) return;
    const av::Snapshot* snap = snapshot_for(sector, now);
    const std::uint32_t fl = rd32(obj + F::obj_flight);
    av::Self self;
    for (unsigned c = 0; c < 3; ++c) {
        self.x[c] = in.x[c];
        self.f[c] = in.row[2][c];
        self.slip[c] = in.slip[c];
    }
    self.up[0] = 0.0f;
    self.up[1] = 1.0f;
    self.up[2] = 0.0f;
    self.v = in.v;
    self.r = dyn.r_own;
    self.a = dyn.a_lin;
    self.omega = core::fmin_f(dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f, dyn.W[1] > 0.0f ? dyn.W[1] : 1e30f);
    if (self.omega >= 1e29f) self.omega = 0.0f;
    self.alpha = self.omega * dyn.k;
    self.ptr = obj;
    self.target = rd32(fl + F::flight_target);
    self.leader = (in.order == core::kOrderEscort || in.order == core::kOrderFollow) ? self.target : 0;
    self.leader_x[0] = self.leader_x[1] = self.leader_x[2] = 0.0f;
    self.L_leader = 0.0f;
    if (self.leader) {
        const core::Ship* ls = table_.find(self.leader);
        if (ls) {
            const unsigned li = unsigned(ls - table_.entries);
            const av::ShipAvoid& la = avoid_[li];
            // a fresh decision of the same leader object only (the age scaled with the frame step)
            if (la.obj == self.leader && table_id_[li] == rd32(self.leader + F::obj_id) && now - la.stamp_ms <= step_window_ms(F::clamp_max_age_ms))
                self.L_leader = la.L;
        }
    }
    self.order = in.order;
    self.capital = code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
    self.exclude_target = in.order == core::kOrderFlyBefore || in.order == core::kOrderDock || in.order == core::kOrderEscort ||
                          in.order == core::kOrderFollow || in.order == core::kOrderAttack || in.order == core::kOrderAttackBig ||
                          in.order == core::kOrderCapture;
    av::ShipAvoid& state = avoid_[slot];
    if (state.obj != obj) state = av::ShipAvoid{};
    state.obj = obj;
    const bool was_unresolved = state.unresolved;
    const av::Result r = av::resolve(*snap, part_cache_.sets, part_cache_.count, self, P, state, now);
    for (unsigned i = 0; i < state.n_resolved && i < av::kMaxResolved; ++i) // this call's snapshot objects: live now
        resolved_id_[slot][i] = rd32(state.resolved[i] + F::obj_id);
    if (r.conflicts) av_conflicts_.add(r.conflicts);
    if (r.unresolved) {
        av_unresolved_.add();
        av_unresolved_why_[r.reason < unsigned(av::Unresolved::count) ? r.reason : 0u].add();
    }
    const bool conflict_now = r.conflicts > 0;
    if (conflict_now && !state.in_conflict) av_conflict_episodes_.add();
    if (r.unresolved && !was_unresolved) av_unresolved_episodes_.add();
    state.in_conflict = conflict_now;
    if (r.emergency) av_emergency_.add();
    if (r.excluded_followers) avoid_excluded_followers_.add(r.excluded_followers);
    if (r.skipped_small) avoid_skipped_small_.add(r.skipped_small);
    if (r.excluded_members) avoid_excluded_member_.add(r.excluded_members);
    std::memcpy(v_emergency_out, &state.v_emergency, 4);
    if (!r.adjusted) return;
    av_detours_.add();
    P[0] = r.P[0];
    P[1] = r.P[1];
    P[2] = r.P[2];
    if (debug_) {
        const std::uint32_t w = detour_write_.load(std::memory_order_relaxed);
        if (w - detour_read_ < 16) {
            DetourEvent& e = detours_[w & 15];
            e.obj = obj;
            e.obstacle = r.obstacle;
            e.kind = r.obstacle_kind;
            e.side = std::uint32_t(std::int32_t(r.side));
            e.vertical = r.vertical ? 1 : 0;
            e.order = in.order;
            e.obstacle_leader = r.obstacle_leader;
            e.obstacle_order = r.obstacle_order;
            e.t_cpa = r.t_cpa;
            e.miss = r.miss;
            e.C = r.C;
            e.L = r.L;
            for (unsigned c = 0; c < 3; ++c) {
                e.P[c] = r.P[c];
                e.x[c] = in.x[c];
            }
            detour_write_.store(w + 1, std::memory_order_release);
        }
    }
}
void avoidance_initialize() noexcept {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_SHIP_AVOIDANCE", text, 8);
    bool on = true;
    if (length && (length >= 8 || !core::parse_switch(text, length, &on))) {
        x3m::log("ship_avoidance_refused reason=invalid_setting edition=fl");
        avoid_state_ = "invalid_setting";
        avoidance_on_ = false;
        return;
    }
    if (!on) {
        avoid_state_ = "off";
        avoidance_on_ = false;
        return;
    }
    failed_window_ = FA::window_count;
    if (!windows_ok(2)) { // the snapshot's native contracts: no target adjustment, the native warn untouched
        avoid_state_ = "window_mismatch";
        avoidance_on_ = false;
        x3m::log("ship_avoidance_refused reason=window_mismatch edition=fl base=0x%08lx window=%s", static_cast<unsigned long>(base_),
                 failed_window_ < FA::window_count ? FA::windows[failed_window_].name : "-");
        return;
    }
    avoid_state_ = "active";
    avoidance_on_ = true;
    failed_window_ = FA::window_count;
    if (const char* why = claim_e())
        warn_state_ = why;
    else {
        warn_state_ = "active";
        warn_live_.store(true, std::memory_order_release);
    }
    x3m::log("ship_avoidance installed=1 edition=fl warn_site=0x%08lx warn_rva=0x%06lx warn_status=%s write=%s window=%s "
             "snapshot_capacity=%u snapshot_slots=%u station_capacity=%u parts_per_station=%u candidates=%u resolved_age_ms=max(%lu,4*step) "
             "handler_e=0x%08lx",
             static_cast<unsigned long>(base_ + FA::warn_rva), static_cast<unsigned long>(FA::warn_rva), warn_state_, write_name(warn_site_),
             failed_window_ < FA::window_count ? FA::windows[failed_window_].name : "-", av::kSnapshotCapacity, FA::snapshot_slots,
             av::kStationCapacity, av::kMaxParts, av::kMaxCandidates, static_cast<unsigned long>(av::kResolvedAgeMs),
             static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_fl_ship_avoid_suppress)));
}
bool avoidance_shutdown() noexcept {
    warn_live_.store(false, std::memory_order_release);
    if (!warn_site_.patched_in) return true;
    if (!engine_patch::restore(warn_site_)) return false;
    warn_site_ = engine_patch::Site{};
    return true;
}
// --debug: the census rows (once per sector per session) and the detour samples (at most 32 rows per second).
void avoidance_rows(unsigned long long frame) noexcept {
    for (std::uint32_t w = census_write_.load(std::memory_order_acquire); census_read_ != w; ++census_read_) {
        const Census& c = census_[census_read_ & 3];
        char classes[256], buckets[256];
        int n = 0;
        classes[0] = 0;
        for (unsigned k = 0; k < 32; ++k)
            if (c.classes[k]) {
                const int m = std::snprintf(classes + n, sizeof classes - size_t(n), "%s%u:%u", n ? "," : "", k, unsigned(c.classes[k]));
                if (m > 0 && n + m < int(sizeof classes)) n += m;
            }
        n = 0;
        for (unsigned k = 0; k < 32; ++k) {
            const int m = std::snprintf(buckets + n, sizeof buckets - size_t(n), "%s%u", k ? "," : "", unsigned(c.buckets[k]));
            if (m > 0 && n + m < int(sizeof buckets)) n += m;
        }
        float root_r;
        std::memcpy(&root_r, &c.station_root_r, 4);
        x3m::log("ship_avoid_census frame=%llu edition=fl sector=0x%08lx objects=%lu overflow=%lu classes=%s buckets=%s", frame,
                 static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.objects), static_cast<unsigned long>(c.overflow),
                 classes, buckets);
        if (c.station)
            x3m::log("ship_avoid_station frame=%llu edition=fl sector=0x%08lx obj=0x%08lx root_r_m=%.1f node_scale_0x70=0x%08lx "
                     "own_r_units=%lu children=%lu unfilled=%lu scaled=%lu extent_units=%lu model=%s",
                     frame, static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.station), double(root_r),
                     static_cast<unsigned long>(c.station_scale), static_cast<unsigned long>(c.station_own_r),
                     static_cast<unsigned long>(c.station_children), static_cast<unsigned long>(c.station_dirty),
                     static_cast<unsigned long>(c.station_scaled), static_cast<unsigned long>(c.station_extent),
                     c.station_valid ? "parts" : "fallback");
    }
    const unsigned long long now = GetTickCount64();
    if (now - last_detour_tick_ >= 1000) {
        last_detour_tick_ = now;
        detour_rows_this_second_ = 0;
    }
    for (std::uint32_t w = detour_write_.load(std::memory_order_acquire); detour_read_ != w; ++detour_read_) {
        if (detour_rows_this_second_ >= 32) continue;
        ++detour_rows_this_second_;
        const DetourEvent& e = detours_[detour_read_ & 15];
        x3m::log("ship_avoid_detour frame=%llu edition=fl obj=0x%08lx order=0x%lx obstacle=0x%08lx kind=%lu side=%ld vertical=%lu "
                 "t_cpa_s=%.2f miss_m=%.1f C_m=%.1f L_m=%.0f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f leader=0x%08lx obstacle_order=0x%lx",
                 frame, static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.order), static_cast<unsigned long>(e.obstacle),
                 static_cast<unsigned long>(e.kind), long(std::int32_t(e.side)), static_cast<unsigned long>(e.vertical), double(e.t_cpa),
                 double(e.miss), double(e.C), double(e.L), double(e.P[0]), double(e.P[1]), double(e.P[2]), double(e.x[0]),
                 double(e.x[1]), double(e.x[2]), static_cast<unsigned long>(e.obstacle_leader), static_cast<unsigned long>(e.obstacle_order));
    }
}
// The summary row's avoidance text (the AP fields of the base group; per-interval deltas, then totals since install).
void avoidance_summary(char* out, unsigned size) noexcept {
    const Counter* cs[13] = {&av_calls_, &av_conflicts_, &av_detours_, &av_unresolved_, &av_overflow_, &av_snapshots_, &av_walk_objects_,
                             &av_emergency_, &e_calls_, &e_suppressed_, &part_built_, &part_fallback_, &part_invalid_};
    std::uint32_t d[13];
    for (unsigned i = 0; i < 13; ++i) {
        const std::uint32_t v = cs[i]->get();
        d[i] = v - rep_av_[i];
        rep_av_[i] = v;
    }
    unsigned snap_max = 0;
    for (unsigned i = 0; i < FA::snapshot_slots; ++i)
        if (snaps_[i].count > snap_max) snap_max = snaps_[i].count;
    std::uint32_t why[unsigned(av::Unresolved::count)];
    for (unsigned i = 0; i < unsigned(av::Unresolved::count); ++i) {
        const std::uint32_t v = av_unresolved_why_[i].get();
        why[i] = v - rep_av_why_[i];
        rep_av_why_[i] = v;
    }
    const Counter* ep[4] = {&av_conflict_episodes_, &av_unresolved_episodes_, &part_dirty_, &part_scaled_};
    std::uint32_t e[4];
    for (unsigned i = 0; i < 4; ++i) {
        const std::uint32_t v = ep[i]->get();
        e[i] = v - rep_av_ep_[i];
        rep_av_ep_[i] = v;
    }
    std::uint32_t er_small[unsigned(av::SmallPair::count)];
    for (unsigned i = 0; i < unsigned(av::SmallPair::count); ++i) er_small[i] = e_small_pass_reason_[i].get();
    const av::MemberStats& ms = member_stats_;
    std::snprintf(out, size,
                  " avoid_calls=%lu conflicts=%lu conflict_episodes=%lu detours=%lu unresolved=%lu unresolved_episodes=%lu "
                  "unresolved_inside_stop=%lu unresolved_inside_turn=%lu unresolved_blocked=%lu unresolved_behind=%lu unresolved_degenerate=%lu "
                  "snapshot_overflow=%lu snapshots=%lu walked=%lu emergency=%lu e_calls=%lu e_suppressed=%lu part_models=%lu "
                  "part_fallback=%lu part_invalid=%lu part_unfilled=%lu part_scaled=%lu snapshot_max=%u warn=%s "
                  "e_suppressed_small=%lu e_small_pass=%lu e_small_pass_no_autopilot=%lu e_small_pass_not_steerable=%lu e_small_pass_class=%lu "
                  "e_small_pass_b_dock_leg=%lu e_small_pass_own_leader=%lu e_suppressed_member=%lu avoid_excluded_member=%lu member_set=%u "
                  "member_refresh=%u member_replace=%u member_expire=%u member_leader_gone=%u member_id_mismatch=%u member_reversed=%u "
                  "member_player=%u member_cleared=%u member_full=%u e_small_gap=%lu e_recycled_a=%lu e_resolved_id_mismatch=%lu "
                  "part_pinned_full=%lu",
                  static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(e[0]),
                  static_cast<unsigned long>(d[2]), static_cast<unsigned long>(d[3]), static_cast<unsigned long>(e[1]),
                  static_cast<unsigned long>(why[1]), static_cast<unsigned long>(why[2]), static_cast<unsigned long>(why[3]),
                  static_cast<unsigned long>(why[4]), static_cast<unsigned long>(why[5]), static_cast<unsigned long>(d[4]),
                  static_cast<unsigned long>(d[5]), static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]),
                  static_cast<unsigned long>(d[8]), static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]),
                  static_cast<unsigned long>(d[11]), static_cast<unsigned long>(d[12]), static_cast<unsigned long>(e[2]),
                  static_cast<unsigned long>(e[3]), snap_max, warn_state_, static_cast<unsigned long>(e_suppressed_small_.get()),
                  static_cast<unsigned long>(e_small_pass_.get()), static_cast<unsigned long>(er_small[1]), static_cast<unsigned long>(er_small[2]),
                  static_cast<unsigned long>(er_small[3]), static_cast<unsigned long>(er_small[4]), static_cast<unsigned long>(er_small[5]),
                  static_cast<unsigned long>(e_suppressed_member_.get()), static_cast<unsigned long>(avoid_excluded_member_.get()), ms.set,
                  ms.refresh, ms.replace, ms.expire, ms.leader_gone, ms.id_mismatch, ms.reversed, ms.player, ms.cleared, ms.full,
                  static_cast<unsigned long>(e_small_gap_.get()), static_cast<unsigned long>(e_recycled_a_.get()),
                  static_cast<unsigned long>(e_resolved_id_mismatch_.get()), static_cast<unsigned long>(part_pinned_full_.get()));
}
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
void fixture_fault_e() {
    fixture_fault_e_ = true;
}
void fixture_avoidance_reset() {
    warn_live_.store(false);
    warn_state_ = "not_claimed";
    avoid_state_ = "off";
    avoidance_on_ = false;
    for (av::Snapshot& s : snaps_) s = av::Snapshot{};
    part_cache_ = wk::PartCache{};
    members_ = av::Members{};
}
#endif
} // namespace x3m_fl_ship_steering_rt
