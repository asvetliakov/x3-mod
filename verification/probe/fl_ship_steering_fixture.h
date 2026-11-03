#pragma once
// Shared by the two FL ship-steering native fixtures (fl_ship_steering.cpp, fl_ship_avoidance.cpp): the mapped and relocated
// X3FL.exe (fl_seta_image.h), the CPU-state harness (fl_seta_harness.h), one fixed set of engine objects in a private block (the
// steered ship A, one obstacle B, A's order target, the TShips records, one sector with its 32 buckets, the game clock), the calls
// through the claimed stubs, and the reference: the shared AP cores (core::steer, core::clamp_for, wk::fill_snapshot, av::resolve,
// av::site_e_verdict) called directly with inputs built the way the AP glue builds them (src/proxy/ship_steering_decide.cpp), so a
// difference isolates the FL binding layer. The inputs come from existing tables: the ship_trace rows the engine-model harness
// emits (verification/probe/ship_steering_host.cpp --emit-trace) and the harness's avoidance unit-check cases.
#include "fl_seta_image.h"
#include "fl_seta_harness.h"
#include "../../src/engine/fl/ship_steering_fl_internal.h"
#include "../../src/proxy/engine_patch.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace F = x3m::fl::ship_steering_fl;
namespace FA = x3m::fl::ship_avoidance_fl;
namespace core = x3m::ship_steering::core;
namespace av = x3m::ship_avoidance::core;
namespace wk = x3m::ship_avoidance::walk;
namespace rt = x3m_fl_ship_steering_rt;
namespace ss = x3m::ship_steering;
using std::uint32_t;
using std::int32_t;
namespace x3m::ship_steering {
void fixture_set_base(std::uintptr_t base);
void fixture_fault_claim(unsigned index);
void fixture_reset();
}
namespace x3m_fl_ship_steering_rt {
void fixture_fault_e();
void fixture_avoidance_reset();
}

unsigned checks = 0, failures = 0;
inline void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        if (failures < 60) std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[2048];
bool quiet = false;
unsigned rows_ap_only = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    if (std::strstr(last_row, "status=ap_only reason=not_ported")) ++rows_ap_only;
    if (!quiet) std::printf("LOG %s\n", last_row);
}
namespace fl {
std::uintptr_t module_base() {
    return 0; // the fixture binds through fixture_set_base
}
}
}

FlImage im;
// ---- the object block ----
constexpr uint32_t kObjA = 0x0000, kObjB = 0x2000, kObjTarget = 0x4000, kFlightOff = 0x400, kPhysOff = 0x1400;
constexpr uint32_t kRecords = 0x10000, kSector = 0x20000, kBucketTable = 0x20100, kSentinels = 0x20400, kClock = 0x21000,
                   kPoint = 0x22000, kOut = 0x22100, kChildBase = 0x23000;
