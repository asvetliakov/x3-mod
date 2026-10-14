// Final review item 2: site B's tick deferral on three order-0 frames whose tail did not run (f+0x210 still the previous deferral), then
// a lost restore (the next order's first frame never reaches site B) and the late restore two frames on. -DX3M_HEAD: the reviewed
// signature (base and restore from the stamp read).
#include <initializer_list>
#include <cstdio>
#include <cmath>
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
namespace core = x3m::ship_steering::core;
static gc::LaneDefer step(gc::Gate& g, gc::Member& m, std::uint16_t o, std::uint32_t f210, std::uint32_t now, const float* p) {
#ifdef X3M_HEAD
    (void)now;
    return gc::lane_defer_step(g, m, o, 0x4000, f210, p);
#else
    return gc::lane_defer_step(g, m, o, 0x4000, f210, now, p);
#endif
}
int main() {
    gc::Gate g{}; g.gate = 0x4000; g.r_gate = 1105; g.F[2] = 1; g.step_ms = 33;
    gc::Member m{}; m.ln = 1; m.q_ok = 1; m.role = gc::Role::escort; m.res_until = 5000; m.sb = gc::Sb::funnel; m.lane_esc = 1; m.r = 760;
    const float p[3] = {0, 0, -3730};
    std::uint32_t f210 = 200000u, now = 200000u;
    for (int k = 0; k < 3; ++k, now += 33u) { const gc::LaneDefer w = step(g, m, core::kOrderNone, f210, now, p); if (w.write210) f210 = w.v210; }
    std::printf("3 order-0 frames without the tail: f+0x210 %d ms after the last order-0 frame (one deferral %u ms)\n", int(f210 - (now - 33u)),
                gc::kLaneDeferMs + 33u);
    const gc::LaneDefer r = step(g, m, core::kOrderFlyBefore, f210, now + 33u, p);
    std::printf("late restore two frames on: write %d, f+0x210 %d ms from now\n", int(r.write210), int(std::int32_t(r.v210 - (now + 33u))));
}
