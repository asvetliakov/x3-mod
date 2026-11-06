// Host model of the own-ship bubble draw in the cockpit view (Run240 Flight G triage, 2026-11-06): the production
// stage (fill_bubble, bubble_box) builds the face draws for hits on the nose cap; the box rasterisation (back faces,
// near clip) and the bubble pixel program (weapon_bubble_ps.hlsl, ported) run on the CPU for an eye inside the bubble.
// Prints lit pixels per screen half. Not production code.
#include "weapon_effects_core.h"
#include "weapon_effects_stage.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;
using x3m::renderer::WeaponPatch;
struct V3 { double x, y, z; };
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
int main(int argc, char** argv) {
    // args: eye_x eye_y eye_z (owner frame, view units) pitch_deg  lane_mode(0 sky, 1 roof plane at y=eye+h above) roof_h
    double E[3] = {argc > 3 ? atof(argv[1]) : 0, argc > 3 ? atof(argv[2]) : 20, argc > 3 ? atof(argv[3]) : 300};
    const double pitch = (argc > 4 ? atof(argv[4]) : 0) * M_PI / 180.0;
    const int lane_mode = argc > 5 ? atoi(argv[5]) : 0;
    const double roof_h = argc > 6 ? atof(argv[6]) : 10;
    const double near_z = argc > 7 ? atof(argv[7]) : 1.0;
    // The view: x right, y up, z forward; the owner's axes in view (pitched about x: nose up for pitch > 0 means the
    // camera looks down at the owner? here the camera is pitched relative to the ship by `pitch`).
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    double L[3][3] = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}; // owner axis k in view
    float frame[9], centre[3];
    for (int k = 0; k < 3; ++k) for (int c = 0; c < 3; ++c) frame[3 * k + c] = float(L[k][c]);
    for (int c = 0; c < 3; ++c) centre[c] = float(-(E[0] * L[0][c] + E[1] * L[1][c] + E[2] * L[2][c]));
    ws::Bubble b; // a corvette-like hull bubble (view units: radius 55000 x 0.01 = 550)
    b.c[0] = 0; b.c[1] = 0; b.c[2] = 0; b.e[0] = 160; b.e[1] = 90; b.e[2] = 600;
    // The own ship's Run240 hits (weapon_event owner=4a80a238, frames 83513/83514, 80919, 78334/5, 75734, 73187/8 newest
    // first), owner frame engine units x 0.01.
    const int late[8][3] = {{1913, -5550, 53327}, {2868, -5163, 54400}, {-1309, -6181, 51396}, {-1045, -6423, 51372},
                            {929, -3229, 54449}, {2973, -5993, 54227}, {-194, -3859, 53328}, {-1300, -2776, 55004}};
    // frames 47136..47160 and 49713..49716 (the early fight: hits above the nose's axis)
    const int early[8][3] = {{-3805, 1282, 54322}, {-3129, 9, 54387}, {-2658, 4093, 45565}, {-4211, -224, 51521},
                             {2811, 3610, 46291}, {-1087, 3106, 52998}, {-3639, 5391, 40472}, {-5937, 2107, 52798}};
    const char* set = std::getenv("COCKPIT_HALF_HITS");
    const int (*raw)[3] = set && !std::strcmp(set, "early") ? early : late;
    if (const char* e = std::getenv("COCKPIT_HALF_E")) std::sscanf(e, "%f,%f,%f", &b.e[0], &b.e[1], &b.e[2]);
    float hits[8][3], ages[8];
    for (int h = 0; h < 8; ++h) { for (int k = 0; k < 3; ++k) hits[h][k] = raw[h][k] * 0.01f; ages[h] = 0.05f + 0.01f * h; }
    static WeaponPatch patches[64];
    unsigned left = 6;
    ws::Counts n{};
    ws::Tuning t{};
    const float colour[3] = {1, 1, 1};
    ws::fill_bubble(patches, 64, &left, centre, frame, wc::patch_corvette, b, hits, ages, 8, colour, t, &n);
    std::printf("DRAWS patches=%u dropped=%u cells_dropped=%u\n", n.patches, n.dropped, n.cells_dropped);
    const int W = 1024, H = 288; // 32:9 (the session's 5120x1440)
    const double m11 = 1.0 / 0.5625, m00 = m11 * H / W; // half_vfov_tan 0.5625 (chase_camera first_applied)
    unsigned long raster[2] = {0, 0}, lit[2] = {0, 0}, lit_uncovered[2] = {0, 0}, eye_in_box = 0, box_cut[2] = {0, 0};
    static unsigned char img[288][1024][3];
    static int draw_first[64], draw_last[64]; static unsigned long draw_lit[64][2];
    for (int k = 0; k < 64; ++k) { draw_first[k] = 1 << 20; draw_last[k] = -1; }
    static unsigned long row_lit[720], row_cut[720], cut_why[64][2][3][2], cut_side[2];
    for (unsigned i = 0; i < n.patches; ++i) {
        const WeaponPatch& p = patches[i];
        // the box in view (corner = centre + U u + V v + A a); the eye (view origin) in the face frame
        double eu = -dot({p.centre[0], p.centre[1], p.centre[2]}, {p.view_u[0], p.view_u[1], p.view_u[2]});
        double ev = -dot({p.centre[0], p.centre[1], p.centre[2]}, {p.view_v[0], p.view_v[1], p.view_v[2]});
        double ea = -dot({p.centre[0], p.centre[1], p.centre[2]}, {p.view_a[0], p.view_a[1], p.view_a[2]});
        const bool inside = eu > p.box[0] && eu < p.box[3] && ev > p.box[1] && ev < p.box[4] && ea > p.box[2] && ea < p.box[5];
        eye_in_box += inside;
        std::printf("DRAW i=%u face=%.0f,%.0f,%.0f cells=%u box_u=%.1f..%.1f box_v=%.1f..%.1f box_a=%.1f..%.1f eye_face=%.1f,%.1f,%.1f eye_in_box=%d\n",
                    i, p.anchor[0], p.anchor[1], p.anchor[2], p.cells, p.box[0], p.box[3], p.box[1], p.box[4], p.box[2],
                    p.box[5], eu, ev, ea, int(inside));
        // the normalised box (bubble_box_normalised) and the per-draw constants (bubble_block)
        double blo[3], bhi[3];
        const float* dirs[3] = {p.anchor_u, p.anchor_v, p.anchor};
        for (int k = 0; k < 3; ++k) {
            for (int f = 0; f < 3; ++f) {
                const float d = dirs[f][k];
                if (!(d > 0.5f || d < -0.5f)) continue;
                const double a = d > 0 ? p.box[f] : -p.box[3 + f], bb = d > 0 ? p.box[3 + f] : -p.box[f];
                blo[k] = (a - p.bubble_c[k]) / p.bubble_e[k];
                bhi[k] = (bb - p.bubble_c[k]) / p.bubble_e[k];
            }
        }
        double O[3];
        for (int k = 0; k < 3; ++k) {
            const double eye = -(p.centre[0] * p.axes[k][0] + p.centre[1] * p.axes[k][1] + p.centre[2] * p.axes[k][2]);
            O[k] = (eye - p.bubble_c[k]) / p.bubble_e[k];
        }
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const int half = y < H / 2 ? 0 : 1; // 0 top, 1 bottom
            const double dv[3] = {((2.0 * x / W - 1.0)) / m00, (1.0 - 2.0 * y / H) / m11, 1.0}; // view ray, z = t
            // raster: back faces of the box, near clip: the ray's exit from the slab set beyond near_z
            double tin = -1e300, tout = 1e300;
            for (int f = 0; f < 3; ++f) {
                const float* ax = f == 0 ? p.view_u : f == 1 ? p.view_v : p.view_a;
                const double o = (f == 0 ? eu : f == 1 ? ev : ea);
                const double dd = dv[0] * ax[0] + dv[1] * ax[1] + dv[2] * ax[2];
                const double lo = p.box[f], hi = p.box[3 + f];
                if (std::fabs(dd) < 1e-12) { if (o < lo || o > hi) { tin = 1; tout = 0; } continue; }
                double t0 = (lo - o) / dd, t1 = (hi - o) / dd;
                if (t0 > t1) std::swap(t0, t1);
                tin = t0 > tin ? t0 : tin; tout = t1 < tout ? t1 : tout;
            }
            const bool covered = tout >= tin && tout > near_z;
            if (covered && img[y][x][2] < 40) img[y][x][2] += 8;
            raster[half] += covered;
            // the pixel program
            double d[3];
            for (int k = 0; k < 3; ++k) d[k] = dot({dv[0], dv[1], dv[2]}, {p.axes[k][0], p.axes[k][1], p.axes[k][2]}) / p.bubble_e[k];
            const double a = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const double tm = -(O[0] * d[0] + O[1] * d[1] + O[2] * d[2]) / a;
            double hpt[3];
            for (int k = 0; k < 3; ++k) hpt[k] = O[k] + tm * d[k];
            const double disc = 1.0 - (hpt[0] * hpt[0] + hpt[1] * hpt[1] + hpt[2] * hpt[2]);
            if (!(disc > 0)) continue;
            const double ht = std::sqrt(disc / a);
            // occluder: sky, or a roof plane at owner y = E.y + roof_h (lane_mode 1) seen from inside
            double zl = 1e30;
            if (lane_mode == 1) {
                const double dy = dot({dv[0], dv[1], dv[2]}, {L[1][0], L[1][1], L[1][2]});
                if (dy > 1e-9) zl = roof_h / dy;
            }
            for (int side = 0; side < 2; ++side) {
                const double tt = side ? tm + ht : tm - ht;
                double q[3];
                for (int k = 0; k < 3; ++k) q[k] = hpt[k] + (side ? ht : -ht) * d[k];
                if (!(tt > near_z && tt < zl)) continue;
                bool inb = true;
                for (int k = 0; k < 3; ++k) inb = inb && q[k] >= blo[k] && q[k] <= bhi[k];
                double nn[3], s2 = 0;
                for (int k = 0; k < 3; ++k) { nn[k] = q[k] / p.bubble_e[k]; s2 += nn[k] * nn[k]; }
                s2 = std::sqrt(s2);
                for (int k = 0; k < 3; ++k) nn[k] /= s2;
                double m[3], sm = 0, s[3], ss = 0;
                for (int k = 0; k < 3; ++k) { m[k] = std::pow(std::fmax(std::fabs(nn[k]), 1e-6), wc::triplanar_sharpness); sm += m[k]; }
                for (int k = 0; k < 3; ++k) { s[k] = std::fmax(m[k] / sm - wc::triplanar_floor, 0.0); ss += s[k]; }
                const double na = nn[0] * p.anchor[0] + nn[1] * p.anchor[1] + nn[2] * p.anchor[2];
                const double w = na > 0 ? (s[0] * std::fabs(p.anchor[0]) + s[1] * std::fabs(p.anchor[1]) + s[2] * std::fabs(p.anchor[2])) / std::fmax(ss, 1e-6) : 0;
                if (!(w > 0)) continue;
                // the cell: plane point f = (q . e u + c . u, q . e v + c . v) in view units, its key in the map
                double fu = 0, fv = 0;
                for (int k = 0; k < 3; ++k) { fu += q[k] * p.bubble_e[k] * p.anchor_u[k] + p.bubble_c[k] * p.anchor_u[k]; fv += q[k] * p.bubble_e[k] * p.anchor_v[k] + p.bubble_c[k] * p.anchor_v[k]; }
                const float g[2] = {float(fu / p.cell), float(fv / p.cell)};
                int key[2]; float cc[2];
                ws::hex_key(g, key, cc);
                const float packed = float((key[0] + 2048) * 4096 + (key[1] + 2048));
                bool found = false;
                for (unsigned c = 0; c < p.cells && !found; ++c) found = p.cell_key[c] == packed;
                if (!found) continue;
                if (!inb) { ++box_cut[half]; ++row_cut[y]; ++cut_side[side];
                    for (int k = 0; k < 3; ++k) { if (q[k] < blo[k]) ++cut_why[i][half][k][0]; if (q[k] > bhi[k]) ++cut_why[i][half][k][1]; }
                    continue; }
                ++row_lit[y]; if (y < draw_first[i]) draw_first[i] = y; if (y > draw_last[i]) draw_last[i] = y; ++draw_lit[i][half]; img[y][x][i % 3] = 255; if (i >= 3) img[y][x][(i + 1) % 3] = 255;
                if (covered) ++lit[half]; else ++lit_uncovered[half];
            }
        }
    }
    std::printf("RESULT eye=%.0f,%.0f,%.0f pitch=%.1f lane=%d roof_h=%.0f near=%.2f eye_in_box=%lu raster_top=%lu raster_bottom=%lu lit_top=%lu lit_bottom=%lu shader_lit_not_rasterised_top=%lu shader_lit_not_rasterised_bottom=%lu\n",
                E[0], E[1], E[2], pitch * 180 / M_PI, lane_mode, roof_h, near_z, eye_in_box, raster[0], raster[1], lit[0], lit[1], lit_uncovered[0], lit_uncovered[1]);
    std::printf("BOXCUT top=%lu bottom=%lu near_side=%lu far_side=%lu\n", box_cut[0], box_cut[1], cut_side[0], cut_side[1]);
    for (unsigned i = 0; i < n.patches; ++i) for (int hf = 0; hf < 2; ++hf) {
        unsigned long tot = 0; for (int k = 0; k < 3; ++k) tot += cut_why[i][hf][k][0] + cut_why[i][hf][k][1];
        if (tot) std::printf("CUTWHY draw=%u half=%s qx_lo=%lu qx_hi=%lu qy_lo=%lu qy_hi=%lu qz_lo=%lu qz_hi=%lu\n", i, hf ? "bottom" : "top",
            cut_why[i][hf][0][0], cut_why[i][hf][0][1], cut_why[i][hf][1][0], cut_why[i][hf][1][1], cut_why[i][hf][2][0], cut_why[i][hf][2][1]); }
    int first = -1, last = -1;
    for (int y = 0; y < H; ++y) if (row_lit[y]) { if (first < 0) first = y; last = y; }
    if (const char* path = std::getenv("COCKPIT_HALF_PPM")) { FILE* fp = std::fopen(path, "wb"); std::fprintf(fp, "P6 %d %d 255\n", W, H); for (int y = 0; y < H; ++y) { img[y][W/2][0] = img[y][W/2][0] | 60; } for (int x = 0; x < W; ++x) img[H/2][x][0] |= 60; std::fwrite(img, 1, sizeof img, fp); std::fclose(fp); }
    for (unsigned i = 0; i < n.patches; ++i) std::printf("FACEROWS draw=%u face=%.0f,%.0f,%.0f first_row=%d last_row=%d centre_row=%d lit_top=%lu lit_bottom=%lu\n", i, patches[i].anchor[0], patches[i].anchor[1], patches[i].anchor[2], draw_first[i], draw_last[i], H / 2, draw_lit[i][0], draw_lit[i][1]);
    std::printf("ROWS first_lit_row=%d last_lit_row=%d of H=%d\n", first, last, H);
    for (int y = 0; y < H; y += 20) std::printf("ROW y=%d lit=%lu box_cut=%lu\n", y, row_lit[y], row_cut[y]);
}
