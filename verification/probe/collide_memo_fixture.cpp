// CPU fixture of --collide-memo (docs/reverse-engineering/sector-collide.md, section 14).
//
// The reference is the engine's own code, run in place. The fixture image is linked at 0x00340000 and owns a zero-filled
// section over the game's addresses, 0x00400000..0x0066ffff, into which it copies (from the untracked engine_ranges_inc.h that
// build_collide_memo.py extracts from the installed X3AP.exe) the mesh-pair query 0x0047f1b0 and everything below it:
// 0x004e29f0, the query 0x004e2780, the descent, the real leaf and triangle tests, the SAT, the helpers, ftol and the
// constants, every byte at its own address and none of them changed.
// The production module therefore runs with its real addresses: initialize() with its body hashes, the real call
// site 0x0047f329, the real globals. A second copy of 0x0047f1b0 at 0x004a0000, whose one call goes straight to
// 0x004e29f0, is the un-memoed engine. Every query of every frame runs through both from the same global state, and
// result, callee-saved registers, x87 environment, both global blocks, the running minimum and LastError must agree.
#include "../../src/proxy/collide_memo.h"
#include "../../src/proxy/collide_sat_sse2.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <xmmintrin.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
static char last_log[2048];
namespace x3m { void log(const char* format, ...) {
    std::va_list a; va_start(a, format); std::vsnprintf(last_log, sizeof last_log, format, a); va_end(a);
    static unsigned lines = 0; if (lines++ < 12) std::printf("%s\n", last_log);
} }
namespace x3m::object_trace { bool executable_verified() { return true; } }
namespace memo = x3m::collide_memo;
namespace core = x3m::collide_memo::core;
namespace engine_patch = x3m::engine_patch;
#include "engine_ranges_inc.h"

struct State { std::uint32_t eax, ebx, ebp, esi, edi, ecx, edx; unsigned char env[28]; std::uint32_t mxcsr; };
extern "C" void __cdecl fx_call(std::uint32_t fn, const void* node_a, const void* node_b, const void* body_a, const void* body_b, std::uint32_t flags, std::uint32_t cap,
                                std::uint32_t tolerance_bits, std::uint32_t edi, State* out);
// 0x0047f1b0: ECX = node a, EAX = node b, EDI = running-minimum pointer, five stack words, the caller pops 0x14.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _fx_call
_fx_call:
    push ebx
    push ebp
    push esi
    push edi
    push dword ptr [esp+48]
    push dword ptr [esp+48]
    push dword ptr [esp+48]
    push dword ptr [esp+48]
    push dword ptr [esp+48]
    mov edx, dword ptr [esp+40]
    mov ecx, dword ptr [esp+44]
    mov eax, dword ptr [esp+48]
    mov edi, dword ptr [esp+72]
    mov ebx, 0x1b1b1b1b
    mov ebp, 0x2b2b2b2b
    mov esi, 0x3b3b3b3b
    call edx
    add esp, 20
    push eax
    mov eax, dword ptr [esp+60]
    pop dword ptr [eax]
    mov dword ptr [eax+4], ebx
    mov dword ptr [eax+8], ebp
    mov dword ptr [eax+12], esi
    mov dword ptr [eax+16], edi
    mov dword ptr [eax+20], ecx
    mov dword ptr [eax+24], edx
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
static void check(bool okay, const char* label);
static void check(bool okay, const char* label) { ++checks; if (!okay) { ++failures; if (failures <= 40) std::printf("FAIL %s (state=%s)\n", label, x3m::collide_memo::state()); } }
template <class P> static std::uint32_t addr(P* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
template <class T> static T& engine(std::uintptr_t va) { return *reinterpret_cast<T*>(va); }
static std::uint64_t rng = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return std::uint32_t(rng >> 16); }
static double u01() { return (rnd() + 0.5) / 4294967296.0; }
static double uniform(double lo, double hi) { return lo + (hi - lo) * u01(); }
static double seconds() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return double(c.QuadPart) / double(f.QuadPart); }

// ---- the engine in place ----
constexpr std::uintptr_t arena_va = 0x00400000, reference_va = 0x004a0000, sse2_flag_va = 0x006619ec;
__attribute__((section(".engine"))) unsigned char engine_arena[0x00670000 - arena_va];
static bool map_engine() {
    if (reinterpret_cast<std::uintptr_t>(engine_arena) != arena_va) { std::printf("FAIL arena at %p\n", static_cast<void*>(engine_arena)); return false; }
    DWORD old_protection = 0;
    if (!VirtualProtect(engine_arena, sizeof engine_arena, PAGE_EXECUTE_READWRITE, &old_protection)) { std::printf("FAIL arena protection error=%lu\n", GetLastError()); return false; }
    for (const FxEngineRange& r : fx_engine_ranges) std::memcpy(reinterpret_cast<void*>(r.va), fx_engine_bytes + r.offset, r.length);
    // The un-memoed engine: 0x0047f1b0 again, its one rel32 re-based so that it still calls 0x004e29f0.
    const unsigned length = 0x0047f341 - core::caller_va, call_at = core::memo_site_va - core::caller_va;
    std::memcpy(reinterpret_cast<void*>(reference_va), reinterpret_cast<void*>(core::caller_va), length);
    const std::int32_t rel = std::int32_t(core::memo_target_va - (reference_va + call_at + 5));
    std::memcpy(reinterpret_cast<void*>(reference_va + call_at + 1), &rel, 4);
    engine<std::uint32_t>(sse2_flag_va) = 1;   // ftol takes its cvttsd2si path, as on any SSE2 machine
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("ENGINE ranges=%u\n", unsigned(sizeof fx_engine_ranges / sizeof fx_engine_ranges[0]));
    return true;
}

