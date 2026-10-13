// Gate traffic controller design (docs/architecture/gate-traffic-controller.md, section Q1): host cost of the capsule
// sweep pair test. Two hulls as capsules (segment c +- (L - w) f, radius w) sampled at 61 points over T = 30 s (dt 0.5 s);
// per sample a sphere broad test (|cA - cB| >= LA + LB + m), then the segment-segment distance (Ericson 5.1.9) against
// wA + wB + m. Cases: "far" (every sample passes the broad test), "near" (every sample needs the narrow test, no conflict),
// "conflict" (stops at the first failing sample, placed at the end so the full sweep runs). float, scalar, -O2.
// Build: g++ -O2 -msse2 -mfpmath=sse -o pair_cost pair_cost.cpp && ./pair_cost
#include <chrono>
#include <cmath>
#include <cstdio>
struct V { float x, y, z; };
static inline V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// squared distance between segments p1-q1 and p2-q2
static inline float seg_seg2(V p1, V q1, V p2, V q2) {
    V d1 = sub(q1, p1), d2 = sub(q2, p2), r = sub(p1, p2);
    float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r), s, t;
    if (a <= 1e-6f && e <= 1e-6f) return dot(r, r);
    if (a <= 1e-6f) { s = 0; t = clamp01(f / e); }
    else {
        float c = dot(d1, r);
        if (e <= 1e-6f) { t = 0; s = clamp01(-c / a); }
        else {
            float b = dot(d1, d2), den = a * e - b * b;
            s = den != 0 ? clamp01((b * f - c * e) / den) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = clamp01(-c / a); } else if (t > 1) { t = 1; s = clamp01((b - c) / a); }
        }
    }
    V c1 = add(p1, mul(d1, s)), c2 = add(p2, mul(d2, t)), d = sub(c1, c2);
    return dot(d, d);
}
struct Sample { V c, f; };
struct Hull { float L, w; };
// returns the first conflicting sample index, or -1; counts narrow tests
static int sweep(const Sample* A, const Sample* B, int n, Hull ha, Hull hb, float m0, float k, float dt, unsigned* narrow) {
    for (int i = 0; i < n; ++i) {
        const float m = m0 + k * dt * float(i);
        V d = sub(A[i].c, B[i].c);
        const float rb = ha.L + hb.L + m;
        if (dot(d, d) >= rb * rb) continue;
        ++*narrow;
        const float ea = ha.L - ha.w, eb = hb.L - hb.w, rn = ha.w + hb.w + m;
        if (seg_seg2(sub(A[i].c, mul(A[i].f, ea)), add(A[i].c, mul(A[i].f, ea)), sub(B[i].c, mul(B[i].f, eb)), add(B[i].c, mul(B[i].f, eb))) < rn * rn) return i;
    }
    return -1;
}
int main() {
    const int n = 61; const float dt = 0.5f;
    static Sample A[n], B[3][n];
    for (int i = 0; i < n; ++i) {
        float t = dt * i;
        A[i] = {{0, -900, -6000 + 100 * t}, {0, 0, 1}};                         // funnel ship on its line at 100 m/s
        B[0][i] = {{6000, 2000, -9000 - 30 * t}, {0, 0, -1}};                   // far: a holder 6 km aside
        B[1][i] = {{1700, -900, -6000 + 100 * t}, {0.05f, 0, 0.9987f}};         // near: side by side 1.7 km, parallel
        B[2][i] = {{1700.0f - (i == n - 1 ? 1500.0f : 0.0f), -900, -6000 + 100 * t}, {0, 0, 1}}; // conflict only at the last sample
    }
    const Hull h = {1337, 668};
    const char* name[3] = {"far (broad only)", "near (61 narrow, clear)", "conflict at sample 61"};
    volatile int sink = 0;
    for (int c = 0; c < 3; ++c) {
        unsigned narrow = 0; const int reps = 2000000;
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; ++r) { B[c][r & 1 ? 0 : 0].c.x += 0.0f; sink += sweep(A, B[c], n, h, h, 50.0f, 5.0f, dt, &narrow); }
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / reps;
        std::printf("%-26s %8.1f ns per pair sweep (61 samples), narrow tests per sweep %.0f, result %d\n", name[c], ns, double(narrow) / reps,
                    sweep(A, B[c], n, h, h, 50.0f, 5.0f, dt, &narrow));
    }
    std::printf("sink %d\n", int(sink));
}
