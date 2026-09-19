// CPU fixture of --collide-descent-sse2 (docs/reverse-engineering/sector-collide.md, section 13).
//
// The reference is the engine's own code, run in place. The fixture image is linked at 0x00400000 like the game and owns
// a zero-filled section at 0x004d0000..0x0060ffff, into which it copies (from the untracked engine_ranges_inc.h that
// build_collide_descent_sse2.py extracts from the installed X3AP.exe) the narrow-phase query 0x004e2780, the descent
// 0x004e2530, the leaf 0x004e2190, the transform helpers, the SAT 0x004e3280 and reps; the per-query globals are the
// section's zeroes. Only the SAT's 24 calls of the float fabs helper are re-pointed (0x0040e710 is inside the fixture's
// own .text). Otherwise nothing is relocated, so the production module runs with its real addresses: initialize() with its
// body hashes, the real call site 0x004e2956, the real globals, and `call 0x004e2190` for the leaf. The leaf's entry
// holds a five-byte jump to the fixture's recorder (the census's site-8 shape), the descent's entry optionally a
// five-byte jump to an entry counter (the census's site-7 shape), and the SAT call 0x004e25a3 is pointed at a visit
// recorder in front of either SAT. Every tree pair goes through the real query 0x004e2780 in five passes:
//   engine + SSE2 SAT (reference; the state --collide-sat-sse2 leaves), engine + engine SAT, the production thunk
//   installed by initialize(), the core template with a recording Env (double), and the same in float (a proxy for an
//   x87 at 24-bit precision control), and the digests are compared pair by pair. Cost: the fastest of 24 queries of
//   one deeply overlapping pair (about 2.1e5 node pairs, as the measured station/ship pair) per configuration.
#include "../../src/proxy/collide_descent_sse2.h"
#include "../../src/proxy/collide_sat_sse2.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <xmmintrin.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
namespace x3m { void log(const char* format, ...) {
    char text[512]; std::va_list a; va_start(a, format); std::vsnprintf(text, sizeof text, format, a); va_end(a);
    static unsigned lines = 0; if (lines++ < 12) std::printf("%s\n", text);
} }
namespace x3m::object_trace { bool executable_verified() { return true; } }
namespace descent = x3m::collide_descent_sse2;
namespace core = x3m::collide_descent_sse2::core;
namespace engine_patch = x3m::engine_patch;
using core::Node;
#include "engine_ranges_inc.h"

struct State { std::uint32_t ebx, ebp, esi, edi, ecx, edx, eax; unsigned char env[28]; std::uint32_t mxcsr; };
extern "C" {
volatile std::uint32_t x3m_collide_narrow_counters[5] = {0, 0, 0, 0, 0};
std::uint32_t fx_entries = 0;
int __cdecl fx_leaf_record(const Node* a, const Node* b);
void __cdecl fx_record_visit(const Node* a, const Node* b, const float* R, const float* T, const float* bs);
int __cdecl fx_query(const std::uint32_t* model_a, const std::uint32_t* model_b, const float* R1, const float* T1, float s1, const float* R2, const float* T2, float s2, int mode);
void __cdecl fx_call_state(void* fn, const Node* a, const Node* b, const float* R, const float* T, float s, State* out);
void fx_leaf(); void fx_entry_stub(); void fx_sat_record_s(); void fx_sat_record_v();
}
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _fx_leaf
_fx_leaf:
    push eax
    push esi
    call _fx_leaf_record
    add esp, 8
    ret
    .p2align 4
    .globl _fx_entry_stub
_fx_entry_stub:
    inc dword ptr [_fx_entries]
    .byte 0xa1
    .long 0x0060854c
    push 0x004e2535
    ret
    .p2align 4
    .globl _fx_sat_record_s
_fx_sat_record_s:
    pushad
    mov eax, dword ptr [esp+40]
    sub eax, 0x30
    mov ecx, dword ptr [esp+36]
    push edi
    push ecx
    push esi
    push ebp
    push eax
    call _fx_record_visit
    add esp, 20
    popad
    jmp _x3m_collide_sat_thunk
    .p2align 4
    .globl _fx_sat_record_v
_fx_sat_record_v:
    pushad
    mov eax, dword ptr [esp+40]
    sub eax, 0x30
    mov ecx, dword ptr [esp+36]
    push edi
    push ecx
    push esi
    push ebp
    push eax
    call _fx_record_visit
    add esp, 20
    popad
    push 0x004e3280
    ret
    .p2align 4
    .globl _fx_query
_fx_query:
    push ebx
    push ebp
    push esi
    push edi
    mov edx, dword ptr [esp+20]
    mov ecx, dword ptr [esp+24]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    push dword ptr [esp+52]
    mov eax, 0x004e2780
    call eax
    add esp, 28
    pop edi
    pop esi
    pop ebp
    pop ebx
    ret
    .p2align 4
    .globl _fx_call_state
