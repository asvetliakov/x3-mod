// Native fixture of the FL no-contact collision memo (src/engine/fl/collide_memo_fl.cpp, collide_memo_sites_fl.h;
// docs/reverse-engineering/fl/collision-memo.md, "Port (2026-11-03)"). It maps two private 64 KB-aligned copies of
// X3FL.exe with their relocations applied (fl_seta_image.h): M carries the production memo (installed with the real
// engine_patch), N is the untouched native reference. Every key fires on both: N's real relocated query/descent/leaf/SAT
// is the ground truth for M's answer. Built -DX3M_EDITION_FL=1 -DX3M_COLLIDE_MEMO_FL_FIXTURE by
// verification/analysis/test_fl_collide_memo.py, run natively through WSL interop (no Wine). Not traversal,
// resource-lifetime or gameplay evidence.
//
// Two minimal built collision models (header +0x14 = 3, a unit root box with no children and one triangle) stand in for
// ship meshes; far translations separate them at the root (0 contacts), near ones reach the leaf (real contacts). The
// harness pushes the six native stack words, sets ECX = &R1, EDX = &T1, XMM2 = s1 and the mode globals the caller
// writes, then calls the thunk (M) or the query (N) under a full CPU image.
//
//   phase A  production verify mode (X3M_COLLIDE_MEMO_VERIFY=1): every would-be hit re-runs the engine and store()
//            compares visits, triangles, tolerance and the root block with the stored entry (verify_mismatches); M's
//            outputs and CPU image equal N's on every run; a below-root geometry change (the documented ABA blind spot)
//            is the negative control that must produce exactly one mismatch.
//   phase B  replay mode: every replay's contacts, counters, first-contact flag, 13 root words, contact record and
//            running minimum are compared with N's engine run of the same key, and its CPU image with the harness's;
//            every miss's CPU image (MXCSR status included) with N's; the ABA control must be detected.
//   then     invalidation (movement, ship change, root-box change, frame gap), the FP context gates, device_reset, the
//            300-frame row, shutdown, late_claim.
// Contact results are never stored (the shared policy): contact pairs are always computed and compared as misses.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "fl_seta_image.h"
#include "../../src/engine/fl/collide_memo_fl.h"
#include "../../src/engine/fl/collide_memo_sites_fl.h"
#include "../../src/proxy/engine_patch.h"

namespace memo = x3m::collide_memo;
namespace F = x3m::fl::collide_memo_fl;
namespace core = x3m::collide_memo::core;
namespace engine_patch = x3m::engine_patch;
using std::uint32_t;

static std::string memo_row;
namespace x3m {
void log(const char* format, ...) {
    char line[2048];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (!std::strncmp(line, "collide_memo device=", 20)) memo_row = line;
}
}
namespace x3m::fl {
std::uintptr_t module_base() {
    return 0;
}
bool verified() {
    return false;
}
}

static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    ++checks;
    if (!ok) {
        ++failures;
        if (failures < 40) std::printf("FAIL %s %s\n", what, detail);
    }
}

