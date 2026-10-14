// B.9 (b9_rollout_bench.txt item 3): the controller structs sizes, read from the compile error text Size<N>
#include "ship_gate_ctl_core.h"
namespace gc = x3m::ship_avoidance::ctl;
template <unsigned N> struct Size;
Size<sizeof(gc::Board)> board;
Size<sizeof(gc::Gate)> gate;
Size<sizeof(gc::Member)> member;