_fx_call_state:
    push ebx
    push ebp
    push esi
    push edi
    push dword ptr [esp+40]
    push dword ptr [esp+40]
    push dword ptr [esp+40]
    push dword ptr [esp+40]
    push dword ptr [esp+40]
    mov eax, dword ptr [esp+40]
    mov ebx, 0x1b1b1b1b
    mov ebp, 0x2b2b2b2b
    mov esi, 0x3b3b3b3b
    mov edi, 0x4b4b4b4b
    mov ecx, 0x5b5b5b5b
    mov edx, 0x6b6b6b6b
    call eax
    add esp, 20
    push eax
    mov eax, dword ptr [esp+48]
    mov dword ptr [eax], ebx
    mov dword ptr [eax+4], ebp
    mov dword ptr [eax+8], esi
    mov dword ptr [eax+12], edi
    mov dword ptr [eax+16], ecx
    mov dword ptr [eax+20], edx
    pop dword ptr [eax+24]
    fnstenv [eax+28]
    fldenv [eax+28]
    stmxcsr dword ptr [eax+56]
    pop edi
    pop esi
    pop ebp
    pop ebx
    ret
    .att_syntax
)");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) { ++checks; if (!okay) { ++failures; if (failures <= 40) std::printf("FAIL %s\n", label); } }
template <class P> static std::uint32_t addr(P* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
template <class T> static T& engine(std::uintptr_t va) { return *reinterpret_cast<T*>(va); }
static std::uint64_t rng = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return std::uint32_t(rng >> 16); }
static double u01() { return (rnd() + 0.5) / 4294967296.0; }
static double uniform(double lo, double hi) { return lo + (hi - lo) * u01(); }

// ---- recording ----
struct Digest { std::uint64_t full, nodes, leafs, last_contact; std::uint32_t visits_seen, leaf_calls, visit_counter, entries; std::int32_t result, contacts; };
static Digest current;
static unsigned contact_permille = 0, nonzero_permille = 0, leaf_mxcsr_seen = 0;
static std::uint32_t pair_serial = 0;
static std::uintptr_t stack_lo = ~std::uintptr_t(0), stack_hi = 0;   // span of the stack addresses the recorder ran at: the nesting witness
static std::uint64_t mix(std::uint64_t h, std::uint32_t v) { return (h ^ v) * 0x100000001b3ull; }
static std::uint32_t canonical(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return (u & 0x7fffffffu) > 0x7f800000u ? 0x7fc00000u : u; }   // any NaN: payload and sign carry no decision
void __cdecl fx_record_visit(const Node* a, const Node* b, const float* R, const float* T, const float* bs) {
    std::uint64_t h = mix(mix(current.full, addr(a)), addr(b));
    for (unsigned i = 0; i < 9; ++i) h = mix(h, canonical(R[i]));
    for (unsigned i = 0; i < 3; ++i) h = mix(mix(h, canonical(T[i])), canonical(bs[i]));
    current.full = h;
    current.nodes = mix(mix(current.nodes, addr(a)), addr(b));
    ++current.visits_seen;
    const std::uintptr_t here = reinterpret_cast<std::uintptr_t>(&h);
    if (here < stack_lo) stack_lo = here;
    if (here > stack_hi) stack_hi = here;
}
static bool leaf_counts_only = false;   // the cost runs: the recorder's hashing would otherwise be a visible share of every configuration
int __cdecl fx_leaf_record(const Node* a, const Node* b) {
    if (leaf_counts_only) { ++current.leaf_calls; return 0; }
    leaf_mxcsr_seen = _mm_getcsr() & 0xffc0u;
    std::uint64_t k = mix(mix(mix(0xcbf29ce484222325ull, addr(a)), addr(b)), pair_serial);
    k ^= k >> 29;
    // The arguments, and the engine state the real leaf could observe at this point.
    current.leafs = mix(mix(mix(mix(current.leafs, addr(a)), addr(b)), engine<std::uint32_t>(core::visits_va)), engine<std::uint32_t>(core::contacts_va));
    current.full = mix(current.full, 0x1eaf1eafu);
    current.nodes = mix(current.nodes, 0x1eaf1eafu);
    ++current.leaf_calls;
    ++engine<std::uint32_t>(0x00608548);
    if (std::uint32_t(k % 1000u) < contact_permille) { ++engine<std::int32_t>(core::contacts_va); current.last_contact = k; }
    return std::uint32_t((k >> 12) % 1000u) < nonzero_permille ? int(std::uint32_t(k) | 1u) : 0;
}

// ---- trees ----
static const float hostile_values[] = {std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), 1e-42f, -1e-42f, std::numeric_limits<float>::min(),
    1e30f, -1e30f, 1e19f, 0.0f, -0.0f};
