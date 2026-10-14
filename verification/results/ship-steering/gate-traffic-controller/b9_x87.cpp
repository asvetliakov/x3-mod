// B.9 (b9_rollout_bench.txt item 4): the x87 count of the changed functions in i686 code (-S, then count the f* mnemonics per function)
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
// non-inline wrappers so each function's code is emitted for the x87 count
__attribute__((noinline)) unsigned w_rollout(const gc::Member& m, const gc::Gate& g, float v, gc::Sample* o, float* vm) { return gc::rollout(m, g, v, o, vm); }
__attribute__((noinline)) float w_plan_speed(const gc::Gate& g, const gc::Member& m) { return gc::plan_speed(g, m); }
__attribute__((noinline)) void w_decide(gc::Gate& g, unsigned i, std::uint32_t now, gc::DecideStats* st) { gc::decide(g, i, now, st, nullptr, nullptr); }
__attribute__((noinline)) float w_pair_gap(const gc::Gate& g, unsigned i, unsigned j) { return gc::pair_gap(g, i, j); }
__attribute__((noinline)) float w_hold_speed(const gc::Gate& g, const gc::Member& m, std::uint32_t now, std::uint32_t t) { return gc::hold_speed(g, m, now, t, nullptr); }