// ---- models: RAPID trees with one real triangle per leaf ----
struct Box { float R[9], c[3], d[3]; const Box* first; const Box* second; const void* triangle; };
struct Triangle { std::uint32_t id; float v[9]; std::uint32_t pad[3]; };
static_assert(sizeof(Box) == 0x48 && sizeof(Triangle) == 0x34, "engine layouts");
struct Model { std::uint32_t header[6]; std::vector<Box> boxes; std::vector<Triangle> triangles; float extent; };
static void rotation(double* m, double amount) {
    double q[4] = {uniform(-1, 1) * amount, uniform(-1, 1) * amount, uniform(-1, 1) * amount, amount >= 1.0 ? uniform(-1, 1) : 1.0};
    const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double& v : q) v /= n;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    std::memcpy(m, r, sizeof r);
}
// M, p: the box's frame in model space, so that the leaf's triangle really lies inside the leaf's box.
static double triangle_scale = 1.0;   // below 1: triangles much smaller than their leaf boxes, so boxes overlap deeply without the meshes touching
static unsigned grow(Model& m, const double* M, const double* p, const float* d, unsigned leaves, bool root) {
    const unsigned index = unsigned(m.boxes.size());
    m.boxes.push_back(Box{});
    Box n{};
    double R[9], c[3], Mn[9], pn[3];
    rotation(R, root ? 0.05 : 0.4);
    for (unsigned i = 0; i < 3; ++i) { c[i] = root ? 0.0 : uniform(-0.45, 0.45) * d[i]; n.c[i] = float(c[i]); n.d[i] = d[i]; }
    for (unsigned i = 0; i < 9; ++i) n.R[i] = float(R[i]);
    for (unsigned i = 0; i < 3; ++i) {
        pn[i] = p[i] + M[3 * i] * c[0] + M[3 * i + 1] * c[1] + M[3 * i + 2] * c[2];
        for (unsigned j = 0; j < 3; ++j) Mn[3 * i + j] = M[3 * i] * R[j] + M[3 * i + 1] * R[3 + j] + M[3 * i + 2] * R[6 + j];
    }
    if (leaves > 1) {
        for (unsigned side = 0; side < 2; ++side) {
            float child[3];
            for (unsigned i = 0; i < 3; ++i) child[i] = float(d[i] * uniform(0.6, 0.85));
            const unsigned at = grow(m, Mn, pn, child, side == 0 ? leaves / 2 : leaves - leaves / 2, false);
            (side == 0 ? n.first : n.second) = &m.boxes[0] + at;
        }
    } else {
        Triangle t{};
        t.id = unsigned(m.triangles.size());
        for (unsigned v = 0; v < 3; ++v) {
            const double local[3] = {uniform(-1, 1) * d[0] * triangle_scale, uniform(-1, 1) * d[1] * triangle_scale, uniform(-1, 1) * d[2] * triangle_scale};
            for (unsigned i = 0; i < 3; ++i) t.v[3 * v + i] = float(pn[i] + Mn[3 * i] * local[0] + Mn[3 * i + 1] * local[1] + Mn[3 * i + 2] * local[2]);
        }
        m.triangles.push_back(t);
        n.triangle = &m.triangles[0] + (m.triangles.size() - 1);
    }
    m.boxes[index] = n;
    return index;
}
static Model* make_model(unsigned leaves, float extent) {
    Model* m = new Model{};
    m->boxes.reserve(2 * leaves); m->triangles.reserve(leaves);
    const float d[3] = {extent, extent * float(uniform(0.5, 1.0)), extent * float(uniform(0.3, 1.0))};
    const double identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, origin[3] = {0, 0, 0};
    grow(*m, identity, origin, d, leaves, true);
    m->header[0] = addr(&m->boxes[0]); m->header[1] = addr(&m->triangles[0]); m->header[2] = leaves; m->header[3] = leaves; m->header[4] = unsigned(m->boxes.size()); m->header[5] = 3;
    m->extent = extent;
    return m;
}

// ---- scene objects: the node words 0x0047f1b0 reads, and a body whose +0x5c is the model ----
struct Object { std::uint32_t node[0x190 / 4]; std::uint32_t body[0x60 / 4]; };
static void set_rotation(Object& o, const double* m) {
    static const unsigned at[9] = {0xc0, 0xc4, 0xc8, 0xd0, 0xd4, 0xd8, 0xe0, 0xe4, 0xe8};
    for (unsigned i = 0; i < 9; ++i) o.node[at[i] / 4] = std::uint32_t(std::int32_t(std::lround(m[i] * 65536.0)));
}
static void place(Object& o, const Model* model, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t scale) {
    o.node[0x70 / 4] = std::uint32_t(scale); o.node[0xb0 / 4] = std::uint32_t(x); o.node[0xb4 / 4] = std::uint32_t(y); o.node[0xb8 / 4] = std::uint32_t(z);
    o.body[0x5c / 4] = addr(model->header);
}
static const unsigned key_node_words[13] = {0x70, 0xb0, 0xb4, 0xb8, 0xc0, 0xc4, 0xc8, 0xd0, 0xd4, 0xd8, 0xe0, 0xe4, 0xe8};

