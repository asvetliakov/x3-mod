// Native fixture of the FL base ship steering (src/engine/fl/ship_steering_fl.cpp, ship_steering_sites_fl.h;
// docs/reverse-engineering/fl/ship-steering.md, "Port (2026-11-03)"): built with -DX3M_EDITION_FL=1 -DX3M_SHIP_STEERING_FL_FIXTURE by
// verification/analysis/test_fl_ship_steering.py and run natively through WSL interop; nothing is written to X3FL.exe (argv[1] is
// mapped into a private image with its relocations applied, fl_seta_image.h). argv[2]: the ship_trace rows the engine-model harness
// emits (ship_steering_host.cpp --emit-trace): every row's inputs are laid into the FL fields of one ship and run through the claimed
// stubs and the real FL getters; the reference is the shared core called directly with the AP glue's input construction
// (fl_ship_steering_fixture.h), so the comparison isolates the binding layer.
//
// Covered: exact-byte refusal of every window (a flipped byte each, a foreign fixup) before any write; read-back faults on each
// of the four claims rolled back; the install rows and the five ap_only rows; the differential over the trace rows (A: handled,
// the three remainders, the 64-byte Ship entry; B: the desired speed); the class filter (X3M_SHIP_STEERING_CLASSES=TS); the
// SETA-scaled clamp age; a recycled pointer; the CPU state (GPRs, EFLAGS with DF, XMM0-7, x87 CW/stack, MXCSR, LastError, ESP) of
// A handled / refused and B filtered / clamping at every 4-byte ESP skew against the replayed span; shutdown restoring every byte;
// the late-window refusal.
#include "fl_ship_steering_fixture.h"
#include <vector>