unsigned char* blk = nullptr;
inline uint32_t va(uint32_t off) { return uint32_t(reinterpret_cast<std::uintptr_t>(blk + off)); }
inline void put(uint32_t at, uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &v, 4); }
inline void puts32(uint32_t at, int32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &v, 4); }
inline void put16(uint32_t at, std::uint16_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(at)), &v, 2); }
inline uint32_t get(uint32_t at) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(at)), 4);
    return v;
}
inline uint32_t obj(uint32_t off) { return va(off); }
inline uint32_t flight(uint32_t off) { return va(off + kFlightOff); }
inline uint32_t phys(uint32_t off) { return va(off + kPhysOff); }
inline uint32_t record(unsigned type) { return va(kRecords + type * F::record_stride); }
bool map_block() {
    blk = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x40000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!blk) return false;
    im.u32(F::ship_table_slot_rva) = va(kRecords);
    im.u32(F::clock_slot_rva) = va(kClock);
    im.pristine.assign(im.base, im.base + im.size); // the two slots are runtime state, not windows
    return true;
}
inline void set_clock(uint32_t t_ms, uint32_t step_ms) {
    put(va(kClock) + F::clock_game_time, t_ms);
    put(va(kClock) + F::clock_frame_step, step_ms);
}
// The engine getters' inputs for one ship (the real FL getters run on them): max speed through the flight override +0x27c, the
// caps and the acceleration straight from the record (no flight tuning), the record's class code and rotation acceleration.
struct Plant {
    int32_t W[3];  // yaw, pitch, roll caps, binary angle/s
    int32_t vmax, acc, rot, rot_pct, radius;
    uint32_t code;
};
inline void set_record(unsigned type, const Plant& p) {
    const uint32_t r = record(type);
    std::memset(reinterpret_cast<void*>(std::uintptr_t(r)), 0, F::record_stride);
    put(r + F::record_class_code, p.code);
    puts32(r + 8, p.W[0]);    // yaw cap
    puts32(r + 0xc, p.W[1]);  // pitch cap
    puts32(r + 0x10, p.W[2]); // roll cap
    puts32(r + 0x44, p.vmax);
    puts32(r + 0x48, p.acc);
    puts32(r + F::record_rot_accel, p.rot_pct);
}
// One ship (class 7, autopilot, the type's record), its flight and phys, at `off`; basis rows 16.16.
inline void set_ship(uint32_t off, unsigned type, const Plant& p, const int32_t pos[3], const int32_t rows[3][3], int32_t v,
                     const int32_t rate[3], const int32_t slip[3], std::uint16_t order, uint32_t target, int32_t distance, int32_t port,
                     uint32_t id) {
    const uint32_t o = obj(off), f = flight(off), ph = phys(off);
    std::memset(reinterpret_cast<void*>(std::uintptr_t(o)), 0, 0x1600);
    put(o + F::obj_id, id);
    puts32(o + F::obj_speed, v);
    for (int i = 0; i < 3; ++i) {
        puts32(o + F::obj_rates + 4 * i, rate[i]);
        puts32(o + F::obj_slip + 4 * i, slip[i]);
        puts32(ph + F::phys_position + 4 * i, pos[i]);
    }
    put(o + F::obj_flags, F::flag_autopilot | 0x40000000u); // 0x40000000: the max-speed getter skips its equipment path
    put16(o + F::obj_class, F::class_ship);
    put16(o + F::obj_type, std::uint16_t(type));
    put(o + F::obj_flight, f);
    put(o + F::obj_sector, va(kSector));
    put(o + F::obj_phys, ph);
    puts32(o + F::obj_radius, p.radius);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            puts32(f + F::flight_basis + 0x10 * r + 4 * c, rows[r][c]);
            puts32(ph + F::phys_basis + 0x10 * r + 4 * c, rows[r][c]);
        }
    put16(f + F::flight_order, order);
    put(f + F::flight_target, target);
    puts32(f + F::flight_distance, distance);
    puts32(f + F::flight_port_index, port);
    puts32(f + F::flight_rot_accel, p.rot);
    puts32(f + 0x27c, p.vmax); // the max-speed override the getter returns as is
}
// The sector: 32 buckets, each a sentinel-only list; link(off, class) puts an object at the head of its class bucket.
inline void reset_sector() {
    std::memset(blk + kSector, 0, 0x1000);
    put(va(kSector) + 0x50, va(kBucketTable));
    for (unsigned k = 0; k < 32; ++k) put(va(kBucketTable) + 0xc * k, va(kSentinels + 0x10 * k));
}
inline void link(uint32_t off, unsigned cls) {
    const uint32_t o = obj(off), head = va(kBucketTable) + 0xc * cls;
    put(o, get(head));
    put(head, o);
}