// ---- one query through both engines ----
struct Mode { std::uint32_t flags, cap; float tolerance; bool minimum; float minimum_value; };
struct Tally { unsigned long queries = 0, hits = 0, contacts = 0, differences = 0, stale = 0, hit_on_contact = 0, register_differences = 0; };
static Tally tally;
static float minimum_slot;
static bool last_contact = false;
static bool query(const Object& a, const Object& b, const Mode& mode, const char* label) {
    unsigned char root0[56], state0[52], root1[56], state1[52];
    std::memcpy(root0, reinterpret_cast<void*>(core::root_block_va), 56); std::memcpy(state0, reinterpret_cast<void*>(0x0060851c), 52);
    std::uint32_t tolerance_bits; std::memcpy(&tolerance_bits, &mode.tolerance, 4);
    const std::uint32_t hits_before = memo::counters().hits;
    State with_memo{}, reference{};
    minimum_slot = mode.minimum_value; SetLastError(0x5150);
    fx_call(core::caller_va, a.node, b.node, a.body, b.body, mode.flags, mode.cap, tolerance_bits, mode.minimum ? addr(&minimum_slot) : 0, &with_memo);
    const DWORD error1 = GetLastError(); const float minimum1 = minimum_slot;
    std::memcpy(root1, reinterpret_cast<void*>(core::root_block_va), 56); std::memcpy(state1, reinterpret_cast<void*>(0x0060851c), 52);
    const bool hit = memo::counters().hits != hits_before;
    // The same global state again for the un-memoed engine.
    std::memcpy(reinterpret_cast<void*>(core::root_block_va), root0, 56); std::memcpy(reinterpret_cast<void*>(0x0060851c), state0, 52);
    minimum_slot = mode.minimum_value; SetLastError(0x5150);
    fx_call(reference_va, a.node, b.node, a.body, b.body, mode.flags, mode.cap, tolerance_bits, mode.minimum ? addr(&minimum_slot) : 0, &reference);
    const bool contact = reference.eax != 0;
    last_contact = contact;
    bool same = with_memo.eax == reference.eax && with_memo.ebx == reference.ebx && with_memo.ebp == reference.ebp && with_memo.esi == reference.esi && with_memo.edi == reference.edi
        && !std::memcmp(with_memo.env, reference.env, 4) && !std::memcmp(with_memo.env + 8, reference.env + 8, 2) && with_memo.mxcsr == reference.mxcsr
        && !std::memcmp(root1, reinterpret_cast<void*>(core::root_block_va), 56) && !std::memcmp(state1, reinterpret_cast<void*>(0x0060851c), 52)
        && !std::memcmp(&minimum1, &minimum_slot, 4) && error1 == GetLastError() && error1 == 0x5150;
    if (!hit && (with_memo.ecx != reference.ecx || with_memo.edx != reference.edx)) { ++tally.register_differences; same = false; }   // on a run the engine's own ECX/EDX reach the caller
    ++tally.queries; tally.hits += hit; tally.contacts += contact;
    if (!same) { ++tally.differences; if (hit) ++tally.stale; if (tally.differences <= 5) std::printf("DETAIL %s query=%lu hit=%u contact=%u result=%u/%u\n", label, tally.queries, hit, contact, with_memo.eax, reference.eax); }
    if (hit && contact) ++tally.hit_on_contact;
    return hit;
}
static unsigned long long frame_serial = 0;
static void next_frame() { memo::present(1, ++frame_serial, false); }

