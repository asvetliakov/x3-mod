// Feasibility measurement for front tracking of the collision descent (docs/reverse-engineering/sector-collide.md,
// section 14.9). Fixture-side only: no production code, no engine bytes. The descent is collide_descent_replica_core.h,
// the C++ replica that section 13 proved bit-identical to the engine's 0x004e2530 (visit order, composed transforms,
// leaf calls) over 126,150 tree pairs, with the SSE2 SAT of collide_sat_sse2_core.h; per node pair it costs what the
// engine's own descent costs with
// --collide-sat-sse2 (31.0 against 31.4 ns there), so "full" below stands for the flight's per-frame query.
//
// Front tracking: a query that reached no leaf leaves its visit tree in preorder (descended pairs and pruned pairs).
// The next frame walks that tree instead of the models: a descended pair gets its two child transforms composed and NO
// SAT; a pruned pair gets one SAT (optionally its last separating axis first); a pruned pair that now overlaps is
// descended exactly as the engine would, its new pairs spliced in; a leaf pair anywhere abandons the front and the full
// query runs. The front always covers the pair space (pairs are only ever replaced by their children), and each pair's
// transform is composed along the same path as the engine's, so "every front pair disjoint" means the engine, which
// prunes at that pair or above it, reaches no leaf.
#include "collide_descent_replica_core.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace core = x3m::collide_descent_sse2::core;
namespace sat = x3m::collide_sat_sse2::core;
using core::Node;
using core::Pair;

static std::uint64_t rng = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return std::uint32_t(rng >> 16);
}
static double uniform(double lo, double hi) {
    return lo + (hi - lo) * ((rnd() + 0.5) / 4294967296.0);
}
static double now_ns() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return double(c.QuadPart) * 1e9 / double(f.QuadPart);
}
static void rotation(float* R, double amount) {
    double q[4] = {uniform(-1, 1) * amount, uniform(-1, 1) * amount, uniform(-1, 1) * amount,
                   amount >= 1.0 ? uniform(-1, 1) : 1.0};
    const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double& v : q) v /= n;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double m[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                         2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (unsigned i = 0; i < 9; ++i) R[i] = float(m[i]);
}

// ---- trees: nested boxes; leaves much smaller than their parents, so that boxes overlap deeply without leaf pairs
// meeting ----
struct Tree {
    Node* nodes;
    unsigned count, capacity;
    float extent;
    const Node* root;
};
static double leaf_shrink = 0.05;
static unsigned grow(Tree& t, const float* d, unsigned leaves, bool root, const unsigned* slots) {
    const unsigned index = t.count++;
    Node n{};
    rotation(n.R, root ? 0.05 : 0.35);
    for (unsigned i = 0; i < 3; ++i) {
        n.c[i] = root ? 0.0f : float(uniform(-0.3, 0.3) * d[i]);
        n.d[i] = d[i];
    }
    if (leaves > 1) {
        for (unsigned side = 0; side < 2; ++side) {
            const unsigned count = side == 0 ? leaves / 2 : leaves - leaves / 2;
            float child[3];
            for (unsigned i = 0; i < 3; ++i)
                child[i] = float(d[i] * uniform(0.78, 0.92) * (count == 1 ? leaf_shrink : 1.0));
            std::sort(child, child + 3, [](float x, float y) { return x > y; });
            const unsigned at = grow(t, child, count, false, slots);
            (side == 0 ? n.first : n.second) = t.nodes + slots[at];
        }
    }
    t.nodes[slots[index]] = n;
    return index;
}
// scattered: the nodes of one tree spread at random over `capacity` 0x48-byte slots (the engine allocates 2N boxes per
// model in one block, but a station is many models loaded at different times); contiguous: preorder, the best case for
// the cache.
static Tree make_tree(unsigned leaves, float extent, unsigned capacity, bool scattered) {
    Tree t{static_cast<Node*>(
               VirtualAlloc(nullptr, std::size_t(capacity) * sizeof(Node), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)),
           0, capacity, extent, nullptr};
    if (t.nodes == nullptr) {
        std::printf("FAIL allocation of %u nodes\n", capacity);
        std::exit(1);
    }
    std::vector<unsigned> slots(capacity);
    for (unsigned i = 0; i < capacity; ++i) slots[i] = i;
    if (scattered)
        for (unsigned i = capacity - 1; i > 0; --i) std::swap(slots[i], slots[rnd() % (i + 1)]);
    const float d[3] = {extent, extent * 0.8f, extent * 0.6f};
    grow(t, d, leaves, true, slots.data());
    t.root = t.nodes + slots[0];
    return t;
}

