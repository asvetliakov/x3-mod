// Host driver for the chase camera's portable pipeline (src/proxy/chase_camera_math.h),
// built with the host compiler by verification/analysis/test_chase_camera.py.
// No engine, no Windows: the test feeds synthetic ship/camera poses frame by
// frame and reads the pipeline's pose, verdict and diagnostics back.
//
// stdin, one command per line:
//   T rot_tau pos_tau offset_y distance_scale lag_clamp_deg pos_lag_clamp max_dt snap_ratio [combat_tightness [snap_coalesce_frames]]
//   D   print the compiled defaults: D rot_tau pos_tau offset_y distance_scale lag_clamp_deg pos_lag_clamp combat_tightness max_dt snap_ratio snap_coalesce_frames
//   F dt mode connect ref sector ship_x ship_y ship_z ship_basis(9) boom_local_x boom_local_y boom_local_z view_rel(9) half_vfov_tan [flags_1a0 [locked]]
//       (the vanilla camera is built as view_rel * ship_basis at ship + boom_local * ship_basis, as the engine does)
//   R   reset the state (as after a refused frame / hook gap)
//   L steps dt yaw_rate roll_rate
//       long run in-process (the pipe would dominate 10^5 frames): a ship
//       turning at yaw_rate rad/s and rolling at roll_rate rad/s while flying
//       forward. Prints: L applied refused snaps max_ortho_error max_lag_deg
//       max_identity_error final_lag_deg  (identity error = |camera - view_rel * ship|).
// Each F prints: verdict snapped coalesced locked snap_reason lag_deg pos_lag distance pos(3) basis(9) view_rel(9) ortho_error
#include "../../src/proxy/chase_camera_math.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

// Token reader that accepts nan/inf (the stream extractor may not).
struct Reader {
    std::istringstream& ss;
    Reader& operator>>(double& v) { std::string tok; ss >> tok; v = tok.empty() ? 0.0 : std::strtod(tok.c_str(), nullptr); return *this; }
    template <class T> Reader& operator>>(T& v) { ss >> v; return *this; }
};

using namespace x3m::chase;

