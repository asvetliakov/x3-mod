// Native fixture of the FL ship avoidance (src/engine/fl/ship_avoidance_fl.cpp, ship_avoidance_sites_fl.h;
// docs/reverse-engineering/fl/ship-avoidance.md, "Port (2026-11-03)"): built with -DX3M_EDITION_FL=1 -DX3M_SHIP_STEERING_FL_FIXTURE by
// verification/analysis/test_fl_ship_avoidance.py and run natively through WSL interop on the mapped, relocated X3FL.exe (nothing is
// written to the file). The cases are the engine-model harness's avoidance unit checks (verification/probe/ship_steering_host.cpp
// avoidance_unit_checks: the head-on asteroid with and without the slip that swings the velocity clear, the station fallback, the
// M5 head-on ship at 45..400 m, the capital / small-ship row of the E checks) and its walk check's station tree (scaled, unscaled and
// unfilled children), laid into the FL fields; the asteroid case is also swept over lateral offsets and slips. The reference is the
// shared cores (wk::fill_snapshot, av::resolve, av::suppressed, av::site_e_verdict) called directly with the AP glue's inputs.
//
// Covered: the avoidance and E windows refused byte by byte (a snapshot window turns the avoidance off, an E window E alone); E's
// read-back fault rolled back; the snapshot, the detour point (the leaf's redirected stack argument; the caller's buffer untouched)
// and E's verdict against the reference; the property that a suppressed pair's detour path keeps the hulls apart; the station
// layout against the native subtree-radius getter; E through the three real callers' bytes; the E stub's CPU state at every skew;
// a recycled A or B never answered from the old decision; the E window under SETA steps.
#include "fl_ship_steering_fixture.h"