// ---- the CPU-image call harness: pushes the six words, sets ECX/EDX/XMM2, calls the thunk ----
extern "C" {
struct alignas(16) State {
    uint32_t result, ebx, ebp, esi, edi, ecx, edx, esp_balanced; // result, preserved GPRs, scratch, esp balance flag
    alignas(16) unsigned char fp_in[512], fp_out[512], fp_host[512];
};
State* fx_state = nullptr;
void* fx_thunk = nullptr;
uint32_t fx_saved_esp = 0, fx_tmp_mxcsr = 0;
// fx_call(regs*, args[6], thunk, state, mxcsr)
void fx_call(const F::Regs*, const uint32_t*, void*, State*, uint32_t);
}
asm(R"(
.intel_syntax noprefix
.text
.globl _fx_call
_fx_call:
 push ebp
 push ebx
 push esi
 push edi
 mov eax,[esp+32]           # state*
 mov [_fx_state],eax
 mov ecx,[esp+28]           # thunk
 mov [_fx_thunk],ecx
 mov ecx,[esp+36]           # mxcsr
 mov [_fx_tmp_mxcsr],ecx
 fxsave [eax+1056]          # fp_host
 fninit
 fld1
 ldmxcsr [_fx_tmp_mxcsr]
 pxor xmm0,xmm0
 pxor xmm1,xmm1
 pxor xmm3,xmm3
 pxor xmm4,xmm4
 pxor xmm5,xmm5
 pxor xmm6,xmm6
 pxor xmm7,xmm7
 mov ebx,[esp+20]           # regs*
 movd xmm2,[ebx+8]          # s1 bits -> xmm2 low
 fxsave [eax+32]           # fp_in
 mov edi,[esp+24]           # args*
 mov [_fx_saved_esp],esp
 push dword ptr [edi+20]
 push dword ptr [edi+16]
 push dword ptr [edi+12]
 push dword ptr [edi+8]
 push dword ptr [edi+4]
 push dword ptr [edi]
 mov ecx,[ebx]              # &R1
 mov edx,[ebx+4]            # &T1
 mov ebx,0x0b0b0b0b
 mov ebp,0x0e0e0e0e
 mov esi,0x05050505
 mov edi,0x0d0d0d0d
 call dword ptr [_fx_thunk]
 add esp,24                 # drop the six words (the caller's job)
 mov ecx,[_fx_saved_esp]
 xor ecx,esp
 push eax
 push ecx
 mov eax,[_fx_state]
 pop ecx
 mov [eax+28],ecx           # esp_balanced = 0 when esp returned to pre-push
 pop dword ptr [eax]        # result (thunk EAX)
 mov [eax+4],ebx
 mov [eax+8],ebp
 mov [eax+12],esi
 mov [eax+16],edi
 mov [eax+20],ecx
 mov [eax+24],edx
 fxsave [eax+544]          # fp_out
 fxrstor [eax+1056]         # restore the host FP state
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
.att_syntax
)");