struct Row {
    unsigned frame;
    uint32_t t_ms, obj, order, tgt_class, dt;
    char cls[8];
    int32_t pos[3], rate[3], v, des_engine, given[3], e[3], W[3], rot_pct, vmax, acc, slip[3];
    uint32_t rot;
    float fwd[3], up[3];
    bool leaf;
};
std::vector<Row> rows;
const Row* clamp_row = nullptr; // the first trace row whose B clamped (the SETA cases replay its decision)
bool read_rows(const char* path) {
    std::FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        Row r{};
        char e[64] = {};
        int n = std::sscanf(line,
                            "ship_trace frame=%u t_ms=%u obj=0x%x class=%7s order=0x%x tgt=0x%*x tgt_class=%u pos=%d,%d,%d fwd=%f,%f,%f "
                            "up=%f,%f,%f rate=%d,%d,%d v=%d des=%*d des_engine=%d point=%*d,%*d,%*d given=%d,%d,%d e=%63s dt_ms=%u "
                            "W=%d,%d,%d rot=0x%x rot_pct=%d vmax=%d acc=%d",
                            &r.frame, &r.t_ms, &r.obj, r.cls, &r.order, &r.tgt_class, &r.pos[0], &r.pos[1], &r.pos[2], &r.fwd[0],
                            &r.fwd[1], &r.fwd[2], &r.up[0], &r.up[1], &r.up[2], &r.rate[0], &r.rate[1], &r.rate[2], &r.v, &r.des_engine,
                            &r.given[0], &r.given[1], &r.given[2], e, &r.dt, &r.W[0], &r.W[1], &r.W[2], &r.rot, &r.rot_pct, &r.vmax, &r.acc);
        if (n != 32) continue;
        r.leaf = e[0] != '-';
        const char* s = std::strstr(line, " slip=");
        if (s) std::sscanf(s, " slip=%d,%d,%d", &r.slip[0], &r.slip[1], &r.slip[2]);
        rows.push_back(r);
    }
    std::fclose(f);
    return !rows.empty();
}
uint32_t class_code(const char* name) {
    for (uint32_t c = 0; c < core::kClassCount; ++c)
        if (!std::strcmp(core::kClassNames[c], name)) return c;
    return 0;
}
int32_t q16(float v) { return int32_t(std::lround(double(v) * 65536.0)); }
// One row as the reference's raw inputs, and laid into the FL fields of ship A (type = the class code's record).
RefIn ref_of(const Row& r) {
    RefIn in{};
    in.p.code = class_code(r.cls);
    for (int i = 0; i < 3; ++i) in.p.W[i] = r.W[i];
    in.p.vmax = r.vmax;
    in.p.acc = r.acc;
    in.p.rot = int32_t(r.rot);
    in.p.rot_pct = r.rot_pct;
    in.p.radius = 20000;
    const float right[3] = {r.up[1] * r.fwd[2] - r.up[2] * r.fwd[1], r.up[2] * r.fwd[0] - r.up[0] * r.fwd[2],
                            r.up[0] * r.fwd[1] - r.up[1] * r.fwd[0]};
    for (int i = 0; i < 3; ++i) {
        in.pos[i] = r.pos[i];
        in.rows[0][i] = q16(right[i]);
        in.rows[1][i] = q16(r.up[i]);
        in.rows[2][i] = q16(r.fwd[i]);
        in.rate[i] = r.rate[i];
        in.slip[i] = r.slip[i];
        in.point[i] = r.given[i];
    }
    in.v = r.v;
    in.order = std::uint16_t(r.order);
    in.tgt_class = std::uint16_t(r.tgt_class);
    in.has_tgt = r.tgt_class != 0;
    in.tgt_radius = r.tgt_class == F::class_gate ? 552500 : 400000;
    in.port = r.tgt_class == F::class_gate ? -1 : 0;
    in.distance = 50000;
    return in;
}
void lay(const RefIn& in, uint32_t id) {
    set_record(in.p.code, in.p);
    const uint32_t target = in.has_tgt ? obj(kObjTarget) : 0;
    set_ship(kObjA, in.p.code, in.p, in.pos, in.rows, in.v, in.rate, in.slip, in.order, target, in.distance, in.port, id);
    if (in.has_tgt) {
        std::memset(reinterpret_cast<void*>(std::uintptr_t(obj(kObjTarget))), 0, 0x200);
        put16(obj(kObjTarget) + F::obj_class, in.tgt_class);
        puts32(obj(kObjTarget) + F::obj_radius, in.tgt_radius);
    }
    for (int i = 0; i < 3; ++i) puts32(va(kPoint) + 4 * i, in.point[i]);
}
// The reference's ship state and the comparison of one row.
struct Ref {
    core::Ship s;
    bool exists = false;
    uint32_t mask = 0;
};
struct Tally {
    unsigned a_rows = 0, a_handled = 0, a_passed = 0, a_same = 0, ship_same = 0, b_rows = 0, b_clamped = 0, b_same = 0, excluded = 0;
};
// One A call (when the row had a leaf call) and one B call, FL through the stubs, the reference through the cores.
void run_row(const Row& row, Ref& ref, Tally& t, unsigned leaf, unsigned skew, uint32_t window_base = F::clamp_max_age_ms) {
    const RefIn in = ref_of(row);
    lay(in, row.obj);
    set_clock(row.t_ms, row.dt);
    const uint32_t now = row.t_ms;
    const uint32_t o = obj(kObjA);
    if (row.leaf) {
        ++t.a_rows;
        for (int i = 0; i < 3; ++i) puts32(o + F::obj_remaining + 4 * i, 0x7f7f7f7f);
        h_sentinels(row.frame, row.frame & 1);
        uint32_t arg = 0;
        DWORD err = 0;
        const int handled = call_a(leaf, o, va(kPoint), skew, &arg, &err);
        // the reference
        bool want = false;
        core::Command cmd;
        const core::OrderPolicy policy = core::order_policy(in.order);
        const bool excluded = in.p.code < core::kClassCount && ((ref.mask >> in.p.code) & 1u);
        if (excluded) ++t.excluded;
        if (policy.steered && !excluded && in.p.rot_pct > 0) {
            if (!ref.exists) {
                ref.s = core::Ship{};
                ref.s.obj = o;
                ref.exists = true;
            }
            core::Dynamics dyn;
            ref_dynamics(in, dyn);
            core::Inputs ci;
            ref_inputs(in, dyn, ci);
            uint32_t frame_ms = row.dt;
            if (frame_ms < 1 || frame_ms > 1000) frame_ms = 1;
            const uint32_t gap = ref.s.last_call_ms && now >= ref.s.last_call_ms ? now - ref.s.last_call_ms : 50;
            ci.tick_s = core::servo_tick(gap, frame_ms, ref.s.per_frame_run);
            ref_arrival(in, dyn, ref.s, now, av::fresh_window_ms(core::kPortHoldGapMs, row.dt), ci);
            want = core::steer(ci, dyn, &cmd) == core::Reason::handled;
            if (want) ref_record(ref.s, ci, dyn, cmd, now);
        }
        handled ? ++t.a_handled : ++t.a_passed;
        bool same = handled == int(want) && arg == va(kPoint) && err == 0x5150;
        for (int i = 0; i < 3; ++i) same = same && get(o + F::obj_remaining + 4 * i) == (want ? uint32_t(cmd.e[i]) : 0x7f7f7f7fu);
        if (same) ++t.a_same;
        else {
            char d[260];
            std::snprintf(d, sizeof d, "frame=%u order=0x%x handled=%d want=%d e=%d,%d,%d ref=%d,%d,%d arg=%08x point=%08x err=%lx", row.frame, row.order,
                          handled, int(want), int(get(o + 0x30)), int(get(o + 0x34)), int(get(o + 0x38)), want ? cmd.e[0] : 0,
                          want ? cmd.e[1] : 0, want ? cmd.e[2] : 0, arg, va(kPoint), static_cast<unsigned long>(err));
            check(false, "differential_a", d);
        }
        const core::Ship* fs = fl_ship(o);
        const bool ship_same = ref.exists ? fs && !std::memcmp(fs, &ref.s, sizeof ref.s) : fs == nullptr;
        if (ship_same) ++t.ship_same;
        else {
            char d[200];
            std::snprintf(d, sizeof d, "frame=%u flags %u/%u v_ref %g/%g d_arr %g/%g run %u/%u", row.frame, fs ? fs->flags : 0, ref.s.flags,
                          fs ? double(fs->v_ref) : -1.0, double(ref.s.v_ref), fs ? double(fs->d_arr) : -1.0, double(ref.s.d_arr),
                          fs ? fs->per_frame_run : 0, ref.s.per_frame_run);
            check(false, "differential_ship", d);
        }
    }
    // B with the engine's desired speed of the row
    ++t.b_rows;
    puts32(o + F::obj_desired_speed, row.des_engine);
    DWORD err = 0;
    h_sentinels(row.frame ^ 0x55, false);
    call_b(o, skew, &err);
    int32_t want = row.des_engine;
    if (ref.exists) {
        float x[3];
        for (int i = 0; i < 3; ++i) x[i] = float(row.pos[i]) / core::kUnitsPerMetre;
        const float v = core::clamp_for(ref.s, x, std::uint16_t(row.order), now, av::fresh_window_ms(window_base, row.dt), nullptr);
        const int32_t units = v > 0.0f ? core::clamp_units(v, row.des_engine) : 0;
        if (units > 0) want = units;
    }
    if (want != row.des_engine) {
        ++t.b_clamped;
        if (!clamp_row) clamp_row = &row;
    }
    if (int32_t(get(o + F::obj_desired_speed)) == want && err == 0x5151) ++t.b_same;
    else {
        char d[120];
        std::snprintf(d, sizeof d, "frame=%u des %d want %d", row.frame, int(get(o + F::obj_desired_speed)), int(want));
        check(false, "differential_b", d);
    }
}
bool spans_original() {
    bool ok = true;
    for (const F::Span& s : F::spans) ok = ok && !std::memcmp(im.base + s.rva, im.pristine.data() + s.rva, s.length);
    return ok;
}
bool initialize(const wchar_t* classes) {
    SetEnvironmentVariableW(L"X3M_SHIP_STEERING", L"1");
    SetEnvironmentVariableW(L"X3M_SHIP_AVOIDANCE", L"0");
    SetEnvironmentVariableW(L"X3M_SHIP_STEERING_CLASSES", classes);
    return ss::initialize();
}
void reset() {
    ss::shutdown();
    ss::fixture_reset();
}
// The continuations of the four claims carry `jmp h_record` while the stubs are exercised (restored before a re-install).
void landings(bool on) {
    for (const F::Span& s : F::spans)
        if (on)
            im.jump(s.rva + s.length, reinterpret_cast<void*>(&h_record));
        else
            im.restore(s.rva + s.length, 5);
}
Tally replay(uint32_t mask, unsigned skew_seed) {
    Tally t;
    Ref ref;
    ref.mask = mask;
    uint32_t run_obj = 0;
    unsigned i = 0;
    for (const Row& r : rows) {
        if (r.obj != run_obj) { // a new harness run: a fresh ship on both sides
            run_obj = r.obj;
            reset_fl_state();
            ref = Ref{};
            ref.mask = mask;
        }
        run_row(r, ref, t, i % 3, ((i + skew_seed) % 4) * 4);
        ++i;
    }
    return t;
}