// ---- the plain query (what the engine does every frame) ----
struct CountingEnv {
    std::uint32_t visits = 0, leaves = 0;
    std::int32_t contacts() const { return 0; }
    std::int32_t first_contact() const { return 1; }
    unsigned flags() const { return 2; }
    std::int32_t cap() const { return 1; }
    void add_visits(std::uint32_t n) { visits += n; }
    void add_entries(std::uint32_t) {}
    void visit(const Pair&, const float*) const {}
    int leaf(const Node*, const Node*) {
        ++leaves;
        return 0;
    }
};

// ---- the front ----
enum Kind : std::uint8_t { pruned, split_a, split_b };
struct Record {
    const Node* a;
    const Node* b;
    std::uint8_t kind, axis;
};
struct Stats {
    std::uint64_t sat_tests = 0, axis_first_hits = 0, composes = 0, regrown = 0;
};
static bool use_cached_axis = true;

// One axis of core::obb_disjoint, the same expressions (1..15).
static bool axis_separates(unsigned k, const float* R, const float* bx, const float* Tx, const float* ax) {
    const auto f = [&](unsigned i) { return sat::bf(R[i]); };
    const double a0 = ax[0], a1 = ax[1], a2 = ax[2], b0 = bx[0], b1 = bx[1], b2 = bx[2], T0 = Tx[0], T1 = Tx[1],
                 T2 = Tx[2];
    const auto r = [&](unsigned i) { return double(R[i]); };
    const auto tf = [](double v) { return sat::through_float(v); };
    switch (k) {
    case 1: return sat::separates(sat::abs_f(Tx[0]), ((b2 * f(2) + b1 * f(1)) + b0 * f(0)) + a0);
    case 2: return sat::separates(tf((T1 * r(3) + T2 * r(6)) + r(0) * T0), ((a2 * f(6) + a1 * f(3)) + a0 * f(0)) + b0);
    case 3: return sat::separates(sat::abs_f(Tx[1]), ((b2 * f(5) + b1 * f(4)) + b0 * f(3)) + a1);
    case 4: return sat::separates(sat::abs_f(Tx[2]), ((b2 * f(8) + b1 * f(7)) + b0 * f(6)) + a2);
    case 5: return sat::separates(tf((T2 * r(7) + r(4) * T1) + r(1) * T0), ((a2 * f(7) + a1 * f(4)) + a0 * f(1)) + b1);
    case 6: return sat::separates(tf((T0 * r(2) + T1 * r(5)) + r(8) * T2), ((a2 * f(8) + a1 * f(5)) + a0 * f(2)) + b2);
    case 7: return sat::separates(tf(T2 * r(3) - T1 * r(6)), ((b1 * f(2) + b2 * f(1)) + a1 * f(6)) + a2 * f(3));
    case 8: return sat::separates(tf(r(4) * T2 - T1 * r(7)), ((b2 * f(0) + a1 * f(7)) + a2 * f(4)) + b0 * f(2));
    case 9: return sat::separates(tf(T2 * r(5) - r(8) * T1), ((b1 * f(0) + a1 * f(8)) + a2 * f(5)) + b0 * f(1));
    case 10: return sat::separates(tf(T0 * r(6) - T2 * r(0)), ((b1 * f(5) + b2 * f(4)) + a0 * f(6)) + a2 * f(0));
    case 11: return sat::separates(tf(T0 * r(7) - T2 * r(1)), ((b2 * f(3) + a0 * f(7)) + a2 * f(1)) + b0 * f(5));
    case 12: return sat::separates(tf(r(8) * T0 - T2 * r(2)), ((b1 * f(3) + a0 * f(8)) + a2 * f(2)) + b0 * f(4));
    case 13: return sat::separates(tf(T1 * r(0) - T0 * r(3)), ((b1 * f(8) + b2 * f(7)) + a0 * f(3)) + a1 * f(0));
    case 14: return sat::separates(tf(T1 * r(1) - r(4) * T0), ((b2 * f(6) + a0 * f(4)) + a1 * f(1)) + b0 * f(8));
    default: return sat::separates(tf(T1 * r(2) - r(5) * T0), ((b1 * f(6) + a0 * f(5)) + a1 * f(2)) + b0 * f(7));
    }
}
static void children(const Pair& p, bool split_a_side, float s, Pair& second, Pair& first) {
    double P[9], T[3];
    for (unsigned i = 0; i < 9; ++i) P[i] = p.R[i];
    for (unsigned i = 0; i < 3; ++i) T[i] = p.T[i];
    const Node* const split = split_a_side ? p.a : p.b;
    core::compose<double>(second, p, P, T, double(s), split_a_side, split->second);
    core::compose<double>(first, p, P, T, double(s), split_a_side, split->first);
}
// The engine's descent of one pair, recorded in preorder. False: a leaf pair was reached (the front is abandoned).
static bool grow_front(const Pair& p, float s, std::vector<Record>& out, Stats& st) {
    const float bs[3] = {float(double(p.b->d[0]) * double(s)), float(double(p.b->d[1]) * double(s)),
                         float(double(p.b->d[2]) * double(s))};
    ++st.sat_tests;
    const int axis = sat::obb_disjoint(p.R, bs, p.T, p.a->d);
    if (axis != 0) {
        out.push_back(Record{p.a, p.b, pruned, std::uint8_t(axis)});
        return true;
    }
    const bool a_leaf = !p.a->first && !p.a->second, b_leaf = !p.b->first && !p.b->second;
    if (a_leaf && b_leaf) return false;
    const bool side = b_leaf || (!a_leaf && p.b->d[0] < p.a->d[0]);
    out.push_back(Record{p.a, p.b, side ? split_a : split_b, 0});
    Pair second, first;
    children(p, side, s, second, first);
    st.composes += 2;
    return grow_front(second, s, out, st) && grow_front(first, s, out, st);
}
// One frame over last frame's front. False: abandoned.
static bool walk_front(const Pair& p, float s, const Record*& cursor, std::vector<Record>& out, Stats& st) {
    const Record r = *cursor++;
    if (r.kind == pruned) {
        const float bs[3] = {float(double(p.b->d[0]) * double(s)), float(double(p.b->d[1]) * double(s)),
                             float(double(p.b->d[2]) * double(s))};
        if (use_cached_axis && axis_separates(r.axis, p.R, bs, p.T, p.a->d)) {
            ++st.sat_tests;
            ++st.axis_first_hits;
            out.push_back(r);
            return true;
        }
        ++st.regrown;
        return grow_front(p, s, out, st); // the whole SAT; still disjoint: one record again; overlapping: the engine's
                                          // descent of this pair
    }
    out.push_back(r);
    Pair second, first;
    children(p, r.kind == split_a, s, second, first);
    st.composes += 2;
    return walk_front(second, s, cursor, out, st) && walk_front(first, s, cursor, out, st);
}