// ---- two mapped images: M carries the memo, N is the untouched native reference ----
static FlImage img_m, img_n;
static uint32_t base_m = 0, base_n = 0;
static uint32_t address(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static uint32_t& word(uint32_t base, uint32_t rva) {
    return *reinterpret_cast<uint32_t*>(std::uintptr_t(base) + rva);
}
// A collision model: 24-byte header (word 0 = root box pointer, word 5 = state 3) and one 0x48-byte root box (identity
// matrix at +0, unit half extents at +0x30/34/38, no children at +0x3c/+0x40, a triangle at +0x44); one triangle record
// (id + three vertices). Both images read the same models (they are inputs, as the engine's are).
struct Model {
    uint32_t header[6];
    alignas(16) float box[18];
    uint32_t triangle[1 + 9];
};
static Model model_a, model_b;
static float one = 1.0f;
static void build_model(Model& m) {
    std::memset(&m, 0, sizeof m);
    m.header[0] = address(m.box);
    m.header[5] = 3; // built
    m.box[0] = m.box[4] = m.box[8] = 1.0f;    // identity matrix
    m.box[12] = m.box[13] = m.box[14] = 1.0f; // half extents +0x30/34/38
    const uint32_t zero = 0, tri = address(m.triangle);
    std::memcpy(reinterpret_cast<unsigned char*>(m.box) + 0x3c, &zero, 4); // children 0: a leaf
    std::memcpy(reinterpret_cast<unsigned char*>(m.box) + 0x40, &zero, 4);
    std::memcpy(reinterpret_cast<unsigned char*>(m.box) + 0x44, &tri, 4); // triangle pointer
    m.triangle[0] = 1;
    float* v = reinterpret_cast<float*>(m.triangle + 1);
    const float vertices[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(v, vertices, sizeof vertices);
}
static void set_triangle_z(Model& m, float z) {
    float* v = reinterpret_cast<float*>(m.triangle + 1);
    v[2] = v[5] = v[8] = z;
}
static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
struct Query {
    float R1[9], T1[3], R2[9], T2[3];
    uint32_t s1, s2;
    uint32_t model_a, model_b, flags, cap, tolerance, init;
    bool with_minimum;
};
// The running minimum each image's leaf may write (distance mode): separate floats, reset equal before every fire.
static float minimum_m = 0, minimum_n = 0;
constexpr float minimum_start = 1.0e30f;
// Writes the caller's mode globals into one image, then calls (M) the production thunk or (N) the native query directly,
// with the six stack words and ECX/EDX/XMM2 set, under a full CPU image.
static State fire_on(uint32_t base, const Query& q, uint32_t mxcsr) {
    const bool on_m = base == base_m;
    float* minimum = on_m ? &minimum_m : &minimum_n;
    *minimum = minimum_start;
    word(base, F::flags_rva) = q.flags;
    word(base, F::cap_rva) = q.cap;
    word(base, F::tolerance_rva) = q.tolerance;
    word(base, F::minimum_rva) = q.with_minimum ? address(minimum) : 0u;
    word(base, F::init_rva) = q.init;
    F::Regs regs{q.R1, q.T1, q.s1};
    const uint32_t args[6] = {q.model_a, address(q.R2), address(q.T2), q.s2, q.model_b, q.flags & F::mode_mask};
    State st{};
    SetLastError(0xcafef00d);
    void* const target = on_m ? reinterpret_cast<void*>(&x3m_collide_memo_fl_thunk)
                              : reinterpret_cast<void*>(std::uintptr_t(base_n) + F::query_rva);
    fx_call(&regs, args, target, &st, mxcsr);
    check(GetLastError() == 0xcafef00d, "query preserves LastError");
    return st;
}
// Everything a query leaves behind: the caller's result global, the counters, the root scratch (AP order), the contact
// record and the running minimum.
struct Outputs {
    uint32_t contacts, visits, triangles, first_contact, root[13], record[6];
    float minimum;
};
static Outputs outputs(uint32_t base) {
    Outputs o{};
    o.contacts = word(base, F::contacts_rva);
    o.visits = word(base, F::visits_rva);
    o.triangles = word(base, F::triangles_rva);
    o.first_contact = word(base, F::first_contact_rva);
    for (unsigned i = 0; i < 3; ++i) o.root[i] = word(base, F::root_t_rva + 4 * i);
    o.root[3] = word(base, F::root_scale_rva);
    for (unsigned i = 0; i < 9; ++i) o.root[4 + i] = word(base, F::root_r_rva + 4 * i);
    for (unsigned i = 0; i < 6; ++i) o.record[i] = word(base, 0x1aa0a8 + 4 * i);
    o.minimum = base == base_m ? minimum_m : minimum_n;
    return o;
}
static bool same(const Outputs& a, const Outputs& b) {
    return !std::memcmp(&a, &b, sizeof a);
}
// The miss path is transparent: M's thunk ran the engine, so its whole CPU image (result, ECX/EDX, callee-saved, stack
// balance, x87 environment, MXCSR, XMM0-7) equals N's direct engine call.
static bool same_cpu(const State& m, const State& n) {
    return m.result == n.result && m.ecx == n.ecx && m.edx == n.edx && m.ebx == n.ebx && m.ebp == n.ebp &&
           m.esi == n.esi && m.edi == n.edi && m.esp_balanced == 0 && n.esp_balanced == 0 &&
           !std::memcmp(m.fp_out, n.fp_out, 288);
}
static Query make_query(float dx, bool distance_mode) {
    Query q{};
    std::memcpy(q.R1, identity, sizeof identity);
    std::memcpy(q.R2, identity, sizeof identity);
    q.T2[0] = dx;
    std::memcpy(&q.s1, &one, 4);
    std::memcpy(&q.s2, &one, 4);
    q.model_a = address(&model_a);
    q.model_b = address(&model_b);
    q.flags = distance_mode ? 0xc : 2; // the two encoded callers: distance (0xc, cap 8, minimum) and boolean (2, cap 1)
    q.cap = distance_mode ? 8 : 1;
    q.with_minimum = distance_mode;
    q.init = 1;
    return q;
}
static uint32_t rng = 0x9e3779b9u;
static float randf(float lo, float hi) {
    rng += 0x9e3779b9u;
    uint32_t z = rng;
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    z ^= z >> 16;
    return lo + (hi - lo) * float(z >> 8) / float(1 << 24);
}
// A random key: one query in six brings model B within reach of the leaf (dx in [-2, 2]), the rest are far apart; a
// matrix entry and a translation are jittered so keys differ; half run in distance mode.
static Query random_query() {
    const bool near_pair = randf(0.f, 6.f) < 1.f;
    Query q = make_query(near_pair ? randf(-2.f, 2.f) : randf(100.f, 1.0e6f), randf(0.f, 1.f) < 0.5f);
    q.R2[0] = 1.0f + randf(-0.01f, 0.01f);
    q.T2[1] = randf(-0.5f, 0.5f);
    return q;
}
static void present(uint32_t frame) {
    memo::present(7, frame, false);
}
// The below-root (ABA) control: a near key whose run reaches the leaf without a contact while B's triangle sits at z,
// and finds a contact with the triangle at 0 (same header, same root box: the memo key cannot see the change).
static bool find_aba(Query* out, float* z_out) {
    for (const float z : {0.5f, 0.25f, 0.75f, 0.9f})
        for (const float dx : {0.0f, 0.1f, 0.25f, 0.4f}) {
            const Query q = make_query(dx, false);
            set_triangle_z(model_b, z);
            fire_on(base_n, q, 0x1f80);
            fire_on(base_m, q, 0x1f80);
            const Outputs apart = outputs(base_n);
            set_triangle_z(model_b, 0.0f);
            fire_on(base_n, q, 0x1f80);
            fire_on(base_m, q, 0x1f80);
            const Outputs touching = outputs(base_n);
            if (apart.contacts == 0 && apart.triangles > 0 && touching.contacts > 0) {
                *out = q;
                *z_out = z;
                return true;
            }
        }
    return false;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) {
        std::printf("FAIL arguments argc=%d\n", argc);
        return 2;
    }
    if (!map_fl_image(argv[1], img_m) || !map_fl_image(argv[1], img_n)) {
        std::printf("SKIP image\n");
        return 0;
    }
    base_m = address(img_m.base);
    base_n = address(img_n.base);
    std::printf("IMAGE size=%u relocated=%u images=2\n", img_m.size, img_m.relocated);
    build_model(model_a);
    build_model(model_b);
    memo::fixture_base(base_m);
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", L"1");
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO_VERIFY", L"1");
    SetEnvironmentVariableW(L"X3M_TELEMETRY", L"1");
    const uint32_t gate = 0x1f80;

    // ================= phase A: production verify mode =================
    // Every would-be hit runs the engine anyway and the production store() compares visits, triangles, the tolerance
    // integer and the 14-word root block (and contacts == 0) against the stored entry: verify_mismatches is the
    // production comparison, against a real engine run of the same query.
    SetLastError(0xaabbccdd);
    check(memo::initialize() && !std::strcmp(memo::state(), "ok"), "install ok (verify)", memo::state());
    check(GetLastError() == 0xaabbccdd, "install preserves LastError");
    check(img_m.base[F::site_rva] == 0xe8 && std::memcmp(img_m.base + F::site_rva, F::expected_call, 5) != 0,
          "call redirected in M");
    check(!std::memcmp(img_n.base + F::site_rva, F::expected_call, 5), "N keeps the engine's call");
    unsigned a_queries = 0, a_contact = 0, a_miss_cpu = 0;
    const core::Counters a0 = memo::counters();
    for (uint32_t frame = 1; frame <= 200; ++frame) {
        present(frame);
        for (unsigned i = 0; i < 12; ++i) {
            const Query q = random_query();
            for (unsigned rep = 0; rep < 3; ++rep) { // a store, then two would-be hits verified against the engine
                const State sn = fire_on(base_n, q, gate);
                const Outputs on = outputs(base_n);
                const State sm = fire_on(base_m, q, gate);
                const Outputs om = outputs(base_m);
                ++a_queries;
                a_contact += on.contacts != 0;
                // in verify mode M always runs the engine: its outputs and its CPU image equal N's
                check(same(om, on), "verify mode: M's outputs equal the native run");
                a_miss_cpu += !same_cpu(sm, sn);
            }
        }
    }
    check(a_miss_cpu == 0, "the thunk's engine path is CPU-transparent (x87, MXCSR, XMM, GPRs equal to N)");
    const core::Counters a1 = memo::counters();
    const uint32_t a_verified = a1.verified - a0.verified, a_mismatch = a1.verify_mismatches - a0.verify_mismatches;
    check(a_verified > 1000 && a_mismatch == 0, "verify mode: every would-be hit matched a real engine run");
    // negative control: the same key after a below-root geometry change (the documented ABA blind spot) must show up as
    // a verify mismatch, so the comparison above can fail.
    Query aba{};
    float aba_z = 0;
    const bool aba_found = find_aba(&aba, &aba_z);
    check(aba_found, "an ABA control key exists (leaf reached, no contact at z, a contact at 0)");
    uint32_t a_control = 0;
    if (aba_found) {
        present(201);
        set_triangle_z(model_b, aba_z);
        fire_on(base_n, aba, gate);
        fire_on(base_m, aba, gate); // stored: no contact
        set_triangle_z(model_b, 0.0f);
        const core::Counters before = memo::counters();
        fire_on(base_n, aba, gate);
        fire_on(base_m, aba, gate); // a would-be hit; the engine now finds a contact
        a_control = memo::counters().verify_mismatches - before.verify_mismatches;
        check(a_control == 1, "verify mode detects the below-root change (negative control)");
    }
    std::printf("VERIFY queries=%u contact_runs=%u verified=%lu verify_mismatches=%lu miss_cpu_differences=%u "
                "aba_control_mismatches=%lu\n",
                a_queries, a_contact, (unsigned long)a_verified, (unsigned long)a_mismatch, a_miss_cpu,
                (unsigned long)a_control);
    check(memo::shutdown() && !std::strcmp(memo::state(), "restored"), "verify install restored", memo::state());

    // ================= phase B: replay mode, every replay compared with N's engine run =================
    check(memo::install_base(base_m, false) && !std::strcmp(memo::state(), "ok"), "install ok (replay)", memo::state());
    unsigned b_queries = 0, b_contact_pairs = 0, replays = 0, replay_disagreements = 0, miss_disagreements = 0,
             replay_cpu = 0, miss_cpu = 0;
    const core::Counters b0 = memo::counters();
    for (uint32_t frame = 300; frame < 500; ++frame) {
        present(frame);
        for (unsigned i = 0; i < 12; ++i) {
            const Query q = random_query();
            for (unsigned rep = 0; rep < 3; ++rep) {
                const State sn = fire_on(base_n, q, gate);
                const Outputs on = outputs(base_n);
                const uint32_t hits_before = memo::counters().hits;
                const State sm = fire_on(base_m, q, gate);
                const Outputs om = outputs(base_m);
                ++b_queries;
                if (rep == 0) b_contact_pairs += on.contacts != 0;
                if (memo::counters().hits > hits_before) {
                    ++replays; // the engine was skipped: every output must still equal the native run's
                    replay_disagreements += !same(om, on);
                    replay_cpu += !(sm.ebx == 0x0b0b0b0b && sm.ebp == 0x0e0e0e0e && sm.esi == 0x05050505 &&
                                    sm.edi == 0x0d0d0d0d && sm.esp_balanced == 0 &&
                                    !std::memcmp(sm.fp_in, sm.fp_out, 288));
                } else {
                    miss_disagreements += !same(om, on);
                    miss_cpu += !same_cpu(sm, sn);
                }
            }
        }
    }
    const core::Counters b1 = memo::counters();
    check(replays > 1000, "real replays exercised");
    check(b_contact_pairs > 100, "real contact pairs exercised (computed, never stored by policy)");
    check(replay_disagreements == 0, "every replay equals the native run (contacts, counters, root block, record, min)");
    check(miss_disagreements == 0, "every engine run through the thunk equals the native run");
    check(replay_cpu == 0, "a replay preserves callee-saved GPRs, stack balance, x87, MXCSR and XMM exactly");
    check(miss_cpu == 0, "a miss is CPU-transparent (equal to N, MXCSR status included)");
    // negative control in replay mode: the ABA key replays the stale no-contact while N finds the contact
    uint32_t b_control = 0;
    if (aba_found) {
        present(500);
        set_triangle_z(model_b, aba_z);
        fire_on(base_n, aba, gate);
        fire_on(base_m, aba, gate); // stored: no contact
        set_triangle_z(model_b, 0.0f);
        fire_on(base_n, aba, gate);
        const Outputs on = outputs(base_n);
        const uint32_t hits_before = memo::counters().hits;
        fire_on(base_m, aba, gate);
        const Outputs om = outputs(base_m);
        b_control = memo::counters().hits > hits_before && !same(om, on) && on.contacts > 0 && om.contacts == 0;
        check(b_control == 1, "the replay comparison detects the below-root change (negative control)");
    }
    std::printf("DIFFERENTIAL queries=%u hits=%lu misses=%lu stored=%lu contacts=%lu contact_pairs=%u replays=%u "
                "replay_disagreements=%u miss_disagreements=%u replay_cpu_differences=%u miss_cpu_differences=%u "
                "aba_control_detected=%u\n",
                b_queries, (unsigned long)(b1.hits - b0.hits), (unsigned long)(b1.misses - b0.misses),
                (unsigned long)(b1.stored - b0.stored), (unsigned long)(b1.contacts - b0.contacts), b_contact_pairs,
                replays, replay_disagreements, miss_disagreements, replay_cpu, miss_cpu, unsigned(b_control));

    // ================= invalidation: movement, ship change, frame gap =================
    present(600);
    const Query store_q = make_query(5.0e5f, false);
    fire_on(base_m, store_q, gate);
    const core::Counters before_move = memo::counters();
    Query moved = store_q;
    moved.T1[0] = 123.0f; // movement: a different transform word
    fire_on(base_m, moved, gate);
    check(memo::counters().misses > before_move.misses, "a moved transform misses");
    Model other;
    build_model(other);
    Query ship = store_q;
    ship.model_b = address(&other); // a different ship: the model pointer changes
    const core::Counters before_ship = memo::counters();
    fire_on(base_m, ship, gate);
    check(memo::counters().misses > before_ship.misses, "a changed ship misses");
    other.box[12] = 2.0f; // the same ship pointer with a changed root box: a stamp word differs
    const core::Counters before_root = memo::counters();
    fire_on(base_m, ship, gate);
    check(memo::counters().misses > before_root.misses, "a changed root box misses");
    present(601);
    const core::Counters before_hit = memo::counters();
    fire_on(base_m, store_q, gate);
    check(memo::counters().hits > before_hit.hits, "the next frame still hits");
    present(602);
    present(603);
    const core::Counters before_expire = memo::counters();
    fire_on(base_m, store_q, gate);
    check(memo::counters().misses > before_expire.misses, "a two-frame gap expires the entry (miss)");

    // ================= the FP context gates =================
    present(700);
    const Query fp_q = make_query(4.0e5f, false);
    fire_on(base_m, fp_q, gate);
    fire_on(base_m, fp_q, gate);
    const memo::FpCounters fp0 = memo::fp_counters();
    const core::Counters bypass_before = memo::counters();
    fire_on(base_m, fp_q, 0x1f00); // invalid-operation unmasked (these inputs raise none): bypass, native trap behaviour stays
    check(memo::fp_counters().unmasked > fp0.unmasked, "an unmasked exception bypasses the memo");
    check(memo::counters().hits == bypass_before.hits, "the bypass is not a hit");
    const memo::FpCounters fp1 = memo::fp_counters();
    fire_on(base_m, fp_q, 0x3f80); // a changed rounding control: the table is cleared
    check(memo::fp_counters().context_clears > fp1.context_clears, "a changed MXCSR control clears the table");

    // ================= device_reset, the 300-frame row, shutdown, late_claim =================
    memo::device_reset();
    present(800);
    const core::Counters after_reset = memo::counters();
    fire_on(base_m, store_q, gate);
    check(memo::counters().clears > after_reset.clears, "device_reset drops the table at the next query");
    for (uint32_t f = 801; f <= 900; ++f) present(f);
    check(!memo_row.empty() && memo_row.find("edition=fl") != std::string::npos, "the 300-frame memo row is edition=fl");
    SetLastError(0x5a5a5a5a);
    check(memo::shutdown() && !std::strcmp(memo::state(), "restored"), "shutdown restores", memo::state());
    check(GetLastError() == 0x5a5a5a5a, "shutdown preserves LastError");
    check(!std::memcmp(img_m.base + F::site_rva, F::expected_call, F::call_length), "the call is back to the engine's bytes");
    engine_patch::close_install_window("fixture_late");
    check(!memo::install_base(base_m, false) && !std::strcmp(memo::state(), "late_claim"), "late_claim after the window",
          memo::state());

    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