static float hostile_value() { return hostile_values[rnd() % (sizeof hostile_values / sizeof hostile_values[0])]; }
static void rotation(float* R, double amount) {
    double q[4] = {uniform(-1, 1) * amount, uniform(-1, 1) * amount, uniform(-1, 1) * amount, amount >= 1.0 ? uniform(-1, 1) : 1.0};
    const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double& v : q) v /= n;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double m[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    for (unsigned i = 0; i < 9; ++i) R[i] = float(m[i]);
}
enum Shape { balanced, chain, ragged };
struct Tree { std::vector<Node> nodes; std::uint32_t model[6]; float extent; unsigned leaves; };
static unsigned grow(Tree& t, const float* d, unsigned leaves, Shape shape, double shrink_lo, double shrink_hi, double spread, bool root) {
    const unsigned index = unsigned(t.nodes.size());
    t.nodes.push_back(Node{});
    Node n{};
    rotation(n.R, root ? 0.05 : (rnd() % 4 ? 0.3 : 1.0));
    for (unsigned i = 0; i < 3; ++i) { n.c[i] = root ? 0.0f : float(uniform(-spread, spread) * d[i]); n.d[i] = d[i]; }
    n.triangle = &t;
    if (leaves > 1) {
        const unsigned left = shape == balanced ? leaves / 2 : shape == chain ? 1 : 1 + rnd() % (leaves - 1);
        for (unsigned side = 0; side < 2; ++side) {
            const unsigned count = side == 0 ? left : leaves - left;
            float child[3];
            for (unsigned i = 0; i < 3; ++i) child[i] = float(d[i] * uniform(shrink_lo, shrink_hi) * (count == 1 ? 0.3 : 1.0));
            if (child[1] > child[0]) { const float s = child[0]; child[0] = child[1]; child[1] = s; }
            if (child[2] > child[0]) { const float s = child[0]; child[0] = child[2]; child[2] = s; }
            const unsigned at = grow(t, child, count, shape, shrink_lo, shrink_hi, spread, false);
            (side == 0 ? n.first : n.second) = &t.nodes[0] + at;   // storage is reserved up front, so the base never moves
        }
    }
    t.nodes[index] = n;
    return index;
}
static Tree* make_tree(unsigned leaves, Shape shape, float extent, double shrink_lo, double shrink_hi, double spread) {
    Tree* t = new Tree{};
    t->nodes.reserve(2 * leaves);
    const float d[3] = {extent, extent * float(uniform(0.4, 1.0)), extent * float(uniform(0.2, 1.0))};
    grow(*t, d, leaves, shape, shrink_lo, shrink_hi, spread, true);
    t->model[0] = addr(&t->nodes[0]); t->model[5] = 3;   // [model+0] = root box, [model+0x14] == 3: built
    t->extent = extent; t->leaves = leaves;
    return t;
}
static Tree* poisoned(const Tree& source) {
    Tree* t = new Tree(source);
    const std::ptrdiff_t shift = reinterpret_cast<const char*>(&t->nodes[0]) - reinterpret_cast<const char*>(&source.nodes[0]);
    for (Node& n : t->nodes) {
        if (n.first) n.first = reinterpret_cast<const Node*>(reinterpret_cast<const char*>(n.first) + shift);
        if (n.second) n.second = reinterpret_cast<const Node*>(reinterpret_cast<const char*>(n.second) + shift);
        if (rnd() % 16 == 0) reinterpret_cast<float*>(&n)[rnd() % 15] = hostile_value();
    }
    t->model[0] = addr(&t->nodes[0]);
    return t;
}
static std::vector<Tree*> big_trees, small_trees, chains, poisoned_big_trees, poisoned_small_trees;
static Tree* bench_a = nullptr; static Tree* bench_b = nullptr;

// ---- categories ----
struct Category { const char* name; unsigned pairs; int query_mode; unsigned flags; int cap; unsigned contact, nonzero; double reach; int kind; };   // kind: 0 normal, 1 chains, 2 hostile
static const Category categories[] = {
    {"realistic", 40000, 2, 2, 1, 20, 0, 1.2, 0},
    {"overlap_no_contact", 6000, 2, 2, 1, 0, 0, 0.3, 0},
    {"first_contact", 15000, 2, 2, 1, 100, 0, 0.8, 0},
    {"cap8_distance", 15000, 1, 0xc, 8, 300, 0, 0.8, 0},
    {"cap1", 10000, 1, 4, 1, 300, 0, 0.8, 0},
    {"all_contacts", 10000, 1, 0, 0, 300, 0, 0.8, 0},
    {"deep_unbalanced", 150, 1, 0, 0, 50, 0, 0.05, 1},
    {"hostile", 25000, 2, 2, 1, 50, 0, 0.8, 2},
    {"leaf_nonzero", 5000, 1, 0, 0, 100, 50, 0.6, 0},
};
constexpr unsigned category_count = sizeof categories / sizeof categories[0];
static std::vector<Digest> run_pass(const char* name) {
    std::vector<Digest> out;
    stack_lo = ~std::uintptr_t(0); stack_hi = 0;
    rng = 0x2545f4914f6cdd1dull;
    pair_serial = 0;
    const double started = double(GetTickCount());
    for (const Category& c : categories) {
        engine<std::uint32_t>(core::flags_va) = c.flags;
        engine<std::int32_t>(core::cap_va) = c.cap;
        contact_permille = c.contact; nonzero_permille = c.nonzero;
        for (unsigned p = 0; p < c.pairs; ++p, ++pair_serial) {
            const bool hostile_tree = c.kind == 2 && p % 2 == 0;
            const Tree* a = c.kind == 1 ? chains[rnd() % chains.size()] : hostile_tree ? poisoned_big_trees[rnd() % poisoned_big_trees.size()] : big_trees[rnd() % big_trees.size()];
            const Tree* b = c.kind == 1 ? chains[rnd() % chains.size()] : hostile_tree ? poisoned_small_trees[rnd() % poisoned_small_trees.size()] : small_trees[rnd() % small_trees.size()];
            float R1[9], R2[9], T1[3], T2[3];
            rotation(R1, 1.0); rotation(R2, 1.0);
            float s1 = float(uniform(0.5, 2.0)), s2 = float(uniform(0.5, 2.0));
            const double reach = uniform(0.0, c.reach) * (double(a->extent) * s1 + double(b->extent) * s2);
            for (unsigned i = 0; i < 3; ++i) { T1[i] = float(uniform(-100, 100)); T2[i] = T1[i] + float(uniform(-1, 1) * reach); }
            if (c.kind == 2 && p % 2 == 1) {   // one poisoned slot of the query's own inputs
                const unsigned slot = rnd() % 26;
                const float v = hostile_value();
                if (slot < 9) R1[slot] = v; else if (slot < 18) R2[slot - 9] = v; else if (slot < 21) T1[slot - 18] = v; else if (slot < 24) T2[slot - 21] = v; else (slot == 24 ? s1 : s2) = v;
            }
            current = Digest{};
            current.full = current.nodes = current.leafs = 0xcbf29ce484222325ull;
            fx_entries = 0;
            const std::uint32_t census = x3m_collide_narrow_counters[1];
            current.result = fx_query(a->model, b->model, R1, T1, s1, R2, T2, s2, c.query_mode);
            current.contacts = engine<std::int32_t>(core::contacts_va);
            current.visit_counter = engine<std::uint32_t>(core::visits_va);
            current.entries = fx_entries + (x3m_collide_narrow_counters[1] - census);
            out.push_back(current);
        }
    }
    std::printf("PASS %s pairs=%u seconds=%.1f stack_span_bytes=%lu\n", name, unsigned(out.size()), (double(GetTickCount()) - started) / 1000.0, static_cast<unsigned long>(stack_hi > stack_lo ? stack_hi - stack_lo : 0));
    return out;
}
static void write_code(std::uintptr_t va, const void* bytes, unsigned n) { std::memcpy(reinterpret_cast<void*>(va), bytes, n); FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(va), n); }
template <class P> static void point_rel32(std::uintptr_t opcode_va, unsigned char opcode, P* target) {
    unsigned char code[5] = {opcode};
    const std::uint32_t rel = addr(target) - std::uint32_t(opcode_va + 5);
    std::memcpy(code + 1, &rel, 4);
    write_code(opcode_va, code, 5);
}
static double seconds() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return double(c.QuadPart) / double(f.QuadPart); }