// ---- calls through the claimed stubs (fl_seta_harness.h: every GPR, EFLAGS, XMM0-7, x87, MXCSR, ESP skew) ----
// The return address of A and E: `lea esp,[esp-0x40]; jmp h_record` (no flag written), so h_record's own PUSHFD lands below the
// stack argument the stub may have redirected; a returned call records ESP = entry + pop - 0x40.
constexpr uint32_t kLanding = 0x40;
void* landing_ = nullptr;
inline uint32_t landing() {
    if (!landing_) {
        auto* p = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        const unsigned char lea[4] = {0x8d, 0x64, 0x24, 0xc0};
        std::memcpy(p, lea, 4);
        const int32_t rel = int32_t(reinterpret_cast<std::uintptr_t>(&h_record) - (reinterpret_cast<std::uintptr_t>(p) + 9));
        p[4] = 0xe9;
        std::memcpy(p + 5, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), p, 9);
        landing_ = p;
    }
    return uint32_t(reinterpret_cast<std::uintptr_t>(landing_));
}
inline uint32_t stack_top(unsigned skew) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(h_stack + sizeof h_stack - 0x400 - skew));
}
// A at `leaf` for obj with the target buffer at `target`: 1 handled (`ret 4`), 0 the claim tail (its continuation carries
// `jmp h_record`). *arg = the stack argument after the call (the redirected point when it changed).
inline int call_a(unsigned leaf, uint32_t o, uint32_t target, unsigned skew, uint32_t* arg, DWORD* error_after) {
    const uint32_t esp = stack_top(skew);
    put(esp, landing());
    put(esp + 4, target);
    h_in.ecx = o;
    h_run(im.at(F::spans[leaf].rva), skew, 0x5150, error_after);
    *arg = get(esp + 4);
    return h_out.esp == esp + 8 - kLanding ? 1 : 0;
}
inline void call_b(uint32_t o, unsigned skew, DWORD* error_after) {
    h_in.ebx = o;
    h_run(im.at(F::spans[F::loop_continue].rva), skew, 0x5151, error_after);
}
// E for (A, B): 1 suppressed (EAX = 0, `ret 0xc`), 0 the claim tail.
inline int call_e(uint32_t a, uint32_t b, unsigned skew, DWORD* error_after) {
    const uint32_t esp = stack_top(skew);
    put(esp, landing());
    put(esp + 4, b);
    put(esp + 8, 12345);
    put(esp + 12, va(kOut));
    h_in.ecx = a;
    h_run(im.at(FA::warn_rva), skew, 0x5152, error_after);
    return h_out.esp == esp + 16 - kLanding && h_out.eax == 0 ? 1 : 0;
}