int main(int argc, char** argv) {
    if (argc < 3 || !map_fl_image(argv[1], im) || !read_rows(argv[2])) {
        std::printf("RESULT FAIL cannot map %s or read %s\n", argc > 1 ? argv[1] : "(no exe)", argc > 2 ? argv[2] : "(no rows)");
        return 2;
    }
    std::printf("IMAGE base=0x%08lx size=0x%08x timestamp=0x%08x relocated=%u\n",
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(im.base)), im.size, im.timestamp, im.relocated);
    check(im.timestamp == 0x618416c8 && im.size == 0x00391000, "fl_identity");
    if (!map_block()) return 2;
    reset_sector();
    ss::fixture_set_base(reinterpret_cast<std::uintptr_t>(im.base));
    std::printf("ROWS %u\n", unsigned(rows.size()));

    // ---- exact-byte refusal: one flipped byte per window (outside its fixups), one foreign fixup; nothing written ----
    unsigned flips = 0, refused = 0;
    quiet = true;
    for (unsigned w = 0; w < F::window_count; ++w) {
        const auto& win = F::windows[w];
        unsigned at = win.length / 2;
        for (unsigned f = 0; f < win.fixups; ++f)
            if (at + 4 > win.fixup[f].offset && at < win.fixup[f].offset + 4) at = win.fixup[f].offset + 4 < win.length ? win.fixup[f].offset + 4 : 0;
        im.base[win.rva + at] ^= 0x5a;
        ++flips;
        const bool ok = initialize(L"");
        im.restore(win.rva + at, 1);
        if (!ok && !std::strcmp(ss::state(), "window_mismatch") && spans_original()) ++refused;
        else std::printf("FLIP window=%s at=%u state=%s\n", win.name, at, ss::state());
        reset();
    }
    {
        const auto& win = F::windows[F::window_accel];
        check(win.fixups > 0, "accel window has a fixup");
        im.u32(win.rva + win.fixup[0].offset) += 0x1000;
        check(!initialize(L"") && !std::strcmp(ss::state(), "fixup_mismatch") && spans_original(), "fixup_mismatch_refused");
        im.restore(win.rva + win.fixup[0].offset, 4);
        reset();
    }
    std::printf("FLIPS windows=%u flips=%u refused=%u\n", unsigned(F::window_count), flips, refused);
    check(refused == F::window_count, "every window flip refused before any write");
    // ---- read-back faults: each claim rolled back with every earlier one ----
    unsigned rolled = 0;
    for (unsigned i = 0; i < F::count; ++i) {
        ss::fixture_fault_claim(i);
        if (!initialize(L"") && !std::strcmp(ss::state(), "readback_failed") && spans_original()) ++rolled;
        reset();
    }
    std::printf("ROLLBACK claims=%u rolled_back=%u\n", unsigned(F::count), rolled);
    check(rolled == F::count, "every read-back fault rolled back");
    // ---- the install ----
    quiet = false;
    rows_ap_only = 0;
    check(initialize(L"") && ss::installed(), "installed");
    check(rows_ap_only == 5, "five ap_only rows");
    bool jumps = true;
    for (unsigned i = 0; i < F::count; ++i)
        jumps = jumps && im.base[F::spans[i].rva] == 0xe9 && !std::memcmp(im.base + F::spans[i].rva + 5, im.pristine.data() + F::spans[i].rva + 5,
                                                                           F::spans[i].length - 5);
    check(jumps, "four jumps written, the rest of each span untouched");
    quiet = true;
    landings(true);

    // ---- the differential over the harness's trace rows ----
    const Tally t = replay(0, 0);
    std::printf("DIFF a_rows=%u a_handled=%u a_passed=%u a_same=%u ship_same=%u b_rows=%u b_clamped=%u b_same=%u\n", t.a_rows, t.a_handled,
                t.a_passed, t.a_same, t.ship_same, t.b_rows, t.b_clamped, t.b_same);
    check(t.a_same == t.a_rows && t.ship_same == t.a_rows && t.b_same == t.b_rows && t.a_handled > 0 && t.b_clamped > 0, "differential");

    // ---- the class filter: X3M_SHIP_STEERING_CLASSES=TS (the TS rows pass to the native leaf, the others steer) ----
    landings(false);
    reset();
    check(initialize(L"TS") && rt::class_mask_ == (1u << class_code("TS")), "class mask TS");
    landings(true);
    const Tally c = replay(1u << class_code("TS"), 1);
    std::printf("CLASSES mask=TS a_rows=%u excluded=%u a_handled=%u a_same=%u ship_same=%u b_same=%u\n", c.a_rows, c.excluded, c.a_handled,
                c.a_same, c.ship_same, c.b_same);
    check(c.excluded > 0 && c.a_same == c.a_rows && c.ship_same == c.a_rows && c.b_same == c.b_rows && c.a_handled < t.a_handled,
          "class filter honoured");
    landings(false);
    reset();
    check(initialize(L""), "reinstalled");
    landings(true);

    // ---- SETA: the clamp's decision age scales with the game-time frame step (max(250, 4 x step)) ----
    {
        const Row* first = nullptr;
        for (const Row& r : rows)
            if (r.leaf && core::order_policy(std::uint16_t(r.order)).steered) {
                first = &r;
                break;
            }
        unsigned seta_ok = 0, seta_cases = 0, seta_scaled = 0, seta_wrong = 0;
        check(clamp_row != nullptr && clamp_row->leaf, "a clamping trace row");
        if (clamp_row && clamp_row->leaf) first = clamp_row;
        const uint32_t steps[4] = {16, 62, 200, 250}, ages[4] = {240, 300, 700, 1100};
        for (uint32_t step : steps)
            for (uint32_t age : ages) {
                reset_fl_state();
                Ref ref;
                Tally tt;
                Row r = *first;
                r.dt = step;
                run_row(r, ref, tt, 0, 0); // the decision at t
                Row b = r;
                b.leaf = false;
                b.t_ms = r.t_ms + age;
                b.des_engine = r.vmax; // the engine asks for full speed: a live clamp lowers it
                Tally tb;
                run_row(b, ref, tb, 0, 0);
                const bool live = age <= av::fresh_window_ms(F::clamp_max_age_ms, step);
                ++seta_cases;
                if (tb.b_same == 1) ++seta_ok;
                if (tb.b_clamped && age > F::clamp_max_age_ms) ++seta_scaled; // AP's fixed 250 ms would have left it to the engine
                if (tb.b_clamped && !live) ++seta_wrong;
            }
        std::printf("SETA cases=%u ok=%u clamped_beyond_250ms=%u clamped_beyond_window=%u window_ms(16)=%u window_ms(200)=%u\n", seta_cases,
                    seta_ok, seta_scaled, seta_wrong, av::fresh_window_ms(F::clamp_max_age_ms, 16), av::fresh_window_ms(F::clamp_max_age_ms, 200));
        check(seta_ok == seta_cases && seta_scaled > 0 && seta_wrong == 0, "SETA-scaled clamp age");
        // a recycled pointer: the same object address with another native id gets no clamp from the old decision and a fresh entry
        reset_fl_state();
        Ref ref;
        Tally tt;
        Row r = *first;
        run_row(r, ref, tt, 0, 0);
        const unsigned before = rt::recycled_.get();
        put(obj(kObjA) + F::obj_id, r.obj ^ 0x100);
        puts32(obj(kObjA) + F::obj_desired_speed, r.vmax);
        DWORD err = 0;
        call_b(obj(kObjA), 0, &err);
        check(int32_t(get(obj(kObjA) + F::obj_desired_speed)) == r.vmax, "recycled pointer: no clamp from the old decision");
        uint32_t arg = 0;
        set_clock(r.t_ms + 16, 16);
        h_sentinels(3, false);
        call_a(0, obj(kObjA), va(kPoint), 0, &arg, &err);
        const core::Ship* fs = fl_ship(obj(kObjA));
        check(rt::recycled_.get() == before + 1 && fs && fs->last_call_ms == r.t_ms + 16 && fs->per_frame_run == 0, "recycled pointer: a new entry");
        std::printf("RECYCLED resets=%u\n", rt::recycled_.get() - before);
    }

    // ---- CPU state at every stub path, every ESP skew, flags set and clear ----
    {
        void* ref_a = h_reference(im.pristine.data() + F::spans[0].rva, F::spans[0].length);
        void* ref_b = h_reference(im.pristine.data() + F::spans[3].rva, F::spans[3].length);
        const Row* first = nullptr;
        for (const Row& r : rows)
            if (r.leaf && core::order_policy(std::uint16_t(r.order)).steered) {
                first = &r;
                break;
            }
        unsigned runs = 0, ok = 0;
        for (unsigned skew = 0; skew < 16; skew += 4)
            for (int flags = 0; flags < 2; ++flags) {
                reset_fl_state();
                lay(ref_of(*first), first->obj);
                set_clock(first->t_ms, first->dt);
                // A handled: every register as it entered, ESP + 8
                h_sentinels(skew + flags, flags);
                HCpu in = h_in;
                uint32_t arg = 0;
                DWORD err = 0;
                const int handled = call_a(0, obj(kObjA), va(kPoint), skew, &arg, &err);
                HCpu want = in;
                want.ecx = obj(kObjA);
                want.esp = stack_top(skew) + 8 - kLanding;
                want.fpusw = std::uint16_t(7u << 11); // one value on the x87 stack (TOP 7), as loaded
                ++runs;
                if (handled && err == 0x5150 && same_cpu(h_out, want, "cpu_a_handled")) ++ok;
                // A refused (not a ship): the claim tail's image equals the replayed span's
                put16(obj(kObjA) + F::obj_class, 0x13);
                h_sentinels(skew + flags + 1, flags);
                h_in.ecx = obj(kObjA);
                put(stack_top(skew), uint32_t(reinterpret_cast<std::uintptr_t>(&h_record)));
                put(stack_top(skew) + 4, va(kPoint));
                h_run(reinterpret_cast<std::uintptr_t>(ref_a), skew, 0x5150, &err);
                want = h_out;
                h_sentinels(skew + flags + 1, flags);
                const int passed = !call_a(0, obj(kObjA), va(kPoint), skew, &arg, &err);
                ++runs;
                if (passed && err == 0x5150 && same_cpu(h_out, want, "cpu_a_refused")) ++ok;
                // B filtered (class 0x13) and B clamping (class 7, autopilot): both images equal the replayed span's
                for (int cls = 0; cls < 2; ++cls) {
                    put16(obj(kObjA) + F::obj_class, cls ? F::class_ship : 0x13);
                    h_sentinels(skew + flags + 2, flags);
                    h_in.ebx = obj(kObjA);
                    h_run(reinterpret_cast<std::uintptr_t>(ref_b), skew, 0x5151, &err);
                    want = h_out;
                    const uint32_t slot_want = get(stack_top(skew) + 0x108);
                    h_sentinels(skew + flags + 2, flags);
                    put(stack_top(skew) + 0x108, 0);
                    call_b(obj(kObjA), skew, &err);
                    ++runs;
                    if (err == 0x5151 && get(stack_top(skew) + 0x108) == slot_want && same_cpu(h_out, want, cls ? "cpu_b_clamp" : "cpu_b_filtered")) ++ok;
                }
            }
        std::printf("CPU runs=%u ok=%u\n", runs, ok);
        check(runs == 32 && ok == runs, "cpu state at every stub path");
    }

    // ---- shutdown restores every byte; the late window refuses ----
    landings(false);
    reset();
    check(spans_original(), "shutdown restored the four spans");
    x3m::engine_patch::close_install_window("fixture_present");
    check(!initialize(L"") && !std::strcmp(ss::state(), "late_claim") && spans_original(), "late_claim_refused");
    std::printf("ARENA used=%u capacity=%u\n", x3m::engine_patch::arena_used(), x3m::engine_patch::arena_capacity());
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