// The core with a recording Env, entered through the real call site like the thunk.
struct RecordingEnv {
    std::int32_t contacts() const { return engine<std::int32_t>(core::contacts_va); }
    std::int32_t first_contact() const { return engine<std::int32_t>(core::first_contact_va); }
    unsigned flags() const { return engine<unsigned char>(core::flags_va); }
    std::int32_t cap() const { return engine<std::int32_t>(core::cap_va); }
    void add_visits(std::uint32_t n) const { engine<std::uint32_t>(core::visits_va) += n; }
    void add_entries(std::uint32_t n) const { fx_entries += n; }
    void visit(const core::Pair& p, const float* bs) const { fx_record_visit(p.a, p.b, p.R, p.T, bs); }
    int leaf(const Node* a, const Node* b) const { return x3m_collide_descent_leaf(a, b); }
};
extern "C" int __cdecl fx_core_double(const Node* a, const Node* b, const float* R, const float* T, float s) { RecordingEnv env; return core::descend<double>(a, b, R, T, s, env); }
extern "C" int __cdecl fx_core_float(const Node* a, const Node* b, const float* R, const float* T, float s) { RecordingEnv env; return core::descend<float>(a, b, R, T, s, env); }

struct Tally { unsigned pairs = 0, full = 0, nodes = 0, leafs = 0, outputs = 0, entries = 0; unsigned long long visits = 0, leaf_calls = 0, contacts = 0; };
static bool same_outputs(const Digest& x, const Digest& y) { return x.result == y.result && x.contacts == y.contacts && x.visit_counter == y.visit_counter && x.last_contact == y.last_contact && x.leaf_calls == y.leaf_calls; }
static void compare(const char* label, const std::vector<Digest>& reference, const std::vector<Digest>& other, int recorded, bool strict, Tally* totals) {   // recorded: 0 leaf calls and outputs only, 1 + node sequence, 2 + composed transforms
    unsigned at = 0;
    for (const Category& c : categories) {
        Tally t;
        for (unsigned p = 0; p < c.pairs; ++p, ++at) {
            const Digest& x = reference[at]; const Digest& y = other[at];
            ++t.pairs; t.visits += x.visit_counter; t.leaf_calls += x.leaf_calls; t.contacts += unsigned(x.contacts);
            if (recorded == 2 && x.full != y.full) ++t.full;
            if (recorded >= 1 && (x.nodes != y.nodes || x.visits_seen != y.visits_seen)) ++t.nodes;
            if (x.leafs != y.leafs) { if (++t.leafs <= 3 && strict) std::printf("DETAIL %s %s pair=%u leaf sequence differs (%u vs %u calls)\n", label, c.name, at, x.leaf_calls, y.leaf_calls); }
            if (!same_outputs(x, y)) ++t.outputs;
            if (x.entries != y.entries) ++t.entries;
        }
        std::printf("COMPARE %s category=%s pairs=%u visits=%llu leaf_calls=%llu contacts=%llu transform_sequence_differs=%u node_sequence_differs=%u leaf_sequence_differs=%u outputs_differ=%u entries_differ=%u\n",
                    label, c.name, t.pairs, t.visits, t.leaf_calls, t.contacts, t.full, t.nodes, t.leafs, t.outputs, t.entries);
        if (totals) { totals->pairs += t.pairs; totals->full += t.full; totals->nodes += t.nodes; totals->leafs += t.leafs; totals->outputs += t.outputs; totals->entries += t.entries;
                      totals->visits += t.visits; totals->leaf_calls += t.leaf_calls; totals->contacts += t.contacts; }
    }
}