int main() {
    Tunables t; State s;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream stream(line);
        Reader ss{stream};
        char op = 0; ss >> op;
        if (op == 'T') {
            ss >> t.rot_tau >> t.pos_tau >> t.offset_y >> t.distance_scale >> t.lag_clamp_deg >> t.pos_lag_clamp >> t.max_dt >> t.snap_ratio;
            double tightness = 0, coalesce = 3; ss >> tightness >> coalesce; // optional (0 / 3 when absent)
            t.combat_tightness = tightness; t.snap_coalesce_frames = unsigned(coalesce);
            std::printf("T %d\n", int(valid(t)));
        } else if (op == 'D') {
            const Tunables d;
            std::printf("D %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %u\n", d.rot_tau, d.pos_tau, d.offset_y, d.distance_scale, d.lag_clamp_deg, d.pos_lag_clamp, d.combat_tightness, d.max_dt, d.snap_ratio, d.snap_coalesce_frames);
        } else if (op == 'R') {
            note_gap(s); std::printf("R\n");
        } else if (op == 'F') {
            Input in; double dt = 0; Vec3 boom_local; Mat3 ship;
            ss >> dt >> in.view_mode >> in.connect_mode >> in.ref_object >> in.sector >> in.ship_pos.x >> in.ship_pos.y >> in.ship_pos.z;
            for (auto& r : ship.m) for (double& v : r) ss >> v;
            ss >> boom_local.x >> boom_local.y >> boom_local.z;
            for (auto& r : in.view_rel.m) for (double& v : r) ss >> v;
            ss >> in.half_vfov_tan;
            double flags = 0, locked = 0; ss >> flags >> locked; // optional
            in.flags_1a0 = std::uint32_t(flags); in.target_locked = locked != 0;
            in.vanilla_cam = mul(in.view_rel, ship);
            in.vanilla_pos = in.ship_pos + mul(boom_local, ship);
            Pose pose;
            const Step r = step(s, in, dt, t, &pose);
            std::printf("F %u %d %d %d %u %.17g %.17g %.17g", unsigned(r.verdict), int(r.snapped), int(r.coalesced), int(r.target_locked), r.snap_reason, r.lag_deg, r.pos_lag, r.distance);
            if (r.verdict == Verdict::Applied) {
                std::printf(" %.17g %.17g %.17g", pose.pos.x, pose.pos.y, pose.pos.z);
                for (auto& row : pose.basis.m) for (double v : row) std::printf(" %.17g", v);
                for (auto& row : pose.view_rel.m) for (double v : row) std::printf(" %.17g", v);
                std::printf(" %.3g", orthonormality_error(pose.basis));
            }
            std::printf("\n");
        } else if (op == 'X') {
            // exp/log round trip and fixed-point conversions for the unit tests.
            Vec3 r; ss >> r.x >> r.y >> r.z;
            const Mat3 w = exp_rotation(r); const Vec3 back = log_rotation(w);
            std::int32_t fixed[12] = {}; const bool ok = to_fixed(w, fixed); const Mat3 again = from_fixed(fixed);
            double err = 0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) err = std::fmax(err, std::fabs(again.m[i][j] - w.m[i][j]));
            std::printf("X %.17g %.17g %.17g %.3g %d %.3g\n", back.x, back.y, back.z, orthonormality_error(w), int(ok), err);
        } else if (op == 'L') {
            double steps_d = 0, dt = 0, yaw_rate = 0, roll_rate = 0;
            ss >> steps_d >> dt >> yaw_rate >> roll_rate;
            double max_ortho = 0, max_lag = 0, max_identity = 0, last_lag = 0;
            unsigned long long applied = 0, refused = 0, snaps = 0;
            Vec3 flown; // integrated along the ship's own forward axis, as a ship flies
            for (long long n = 0; n < static_cast<long long>(steps_d); ++n) {
                const double time = double(n) * dt, a = yaw_rate * time, b = roll_rate * time;
                Mat3 yaw; yaw.m[0][0] = std::cos(a); yaw.m[0][2] = -std::sin(a); yaw.m[2][0] = std::sin(a); yaw.m[2][2] = std::cos(a);
                Mat3 rl; rl.m[0][0] = std::cos(b); rl.m[0][1] = std::sin(b); rl.m[1][0] = -std::sin(b); rl.m[1][1] = std::cos(b);
                const Mat3 ship = mul(rl, yaw);
                flown = flown + row(ship, 2) * (200.0 * dt); // 200 units/s forward
                Input in; in.ship_pos = flown;
                in.view_rel = Mat3{}; in.vanilla_cam = mul(in.view_rel, ship);
                in.vanilla_pos = in.ship_pos + mul(Vec3{0, 40, -200}, ship);
                in.view_mode = 2; in.ref_object = 1; in.sector = 1;
                Pose pose; const Step r = step(s, in, dt, t, &pose);
                if (r.verdict != Verdict::Applied) { ++refused; continue; }
                ++applied; if (r.snapped) ++snaps;
                last_lag = r.lag_deg;
                max_ortho = std::fmax(max_ortho, orthonormality_error(pose.basis));
                max_lag = std::fmax(max_lag, r.lag_deg);
                const Mat3 recomposed = mul(pose.view_rel, ship);
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) max_identity = std::fmax(max_identity, std::fabs(recomposed.m[i][j] - pose.basis.m[i][j]));
            }
            std::printf("L %llu %llu %llu %.3g %.17g %.3g %.17g\n", applied, refused, snaps, max_ortho, max_lag, max_identity, last_lag);
        } else if (op == 'S') {
            // spring step response: x0 v0 tau dt steps
            Spring sp; double tau, dt; int steps; ss >> sp.x.x >> sp.v.x >> tau >> dt >> steps;
            for (int i = 0; i < steps; ++i) spring_step(sp, tau, dt);
            std::printf("S %.17g %.17g\n", sp.x.x, sp.v.x);
        }
        std::fflush(stdout);
    }
    return 0;
}