// ---- the harness's SectorBuilder over the fixture's block (plain memory) ----
uint32_t next_ = kChildBase;
uint32_t alloc(uint32_t size) {
    const uint32_t a = va(next_);
    next_ += (size + 0xf) & ~0xfu;
    std::memset(reinterpret_cast<void*>(std::uintptr_t(a)), 0, size);
    return a;
}
uint32_t child(uint32_t parent, const int32_t off[3], int32_t sub_r, bool scaled, int32_t own_r = 0, int32_t scale = 0x10000) {
    const uint32_t c = alloc(0x200);
    for (int i = 0; i < 3; ++i) puts32(c + 0x30 + 4 * i, off[i]);
    puts32(c + 0xa4, sub_r);
    puts32(c + 0xa0, own_r);
    puts32(c + 0x70, scale);
    put(c + 0x12c, scaled ? 0x40 : 0);
    put(c + 0xc, alloc(0x10)); // its own (empty) children list: the native getter reads the sentinel
    if (get(parent + 0xc) == 0) put(parent + 0xc, alloc(0x10));
    put(c, get(parent + 0xc));
    put(parent + 0xc, c);
    return c;
}
// An obstacle object at kObjB: class, position (units), radius, its node (identity basis) and, for ships, a flight block.
void obstacle(std::uint16_t cls, const int32_t pos[3], int32_t radius, int32_t own_r, uint32_t id) {
    const uint32_t o = obj(kObjB), ph = phys(kObjB);
    std::memset(reinterpret_cast<void*>(std::uintptr_t(o)), 0, 0x1600);
    put16(o + F::obj_class, cls);
    put(o + F::obj_id, id);
    puts32(o + F::obj_radius, radius);
    put(o + F::obj_phys, ph);
    for (int i = 0; i < 3; ++i) puts32(ph + F::phys_position + 4 * i, pos[i]);
    puts32(ph + 0x40, 0x10000);
    puts32(ph + 0x54, 0x10000);
    puts32(ph + 0x68, 0x10000);
    puts32(ph + 0x70, 0x10000);
    puts32(ph + 0xa0, own_r);
    puts32(ph + 0xa4, radius);
    put(ph + 0xc, alloc(0x10)); // the children list: its sentinel
    link(kObjB, cls);
}
// ---- the steered ship from the harness's Self: f = +z, up = +y, the plant through the record ----
struct Me {
    float v, r, a, omega, alpha, slip[3];
    uint32_t code;
};
Plant plant_of(const Me& m) {
    Plant p{};
    const int32_t w = int32_t(std::lround(double(m.omega) / double(core::kRadPerBinary)));
    p.W[0] = p.W[1] = p.W[2] = w;
    p.vmax = int32_t(std::lround(double(m.v) * 2.0 * 500.0));
    p.acc = int32_t(std::lround(double(m.a) * 500.0));
    p.rot = 0x10000;
    const float k = m.omega > 0.0f ? m.alpha / m.omega : 0.0f;
    p.rot_pct = m.alpha > 0.0f ? int32_t(std::lround(double(k) * 100.0)) : 1;
    if (m.alpha <= 0.0f) p.rot = 0; // k = 0: alpha 0, as the harness's Self
    p.radius = int32_t(std::lround(double(m.r) * 500.0));
    p.code = m.code;
    return p;
}
RefIn self_ref(const Me& m, std::uint16_t order, const int32_t point[3]) {
    RefIn in{};
    in.p = plant_of(m);
    const int32_t rows[3][3] = {{0x10000, 0, 0}, {0, 0x10000, 0}, {0, 0, 0x10000}};
    std::memcpy(in.rows, rows, sizeof rows);
    in.v = int32_t(std::lround(double(m.v) * 500.0));
    for (int i = 0; i < 3; ++i) {
        in.slip[i] = int32_t(std::lround(double(m.slip[i]) * 500.0));
        in.point[i] = point[i];
    }
    in.order = order;
    in.distance = 0;
    in.port = -1;
    return in;
}
void lay_self(const RefIn& in, uint32_t id) {
    set_record(in.p.code, in.p);
    set_ship(kObjA, in.p.code, in.p, in.pos, in.rows, in.v, in.rate, in.slip, in.order, 0, in.distance, in.port, id);
    link(kObjA, F::class_ship);
    for (int i = 0; i < 3; ++i) puts32(va(kPoint) + 4 * i, in.point[i]);
}
// The reference reader: plain loads, the TShips table straight from the block (the AP glue's slot read, here a value).
struct RefMem {
    uint32_t u32(uint32_t at) const { return get(at); }
    int32_t s32(uint32_t at) const { return int32_t(get(at)); }
    std::uint16_t u16(uint32_t at) const {
        std::uint16_t v;
        std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(at)), 2);
        return v;
    }
    uint32_t ship_table() const { return va(kRecords); }
};
av::Snapshot ref_snap;
wk::PartCache ref_cache;
av::Members ref_members;
av::MemberStats ref_mstats;
av::ShipAvoid ref_state;
struct Outcome {
    int handled, suppressed, ref_suppressed;
    bool redirected, same_point, same_snapshot, same_resolved;
    float P[3];
};
// One case: A decided `calls` times one step apart (FL through its stub; the reference through the cores; as the harness's case the
// slip is the case's only on the last call), then E(A, B) at the last decision + age.
Outcome run_case(const RefIn& in_case, uint32_t now0, uint32_t step, uint32_t age, uint32_t e_step, unsigned calls = 1) {
    Outcome o{};
    uint32_t now = now0;
    RefIn in = in_case;
    for (unsigned k = 0; k < calls; ++k) {
    now = now0 + k * step;
    in = in_case;
    if (k + 1 < calls) in.slip[0] = in.slip[1] = in.slip[2] = 0;
    for (int i = 0; i < 3; ++i) puts32(obj(kObjA) + F::obj_slip + 4 * i, in.slip[i]);
    set_clock(now, step);
    h_sentinels(now & 0xff, now & 1);
    uint32_t arg = 0;
    DWORD err = 0;
    o.handled = call_a(0, obj(kObjA), va(kPoint), 0, &arg, &err);
    // the reference snapshot and resolve, as the AP avoid_adjust builds them
    core::Dynamics dyn;
    ref_dynamics(in, dyn);
    core::Inputs ci;
    ref_inputs(in, dyn, ci);
    RefMem mem;
    wk::Stats st;
    wk::fill_snapshot(mem, va(kSector), now, ref_snap, ref_cache, st, nullptr, &ref_members, &ref_mstats);
    av::Self self;
    for (int c = 0; c < 3; ++c) {
        self.x[c] = ci.x[c];
        self.f[c] = ci.row[2][c];
        self.slip[c] = ci.slip[c];
    }
    self.up[1] = 1.0f;
    self.v = ci.v;
    self.r = dyn.r_own;
    self.a = dyn.a_lin;
    self.omega = core::fmin_f(dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f, dyn.W[1] > 0.0f ? dyn.W[1] : 1e30f);
    if (self.omega >= 1e29f) self.omega = 0.0f;
    self.alpha = self.omega * dyn.k;
    self.ptr = obj(kObjA);
    self.order = in.order;
    self.capital = in.p.code < core::kClassCount && ((core::kCapitalMask >> in.p.code) & 1u);
    self.exclude_target = false;
    if (ref_state.obj != obj(kObjA)) ref_state = av::ShipAvoid{};
    ref_state.obj = obj(kObjA);
    const av::Result r = av::resolve(ref_snap, ref_cache.sets, ref_cache.count, self, ci.P, ref_state, now);
    o.redirected = arg != va(kPoint);
    int32_t want[3];
    for (int i = 0; i < 3; ++i) want[i] = r.adjusted ? core::round_to_int(r.P[i] * core::kUnitsPerMetre) : in.point[i];
    const uint32_t seen = o.redirected ? arg : va(kPoint);
    o.same_point = o.redirected == r.adjusted;
    for (int i = 0; i < 3; ++i) {
        o.same_point = o.same_point && int32_t(get(seen + 4 * i)) == want[i] && int32_t(get(va(kPoint) + 4 * i)) == in.point[i];
        o.P[i] = float(want[i]) / core::kUnitsPerMetre;
    }
    const av::Snapshot* fs = nullptr;
    for (const av::Snapshot& s : rt::snaps_)
        if (s.sector == va(kSector) && s.epoch == now) fs = &s;
    o.same_snapshot = fs && fs->count == ref_snap.count && fs->overflow == ref_snap.overflow &&
                      !std::memcmp(fs->items, ref_snap.items, sizeof(av::Obstacle) * ref_snap.count);
    const core::Ship* ship = fl_ship(obj(kObjA));
    const av::ShipAvoid* fa = ship ? &rt::avoid_[unsigned(ship - rt::table_.entries)] : nullptr;
    o.same_resolved = fa && fa->n_resolved == ref_state.n_resolved && !std::memcmp(fa->resolved, ref_state.resolved, sizeof fa->resolved);
    }
    // E at now + age: FL through its stub, the reference through the AP e_verdict's inputs
    DWORD err = 0;
    core::Dynamics dyn;
    ref_dynamics(in, dyn);
    const bool a_capital = in.p.code < core::kClassCount && ((core::kCapitalMask >> in.p.code) & 1u);
    set_clock(now + age, e_step);
    h_sentinels((now + 7) & 0xff, false);
    o.suppressed = call_e(obj(kObjA), obj(kObjB), 0, &err);
    av::SiteE e;
    e.a_order = in.order;
    e.a_in_table = true;
    e.a_age_ms = age;
    e.frame_ms = e_step;
    e.avoidance_on = true;
    const std::uint16_t cls_b = std::uint16_t(get(obj(kObjB) + F::obj_class) & 0xffff);
    if (cls_b == F::class_ship) {
        e.b_ship = true;
        e.codes_readable = true;
        e.a_capital = a_capital;
        if (e.a_capital) {
            const uint32_t code_b = get(record(get(obj(kObjB) + F::obj_type) & 0xffff) + F::record_class_code);
            e.b_readable = code_b < core::kClassCount;
            e.b_capital_code = e.b_readable && ((core::kCapitalMask >> code_b) & 1u);
            e.r_a = dyn.r_own;
            e.r_b = float(int32_t(get(obj(kObjB) + F::obj_radius))) / core::kUnitsPerMetre;
            e.b_flags = get(obj(kObjB) + F::obj_flags);
            const uint32_t flb = get(obj(kObjB) + F::obj_flight);
            e.b_order = flb ? std::uint16_t(get(flb + F::flight_order) & 0xffff) : 0;
            if (e.b_readable && av::small_pair(true, e.r_a, true, e.b_capital_code, e.r_b))
                e.b_is_a_leader = ref_members.current(obj(kObjA), obj(kObjB), get(obj(kObjA) + 8), get(obj(kObjB) + 8), now + age) != nullptr;
            else if (e.b_readable)
                e.b_member_current = ref_members.current(obj(kObjB), obj(kObjA), get(obj(kObjB) + 8), get(obj(kObjA) + 8), now + age) != nullptr;
        }
    }
    e.b_resolved = av::suppressed(ref_state, obj(kObjA), obj(kObjB), now + age, av::fresh_window_ms(av::kResolvedAgeMs, e_step));
    o.ref_suppressed = av::e_suppresses(av::site_e_verdict(e)) ? 1 : 0;
    return o;
}
void fresh() {
    reset_fl_state();
    reset_sector();
    next_ = kChildBase;
    ref_snap = av::Snapshot{};
    ref_cache = wk::PartCache{};
    ref_members = av::Members{};
    ref_mstats = av::MemberStats{};
    ref_state = av::ShipAvoid{};
}
float segment_distance(const float a[3], const float b[3], const float c[3]) {
    float ab[3], ac[3];
    for (int i = 0; i < 3; ++i) {
        ab[i] = b[i] - a[i];
        ac[i] = c[i] - a[i];
    }
    const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = l2 > 0.0f ? (ac[0] * ab[0] + ac[1] * ab[1] + ac[2] * ab[2]) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = a[i] + t * ab[i] - c[i];
        d2 += d * d;
    }
    return std::sqrt(d2);
}
struct Tally {
    unsigned cases = 0, same = 0, detours = 0, suppressed = 0, property_ok = 0;
};
// property: the case is a resolved-row case (its suppression rests on the detour path); the small-ship and member rows skip the
// native warn without a detour-path proof (the inherited AP rule) and are compared but not counted for the property.
void tally(Tally& t, const Outcome& o, const char* label, const float b_pos[3], float r_a, float r_b, bool property = true) {
    ++t.cases;
    const bool same = o.same_point && o.same_snapshot && o.same_resolved && o.suppressed == o.ref_suppressed;
    if (same) ++t.same;
    else {
        char d[200];
        std::snprintf(d, sizeof d, "%s handled=%d point=%d snapshot=%d resolved=%d e=%d/%d", label, o.handled, int(o.same_point),
                      int(o.same_snapshot), int(o.same_resolved), o.suppressed, o.ref_suppressed);
        check(false, "differential_e", d);
    }
    if (o.redirected) ++t.detours;
    if (o.suppressed && property) {
        ++t.suppressed;
        const float x0[3] = {0, 0, 0};
        if (segment_distance(x0, o.P, b_pos) >= r_a + r_b) ++t.property_ok; // the adjusted path keeps the hulls apart
        else {
            char d[160];
            std::snprintf(d, sizeof d, "%s P=%.1f,%.1f,%.1f d=%.1f", label, double(o.P[0]), double(o.P[1]), double(o.P[2]),
                          double(segment_distance(x0, o.P, b_pos)));
            check(false, "toward_skipped_obstacle", d);
        }
    }
}
const int32_t kOrderPoint[3] = {0, 0, 10000 * 500};