// The arena is the fixture image's own section .engine, linked at 0x004d0000 (build_collide_descent_sse2.py): by the time
// main() runs the low addresses are otherwise taken (Wine maps its locale files at 0x00440000 when the image leaves room).
constexpr std::uintptr_t arena_va = 0x004d0000;
__attribute__((section(".engine"))) unsigned char engine_arena[0x00610000 - arena_va];   // 0x004d0000..0x0060ffff, zero-filled
static bool map_engine() {
    if (reinterpret_cast<std::uintptr_t>(engine_arena) != arena_va) { std::printf("FAIL arena at %p\n", static_cast<void*>(engine_arena)); return false; }
    DWORD old_protection = 0;
    if (!VirtualProtect(engine_arena, sizeof engine_arena, PAGE_EXECUTE_READWRITE, &old_protection)) { std::printf("FAIL arena protection error=%lu\n", GetLastError()); return false; }
    // 0x0040e710 is inside the fixture's own .text, so the 15-byte position-independent float `fabs` helper 0x0040e710 lives at
    // fabs_copy_va instead and the SAT's 24 calls of it (nothing else here calls it: the leaf is stubbed) are re-pointed.
    constexpr std::uintptr_t fabs_va = 0x0040e710, fabs_copy_va = 0x004d0100, sat_va = 0x004e3280, sat_end = 0x004e38ae;
    for (const FxEngineRange& r : fx_engine_ranges) std::memcpy(reinterpret_cast<void*>(r.va == fabs_va ? fabs_copy_va : r.va), fx_engine_bytes + r.offset, r.length);
    unsigned repointed = 0;
    for (std::uintptr_t at = sat_va; at + 5 <= sat_end; ++at) {
        std::int32_t rel; std::memcpy(&rel, reinterpret_cast<void*>(at + 1), 4);
        if (engine<unsigned char>(at) != 0xe8 || std::uint32_t(at + 5 + rel) != fabs_va) continue;
        rel = std::int32_t(fabs_copy_va - (at + 5)); std::memcpy(reinterpret_cast<void*>(at + 1), &rel, 4); ++repointed;
    }
    if (repointed != 24) { std::printf("FAIL fabs calls repointed=%u\n", repointed); return false; }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}