static double median(std::vector<double>& v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned failures = 0;
    for (const bool scattered : {false, true}) {
        rng = 0x1234567811223344ull;
        const Tree a = make_tree(1u << 17, 4000.0f, scattered ? (1u << 21) : (1u << 18), scattered); // 131,072 leaves;
                                                                                                     // scattered over
                                                                                                     // 144 MB
        const Tree b = make_tree(1u << 9, 120.0f, scattered ? (1u << 14) : (1u << 10), scattered);
        // The deepest contact-free, leaf-free placement out of 300.
        Pair best{};
        std::uint32_t most = 0;
        for (unsigned attempt = 0; attempt < 300; ++attempt) {
            Pair p{};
            p.a = a.root;
            p.b = b.root;
            rotation(p.R, 1.0);
            for (unsigned i = 0; i < 3; ++i) p.T[i] = float(uniform(-0.6, 0.6) * a.extent);
            CountingEnv env;
            core::descend_pair<double>(p, 1.0f, env);
            if (env.leaves == 0 && env.visits > most) {
                most = env.visits;
                best = p;
            }
        }
        std::printf("SCENE layout=%s a_nodes=%u b_nodes=%u visits=%u\n", scattered ? "scattered" : "contiguous",
                    a.count, b.count, most);
        if (most < 1000) {
            ++failures;
            continue;
        }
        const double speeds[] = {1, 5, 20};
        for (const double speed : speeds)
            for (const bool cached : {true, false}) {
                use_cached_axis = cached;
                Pair p = best;
                float direction[3];
                {
                    float R[9];
                    rotation(R, 1.0);
                    direction[0] = R[0];
                    direction[1] = R[3];
                    direction[2] = R[6];
                }
                std::vector<Record> front, next;
                Stats grown;
                bool have_front = grow_front(p, 1.0f, front, grown);
                std::vector<double> full_ns, front_ns, visits, tests, hit_rate, sizes;
                unsigned abandoned = 0, first_abandon = 0, frames = 0, unsound = 0;
                for (unsigned frame = 1; frame <= 200; ++frame) {
                    for (unsigned i = 0; i < 3; ++i) p.T[i] += float(speed) * direction[i];
                    float turn[9], turned[9];
                    rotation(turn, 0.002);
                    for (unsigned i = 0; i < 3; ++i)
                        for (unsigned j = 0; j < 3; ++j)
                            turned[3 * i + j] = turn[3 * i] * p.R[j] + turn[3 * i + 1] * p.R[3 + j] +
                                                turn[3 * i + 2] * p.R[6 + j];
                    std::memcpy(p.R, turned, sizeof turned);
                    CountingEnv env;
                    double t0 = now_ns();
                    core::descend_pair<double>(p, 1.0f, env);
                    const double full = now_ns() - t0;
                    if (env.leaves != 0) break; // the scene has left the leaf-free regime: the comparison ends here
                    Stats st;
                    next.clear();
                    next.reserve(front.size() + 4096);
                    t0 = now_ns();
                    bool ok = false;
                    if (have_front) {
                        const Record* cursor = front.data();
                        ok = walk_front(p, 1.0f, cursor, next, st);
                    }
                    if (!ok) {
                        next.clear();
                        have_front = grow_front(p, 1.0f, next, st);
                        ++abandoned;
                        if (!first_abandon) first_abandon = frame;
                    } // the full re-walk, on the front's clock
                    else
                        have_front = true;
                    const double tracked = now_ns() - t0;
                    front.swap(next);
                    if (ok && env.leaves != 0) ++unsound;
                    ++frames;
                    full_ns.push_back(full);
                    front_ns.push_back(tracked);
                    visits.push_back(env.visits);
                    tests.push_back(double(st.sat_tests));
                    hit_rate.push_back(st.sat_tests ? double(st.axis_first_hits) / double(st.sat_tests) : 0.0);
                    sizes.push_back(double(front.size()));
                }
                failures += unsound;
                const double v = median(visits), f = median(full_ns), t = median(front_ns);
                std::printf(
                    "RUN layout=%s speed=%.0f cached_axis=%u frames=%u visits=%.0f full_ns=%.0f full_ns_per_visit=%.1f front_ns=%.0f front_sat_tests=%.0f front_records=%.0f axis_first_hit_rate=%.3f "
                    "abandoned=%u first_abandon_frame=%u unsound=%u speedup=%.2f\n",
                    scattered ? "scattered" : "contiguous", speed, cached ? 1u : 0u, frames, v, f, v ? f / v : 0.0, t,
                    median(tests), median(sizes), median(hit_rate), abandoned, first_abandon, unsound, t ? f / t : 0.0);
            }
    }
    std::printf("COLLIDE FRONT FEASIBILITY failures=%u\n", failures);
    return failures ? 1 : 0;
}