int main(int argc, char** argv) {
    if (argc < 2 || !map_fl_image(argv[1], im)) {
        std::printf("RESULT FAIL cannot map %s\n", argc > 1 ? argv[1] : "(no exe)");
        return 2;
    }
    std::printf("IMAGE base=0x%08lx size=0x%08x timestamp=0x%08x relocated=%u\n",
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(im.base)), im.size, im.timestamp, im.relocated);
    check(im.timestamp == 0x618416c8 && im.size == 0x00391000, "fl_identity");
    if (!map_block()) return 2;
    ss::fixture_set_base(reinterpret_cast<std::uintptr_t>(im.base));
    SetEnvironmentVariableW(L"X3M_SHIP_STEERING", L"1");
    SetEnvironmentVariableW(L"X3M_SHIP_STEERING_CLASSES", L"");
    SetEnvironmentVariableW(L"X3M_SHIP_AVOIDANCE", L"1");
    const auto e_original = [] { return !std::memcmp(im.base + FA::warn_rva, im.pristine.data() + FA::warn_rva, FA::warn_length); };
    const auto reset = [] {
        ss::shutdown();
        ss::fixture_reset();
        rt::fixture_avoidance_reset();
    };
    // ---- the windows: a snapshot window refuses the avoidance, an E window E alone; nothing of E written ----
    quiet = true;
    unsigned av_refused = 0, e_refused = 0;
    for (unsigned w = 0; w < FA::window_count; ++w) {
        const auto& win = FA::windows[w];
        const unsigned at = win.length / 2;
        im.base[win.rva + at] ^= 0x5a;
        const bool ok = ss::initialize();
        im.restore(win.rva + at, 1);
        if (win.part == 2 && ok && !rt::avoidance_on_ && !std::strcmp(rt::avoid_state_, "window_mismatch") && e_original()) ++av_refused;
        if (win.part == 3 && ok && rt::avoidance_on_ && !std::strcmp(rt::warn_state_, "window_mismatch") && e_original()) ++e_refused;
        reset();
    }
    std::printf("FLIPS windows=%u avoidance_refused=%u e_refused=%u\n", unsigned(FA::window_count), av_refused, e_refused);
    check(av_refused + e_refused == FA::window_count, "every avoidance window flip refused");
    rt::fixture_fault_e();
    check(ss::initialize() && rt::avoidance_on_ && !std::strcmp(rt::warn_state_, "readback_failed") && e_original() && !rt::warn_live_,
          "E read-back fault rolled back, the avoidance stays on");
    reset();
    quiet = false;
    check(ss::initialize() && rt::avoidance_on_ && rt::warn_live_ && im.base[FA::warn_rva] == 0xe9, "installed with E");
    quiet = true;
    for (const F::Span& s : F::spans) im.jump(s.rva + s.length, reinterpret_cast<void*>(&h_record));
    im.jump(FA::warn_rva + FA::warn_length, reinterpret_cast<void*>(&h_record));

    // ---- the harness's avoidance unit cases ----
    Tally t;
    uint32_t now = 200000;
    const Me base_me = {100.0f, 10.0f, 40.0f, 1.0f, 0.5f, {0, 0, 0}, 1}; // (alpha: the M5 case's k = 0.5; a steered decision needs k > 0)
    struct Ast {
        const char* label;
        float slip_y;
        std::uint16_t cls;
        int32_t radius;
    };
    const Ast table[3] = {{"asteroid_head_on", 0.0f, 17, 50 * 500}, {"asteroid_slip_clear", 40.0f, 17, 50 * 500},
                          {"station_fallback", 0.0f, 5, 2000 * 500}};
    for (const Ast& c : table) {
        fresh();
        Me me = base_me;
        me.slip[1] = c.slip_y;
        const RefIn in = self_ref(me, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1001);
        const int32_t pos[3] = {0, 0, 240 * 500};
        obstacle(c.cls, pos, c.radius, 0, 0x2001);
        const float bp[3] = {0, 0, 240};
        tally(t, run_case(in, now += 1000, 16, 0, 16, 3), c.label, bp, me.r, float(c.radius) / 500.0f);
    }
    const unsigned table_suppressed = t.suppressed;
    // the M5 head-on ship at 45..400 m (review finding 2): suppressed only when the detour is flyable
    for (int dd : {45, 55, 70, 100, 200, 400}) {
        fresh();
        const Me m5 = {100.0f, 9.2f, 20.0f, 2.58f, 1.29f, {0, 0, 0}, 7};
        const RefIn in = self_ref(m5, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1002);
        const int32_t pos[3] = {0, 0, dd * 500};
        obstacle(F::class_ship, pos, 4600, 0, 0x2002);
        put16(obj(kObjB) + F::obj_type, 7);
        const float bp[3] = {0, 0, float(dd)};
        char label[48];
        std::snprintf(label, sizeof label, "m5_head_on_%dm", dd);
        tally(t, run_case(in, now += 1000, 16, 0, 16), label, bp, m5.r, 9.2f);
    }
    const unsigned m5_suppressed = t.suppressed - table_suppressed;
    // the asteroid case swept over its lateral offset and the slip that swings the velocity clear
    for (int x = -40; x <= 40; x += 10)
        for (int sy = 0; sy <= 60; sy += 20) {
            fresh();
            Me me = base_me;
            me.slip[1] = float(sy);
            const RefIn in = self_ref(me, core::kOrderMovePos, kOrderPoint);
            lay_self(in, 0x1003);
            const int32_t pos[3] = {x * 500, 0, 240 * 500};
            obstacle(17, pos, 50 * 500, 0, 0x2003);
            const float bp[3] = {float(x), 0, 240};
            tally(t, run_case(in, now += 1000, 16, 0, 16, 3), "asteroid_sweep", bp, me.r, 50.0f);
        }
    const unsigned sweep_suppressed = t.suppressed - table_suppressed - m5_suppressed;
    // the capital / small-ship row (the E checks' case b): a capital M2 A, fresh; a small M5 B on 0x12 with the autopilot
    {
        fresh();
        const Me m2 = {70.0f, 1091.0f, 15.0f, 0.2356f, 0.1178f, {0, 0, 0}, 4};
        const RefIn in = self_ref(m2, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1004);
        const int32_t pos[3] = {3000 * 500, 0, 0};
        obstacle(F::class_ship, pos, 64 * 500, 0, 0x2004);
        put16(obj(kObjB) + F::obj_type, 7);
        put(obj(kObjB) + F::obj_flags, F::flag_autopilot);
        put(obj(kObjB) + F::obj_flight, flight(kObjB));
        put16(flight(kObjB) + F::flight_order, core::kOrderAvoid);
        set_record(7, plant_of({100.0f, 9.2f, 20.0f, 2.58f, 1.29f, {0, 0, 0}, 7}));
        const float bp[3] = {3000, 0, 0};
        const Outcome o = run_case(in, now += 1000, 16, 0, 16, 3);
        check(o.suppressed == 1 && o.ref_suppressed == 1, "capital A, small B on 0x12: suppress_small through the FL TShips binding");
        tally(t, o, "capital_small", bp, 0.0f, 0.0f, false); // the small-ship row: compared, not a property case
        // a recycled A (another id at the same address): not answered from the old decision
        put(obj(kObjA) + F::obj_id, 0x1104);
        DWORD err = 0;
        check(call_e(obj(kObjA), obj(kObjB), 0, &err) == 0, "recycled A: the native warn runs");
        put(obj(kObjA) + F::obj_id, 0x1004);
    }
    std::printf("CASES cases=%u same=%u detours=%u suppressed=%u table_suppressed=%u m5_suppressed=%u sweep_suppressed=%u "
                "never_toward_skipped=%u\n", t.cases, t.same, t.detours, t.suppressed, table_suppressed, m5_suppressed,
                sweep_suppressed, t.property_ok);
    check(t.same == t.cases && t.suppressed > 0 && t.property_ok == t.suppressed && t.detours > 0, "avoidance differential and property");

    // ---- a recycled B and the E window under SETA steps (the slip-clear asteroid, resolved) ----
    {
        unsigned seta_ok = 0;
        const uint32_t steps[3] = {16, 62, 200}, ages[3] = {90, 300, 900};
        for (uint32_t step : steps)
            for (uint32_t age : ages) {
                fresh();
                Me me = base_me;
                me.slip[1] = 40.0f;
                const RefIn in = self_ref(me, core::kOrderMovePos, kOrderPoint);
                lay_self(in, 0x1005);
                const int32_t pos[3] = {0, 0, 240 * 500};
                obstacle(17, pos, 50 * 500, 0, 0x2005);
                const Outcome o = run_case(in, now += 2000, 16, age, step, 3);
                if (o.suppressed == o.ref_suppressed && o.suppressed == int(age <= av::fresh_window_ms(av::kResolvedAgeMs, step))) ++seta_ok;
            }
        std::printf("SETA cases=9 ok=%u window_ms(16)=%u window_ms(200)=%u\n", seta_ok, av::fresh_window_ms(av::kResolvedAgeMs, 16),
                    av::fresh_window_ms(av::kResolvedAgeMs, 200));
        check(seta_ok == 9, "E window scaled with the frame step");
        fresh();
        Me me = base_me;
        me.slip[1] = 40.0f;
        const RefIn in = self_ref(me, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1006);
        const int32_t pos[3] = {0, 0, 240 * 500};
        obstacle(17, pos, 50 * 500, 0, 0x2006);
        const Outcome o = run_case(in, now += 1000, 16, 0, 16, 3);
        put(obj(kObjB) + F::obj_id, 0x2106); // B recycled after the resolve
        const unsigned before = rt::e_resolved_id_mismatch_.get();
        DWORD err = 0;
        check(o.suppressed == 1 && call_e(obj(kObjA), obj(kObjB), 0, &err) == 0 && rt::e_resolved_id_mismatch_.get() == before + 1,
              "recycled B: the native warn runs");
    }

    // ---- the station tree of the harness's walk check: the snapshot's parts and the native subtree-radius getter ----
    {
        fresh();
        const RefIn in = self_ref(base_me, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1007);
        const int32_t ps[3] = {0, 0, 3000 * 500};
        obstacle(5, ps, 750000, 100000, 0x2007);
        const uint32_t snode = phys(kObjB);
        put(snode + 0xc, 0); // the children list from the walk check
        const int32_t o1[3] = {200000, 0, 0}, o2[3] = {0, 200000, 0}, o3[3] = {0, 0, 300000}, o4[3] = {0, 0, -600000};
        child(snode, o1, 100000, false);
        child(snode, o2, 50000, true);
        const uint32_t unfilled = child(snode, o3, -1, false, 25000);
        const int32_t og[3] = {0, 0, 75000};
        const uint32_t grand = child(unfilled, og, -1, false, 50000);
        child(snode, o4, 150000, false);
        const float bp[3] = {0, 0, 3000};
        const unsigned built = rt::part_built_.get();
        tally(t, run_case(in, now += 1000, 16, 0, 16), "station_parts", bp, 10.0f, 1500.0f);
        check(rt::part_built_.get() == built + 1, "station part model built from the walk-check tree");
        // the replicated getter against the native one (0x0a39b0, thiscall, `ret 4`), on the unfilled child; the native call fills
        // the caches it visits, so they are put back afterwards
        RefMem mem;
        unsigned budget = wk::kRadiusBudget;
        const int32_t replicated = wk::subtree_radius(mem, unfilled, 1, budget);
        uint32_t node = unfilled;
        int32_t native;
        const std::uintptr_t fn = im.at(0x0a39b0);
        asm volatile("push $0\n\tcall *%2" : "=a"(native), "+c"(node) : "r"(fn) : "edx", "memory", "cc");
        std::printf("RADIUS replicated=%d native=%d\n", int(replicated), int(native));
        check(replicated == native && native == 125000, "the station layout: the native subtree radius equals the replicated one");
        puts32(unfilled + 0xa4, -1);
        puts32(grand + 0xa4, -1);
        // a replaced tree behind the same pointer, root radius and pose (ship-avoidance.md section 4): the cached model is kept
        const unsigned built2 = rt::part_built_.get();
        put(snode + 0xc, alloc(0x10));
        const int32_t o5[3] = {0, 0, 500000};
        child(snode, o5, 250000, false);
        set_clock(now + 16, 16);
        uint32_t arg = 0;
        DWORD err = 0;
        h_sentinels(9, false);
        call_a(0, obj(kObjA), va(kPoint), 0, &arg, &err);
        std::printf("STATION_REPLACED rebuilt=%u (0: the inherited key keeps the old model)\n", rt::part_built_.get() - built2);
    }

    // ---- E through the three real callers' bytes (proximity, its 0x12 refresh, the avoid order): the false path ----
    {
        fresh();
        Me me = base_me;
        me.slip[1] = 40.0f;
        const RefIn in = self_ref(me, core::kOrderMovePos, kOrderPoint);
        lay_self(in, 0x1008);
        const int32_t pos[3] = {0, 0, 240 * 500};
        obstacle(17, pos, 50 * 500, 0, 0x2008);
        const Outcome o = run_case(in, now += 1000, 16, 0, 16, 3);
        check(o.suppressed == 1, "a suppressed pair for the callers");
        im.jump(0x057e08, reinterpret_cast<void*>(&h_record));
        im.jump(0x0609cd, reinterpret_cast<void*>(&h_record));
        unsigned shapes = 0;
        for (uint32_t entry : {0x057dacu, 0x057dc7u}) {
            h_sentinels(0x31, true);
            h_in.esi = obj(kObjA);
            h_in.edi = obj(kObjB);
            h_in.ebx = 12345;
            const uint32_t esp = stack_top(0);
            put(esp + 0x10, 0x0badf00d);
            DWORD err = 0;
            h_run(im.at(entry), 0, 0x5153, &err);
            if (h_out.eax == 0 && h_out.esp == esp && h_out.esi == obj(kObjA) && h_out.edi == obj(kObjB) && h_out.ebx == 12345 &&
                get(esp + 0x10) == 0x0badf00d && err == 0x5153)
                ++shapes;
        }
        {
            h_sentinels(0x32, false);
            h_in.ebx = obj(kObjA);
            h_in.esi = obj(kObjB);
            h_in.eax = va(kOut);
            h_in.edi = 0;
            h_in.edx = va(kOut) + 0x10;
            put(va(kOut) + 0x10, 12345);
            put(va(kOut), 0x0badf00d);
            const uint32_t esp = stack_top(0);
            DWORD err = 0;
            h_run(im.at(0x06057a), 0, 0x5153, &err);
            if (h_out.eax == 0 && h_out.esp == esp && get(va(kOut)) == 0x0badf00d && err == 0x5153) ++shapes;
        }
        im.restore(0x057e08, 5);
        im.restore(0x0609cd, 5);
        std::printf("CALLERS shapes=3 ok=%u\n", shapes);
        check(shapes == 3, "E through the three real callers");

        // ---- E's CPU state: suppressed (EAX 0, `ret 0xc`) and the claim tail against the replayed six bytes, every skew ----
        void* ref_e = h_reference(im.pristine.data() + FA::warn_rva, FA::warn_length);
        unsigned runs = 0, ok = 0;
        for (unsigned skew = 0; skew < 16; skew += 4)
            for (int flags = 0; flags < 2; ++flags) {
                h_sentinels(skew + flags, flags);
                DWORD err = 0;
                const int sup = call_e(obj(kObjA), obj(kObjB), skew, &err);
                HCpu want = h_in;
                want.eax = 0;
                want.ecx = obj(kObjA);
                want.esp = stack_top(skew) + 16 - kLanding;
                want.fpusw = std::uint16_t(7u << 11);
                ++runs;
                if (sup && err == 0x5152 && same_cpu(h_out, want, "cpu_e_suppressed")) ++ok;
                // a pass: B = A's own ship is never in its resolved set
                h_sentinels(skew + flags + 1, flags);
                h_in.ecx = obj(kObjA);
                put(stack_top(skew), landing());
                put(stack_top(skew) + 4, obj(kObjA));
                h_run(reinterpret_cast<std::uintptr_t>(ref_e), skew, 0x5152, &err);
                want = h_out;
                h_sentinels(skew + flags + 1, flags);
                const int passed = !call_e(obj(kObjA), obj(kObjA), skew, &err);
                ++runs;
                if (passed && err == 0x5152 && same_cpu(h_out, want, "cpu_e_pass")) ++ok;
            }
        std::printf("CPU runs=%u ok=%u\n", runs, ok);
        check(runs == 16 && ok == runs, "E cpu state");
    }
    // ---- shutdown restores E; the late window refuses ----
    for (const F::Span& s : F::spans) im.restore(s.rva + s.length, 5);
    im.restore(FA::warn_rva + FA::warn_length, 5);
    reset();
    check(e_original(), "shutdown restored E");
    x3m::engine_patch::close_install_window("fixture_present");
    check(!ss::initialize() && !std::strcmp(ss::state(), "late_claim") && e_original(), "late_claim_refused");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