// Where a visit's time goes: the pieces in isolation on overlapping pairs (every axis evaluated), 1,024 inputs, best of 8 sweeps.
static void micro() {
    struct Input { core::Pair pair; float bs[4]; Node a, b; };
    std::vector<Input> inputs(1024);
    rng = 0x51ed270b1ull;
    for (Input& in : inputs) {
        in = Input{};
        rotation(in.pair.R, 1.0); rotation(in.a.R, 0.3); rotation(in.b.R, 0.3);
        for (unsigned i = 0; i < 3; ++i) { in.pair.T[i] = float(uniform(-0.2, 0.2)); in.bs[i] = float(uniform(1, 2)); in.a.d[i] = in.b.d[i] = float(uniform(1, 2)); in.a.c[i] = in.b.c[i] = float(uniform(-0.1, 0.1)); }
        in.pair.a = &in.a; in.pair.b = &in.b;
    }
    const auto fold = [](const core::Pair& p) { std::uint32_t words[12], all = 0; std::memcpy(words, p.R, 48); for (std::uint32_t w : words) all ^= w; return unsigned(all); };   // every output is used
    const auto best = [&](const char* name, auto&& body) {
        double ns = 1e30; unsigned sink = 0;
        for (unsigned sweep = 0; sweep < 8; ++sweep) {
            const double started = seconds();
            for (unsigned repeat = 0; repeat < 64; ++repeat) for (Input& in : inputs) sink += body(in);
            const double each = (seconds() - started) * 1e9 / (64.0 * double(inputs.size()));
            if (each < ns) ns = each;
        }
        std::printf("MICRO %s ns=%.2f sink=%u\n", name, ns, sink);
    };
    best("sat_scalar_full", [](Input& in) { return unsigned(x3m::collide_sat_sse2::core::obb_disjoint(in.pair.R, in.bs, in.pair.T, in.a.d)); });
    best("compose_split_a", [&](Input& in) { double P[9], T[3]; for (unsigned i = 0; i < 9; ++i) P[i] = in.pair.R[i]; for (unsigned i = 0; i < 3; ++i) T[i] = in.pair.T[i];
                                            core::Pair out; core::compose<double>(out, in.pair, P, T, 1.25, true, &in.a); return fold(out); });
    best("compose_split_b", [&](Input& in) { double P[9], T[3]; for (unsigned i = 0; i < 9; ++i) P[i] = in.pair.R[i]; for (unsigned i = 0; i < 3; ++i) T[i] = in.pair.T[i];
                                            core::Pair out; core::compose<double>(out, in.pair, P, T, 1.25, false, &in.b); return fold(out); });
    best("pair_copy", [](Input& in) { core::Pair copy = in.pair; in.pair = copy; return unsigned(copy.a != nullptr); });
    best("empty", [](Input& in) { return unsigned(in.pair.a != nullptr); });
}
static double bench(const char* name, unsigned rounds, unsigned long long* visits_out, unsigned* leaf_out) {
    float R1[9], R2[9]; const float T1[3] = {10, 20, 30}; float T2[3];
    rng = 0x1234567887654321ull;
    rotation(R1, 1.0); rotation(R2, 1.0);
    for (unsigned i = 0; i < 3; ++i) T2[i] = T1[i] + 0.05f * bench_a->extent;
    engine<std::uint32_t>(core::flags_va) = 2; engine<std::int32_t>(core::cap_va) = 1; contact_permille = nonzero_permille = 0;
    current = Digest{};
    leaf_counts_only = true;
    unsigned long long visits = 0;
    fx_query(bench_a->model, bench_b->model, R1, T1, 1.0f, R2, T2, 1.0f, 2);   // warm
    current.leaf_calls = 0;
    double ns = 1e30;   // the fastest single query: the machine is shared, so a mean would measure its other load
    for (unsigned r = 0; r < rounds; ++r) {
        const double started = seconds();
        fx_query(bench_a->model, bench_b->model, R1, T1, 1.0f, R2, T2, 1.0f, 2);
        const double elapsed = seconds() - started;
        const std::uint32_t seen = engine<std::uint32_t>(core::visits_va);
        visits += seen;
        if (seen && elapsed * 1e9 / seen < ns) ns = elapsed * 1e9 / seen;
    }
    std::printf("BENCH %s rounds=%u visits_per_query=%llu leaf_calls_per_query=%u ns_per_visit=%.2f\n", name, rounds, visits / rounds, current.leaf_calls / rounds, ns);
    *visits_out = visits / rounds; *leaf_out = current.leaf_calls / rounds;
    leaf_counts_only = false;
    return ns;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!map_engine()) { std::printf("COLLIDE DESCENT SSE2 CPU checks=1 failures=1\n"); return 1; }
    unsigned char pristine_site[5], pristine_entry[5], pristine_sat[5];
    std::memcpy(pristine_site, reinterpret_cast<void*>(core::descent_site_va), 5);
    std::memcpy(pristine_entry, reinterpret_cast<void*>(core::descent_target_va), 5);
    std::memcpy(pristine_sat, reinterpret_cast<void*>(0x004e25a3), 5);
    point_rel32(core::leaf_va, 0xe9, &fx_leaf);   // the leaf's entry: five bytes, as the census's site 8

    // ---- refusals: every one leaves the site untouched ----
    const auto site_pristine = [&] { return !std::memcmp(pristine_site, &engine<unsigned char>(core::descent_site_va), 5); };
    SetEnvironmentVariableW(L"X3M_COLLIDE_DESCENT_SSE2", nullptr);
    check(!descent::initialize() && !std::strcmp(descent::state(), "disabled") && site_pristine(), "unset: disabled");
    SetEnvironmentVariableW(L"X3M_COLLIDE_DESCENT_SSE2", L"0");
    check(!descent::initialize() && !std::strcmp(descent::state(), "disabled") && site_pristine(), "0: disabled");
    SetEnvironmentVariableW(L"X3M_COLLIDE_DESCENT_SSE2", L"1");
    for (const std::uintptr_t va : {0x004e2600u, 0x004e2776u, 0x004e2300u, 0x004e2000u, 0x004e20e0u, 0x004dfd90u, 0x004dfe70u}) {
        engine<unsigned char>(va) ^= 0x01;
        check(!descent::initialize() && !std::strcmp(descent::state(), "body_mismatch") && site_pristine(), "changed body byte: body_mismatch");
        engine<unsigned char>(va) ^= 0x01;
    }
    for (const std::uintptr_t va : {0x004e293eu, 0x004e2955u, 0x004e295bu, 0x004e2968u}) {
        engine<unsigned char>(va) ^= 0x01;
        check(!descent::initialize() && !std::strcmp(descent::state(), "bytes_mismatch") && site_pristine(), "changed window byte: bytes_mismatch");
        engine<unsigned char>(va) ^= 0x01;
    }
    check(!descent::install_at(descent::Addresses{0, core::descent_target_va}) && !std::strcmp(descent::state(), "invalid_site"), "null site refused");
    check(!descent::install_at(descent::Addresses{core::descent_site_va, core::leaf_va}) && site_pristine(), "another callee refused");
    { const unsigned char nop = 0x90; const unsigned char call = 0xe8; write_code(core::descent_site_va, &nop, 1);
      check(!descent::install_at(descent::Addresses{core::descent_site_va, core::descent_target_va}), "not a call refused"); write_code(core::descent_site_va, &call, 1); }
    check(site_pristine(), "site pristine after the refusals");

    // ---- trees ----
    for (unsigned i = 0; i < 24; ++i) big_trees.push_back(make_tree(64u << (i % 5), i % 3 == 2 ? ragged : balanced, float(uniform(5, 50)), 0.55, 0.85, 0.5));
    for (unsigned i = 0; i < 24; ++i) small_trees.push_back(make_tree(8u << (i % 5), i % 3 == 2 ? ragged : balanced, float(uniform(0.5, 4)), 0.55, 0.85, 0.5));
    for (unsigned i = 0; i < 8; ++i) chains.push_back(make_tree(120 + 40 * i, chain, float(uniform(2, 6)), 0.985, 0.999, 0.02));
    for (unsigned i = 0; i < 8; ++i) { poisoned_big_trees.push_back(poisoned(*big_trees[i])); poisoned_small_trees.push_back(poisoned(*small_trees[i])); }
    bench_a = make_tree(16384, balanced, 40.0f, 0.80, 0.92, 0.2);   // heavily nested boxes: a small tree deep inside a big one, as the measured station/ship pair
    bench_b = make_tree(1024, balanced, 3.0f, 0.80, 0.92, 0.2);
    unsigned total_pairs = 0;
    for (const Category& c : categories) total_pairs += c.pairs;

    // ---- engine passes ----
    point_rel32(core::descent_target_va, 0xe9, &fx_entry_stub);   // entry counter, as the census's site 7
    point_rel32(0x004e25a3, 0xe8, &fx_sat_record_s);
    const std::vector<Digest> reference = run_pass("engine_sse2_sat");
    point_rel32(0x004e25a3, 0xe8, &fx_sat_record_v);
    const std::vector<Digest> vanilla = run_pass("engine_vanilla_sat");
    write_code(0x004e25a3, pristine_sat, 5);
    write_code(core::descent_target_va, pristine_entry, 5);

    micro();
    // ---- cost: engine as shipped, engine with the SSE2 SAT (run 45), the replacement ----
    unsigned long long bench_visits[3] = {}; unsigned bench_leafs[3] = {};
    const double vanilla_ns = bench("engine_vanilla", 24, &bench_visits[0], &bench_leafs[0]);
    check(x3m::collide_sat_sse2::install_at(x3m::collide_sat_sse2::Addresses{0x004e25a3, 0x004e3280}), "SAT module installs on the in-place engine");
    const double sat_ns = bench("engine_sse2_sat", 24, &bench_visits[1], &bench_leafs[1]);

    // ---- production: initialize() with the SAT call and the entry both rewritten by the other modules ----
    point_rel32(core::descent_target_va, 0xe9, &fx_entry_stub);
    check(descent::initialize() && !std::strcmp(descent::state(), "ok"), "initialize: ok next to the SAT module and an entry claim");
    { std::int32_t rel; std::memcpy(&rel, reinterpret_cast<void*>(core::descent_site_va + 1), 4);
      check(engine<unsigned char>(core::descent_site_va) == 0xe8 && std::uint32_t(core::descent_return_va + rel) == addr(&x3m_collide_descent_thunk), "site calls the thunk"); }
    check(descent::initialize(), "second initialize: already installed");
    check(!descent::install_at(descent::Addresses{core::descent_site_va, core::descent_target_va}) && !std::strcmp(descent::state(), "already_installed"), "second install refused");
    SetLastError(0x1234);
    const std::vector<Digest> production = run_pass("production_thunk");
    check(GetLastError() == 0x1234, "LastError untouched across the pass");
    const std::uint32_t census_before = x3m_collide_narrow_counters[1];
    const double descent_ns = bench("descent_sse2", 24, &bench_visits[2], &bench_leafs[2]);
    { const unsigned entries = (x3m_collide_narrow_counters[1] - census_before) / 25, descended = (entries - 1) / 2;   // 24 rounds and the warm-up; entries = 1 + 2 x descended
      std::printf("MIX visits=%llu descended=%u leaf=%u pruned_by_sat=%llu\n", bench_visits[2], descended, bench_leafs[2], bench_visits[2] - descended - bench_leafs[2]); }
    check(bench_visits[0] == bench_visits[1] && bench_visits[1] == bench_visits[2] && bench_leafs[0] == bench_leafs[2], "bench: same visits and leaf calls in all three");

    // ---- CPU state across a direct call of the thunk ----
    {
        const Tree* a = big_trees[3]; const Tree* b = small_trees[3];
        float R[9]; rotation(R, 1.0); const float T[3] = {0.5f, -0.25f, 0.125f};
        engine<std::uint32_t>(core::flags_va) = 0; engine<std::int32_t>(core::first_contact_va) = 0; contact_permille = 200; nonzero_permille = 0;
        Digest seen[3]; State states[3];
        unsigned short control_word = 0;
        asm volatile("fnstcw %0" : "=m"(control_word));
        const unsigned modes[3] = {0x1f80, 0x5f80, 0x9fc0};   // default; round up; denormals-are-zero + flush-to-zero: the last two take the bracketed path
        for (unsigned k = 0; k < 3; ++k) {
            current = Digest{}; engine<std::int32_t>(core::contacts_va) = 0; engine<std::uint32_t>(core::visits_va) = 0;
            _mm_setcsr(modes[k]); SetLastError(0x4321);
            fx_call_state(reinterpret_cast<void*>(&x3m_collide_descent_thunk), &a->nodes[0], &b->nodes[0], R, T, 1.0f, &states[k]);
            const unsigned after = _mm_getcsr(); _mm_setcsr(0x1f80);
            const State& s = states[k];
            check(s.ebx == 0x1b1b1b1b && s.ebp == 0x2b2b2b2b && s.esi == 0x3b3b3b3b && s.edi == 0x4b4b4b4b && s.ecx == 0x5b5b5b5b && s.edx == 0x6b6b6b6b, "thunk keeps EBX/EBP/ESI/EDI/ECX/EDX");
            check((s.env[8] & s.env[9]) == 0xff && (s.env[0] | (s.env[1] << 8)) == control_word, "x87 stack empty and control word unchanged");
            check(k == 0 ? (after & 0xffc0u) == modes[k] : after == modes[k] && s.mxcsr == modes[k], "MXCSR preserved");
            check(current.leaf_calls > 0 && leaf_mxcsr_seen == modes[k], "leaf runs under the caller's MXCSR");
            check(GetLastError() == 0x4321, "LastError preserved");
            seen[k] = current; seen[k].contacts = engine<std::int32_t>(core::contacts_va); seen[k].visit_counter = engine<std::uint32_t>(core::visits_va); seen[k].result = int(s.eax);
        }
        check(seen[0].leafs == seen[1].leafs && seen[0].leafs == seen[2].leafs && same_outputs(seen[0], seen[1]) && same_outputs(seen[0], seen[2]), "same result under every MXCSR");
        current = Digest{}; engine<std::int32_t>(core::contacts_va) = 0; engine<std::uint32_t>(core::visits_va) = 0;
        State direct;
        fx_call_state(reinterpret_cast<void*>(core::descent_target_va), &a->nodes[0], &b->nodes[0], R, T, 1.0f, &direct);
        Digest engine_run = current; engine_run.contacts = engine<std::int32_t>(core::contacts_va); engine_run.visit_counter = engine<std::uint32_t>(core::visits_va); engine_run.result = int(direct.eax);
        check(engine_run.leafs == seen[0].leafs && same_outputs(engine_run, seen[0]) && engine_run.leaf_calls > 0, "direct call: the engine's descent gives the same leaf sequence and outputs");
    }

    // ---- restore, then the core with a recording Env through the same site ----
    check(descent::shutdown() && site_pristine(), "shutdown restores the call exactly");
    write_code(core::descent_target_va, pristine_entry, 5);
    point_rel32(core::descent_site_va, 0xe8, &fx_core_double);
    const std::vector<Digest> core_double = run_pass("core_double_recorded");
    check(stack_hi - stack_lo > core::stack_entries * sizeof(core::Pair), "deep pairs ran in nested frames (more than 64 pending entries)");
    point_rel32(core::descent_site_va, 0xe8, &fx_core_float);
    const std::vector<Digest> core_float = run_pass("core_float_recorded");
    write_code(core::descent_site_va, pristine_site, 5);
    engine_patch::close_install_window("fixture");
    check(!descent::initialize() && !std::strcmp(descent::state(), "late_claim") && site_pristine(), "closed window: late_claim");

    // ---- verdicts ----
    Tally exact, thunk, vanilla_tally, float_tally;
    compare("core_vs_engine", reference, core_double, 2, true, &exact);
    compare("thunk_vs_engine", reference, production, 0, true, &thunk);
    compare("vanilla_sat_vs_sse2_sat", reference, vanilla, 2, false, &vanilla_tally);
    compare("float_vs_double", reference, core_float, 1, false, &float_tally);
    check(exact.pairs == total_pairs && total_pairs >= 100000, "at least 1e5 tree pairs");
    check(exact.full == 0 && exact.nodes == 0, "core: identical visit sequence, composed transforms included");
    check(exact.leafs == 0 && exact.outputs == 0 && exact.entries == 0, "core: identical leaf calls, outputs and entry count");
    check(thunk.leafs == 0 && thunk.outputs == 0 && thunk.entries == 0, "thunk: identical leaf calls (arguments, counters at the call), outputs and census entry count");
    check(exact.leaf_calls > 100000 && exact.contacts > 10000, "leaf calls and contacts are exercised");
    std::printf("SUMMARY pairs=%u visits=%llu leaf_calls=%llu contacts=%llu core_transform_differs=%u core_nodes_differs=%u core_leafs_differs=%u core_outputs_differ=%u core_entries_differ=%u "
                "thunk_leafs_differs=%u thunk_outputs_differ=%u thunk_entries_differ=%u vanilla_sat_nodes_differs=%u vanilla_sat_leafs_differs=%u float_nodes_differs=%u float_leafs_differs=%u float_outputs_differ=%u\n",
                exact.pairs, exact.visits, exact.leaf_calls, exact.contacts, exact.full, exact.nodes, exact.leafs, exact.outputs, exact.entries,
                thunk.leafs, thunk.outputs, thunk.entries, vanilla_tally.nodes, vanilla_tally.leafs, float_tally.nodes, float_tally.leafs, float_tally.outputs);
    std::printf("COLLIDE DESCENT SSE2 BENCH visits_per_query=%llu leaf_calls_per_query=%u engine_vanilla_ns=%.2f engine_sse2_sat_ns=%.2f descent_sse2_ns=%.2f\n",
                bench_visits[2], bench_leafs[2], vanilla_ns, sat_ns, descent_ns);
    std::printf("COLLIDE DESCENT SSE2 CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