// ---- the reference: the shared cores with the AP glue's input construction ----
inline float fixed16(int32_t v) { return float(v) * (1.0f / 65536.0f); }
struct RefIn { // the raw scalars of one A call, as the AP glue reads them (getters already evaluated)
    Plant p;
    int32_t pos[3], rows[3][3], v, rate[3], slip[3], point[3], distance, port;
    std::uint16_t order, tgt_class;
    int32_t tgt_radius;
    bool has_tgt;
};
inline void ref_dynamics(const RefIn& r, core::Dynamics& dyn) {
    for (int i = 0; i < 3; ++i) dyn.W[i] = float(r.p.W[i]) * core::kRadPerBinary;
    for (float& w : dyn.W)
        if (w < 0.0f) w = 0.0f;
    dyn.k = r.p.rot_pct > 0 ? fixed16(r.p.rot) * float(r.p.rot_pct) * 0.01f : 0.0f;
    dyn.v_max = float(r.p.vmax) / core::kUnitsPerMetre;
    dyn.a_lin = float(r.p.acc) / core::kUnitsPerMetre;
    if (dyn.a_lin < 0.0f) dyn.a_lin = 0.0f;
    dyn.r_own = float(r.p.radius) / core::kUnitsPerMetre;
    dyn.large = r.p.code < core::kClassCount && ((core::kLargeMask >> r.p.code) & 1u);
}
inline void ref_inputs(const RefIn& r, const core::Dynamics& dyn, core::Inputs& in) {
    for (int i = 0; i < 3; ++i) in.x[i] = float(r.pos[i]) / core::kUnitsPerMetre;
    for (int a = 0; a < 3; ++a)
        for (int c = 0; c < 3; ++c) in.row[a][c] = fixed16(r.rows[a][c]);
    in.v = float(r.v) / core::kUnitsPerMetre;
    for (int i = 0; i < 3; ++i) in.w[i] = float(r.rate[i]) * core::kRadPerBinary;
    for (int i = 0; i < 3; ++i) in.slip[i] = float(r.slip[i]) / core::kUnitsPerMetre;
    if (r.order == core::kOrderDock || !core::slip_usable(in.slip, dyn.v_max)) in.slip[0] = in.slip[1] = in.slip[2] = 0.0f;
    for (int i = 0; i < 3; ++i) in.P[i] = float(r.point[i]) / core::kUnitsPerMetre;
    in.order = r.order;
}
// The AP decide()'s arrival parameters and port leg for these inputs (no avoidance detour: P is the order's point).
inline void ref_arrival(const RefIn& r, const core::Dynamics& dyn, const core::Ship& s, uint32_t now, uint32_t hold_window,
                        core::Inputs& in) {
    const core::OrderPolicy policy = core::order_policy(r.order);
    in.arrival_ramp = policy.arrival_ramp;
    if (in.arrival_ramp) {
        if (r.order == core::kOrderFlyBefore) {
            int32_t units;
            if (r.has_tgt && r.tgt_class == F::class_ship)
                units = 111000;
            else if (r.has_tgt && r.tgt_class == F::class_gate)
                units = r.tgt_radius / 4 * 3;
            else {
                units = (dyn.large ? 1 : 3) * r.p.radius;
                if (units < 22200) units = 22200;
                if (units > 111000) units = 111000;
            }
            in.d_arr = float(units) / core::kUnitsPerMetre;
        } else {
            in.d_arr = float(r.distance) / core::kUnitsPerMetre;
            if (r.order == core::kOrderEscort) in.d_arr = in.d_arr > dyn.r_own ? in.d_arr : dyn.r_own;
        }
        if (in.d_arr < 0.0f) in.d_arr = 0.0f;
    }
    if (r.order == core::kOrderDock) {
        in.port_leg = r.has_tgt && r.tgt_class != F::class_gate && r.port >= 0;
        in.port_hold = in.port_leg && s.order == core::kOrderDock && (s.flags & core::kShipFlagPortHold) && now - s.stamp_ms <= hold_window;
    }
}
// The AP decide()'s bookkeeping after a handled core::steer (no gate, traffic or dock branch): the Ship entry as site B reads it.
inline void ref_record(core::Ship& s, const core::Inputs& in, const core::Dynamics& dyn, const core::Command& cmd, uint32_t now) {
    s.stamp_ms = now;
    s.order = in.order;
    const std::uint16_t speed_flags =
        std::uint16_t((in.arrival_ramp && !cmd.formation_speed ? core::kShipFlagRamp : 0) | (cmd.v_cmd > 0.0f ? core::kShipFlagClamp : 0));
    s.flags = std::uint16_t(speed_flags | (cmd.port_aligned ? core::kShipFlagPortHold : 0));
    if (in.order == core::kOrderMovePos && (s.P[0] != in.P[0] || s.P[1] != in.P[1] || s.P[2] != in.P[2])) s.best_d = -1.0f;
    for (int i = 0; i < 3; ++i) s.P[i] = in.P[i];
    s.a_lin = dyn.a_lin;
    s.d_arr = in.d_arr;
    s.v_ref = in.v_ref;
    s.v_corner = cmd.formation_speed ? cmd.v_cmd : cmd.v_corner;
    s.v_slip = cmd.formation_speed ? 0.0f : cmd.v_slip;
    s.v_emergency = 0.0f;
    s.last_call_ms = now;
    if (in.order == core::kOrderMovePos)
        core::stuck_update(s, cmd.d_order, now);
    else
        s.best_d = -1.0f;
}
inline const core::Ship* fl_ship(uint32_t o) { return rt::table_.find(o); }
inline void reset_fl_state() {
    rt::table_ = core::ShipTable{};
    for (uint32_t& id : rt::table_id_) id = 0;
    for (av::ShipAvoid& a : rt::avoid_) a = av::ShipAvoid{};
    for (av::Snapshot& s : rt::snaps_) s = av::Snapshot{};
    rt::part_cache_ = wk::PartCache{};
    rt::members_ = av::Members{};
}
// The comparison of every CPU-state field (fl_seta_harness.h) between a run and the expected image.
inline bool same_cpu(const HCpu& a, const HCpu& b, const char* label) {
    const bool ok = a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edx == b.edx && a.esi == b.esi && a.edi == b.edi &&
                    a.ebp == b.ebp && a.esp == b.esp && a.flags == b.flags && a.fpucw == b.fpucw &&
                    ((a.fpusw >> 11) & 7) == ((b.fpusw >> 11) & 7) && a.st0 == b.st0 && a.mxcsr == b.mxcsr &&
                    !std::memcmp(a.xmm, b.xmm, sizeof a.xmm);
    if (!ok) {
        char d[400];
        std::snprintf(d, sizeof d, "eax %08x/%08x ecx %08x/%08x edx %08x/%08x esp %08x/%08x fl %08x/%08x cw %04x/%04x mx %08x/%08x xmm %d",
                      a.eax, b.eax, a.ecx, b.ecx, a.edx, b.edx, a.esp, b.esp, a.flags, b.flags, a.fpucw, b.fpucw, a.mxcsr, b.mxcsr,
                      std::memcmp(a.xmm, b.xmm, sizeof a.xmm) != 0);
        check(false, label, d);
    }
    return ok;
}
