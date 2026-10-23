// Host build of src/proxy/seta_manual_flight_core.h (test_seta_manual_flight.py): the per-axis decision at k = 1, 2.5
// and 10, the governor, the gate and the mouse-delta skip, the rounding, and the installer's window comparison over a
// flat image of X3AP.exe. No Windows, no engine.
//   (no argument)            decision checks; prints "checks=N PASS"
//   --windows                one "name va length part" line per compared window
//   --sites                  one "name va length" line per claim (the stop byte and the five spans)
//   --image <file> <mode>    the comparison over an RVA-indexed flat image (byte i at VA 0x00400000 + i)
#include "../../src/proxy/seta_manual_flight_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace x3m::seta_manual_flight::core;

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}
bool close_to(double a, double b) {
    return std::fabs(a - b) <= 1e-12 * (std::fabs(b) > 1 ? std::fabs(b) : 1);
}
int decisions() {
    const std::int32_t ms = 167, target = 0x10000;
    struct Case {
        std::int32_t factor, governor;
        Skip skip;
        double k;
    };
    const Case cases[] = {{0x10000, 0x10000, warp_off, 0},  {0x10000, 0x8000, warp_off, 0},
                          {0x0ffff, 0x10000, warp_off, 0},  {0x20000, 0x8000, rate_out_of_range, 0},
                          // warp above 1.0 with the clock below real time (SETA ramp under load): original path
                          {0x18000, 0x6666, rate_out_of_range, 0}, {0x20000, 0x7fff, rate_out_of_range, 0},
                          {0x18000, 0x10000, applied, 1.5},  {0x20000, 0x8001, applied, 65538.0 / 65536.0},
                          {0x28000, 0x10000, applied, 2.5}, {0xa0000, 0x10000, applied, 10.0},
                          {0xa0000, 0x8000, applied, 5.0},  {0x20000, 0x1000, rate_out_of_range, 0},
                          {0x20000, 0, rate_out_of_range, 0}, {0x7fffffff, 0x7fffffff, rate_out_of_range, 0}};
    char what[160];
    for (const Case& c : cases) {
        for (unsigned axis = 0; axis < axis_count; ++axis) {
            Inputs in;
            in.factor = c.factor;
            in.governor = c.governor;
            in.game_ms = ms;
            in.target = axis < strafe_y ? target : 0;
            in.mouse_x = 0xb;
            in.mouse_y = 0xc;
            Out out;
            const Skip s = decide(axis, in, out);
            std::snprintf(what, sizeof what, "factor=0x%x governor=0x%x %s skip=%s", unsigned(c.factor), unsigned(c.governor),
                          axis_names[axis], skip_names[s]);
            check(s == c.skip, what);
            if (s != applied) continue;
            check(out.rate == effective_rate(c.factor, c.governor), what);
            check(close_to(out.step_factor, ms / (c.k * c.k)), what);
            check(close_to(out.inv_k, 1.0 / c.k), what);
            check(out.target == round_scaled(in.target, 1.0 / c.k), what);
        }
    }
    // R is the clock's effective rate, not the warp alone
    check(effective_rate(0xa0000, 0x10000) == 0xa0000 && effective_rate(0xa0000, 0x8000) == 0x50000 &&
              effective_rate(0x28000, 0x10000) == 0x28000 && effective_rate(0x10000, 0x4ccd) == 0x4ccd,
          "effective rate");
    // mouse-delta skip: type 1/2/3 in any of the four type words keeps yaw/pitch/roll original; 4/5 (turret) and strafe
    // never skip
    for (std::uint32_t type = 0; type <= 16; ++type)
        for (unsigned on_y = 0; on_y < 4; ++on_y) // 0/1: ship-view x/y; 2/3: the view-flag pair x/y
            for (unsigned axis = 0; axis < axis_count; ++axis) {
                Inputs in;
                in.factor = 0xa0000;
                in.game_ms = ms;
                in.target = target;
                in.mouse_x = on_y == 0 ? type : 0xb;
                in.mouse_y = on_y == 1 ? type : 0xc;
                in.mouse_turret_x = on_y == 2 ? type : 0xf;
                in.mouse_turret_y = on_y == 3 ? type : 0x10;
                Out out;
                const Skip s = decide(axis, in, out);
                const bool skip = axis < strafe_y && type == axis + 1;
                std::snprintf(what, sizeof what, "mouse %s type %u %s", on_y == 0 ? "x" : on_y == 1 ? "y" : on_y == 2 ? "e48" : "e4c", type, axis_names[axis]);
                check(s == (skip ? mouse_delta : applied), what);
            }
    // rounding: half away from zero, clamped to int32
    check(round_scaled(5, 0.5) == 3 && round_scaled(-5, 0.5) == -3 && round_scaled(4, 0.5) == 2 &&
              round_scaled(-4, 0.5) == -2 && round_scaled(0x10000, 0.4) == 26214 && round_scaled(0x7fffffff, 4.0) == 0x7fffffff &&
              round_scaled(-0x7fffffff, 4.0) == -0x7fffffff,
          "rounding");
    // steady state of the 1/k target: the stored rate times k is the unscaled target
    for (double k : {2.5, 10.0}) {
        const std::int32_t stored = round_scaled(target, 1.0 / k);
        std::snprintf(what, sizeof what, "steady obj+0x24 * k at k=%g", k);
        check(std::fabs(stored * k - target) <= k, what);
    }
    // ramp: the per-frame step accel * game_ms * 0.001 / k^2 at game_ms = k * wall_ms equals accel * wall_ms * 0.001 / k,
    // so (in real units: rate * k) the rise of the real-time rate is accel per real second at every k
    for (double k : {1.0, 2.5, 10.0}) {
        const double wall_ms = 16.0, accel = 1.4249267578125;
        const double step_game = accel * (k * wall_ms) * 0.001 / (k * k);
        std::snprintf(what, sizeof what, "real-time ramp per frame at k=%g", k);
        check(close_to(step_game * k, accel * wall_ms * 0.001), what);
    }
    // strafe: increment clamp(cmd, +-20 * game_ms / k) and decay (2000 - dt) / 2000 move the ship 2 * increment per frame
    // whatever dt; with game_ms = k * wall_ms the increment and the real-time displacement equal the unwarped ones
    for (double k : {1.0, 2.5, 10.0})
        for (double strafe_max : {100.0, 320.0, 5000.0}) {
            const double wall_ms = 16.0, dt = k * wall_ms;
            const double unwarped = std::fmin(strafe_max, 20.0 * wall_ms), scaled = std::fmin(strafe_max, 20.0 * dt / k);
            double v = 0, moved = 0;
            for (int f = 0; f < 4000; ++f) {
                v += scaled;
                moved = v * dt / 1000.0;
                v = v * (2000.0 - dt) / 2000.0;
            }
            std::snprintf(what, sizeof what, "strafe k=%g max=%g: increment and steady displacement per frame", k, strafe_max);
            check(close_to(scaled, unwarped) && std::fabs(moved - 2 * unwarped) < 1e-6 * unwarped, what);
        }
    // the claim table and the windows
    check(sizeof(sites) / sizeof(sites[0]) == 5 && sites_inside_windows(), "five claims inside their windows");
    check(stop_patch.va == 0x0041211d && stop_patch.original[0] == 0x74 && stop_patch.patched == 0xeb, "stop patch byte");
    std::printf("checks=%u failures=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
int image(const char* path, unsigned mode) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 2;
    std::vector<unsigned char> data;
    unsigned char buffer[65536];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) data.insert(data.end(), buffer, buffer + n);
    std::fclose(f);
    Mismatch m;
    const bool ok = compare_windows(
        [&](std::uint32_t va, void* out, unsigned n) {
            if (va < 0x00400000 || va - 0x00400000 + n > data.size()) return false;
            std::memcpy(out, data.data() + (va - 0x00400000), n);
            return true;
        },
        mode, m);
    unsigned compared = 0;
    for (const Window& w : windows) compared += w.part <= mode;
    if (ok) {
        std::printf("image mode=%u windows=%u mismatch=none\n", mode, compared);
        return 0;
    }
    std::printf("image mode=%u windows=%u mismatch=%s va=0x%08x readable=%u got=", mode, compared, m.window->name,
                unsigned(m.window->va), unsigned(m.readable));
    for (unsigned i = 0; i < m.shown; ++i) std::printf("%02x", m.got[i]);
    std::printf("\n");
    return 1;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--windows")) {
        for (const Window& w : windows) std::printf("%s 0x%08x %u %u\n", w.name, unsigned(w.va), w.length, w.part);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--sites")) {
        std::printf("%s 0x%08x %u\n", stop_patch.name, unsigned(stop_patch.va), stop_patch.length);
        for (const Site& s : sites) std::printf("%s 0x%08x %u\n", s.name, unsigned(s.va), s.length);
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[1], "--image")) return image(argv[2], unsigned(argv[3][0] - '0'));
    return decisions();
}
