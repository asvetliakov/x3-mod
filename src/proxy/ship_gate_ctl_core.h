#pragma once
// The gate traffic controller, stage A (docs/architecture/gate-traffic-controller.md D.1, D.4, D.7, D.8 and A.1; engine facts
// ship-navigation-ai.md section 10.10). Portable and pure over the state the DLL reads at its sites (ship_steering.cpp) and the
// host fixture fills from its scenario; SSE2 scalar float only, no allocation, no lock, no CRT math.
//
// One board per gate (kGates, the lane board's successor under ship_steering_gates_traffic): up to kMembers members, each a
// capital that is committed to the gate (the lane board's commit test), a capital escort of a committed leader, or a capital that
// arrived through the gate less than 60 s ago. Each member carries its last live state and one rollout over the next 30 s on the
// predictor's plant (sc::plant_step for an order-4 approach, the engine's kinematics for the orders the engine moves), 61 samples at
// 0.5 s from 0.25 s steps. The hull is a capsule: the segment c +- (L - w) f with radius w, L = max(r, l). A pair is swept sample by
// sample (broad: |c_A - c_B| >= L_A + L_B + m_i; narrow: segment distance >= w_A + w_B + m_i; m_i = 50 m + k t_i +
// |u_A - u_B| dt / 2); the verdict (clear, or handed back) sits in a 24 x 24 matrix per gate with its stamp. A conflict makes the
// lower-priority member yield by speed (0.6 v, 25 m/s, the 10 m/s floor) when it is on a steered order; the first candidate whose
// rollout clears every pair is taken (site B's cap); otherwise the pair is handed back (site E passes, the sphere avoidance applies).
// Under ship_steering_gates_lane the lane (lane_frame, lane_site_b, lane_steer below) flies the members on top of this certificate; with
// the key off the certificate is all there is. Stage B (the petals, the sequencer, the release window, the lap and hold laws) was deleted
// on 2026-10-15 (docs/architecture/ship-steering-holistic-review.md, rank 2).
// Split on 2026-10-15 (docs/architecture/ship-steering-holistic-review.md rank 3) into one header per owner, included here in
// dependency order; every existing include of this header keeps working.
#include "ship_gate_board_core.h"
#include "ship_gate_geometry_core.h"
#include "ship_gate_lane_core.h"
#include "ship_gate_certificate_core.h"
#include "ship_gate_arrival_core.h"