struct Job { const Object* a; const Object* b; State state; };
static DWORD WINAPI foreign_thread_main(void* p) {
    Job& j = *static_cast<Job*>(p);
    for (unsigned k = 0; k < 50; ++k) fx_call(core::caller_va, j.a->node, j.b->node, j.a->body, j.b->body, 2, 1, 0, 0, &j.state);
    return 0;
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!map_engine()) { std::printf("COLLIDE MEMO CPU checks=1 failures=1\n"); return 1; }
    unsigned char pristine_site[5];
    std::memcpy(pristine_site, reinterpret_cast<void*>(core::memo_site_va), 5);
    const auto site_pristine = [&] { return !std::memcmp(pristine_site, &engine<unsigned char>(core::memo_site_va), 5); };

    // ---- refusals: every one leaves the site untouched ----
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", nullptr);
    check(!memo::initialize() && !std::strcmp(memo::state(), "disabled") && site_pristine(), "unset: disabled");
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", L"0");
    check(!memo::initialize() && !std::strcmp(memo::state(), "disabled") && site_pristine(), "0: disabled");
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", L"1");
    for (const std::uintptr_t va : {0x0047f1d8u, 0x004e2a10u, 0x004e2800u, 0x004e2600u, 0x004e2300u, 0x004e2b00u}) {
        engine<unsigned char>(va) ^= 0x01;
        check(!memo::initialize() && !std::strcmp(memo::state(), "body_mismatch") && site_pristine(), "changed body byte: body_mismatch");
        engine<unsigned char>(va) ^= 0x01;
    }
    for (const std::uintptr_t va : {0x0047f32eu, 0x0047f340u}) {   // the pre window lies inside the hashed caller body; the post window does not
        engine<unsigned char>(va) ^= 0x01;
        check(!memo::initialize() && !std::strcmp(memo::state(), "bytes_mismatch") && site_pristine(), "changed window byte: bytes_mismatch");
        engine<unsigned char>(va) ^= 0x01;
    }
    check(!memo::install_at(memo::Addresses{0, core::memo_target_va}, false) && !std::strcmp(memo::state(), "invalid_site"), "null site refused");
    check(!memo::install_at(memo::Addresses{core::memo_site_va, core::query_va}, false) && site_pristine(), "another callee refused");
    check(site_pristine(), "site pristine after the refusals");

    // ---- models and objects ----
    std::vector<Model*> models;
    for (unsigned i = 0; i < 10; ++i) models.push_back(make_model(16u << (i % 4), float(uniform(20, 60))));
    triangle_scale = 0.05;
    Model* big = make_model(8192, 400.0f); Model* ship = make_model(512, 30.0f);
    triangle_scale = 1.0;
    const Mode first_contact{2, 1, 0.0f, false, 0.0f}, distance{0xc, 8, 0.0f, true, 1e9f}, distance_near{0xc, 8, 0.0f, true, 0.5f}, distance_tolerant{0xc, 8, 25.0f, true, 1e9f};

    // The SAT module is in place first, as in a flight with both options on.
    check(x3m::collide_sat_sse2::install_at(x3m::collide_sat_sse2::Addresses{0x004e25a3, 0x004e3280}), "SAT module installs on the in-place engine");
    check(memo::initialize() && !std::strcmp(memo::state(), "ok"), "initialize: ok next to the SAT module");
    { std::int32_t rel; std::memcpy(&rel, reinterpret_cast<void*>(core::memo_site_va + 1), 4);
      check(engine<unsigned char>(core::memo_site_va) == 0xe8 && std::uint32_t(core::memo_return_va + rel) == addr(&x3m_collide_memo_thunk), "site calls the thunk"); }
    check(!memo::install_at(memo::Addresses{core::memo_site_va, core::memo_target_va}, false) && !std::strcmp(memo::state(), "already_installed"), "second install refused");

    const auto section = [&](const char* name, const Tally& before) {
        std::printf("SCENARIO %s queries=%lu hits=%lu contacts=%lu differences=%lu stale=%lu hit_on_contact=%lu\n", name, tally.queries - before.queries, tally.hits - before.hits,
                    tally.contacts - before.contacts, tally.differences - before.differences, tally.stale - before.stale, tally.hit_on_contact - before.hit_on_contact);
    };
    double m[9];
    // 1. Static objects: the first frame runs everything, every later frame answers the no-contact pairs from the memo and recomputes the contacts.
    {
        const Tally before = tally;
        std::vector<Object> objects(12);
        for (unsigned i = 0; i < 12; ++i) { objects[i] = Object{}; rotation(m, 1.0); set_rotation(objects[i], m); place(objects[i], models[i % 10], std::int32_t(uniform(-60, 60)), std::int32_t(uniform(-60, 60)), std::int32_t(uniform(-60, 60)), 1 + i % 3); }
        unsigned first_frame_hits = 0, later_misses_without_contact = 0;
        for (unsigned frame = 0; frame < 8; ++frame, next_frame())
            for (unsigned i = 0; i < 12; ++i) for (unsigned j = i + 1; j < 12; ++j) {
                const unsigned long contacts_before = tally.contacts;
                const bool hit = query(objects[i], objects[j], (i + j) % 2 ? first_contact : distance, "static");
                if (frame == 0 && hit) ++first_frame_hits;
                if (frame > 0 && !hit && tally.contacts == contacts_before) ++later_misses_without_contact;
            }
        check(first_frame_hits == 0 && later_misses_without_contact == 0, "static: nothing hits in the first frame, every no-contact pair hits afterwards");
        check(tally.contacts - before.contacts >= 8 && tally.hits - before.hits >= 100, "static: both contacts and hits occur");
        section("static", before);
    }
    // 2. The smallest change of any one of the 26 node words is a miss in that frame (and a hit again when nothing changes).
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); rotation(m, 1.0); set_rotation(b, m);
        place(a, models[3], 0, 0, 0, 2); place(b, models[4], 100000, 0, 0, 1);   // far apart: never a contact
        query(a, b, first_contact, "ulp"); next_frame();
        unsigned wrong = 0;
        for (unsigned word = 0; word < 26; ++word) {
            Object& o = word < 13 ? a : b;
            o.node[key_node_words[word % 13] / 4] += word % 2 ? 1u : 0xffffffffu;
            if (query(a, b, first_contact, "ulp")) ++wrong;
            next_frame();
            if (!query(a, b, first_contact, "ulp")) ++wrong;
            next_frame();
        }
        check(wrong == 0, "one step of any of the 26 node words misses once, then hits");
        for (unsigned word : {0x30u, 0x34u, 0x38u, 0x12cu, 0x140u, 0x180u}) { a.node[word / 4] ^= 0x55; if (!query(a, b, first_contact, "ulp")) ++wrong; next_frame(); }
        check(wrong == 0, "node words the query never reads do not matter");
        section("one_step", before);
    }
    // 3. Approach: a ship closes in on a large model until the meshes touch, backs off two steps and parks there (boxes deep inside each other,
    //    no contact: the measured case), closes in again to a contact, and leaves, turning on the way.
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); place(a, big, 0, 0, 0, 1);
        rotation(m, 1.0); set_rotation(b, m);
        unsigned contact_frames = 0, hits_in_contact = 0, parked_frames = 0, parked_hits = 0, first_contact_x = 0;
        std::int32_t x = 900;
        for (; x > -900 && contact_frames == 0; x -= 4, next_frame()) { place(b, ship, x, 40, -30, 1); const bool hit = query(a, b, first_contact, "approach"); if (last_contact) { ++contact_frames; hits_in_contact += hit; first_contact_x = unsigned(x + 900); } }
        const std::int32_t parked = x + 4 + 8;   // two steps back from the first contact
        for (unsigned frame = 0; frame < 60; ++frame, next_frame()) { place(b, ship, parked, 40, -30, 1); const bool hit = query(a, b, first_contact, "approach"); if (!last_contact) { ++parked_frames; parked_hits += hit; } }
        for (x = parked; x > parked - 40; x -= 4, next_frame()) { place(b, ship, x, 40, -30, 1); const bool hit = query(a, b, first_contact, "approach"); if (last_contact) { ++contact_frames; hits_in_contact += hit; } }
        for (unsigned frame = 0; frame < 100; ++frame, next_frame()) { rotation(m, 1.0); set_rotation(b, m); place(b, ship, parked + std::int32_t(frame) * 10, 40, -30, 1); const bool hit = query(a, b, first_contact, "approach"); if (last_contact) { ++contact_frames; hits_in_contact += hit; } }
        check(contact_frames >= 2 && hits_in_contact == 0, "approach: contacts occur and none of them is answered from the memo");
        check(parked_frames >= 50 && parked_hits + 1 >= parked_frames, "parked next to the contact point: every frame after the first is answered from the memo");
        section("approach", before);
        std::printf("APPROACH contact_frames=%u parked_frames=%u parked_hits=%u first_contact_step=%u\n", contact_frames, parked_frames, parked_hits, first_contact_x / 4);
    }
    // 4. Modes: the same pair under flags 2 / cap 1 and flags 0xc / cap 8, with running minima and a tolerance; each is its own entry.
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); rotation(m, 1.0); set_rotation(b, m);
        place(a, models[6], 0, 0, 0, 1); place(b, models[7], 25, 10, 0, 1);
        const Mode modes[5] = {first_contact, distance, distance_near, distance_tolerant, Mode{2, 1, 0.0f, true, 3.0f}};
        unsigned wrong = 0, first_frame_hits = 0;
        const std::uint32_t relaxed0 = memo::counters().min_relaxed_hits;
        for (unsigned frame = 0; frame < 6; ++frame, next_frame())
            for (const Mode& mode : modes) { const unsigned long c = tally.contacts; const bool hit = query(a, b, mode, "modes"); if (frame == 0 && hit) ++first_frame_hits; if (frame > 0 && !hit && tally.contacts == c) ++wrong; }
        // In the first frame a mode may only be answered by another mode's entry through the running-minimum rule (same flags, cap and tolerance, no leaf reached).
        check(wrong == 0 && first_frame_hits <= memo::counters().min_relaxed_hits - relaxed0 && first_frame_hits <= 1, "modes: flags, cap, tolerance and pointer null-ness each make their own entry");
        section("modes", before);
    }
    // 4b. The running minimum alone changes. It is read only inside the leaf, so a run that reached no leaf does not depend on it (answered from
    //     the memo whatever its value); a run that did reach one must miss on any other value; and a contact that the minimum filters is recomputed.
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); rotation(m, 1.0); set_rotation(b, m);
        place(a, models[6], 0, 0, 0, 1);
        State s{};
        // Placements by what the un-memoed engine does with them under flags 0xc and a huge minimum: no leaf, leaf without contact, contact.
        std::int32_t no_leaf[3] = {100000, 0, 0}, leaf[3] = {0, 0, 0}, touching[3] = {0, 0, 0};
        bool have_leaf = false, have_touching = false;
        float huge = 1e9f, tolerance = 0.0f;
        std::uint32_t tolerance_bits = 0;
        for (unsigned attempt = 0; attempt < 12000 && !(have_leaf && have_touching); ++attempt) {
            if (attempt % 4000 == 0) { tolerance = attempt == 0 ? 25.0f : attempt == 4000 ? 1000.0f : 1e6f; std::memcpy(&tolerance_bits, &tolerance, 4); have_leaf = false; }   // distance mode counts a contact within its tolerance only
            const std::int32_t at[3] = {std::int32_t(uniform(-90, 90)), std::int32_t(uniform(-90, 90)), std::int32_t(uniform(-90, 90))};
            place(b, models[7], at[0], at[1], at[2], 1);
            minimum_slot = huge;
            fx_call(reference_va, a.node, b.node, a.body, b.body, 0xc, 8, tolerance_bits, addr(&minimum_slot), &s);
            const std::uint32_t triangles = engine<std::uint32_t>(0x00608548);
            if (s.eax == 0 && triangles > 0 && !have_leaf) { std::memcpy(leaf, at, sizeof at); have_leaf = true; }
            if (s.eax != 0 && !have_touching) { std::memcpy(touching, at, sizeof at); have_touching = true; }
        }
        check(have_leaf && have_touching, "placements found: a leaf reached without contact, and a contact");
        Mode mode = distance;
        mode.tolerance = tolerance;
        unsigned no_leaf_hits = 0, leaf_hits_on_change = 0, leaf_hits_on_repeat = 0, filtered = 0, passed = 0;
        const std::uint32_t relaxed0 = memo::counters().min_relaxed_hits;
        place(b, models[7], no_leaf[0], no_leaf[1], no_leaf[2], 1);
        for (unsigned frame = 0; frame < 40; ++frame, next_frame()) { mode.minimum_value = float(1000.0 / (frame + 1)); no_leaf_hits += query(a, b, mode, "running_minimum"); }
        check(no_leaf_hits == 39 && memo::counters().min_relaxed_hits - relaxed0 == 39, "no leaf reached: every frame after the first is answered although the minimum changed");
        place(b, models[7], leaf[0], leaf[1], leaf[2], 1);
        for (unsigned frame = 0; frame < 40; ++frame, next_frame()) {
            mode.minimum_value = float(2000.0 / (frame + 1));
            leaf_hits_on_change += query(a, b, mode, "running_minimum");
            if (!last_contact) leaf_hits_on_repeat += query(a, b, mode, "running_minimum");
        }
        check(leaf_hits_on_change == 0 && leaf_hits_on_repeat > 0, "a leaf was reached: another minimum always misses, the same minimum hits");
        place(b, models[7], touching[0], touching[1], touching[2], 1);
        for (unsigned frame = 0; frame < 60; ++frame, next_frame()) {
            mode.minimum_value = frame % 2 ? huge : float(frame) * 0.05f;   // small minima filter the contact away, the huge one lets it through
            const bool hit = query(a, b, mode, "running_minimum");
            if (!last_contact) ++filtered; else ++passed;
            if (hit && last_contact) ++leaf_hits_on_change;
        }
        check(leaf_hits_on_change == 0 && filtered > 0 && passed > 0, "a contact that depends on the minimum is never answered from the memo");
        section("running_minimum", before);
        std::printf("MINIMUM no_leaf_hits=%u leaf_hits_on_repeat=%u contact_filtered_frames=%u contact_frames=%u tolerance=%.0f\n", no_leaf_hits, leaf_hits_on_repeat, filtered, passed, double(tolerance));
    }
    // 5. Addresses: the same inputs at other node and body addresses are the same query; another model, or other content at the same model address, is not.
    {
        const Tally before = tally;
        Object a{}, b{}, c{}, d{};
        rotation(m, 1.0); set_rotation(a, m); rotation(m, 1.0); set_rotation(b, m);
        place(a, models[1], 0, 0, 0, 1); place(b, models[2], 100000, 50, 0, 2);
        query(a, b, first_contact, "addresses"); next_frame();
        c = a; d = b;
        check(query(c, d, first_contact, "addresses"), "copies of both objects at other addresses: the same query, a hit");
        next_frame();
        std::swap(a, b);   // the objects trade places in memory: a's address now holds b
        check(query(b, a, first_contact, "addresses"), "objects swapped in memory, same order of arguments by content: a hit");
        check(!query(a, b, first_contact, "addresses"), "the reversed pair is another query");
        next_frame();
        place(b, models[5], 0, 0, 0, 1);   // b holds the old a: give it another model
        check(!query(b, a, first_contact, "addresses"), "another model behind the same body: a miss");
        next_frame();
        // The model address reused for other content: header and root box change with it.
        Model* const other = make_model(64, 33.0f);
        Model saved = *models[5];
        std::memcpy(models[5]->header, other->header, sizeof other->header);
        check(!query(b, a, first_contact, "addresses"), "model address reused with another tree: a miss");
        next_frame();
        std::memcpy(models[5]->header, saved.header, sizeof saved.header);
        models[5]->boxes[0].d[0] = std::nextafter(models[5]->boxes[0].d[0], 1e9f);
        check(!query(b, a, first_contact, "addresses"), "same header, root box changed by one float step: a miss");
        models[5]->header[5] = 2;
        check(!query(b, a, first_contact, "addresses") && !query(b, a, first_contact, "addresses"), "a model that is not in the built state is left to the engine");
        models[5]->header[5] = 3;
        section("addresses", before);
    }
    // 6. Expiry: an entry not touched for a whole frame is gone.
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); set_rotation(b, m);
        place(a, models[8], 0, 0, 0, 1); place(b, models[9], 100000, 0, 0, 1);
        query(a, b, first_contact, "expiry"); next_frame();
        check(query(a, b, first_contact, "expiry"), "next frame: a hit");
        next_frame(); next_frame();
        check(!query(a, b, first_contact, "expiry"), "after a frame without the query: a miss");
        check(query(a, b, first_contact, "expiry"), "asked again in the same frame: a hit");
        section("expiry", before);
    }
    // 7. More live queries than the table holds: evictions, never a wrong answer.
    {
        const Tally before = tally;
        const core::Counters c0 = memo::counters();
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); set_rotation(b, m);
        place(a, models[0], 0, 0, 0, 1);
        for (unsigned frame = 0; frame < 3; ++frame, next_frame())
            for (std::int32_t k = 0; k < 3000; ++k) { place(b, models[1], 100000 + k, 0, 0, 1); query(a, b, first_contact, "overflow"); }
        check(memo::counters().evictions > c0.evictions && tally.hits > before.hits, "overflow: live entries are evicted and the survivors still hit");
        section("overflow", before);
    }
    // 8. Random scenes: twelve objects that stay, creep by one unit, jump, turn, rescale or change model; twelve pairs per frame.
    {
        const Tally before = tally;
        std::vector<Object> objects(12);
        std::vector<unsigned> model_of(12);
        for (unsigned i = 0; i < 12; ++i) { objects[i] = Object{}; rotation(m, 1.0); set_rotation(objects[i], m); model_of[i] = i % 10;
                                            place(objects[i], models[i % 10], std::int32_t(uniform(-80, 80)), std::int32_t(uniform(-80, 80)), std::int32_t(uniform(-80, 80)), 1); }
        const Mode modes[4] = {first_contact, distance, distance_near, distance_tolerant};
        for (unsigned frame = 0; frame < 4000; ++frame, next_frame()) {
            if (rnd() % 3 == 0) {
                Object& o = objects[rnd() % 12];
                switch (rnd() % 6) {
                    case 0: o.node[0xb0 / 4 + rnd() % 3] += rnd() % 2 ? 1u : 0xffffffffu; break;
                    case 1: o.node[0xb0 / 4 + rnd() % 3] = std::uint32_t(std::int32_t(uniform(-80, 80))); break;
                    case 2: rotation(m, rnd() % 2 ? 1.0 : 0.001); set_rotation(o, m); break;
                    case 3: o.node[0x70 / 4] = 1 + rnd() % 3; break;
                    case 4: o.body[0x5c / 4] = addr(models[rnd() % 10]->header); break;
                    default: o.node[0xc0 / 4 + rnd() % 3] ^= 1u; break;
                }
            }
            for (unsigned k = 0; k < 12; ++k) { Mode mode = modes[k % 4]; if (mode.minimum && rnd() % 2) mode.minimum_value = float(uniform(0, 60)); query(objects[k], objects[(k + 5) % 12], mode, "random"); }   // the same pairs and modes every frame, as the engine's pair loop
        }
        check(tally.hits - before.hits > 5000 && tally.contacts - before.contacts > 500, "random: hits and contacts both occur in volume");
        section("random", before);
    }
    // 9. Guards: null models, another thread, a re-entered query, an unwind that leaves a query marked in flight, a device Reset, a long run without a Present.
    {
        const Tally before = tally;
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); set_rotation(b, m);
        place(a, models[0], 0, 0, 0, 1); place(b, models[1], 200000, 0, 0, 1);
        const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, zero[3] = {0, 0, 0};
        x3m_collide_memo_args raw{identity, zero, 0x3f800000u, nullptr, identity, zero, 0x3f800000u, models[1]->header, 0, nullptr};
        core::Counters c0 = memo::counters();
        const int null_a = x3m_collide_memo_lookup(2, 1, &raw); x3m_collide_memo_store();
        raw.model_a = models[0]->header; raw.model_b = nullptr;
        const int null_b = x3m_collide_memo_lookup(2, 1, &raw); x3m_collide_memo_store();
        check(null_a == 0 && null_b == 0 && memo::counters().ineligible - c0.ineligible == 2 && memo::counters().stored == c0.stored, "a null model pointer is never dereferenced: the engine's path, nothing stored");
        Object bodiless = a; bodiless.body[0x5c / 4] = 0;
        query(bodiless, b, first_contact, "guards"); query(a, bodiless, first_contact, "guards");   // the caller returns before the site
        // Another thread: straight to the engine, the memo's state untouched.
        query(a, b, first_contact, "guards"); next_frame();
        c0 = memo::counters();
        Job job{&a, &b, {}};
        HANDLE thread = CreateThread(nullptr, 0, foreign_thread_main, &job, 0, nullptr);
        WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
        core::Counters c1 = memo::counters();
        check(c1.foreign_thread - c0.foreign_thread == 50 && c1.hits == c0.hits && c1.misses == c0.misses && c1.stored == c0.stored && job.state.eax == 0, "another thread: 50 queries run in the engine, none through the memo");
        check(query(a, b, first_contact, "guards"), "the owner thread still hits afterwards");
        // Re-entered: a second lookup while one query is in flight.
        raw.model_a = models[0]->header; raw.model_b = models[1]->header;
        const int first = x3m_collide_memo_lookup(2, 1, &raw), second = x3m_collide_memo_lookup(2, 1, &raw);
        x3m_collide_memo_store();
        check(first == 0 && second == 2 && memo::counters().reentered - c1.reentered == 1, "re-entered lookup: the engine's path, counted");
        // An unwind past the thunk: lookup without its store. The next Present on this thread notices, drops the table and re-arms.
        raw.s1 = 0x40000000u;
        c1 = memo::counters();
        const int stuck = x3m_collide_memo_lookup(2, 1, &raw);
        const int while_stuck = x3m_collide_memo_lookup(2, 1, &raw);
        next_frame();
        const bool after_unwind = query(a, b, first_contact, "guards");
        core::Counters c2 = memo::counters();
        check(stuck == 0 && while_stuck == 2 && c2.stuck_busy - c1.stuck_busy == 1 && c2.clears - c1.clears == 1 && !after_unwind && query(a, b, first_contact, "guards"),
              "a query left in flight: passes through until the next Present, which re-arms and drops the table");
        // Device Reset.
        next_frame(); memo::device_reset();
        const bool after_reset = query(a, b, first_contact, "guards");
        check(!after_reset && memo::counters().clears - c2.clears == 1 && query(a, b, first_contact, "guards"), "device Reset drops the table");
        // More queries than the limit without a Present.
        c2 = memo::counters();
        State s{};
        for (unsigned k = 0; k <= core::queries_without_tick_limit; ++k) fx_call(core::caller_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &s);
        const core::Counters c3 = memo::counters();
        check(c3.clears - c2.clears == 1 && c3.hits - c2.hits == core::queries_without_tick_limit && c3.misses - c2.misses == 1, "100,000 queries without a Present: the table is dropped once");
        next_frame();
        section("guards", before);
    }
    check(tally.differences == 0 && tally.stale == 0 && tally.hit_on_contact == 0 && tally.register_differences == 0, "every query of every frame: identical to the un-memoed engine, no stale hit, no contact answered from the memo");

    // ---- cost: the hot pair of a parked ship, run and answered ----
    {
        Object a{}, b{};
        rotation(m, 1.0); set_rotation(a, m); rotation(m, 1.0); set_rotation(b, m);
        place(a, big, 0, 0, 0, 1);
        State s{};
        std::int32_t best[3] = {100000, 0, 0}; std::uint32_t most = 0;
        for (unsigned attempt = 0; attempt < 60; ++attempt) {   // the contact-free placement with the most node pairs
            const std::int32_t at[3] = {std::int32_t(uniform(-250, 250)), std::int32_t(uniform(-150, 150)), std::int32_t(uniform(-100, 100))};
            place(b, ship, at[0], at[1], at[2], 1);
            fx_call(reference_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &s);
            if (s.eax == 0 && engine<std::uint32_t>(core::visits_va) > most) { most = engine<std::uint32_t>(core::visits_va); std::memcpy(best, at, sizeof at); }
        }
        place(b, ship, best[0], best[1], best[2], 1);
        double run_ns = 1e30, hit_ns = 1e30;
        for (unsigned r = 0; r < 12; ++r) { const double t = seconds(); fx_call(reference_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &s); const double e = (seconds() - t) * 1e9; if (e < run_ns) run_ns = e; }
        const std::uint32_t visits = engine<std::uint32_t>(core::visits_va), contact = s.eax;
        fx_call(core::caller_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &s);
        const std::uint32_t hits0 = memo::counters().hits;
        for (unsigned r = 0; r < 12; ++r) {
            const double t = seconds();
            for (unsigned k = 0; k < 1000; ++k) fx_call(core::caller_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &s);
            const double e = (seconds() - t) * 1e9 / 1000.0; if (e < hit_ns) hit_ns = e;
        }
        check(contact == 0 && memo::counters().hits - hits0 == 12000, "cost pair: no contact, every repeat answered");
        // The price of a miss: a tiny query (root boxes apart: one node pair) with a new key every time, against the same query in the bare engine and answered.
        Object far_a{}, far_b{};
        rotation(m, 1.0); set_rotation(far_a, m); set_rotation(far_b, m);
        place(far_a, models[0], 0, 0, 0, 1);
        std::int32_t serial = 300000;
        const auto tiny = [&](std::uint32_t fn, bool new_key) {
            double best = 1e30;
            for (unsigned r = 0; r < 8; ++r) {
                place(far_b, models[1], serial, 0, 0, 1);
                if (!new_key) fx_call(fn, far_a.node, far_b.node, far_a.body, far_b.body, 2, 1, 0, 0, &s);
                const double t = seconds();
                for (unsigned k = 0; k < 2000; ++k) { if (new_key) far_b.node[0xb0 / 4] = std::uint32_t(++serial); fx_call(fn, far_a.node, far_b.node, far_a.body, far_b.body, 2, 1, 0, 0, &s); }
                const double e = (seconds() - t) * 1e9 / 2000.0; if (e < best) best = e;
                next_frame();
            }
            return best;
        };
        const core::Counters t0 = memo::counters();
        const double tiny_run = tiny(reference_va, true), tiny_miss = tiny(core::caller_va, true);
        const core::Counters t1 = memo::counters();
        const double tiny_hit = tiny(core::caller_va, false);
        check(t1.misses - t0.misses == 16000 && t1.stored - t0.stored == 16000 && memo::counters().hits - t1.hits >= 16000 && engine<std::uint32_t>(core::visits_va) == 1, "tiny queries: every one a miss and a store, then every one a hit");
        std::printf("COLLIDE MEMO BENCH visits=%u run_ns=%.0f hit_ns=%.1f tiny_run_ns=%.1f tiny_miss_store_ns=%.1f tiny_hit_ns=%.1f miss_store_overhead_ns=%.1f\n", visits, run_ns, hit_ns, tiny_run, tiny_miss, tiny_hit, tiny_miss - tiny_run);
    }
    const core::Counters normal = memo::counters();
    next_frame();
    while (frame_serial % 300 != 0) next_frame();
    check(std::strstr(last_log, "collide_memo device=1 ") != nullptr && std::strstr(last_log, " verify=0 ") != nullptr && std::strstr(last_log, "verify_mismatches=0") != nullptr && std::strstr(last_log, " miss_expired_visits=") != nullptr, "window line written every 300 frames");
    std::printf("WINDOW %s\n", last_log);

    // ---- verify mode: nothing is skipped, every would-be hit is compared; an input outside the key shows up as a mismatch ----
    check(memo::shutdown() && site_pristine(), "shutdown restores the call exactly");
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO_VERIFY", L"1");
    check(memo::initialize() && !std::strcmp(memo::state(), "ok"), "initialize in verify mode");
    {
        const Tally before = tally;
        std::vector<Object> objects(8);
        for (unsigned i = 0; i < 8; ++i) { objects[i] = Object{}; rotation(m, 1.0); set_rotation(objects[i], m); place(objects[i], models[i], std::int32_t(uniform(-60, 60)), std::int32_t(uniform(-60, 60)), std::int32_t(uniform(-60, 60)), 1); }
        for (unsigned frame = 0; frame < 6; ++frame, next_frame()) for (unsigned i = 0; i < 8; ++i) for (unsigned j = i + 1; j < 8; ++j) query(objects[i], objects[j], first_contact, "verify");
        const core::Counters c = memo::counters();
        check(tally.hits == before.hits && c.hits == normal.hits && c.verified - normal.verified > 50 && c.verify_mismatches == normal.verify_mismatches && tally.differences == 0,
              "verify mode: nothing skipped, every would-be hit confirmed, no mismatch");
        // Boxes below the root changed without touching header or root box: the documented blind spot of the key (model data is immutable in the engine).
        Object a{}, b{};
        rotation(m, 0.0); set_rotation(a, m); set_rotation(b, m);
        place(a, models[2], 0, 0, 0, 1); place(b, models[3], 1, 1, 1, 1);
        unsigned mismatches = 0;
        for (unsigned attempt = 0; attempt < 40 && mismatches == 0; ++attempt) {
            query(a, b, distance, "verify"); next_frame();
            for (std::size_t k = 1; k < models[2]->boxes.size(); ++k) for (float& v : models[2]->boxes[k].d) v *= 0.7f;   // every box but the root
            const std::uint32_t seen = memo::counters().verify_mismatches;
            query(a, b, distance, "verify"); next_frame();
            mismatches = memo::counters().verify_mismatches - seen;
        }
        std::printf("VERIFY verified=%u injected_mismatches=%u\n", memo::counters().verified - normal.verified, mismatches);
        check(mismatches > 0, "verify mode reports an input the key does not hold");
        check(tally.differences == 0, "verify mode never changes an answer");
        section("verify", before);
    }
    check(memo::shutdown() && site_pristine(), "second shutdown restores the call exactly");
    engine_patch::close_install_window("fixture");
    check(!memo::initialize() && !std::strcmp(memo::state(), "late_claim") && site_pristine(), "closed window: late_claim");

    const core::Counters c = memo::counters();
    std::printf("SUMMARY queries=%lu hits=%lu contacts=%lu differences=%lu stale_hits=%lu hits_on_contact=%lu register_differences=%lu stored=%u evictions=%u ineligible=%u skipped_visits=%u verified=%u verify_mismatches=%u\n",
                tally.queries, tally.hits, tally.contacts, tally.differences, tally.stale, tally.hit_on_contact, tally.register_differences, c.stored, c.evictions, c.ineligible, c.skipped_visits,
                c.verified, c.verify_mismatches);
    std::printf("GUARDS foreign_thread=%u reentered=%u clears=%u stuck_busy=%u\n", c.foreign_thread, c.reentered, c.clears, c.stuck_busy);
    std::printf("MISSES min_relaxed_hits=%u none_found=%u xform_a=%u xform_b=%u scale=%u mode=%u models=%u min_value=%u expired=%u min_value_visits=%u xform_b_visits=%u\n", c.min_relaxed_hits,
                c.miss_count[0], c.miss_count[1], c.miss_count[2], c.miss_count[3], c.miss_count[4], c.miss_count[5], c.miss_count[6], c.miss_count[7], c.miss_visits[6], c.miss_visits[2]);
    { std::uint32_t classified = 0; for (std::uint32_t n : c.miss_count) classified += n; check(classified == c.misses && c.miss_count[6] > 0 && c.miss_count[2] > 0 && c.miss_count[7] > 0, "every miss is classified; min_value, xform_b and expired all occur"); }
    std::printf("COLLIDE MEMO CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
