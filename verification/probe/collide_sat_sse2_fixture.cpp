// X3 CPU fixture of the SSE2 separating-axis replacement
// (src/proxy/collide_sat_sse2.cpp, sector-collide.md 12.8). The engine's
// 0x004e3280 runs here as a byte replica (generated, untracked
// sat_replica_inc.h: only the rel32 of its 24 `fabs` calls and the abs32 of
// its nine reps loads differ; re-hashed against the pinned FNV below), next to
// a layout-preserving copy of 0x004e2530..0x004e25ae (head, visit counter,
// argument set-up, the call, `add esp,8; test; jne` into the engine's own
// return-0 tail at its engine offset).
// Checks: (i) the replica keeps a pair => the SSE2 version keeps it, over
// > 1e6 node pairs (realistic station/ship scales, near-tangent pairs found by
// bisection on the replica, degenerate, huge, NaN/inf); (ii) how often SSE2
// keeps what the replica prunes; (iii) the separating axis number; (iv)
// registers, EFLAGS, x87 environment and stack, MXCSR (hostile values),
// engine locals and LastError across the patched call; (v) ns per call,
// replica against SSE2, for an early-separation and a full-overlap mix;
// coexistence with the narrow census's site-7 claim on the same function;
// refusals, restore, closed window. Diagnostic timings, not game FPS. Never
// launches the game.
#include "../../src/proxy/collide_sat_sse2.h"
#include "../../src/proxy/collide_narrow_census_core.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <cmath>
#include <limits>
#include <vector>
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    static unsigned lines = 0;
    if (lines++ < 6) std::printf("%s\n", text);
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
namespace sat = x3m::collide_sat_sse2;
namespace core = x3m::collide_sat_sse2::core;
namespace census = x3m::collide_narrow_census::core;
namespace engine_patch = x3m::engine_patch;

extern "C" {
extern unsigned char synthetic_descent[], fx_sat_replica[], fx_fabs[];
float fx_reps = 1e-6f;
std::uint32_t fx_hits = 0, fx_mode = 0, fx_flagword = 0, fx_cap = 0, fx_visits = 0, fx_frame = 0, fx_state = 0,
              fx_saved_ebp = 0, fx_default_mxcsr = 0x1f80;
struct Frame {
    std::uint32_t a_node, b_node, R, T, s_bits; // 0..16: the five arguments of 0x004e2530
    std::uint32_t control_word, mxcsr_in, code; // 20, 24, 28 (code 2 = reached 0x004e25af)
    std::uint32_t out_site[9];                  // 32: PUSHAD order + EFLAGS at 0x004e25af
    std::uint32_t out_return[9];                // 68: the same after the synthetic function returned
    std::uint32_t x87env[7];                    // 104
    std::uint32_t mxcsr_out;                    // 132
    std::uint32_t locals[20];                   // 136: the 0x50-byte engine frame at 0x004e25af
};
struct State {
    std::uint32_t control_word, mxcsr_in;      // 0, 4
    std::uint32_t regs[9];                     // 8: PUSHAD order + EFLAGS right after the call
    std::uint32_t env_before[7], env_after[7]; // 44, 72
    std::uint32_t mxcsr_out, pad[2];           // 100
    double st0, st1, third; // 112, 120, 128: `third` = 1/3 computed in x87 after the call (the rounding mode in force)
};
std::uint32_t fx_descend_call(Frame* frame);
std::uint32_t __cdecl fx_call_fast(void* fn, const float* R, const float* b, const float* T, const float* a);
std::uint32_t __cdecl fx_call_state(void* fn, const float* R, const float* b, const float* T, const float* a,
                                    State* state);
void __cdecl fx_set_cw(std::uint32_t control_word);
void fx_null_sat();
void fx_thunk_bracketed();
}
static_assert(offsetof(Frame, out_site) == 32 && offsetof(Frame, out_return) == 68 && offsetof(Frame, x87env) == 104 &&
                  offsetof(Frame, mxcsr_out) == 132 && offsetof(Frame, locals) == 136,
              "frame layout");
static_assert(offsetof(State, regs) == 8 && offsetof(State, env_before) == 44 && offsetof(State, env_after) == 72 &&
                  offsetof(State, mxcsr_out) == 100 && offsetof(State, st0) == 112 && offsetof(State, st1) == 120,
              "state layout");
#include "sat_replica_inc.h"
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _synthetic_descent
_synthetic_descent:
    .byte 0xa1
    .long _fx_hits
    .byte 0x83,0xec,0x40, 0x83,0x3d
    .long _fx_mode
    .byte 0x00, 0x53,0x55,0x56,0x57, 0x74,0x0e, 0x85,0xc0, 0x7e,0x0a, 0x33,0xc0, 0x5f,0x5e,0x5d,0x5b, 0x83,0xc4,0x40, 0xc3
    .byte 0xf6,0x05
    .long _fx_flagword
    .byte 0x04, 0x74,0x08, 0x3b,0x05
    .long _fx_cap
    .byte 0x7d,0xe5
    .byte 0x8b,0x6c,0x24,0x58, 0xd9,0x45,0x30, 0x8b,0x44,0x24,0x54, 0xd9,0x44,0x24,0x64, 0x8b,0x5c,0x24,0x60, 0x8b,0x74,0x24,0x5c, 0xdc,0xc9, 0x83,0x05
    .long _fx_visits
    .byte 0x01
    .byte 0xd9,0xc9, 0x83,0xc0,0x30, 0xd9,0x5c,0x24,0x20, 0x50, 0xd9,0x45,0x34, 0x53, 0xd8,0xc9, 0x8d,0x7c,0x24,0x28
    .byte 0xd9,0x5c,0x24,0x2c, 0xd8,0x4d,0x38, 0xd9,0x5c,0x24,0x30
    .byte 0xe8
    .long _fx_sat_replica - (. + 4)
    .byte 0x83,0xc4,0x08, 0x85,0xc0, 0x75,0x9a
    jmp _fx_descend_exit
    .p2align 4
_fx_descend_exit:
    pushfd
    pushad
    mov eax, dword ptr [_fx_frame]
    mov esi, esp
    lea edi, [eax+32]
    mov ecx, 9
    cld
    rep movsd
    lea esi, [esp+36]
    lea edi, [eax+136]
    mov ecx, 20
    rep movsd
    mov dword ptr [eax+28], 2
    popad
    popfd
    mov eax, 0x7e57
    pop edi
    pop esi
    pop ebp
    pop ebx
    add esp, 0x40
    ret

    .globl _fx_descend_call
_fx_descend_call:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+8]
    mov dword ptr [_fx_frame], eax
    lea edi, [esp-256]
    mov ecx, 64
    mov eax, 0x5afe5afe
    cld
    rep stosd
    mov eax, dword ptr [_fx_frame]
    fninit
    fldcw word ptr [eax+20]
    ldmxcsr dword ptr [eax+24]
    push dword ptr [eax+16]
    push dword ptr [eax+12]
    push dword ptr [eax+8]
    push dword ptr [eax+4]
    push dword ptr [eax]
    mov ecx, 0xc1c1c1c1
    mov edx, 0xdddddddd
    mov ebx, 0xb0b0b0b0
    mov esi, 0x51515151
    mov edi, 0xd1d1d1d1
    call _synthetic_descent
    lea esp, [esp+0x14]
    pushfd
    pushad
    mov ebx, dword ptr [_fx_frame]
    fnstenv [ebx+104]
    stmxcsr dword ptr [ebx+132]
    mov esi, esp
    lea edi, [ebx+68]
    mov ecx, 9
    cld
    rep movsd
    add esp, 36
    fninit
    ldmxcsr dword ptr [_fx_default_mxcsr]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .globl _fx_call_fast
_fx_call_fast:
    push esi
    push edi
    mov esi, dword ptr [esp+16]
    mov edi, dword ptr [esp+20]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    call dword ptr [esp+20]
    add esp, 8
    pop edi
    pop esi
    ret

    .globl _fx_call_state
_fx_call_state:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+28]
    mov dword ptr [_fx_state], eax
    fninit
    fldcw word ptr [eax]
    fldpi
    fld1
    fnstenv [eax+44]
    ldmxcsr dword ptr [eax+4]
    fldcw word ptr [eax]
    mov esi, dword ptr [ebp+12]
    mov edi, dword ptr [ebp+16]
    push dword ptr [ebp+24]
    push dword ptr [ebp+20]
    mov eax, dword ptr [ebp+8]
    mov ecx, 0xc1c1c1c1
    mov edx, 0xdddddddd
    mov ebx, 0xb0b0b0b0
    mov dword ptr [_fx_saved_ebp], ebp
    mov ebp, 0xebebebeb
    call eax
    lea esp, [esp+8]
    pushfd
    pushad
    mov ebp, dword ptr [_fx_saved_ebp]
    mov ebx, dword ptr [_fx_state]
    mov esi, esp
    lea edi, [ebx+8]
    mov ecx, 9
    cld
    rep movsd
    add esp, 36
    fnstenv [ebx+72]
    stmxcsr dword ptr [ebx+100]
    fstp qword ptr [ebx+112]
    fstp qword ptr [ebx+120]
    fld1
    fld1
    fadd st, st
    fld1
    faddp st(1), st
    fdivp st(1), st
    fstp qword ptr [ebx+128]
    fninit
    ldmxcsr dword ptr [_fx_default_mxcsr]
    mov eax, dword ptr [ebx+36]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

    .globl _fx_set_cw
_fx_set_cw:
    fninit
    fldcw word ptr [esp+4]
    ret
    .globl _fx_null_sat
_fx_null_sat:
    xor eax, eax
    ret
    .globl _fx_thunk_bracketed
_fx_thunk_bracketed:
    push ecx
    push edx
    push eax
    stmxcsr dword ptr [esp]
    ldmxcsr dword ptr [_x3m_collide_sat_mxcsr]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push edi
    push esi
    call _x3m_collide_sat_sse2
    lea esp, [esp+16]
    ldmxcsr dword ptr [esp]
    pop edx
    pop edx
    pop ecx
    ret
    .att_syntax
)");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        if (failures <= 40) std::printf("FAIL %s\n", label);
    }
}
static std::uint32_t addr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static std::uint64_t rng = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return std::uint32_t(rng >> 16);
}
static double u01() {
    return (rnd() + 0.5) / 4294967296.0;
}
static double uniform(double lo, double hi) {
    return lo + (hi - lo) * u01();
}
static double log_uniform(double lo, double hi) {
    return std::exp(uniform(std::log(lo), std::log(hi)));
}

struct Case {
    float R[9], b[3], T[3], a[3];
};
static void rotation(float* R) {
    double q[4], n;
    do {
        n = 0;
        for (double& v : q) {
            v = uniform(-1, 1);
            n += v * v;
        }
    } while (n < 1e-3 || n > 1);
    n = std::sqrt(n);
    for (double& v : q) v /= n;
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double m[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                         2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (unsigned i = 0; i < 9; ++i) R[i] = float(m[i]);
}
// Station-scale `a`, ship-scale `b`, a translation of `k` times the two box diagonals in a random direction.
// world_units = false: model units (extents 1e-3..50 against 1e-4..2). world_units = true: the magnitudes run 44
// observed, a station of radius 9.2e6 against a ship of radius 4.3e4, BVH boxes from the root down to 1e-4 of it, `b`
// multiplied by the per-query relative scale 0.5..2 exactly as 0x004e2530 does before the call (one float32 multiply).
static bool world_units = false;
static Case realistic(double k_lo, double k_hi) {
    Case c;
    rotation(c.R);
    double na = 0, nb = 0, d[3], nd = 0;
    const float scale = float(uniform(0.5, 2));
    for (unsigned i = 0; i < 3; ++i) {
        c.a[i] = float(world_units ? log_uniform(9.2e2, 9.2e6) : log_uniform(1e-3, 50));
        c.b[i] = world_units ? float(log_uniform(4.3, 4.3e4)) * scale : float(log_uniform(1e-4, 2));
        na += double(c.a[i]) * c.a[i];
        nb += double(c.b[i]) * c.b[i];
    }
    do {
        nd = 0;
        for (double& v : d) {
            v = uniform(-1, 1);
            nd += v * v;
        }
    } while (nd < 1e-3 || nd > 1);
    const double reach = uniform(k_lo, k_hi) * (std::sqrt(na) + std::sqrt(nb)) / std::sqrt(nd);
    for (unsigned i = 0; i < 3; ++i) c.T[i] = float(d[i] * reach);
    return c;
}
static Case degenerate(unsigned serial) {
    Case c = realistic(0, 2);
    const unsigned shape = serial % 5;
    if (shape <= 2) { // identity, a signed axis permutation, a rotation about one axis: RAPID's parallel-edge (reps)
                      // cases
        std::memset(c.R, 0, sizeof c.R);
        if (shape == 0)
            c.R[0] = c.R[4] = c.R[8] = 1;
        else if (shape == 1) {
            const unsigned p = rnd() % 3;
            for (unsigned r = 0; r < 3; ++r) c.R[3 * r + (r + p) % 3] = rnd() & 1 ? 1.0f : -1.0f;
        } else {
            const double t = rnd() % 4 ? uniform(0, 6.2831853) : 1.5707963267948966 * (rnd() % 4);
            const unsigned k = rnd() % 3, i = (k + 1) % 3, j = (k + 2) % 3;
            c.R[3 * k + k] = 1;
            c.R[3 * i + i] = c.R[3 * j + j] = float(std::cos(t));
            c.R[3 * i + j] = float(-std::sin(t));
            c.R[3 * j + i] = float(std::sin(t));
        }
    } else if (shape == 3) {
        for (float& r : c.R) r = float(uniform(-2, 2));
    } // not a rotation at all
    for (unsigned i = 0; i < 3; ++i) {
        if (rnd() % 10 < 3) c.a[i] = 0;
        if (rnd() % 10 < 3) c.b[i] = 0;
    }
    const unsigned t = rnd() % 5;
    if (t == 0)
        c.T[0] = c.T[1] = c.T[2] = 0;
    else if (t == 1) {
        const unsigned k = rnd() % 3;
        c.T[(k + 1) % 3] = c.T[(k + 2) % 3] = 0;
    } else if (t == 2) {
        const unsigned k = rnd() % 3;
        c.T[k] = c.a[k] + c.b[k];
        c.T[(k + 1) % 3] = c.T[(k + 2) % 3] = 0;
    } // faces touching exactly
    return c;
}
static Case hostile(unsigned serial) {
    Case c = realistic(0, 3);
    float* slots[18];
    for (unsigned i = 0; i < 9; ++i) slots[i] = &c.R[i];
    for (unsigned i = 0; i < 3; ++i) {
        slots[9 + i] = &c.b[i];
        slots[12 + i] = &c.T[i];
        slots[15 + i] = &c.a[i];
    }
    const float inf = std::numeric_limits<float>::infinity(),
                specials[4] = {std::numeric_limits<float>::quiet_NaN(), inf, -inf, 3.4028235e38f};
    switch (serial % 8) {
    case 0:
        for (unsigned i = 9; i < 18; ++i) *slots[i] *= 1e30f;
        break; // products overflow float32, not double
    case 1:
        for (unsigned i = 9; i < 18; ++i) *slots[i] *= 1e19f;
        break;
    case 2:
        for (unsigned i = 9; i < 18; ++i) *slots[i] *= 1e-38f;
        break; // denormals
    case 3:
        *slots[9 + rnd() % 9] *= -1.0f;
        if (serial & 8) c.a[rnd() % 3] = -c.a[0];
        break; // negative extents / mirrored T
    case 4:
        for (unsigned i = 12; i < 15; ++i) *slots[i] *= 3e37f;
        break; // huge translation only
    default:
        *slots[(serial / 8) % 18] = specials[(serial / 8 / 18) % 4];
        if (serial % 8 == 7) *slots[rnd() % 18] = specials[rnd() % 4];
        break;
    }
    return c;
}
static std::uint32_t call(void* fn, const Case& c) {
    return fx_call_fast(fn, c.R, c.b, c.T, c.a);
}
static void* const replica_fn = fx_sat_replica;
static void* const thunk_fn = reinterpret_cast<void*>(&x3m_collide_sat_thunk);

struct Tally {
    unsigned long cases = 0, both_keep = 0, both_prune = 0, sse_keeps = 0, violations = 0, axis_mismatch = 0,
                  axis_earlier = 0, outside_band = 0, axis_unexplained = 0;
};
// Every disagreement must be the 2^-20 margin and nothing else: every projected distance is linear in T, so with T
// stretched by (1 + 2^-18), four times the margin, the replacement must prune, at the engine's axis or an earlier one.
static std::uint32_t stretched(const Case& c) {
    Case s = c;
    for (float& v : s.T) v = float(double(v) * (1.0 + 0x1p-18));
    return call(thunk_fn, s);
}
static void compare(Tally& t, const Case& c) {
    const std::uint32_t engine = call(replica_fn, c), ours = call(thunk_fn, c);
    ++t.cases;
    if (engine > 15 || ours > 15) {
        ++t.violations;
        return;
    }
    if (!engine) {
        if (ours) {
            if (++t.violations <= 3)
                std::printf("DETAIL violation axis=%lu T=%a,%a,%a\n", (unsigned long)ours, c.T[0], c.T[1], c.T[2]);
        } else
            ++t.both_keep;
        return;
    }
    if (!ours) {
        ++t.sse_keeps;
        const std::uint32_t s = stretched(c);
        if (!s || s > engine) ++t.outside_band;
        return;
    }
    ++t.both_prune;
    if (ours != engine) {
        ++t.axis_mismatch;
        if (ours < engine) ++t.axis_earlier;
        const std::uint32_t s = stretched(c);
        if (!s || s > engine) ++t.axis_unexplained;
    }
}
static void report(const char* name, const Tally& t) {
    std::printf(
        "CATEGORY %s cases=%lu both_keep=%lu both_prune=%lu sse_keeps=%lu violations=%lu axis_mismatch=%lu axis_earlier=%lu outside_band=%lu axis_unexplained=%lu\n",
        name, t.cases, t.both_keep, t.both_prune, t.sse_keeps, t.violations, t.axis_mismatch, t.axis_earlier,
        t.outside_band, t.axis_unexplained);
}
// Scales T to the replica's own keep/prune boundary by bisection (every projected distance is linear in T, so the
// verdict is monotone in the scale), then emits the pairs around it: the adversarial near-tangent set.
static void near_tangent(Tally& t, std::vector<Case>* keep_sample) {
    Case c = realistic(0.2, 1.5);
    double lo = 0, hi = 64;
    auto scaled = [&](double k) {
        Case s = c;
        for (unsigned i = 0; i < 3; ++i) s.T[i] = float(double(c.T[i]) * k);
        return s;
    };
    if (call(replica_fn, scaled(lo)) || !call(replica_fn, scaled(hi))) return;
    for (unsigned i = 0; i < 60 && hi - lo > hi * 1e-16; ++i) {
        const double mid = 0.5 * (lo + hi);
        (call(replica_fn, scaled(mid)) ? hi : lo) = mid;
    }
    for (int step : {-64, -16, -4, -2, -1, 0, 1, 2, 4, 16, 64}) { // float steps of T: the margin is about 16 of them
                                                                  // wide
        Case s = scaled(step < 0 ? lo : hi);
        for (unsigned i = 0; i < 3; ++i)
            for (int n = 0; n < (step < 0 ? -step : step); ++n)
                s.T[i] = std::nextafter(s.T[i], step < 0 ? 0.0f : (s.T[i] < 0 ? -1e38f : 1e38f));
        compare(t, s);
        if (keep_sample && keep_sample->size() < 20000) keep_sample->push_back(s);
    }
}
static double seconds() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return double(c.QuadPart) / double(f.QuadPart);
}
static double bench_ns(void* fn, const std::vector<Case>& mix, unsigned rounds, std::uint64_t* sink) {
    std::uint64_t sum = 0;
    double best = 1e30;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const double t0 = seconds();
        for (unsigned r = 0; r < rounds; ++r)
            for (const Case& c : mix) sum += fx_call_fast(fn, c.R, c.b, c.T, c.a);
        const double ns = (seconds() - t0) * 1e9 / (double(rounds) * double(mix.size()));
        if (ns < best) best = ns;
    }
    *sink = sum;
    return best;
}

struct alignas(16) Node {
    unsigned char bytes[0x48];
};
static Node node_a, node_b;
static Frame frame_storage;
static Case frame_case;
struct Result {
    std::uint32_t code, eax, site[9], ret[9], env[7], mxcsr, locals[20], visits, last_error;
};
static Result run(const Case& c, float scale, std::uint32_t control_word, std::uint32_t mxcsr, unsigned serial) {
    frame_case = c;
    std::memset(&node_a, 0xa5, sizeof node_a);
    std::memset(&node_b, 0x5a, sizeof node_b);
    std::memcpy(node_a.bytes + 0x30, c.a, 12);
    std::memcpy(node_b.bytes + 0x30, c.b, 12);
    Frame& f = frame_storage;
    std::memset(&f, 0, sizeof f);
    f.a_node = addr(&node_a);
    f.b_node = addr(&node_b);
    f.R = addr(frame_case.R);
    f.T = addr(frame_case.T);
    std::memcpy(&f.s_bits, &scale, 4);
    f.control_word = control_word;
    f.mxcsr_in = mxcsr;
    f.code = 1;
    fx_visits = 0;
    SetLastError(0x5150 + serial);
    Result r{};
    r.eax = fx_descend_call(&f);
    r.last_error = GetLastError();
    r.code = f.code;
    std::memcpy(r.site, f.out_site, sizeof r.site);
    std::memcpy(r.ret, f.out_return, sizeof r.ret);
    std::memcpy(r.env, f.x87env, sizeof r.env);
    r.mxcsr = f.mxcsr_out;
    std::memcpy(r.locals, f.locals, sizeof r.locals);
    r.visits = fx_visits;
    // AF is undefined after `test eax,eax` (FEX derives it lazily); ESP values are compared as the frame relation
    // below.
    r.site[8] &= ~0x10u;
    r.ret[8] &= ~0x10u;
    // x87: control word, tag word and TOP must agree. C0-C3, the sticky exception bits and the last-instruction
    // pointers are left by the engine's own fcompp/fadd and dead at the site (the next x87 compare is followed by its
    // own fnstsw).
    r.env[1] &= 0x3800u;
    r.env[3] = r.env[4] = r.env[5] = r.env[6] = 0;
    return r;
}
static bool same(const Result& n, const Result& p, char* why) {
    why[0] = 0;
    if (n.code != p.code || n.eax != p.eax)
        std::sprintf(why, "exit %lu/%lu eax %lx/%lx", (unsigned long)n.code, (unsigned long)p.code,
                     (unsigned long)n.eax, (unsigned long)p.eax);
    else if (std::memcmp(n.site, p.site, sizeof n.site))
        std::sprintf(why, "state at 0x4e25af eax=%lx/%lx ecx=%lx/%lx edx=%lx/%lx esi=%lx/%lx edi=%lx/%lx flags=%lx/%lx",
                     (unsigned long)n.site[7], (unsigned long)p.site[7], (unsigned long)n.site[6],
                     (unsigned long)p.site[6], (unsigned long)n.site[5], (unsigned long)p.site[5],
                     (unsigned long)n.site[1], (unsigned long)p.site[1], (unsigned long)n.site[0],
                     (unsigned long)p.site[0], (unsigned long)n.site[8], (unsigned long)p.site[8]);
    else if (std::memcmp(n.ret, p.ret, sizeof n.ret))
        std::sprintf(why, "state at the return");
    else if (std::memcmp(n.env, p.env, sizeof n.env))
        std::sprintf(why, "x87 env cw=%04lx/%04lx sw=%04lx/%04lx tag=%04lx/%04lx", (unsigned long)n.env[0] & 0xffff,
                     (unsigned long)p.env[0] & 0xffff, (unsigned long)n.env[1], (unsigned long)p.env[1],
                     (unsigned long)n.env[2] & 0xffff, (unsigned long)p.env[2] & 0xffff);
    else if (n.mxcsr != p.mxcsr)
        std::sprintf(why, "MXCSR %lx/%lx", (unsigned long)n.mxcsr, (unsigned long)p.mxcsr);
    else if (std::memcmp(n.locals, p.locals, sizeof n.locals))
        std::sprintf(why, "engine locals");
    else if (n.visits != p.visits || n.last_error != p.last_error)
        std::sprintf(why, "visit counter or LastError");
    return !why[0];
}
static const unsigned site_offset = 0x73, region_length = 0x84;
static unsigned char original_region[region_length];
static bool region_original() {
    return !std::memcmp(synthetic_descent, original_region, region_length);
}
static sat::Addresses addresses() {
    return sat::Addresses{addr(synthetic_descent) + site_offset, addr(fx_sat_replica)};
}

int main() {
    DWORD old = 0;
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(synthetic_descent) & ~std::uintptr_t(0xfff);
    check(VirtualProtect(reinterpret_cast<void*>(page), 0x2000, PAGE_EXECUTE_READWRITE, &old) != FALSE,
          "synthetic code writable");
    std::memcpy(original_region, synthetic_descent, region_length);
    // ---- provenance: the replica is the pinned engine body, the copy of the caller carries the engine windows ----
    {
        static unsigned char body[core::sat_callee_length];
        std::memcpy(body, fx_sat_replica, sizeof body);
        for (unsigned short at : fx_replica_call_fields) {
            const std::uint32_t rel = std::uint32_t(0x0040e710u - (core::sat_target_va + at + 4));
            std::memcpy(body + at, &rel, 4);
        }
        for (unsigned short at : fx_replica_reps_fields) {
            const std::uint32_t va = 0x00565600u;
            std::memcpy(body + at, &va, 4);
        }
        check(core::fnv1a(body, sizeof body) == core::sat_callee_fnv1a &&
                  sizeof fx_replica_call_fields / sizeof fx_replica_call_fields[0] == 24 &&
                  sizeof fx_replica_reps_fields / sizeof fx_replica_reps_fields[0] == 9,
              "replica of 0x004e3280 is the pinned 1,582 bytes (24 rel32 and 9 abs32 operands aside)");
        unsigned char n7[census::n7_window_length];
        census::n7_expected(addr(&fx_hits), addr(&fx_mode), n7);
        const bool exact = !std::memcmp(synthetic_descent, n7, sizeof n7) &&
                           !std::memcmp(synthetic_descent + site_offset - core::sat_pre_length, core::sat_pre_window,
                                        core::sat_pre_length) &&
                           synthetic_descent[site_offset] == 0xe8 &&
                           !std::memcmp(synthetic_descent + site_offset + 5, core::sat_post_window,
                                        core::sat_post_length) &&
                           site_offset == core::sat_site_va - core::sat_function_va && fx_reps == core::reps;
        check(exact, "synthetic caller carries the engine's head and both windows at the engine's offsets");
        if (failures) {
            std::printf("COLLIDE SAT SSE2 CPU checks=%u failures=%u\n", checks, failures);
            return 1;
        }
    }

    // ---- (i)-(iii): verdicts over the node-pair population, x87 at 53-bit precision (the engine's D3D-less default is
    // 0x027f) ----
    fx_set_cw(0x027f);
    Tally real, tangent, degen, host, world, world_tangent;
    std::vector<Case> tangent_sample, early, overlap;
    for (unsigned i = 0; i < 1200000; ++i) compare(real, i & 1 ? realistic(0, 2.5) : realistic(0, 0.6));
    for (unsigned i = 0; i < 40000; ++i) near_tangent(tangent, &tangent_sample);
    for (unsigned i = 0; i < 150000; ++i) compare(degen, degenerate(i));
    for (unsigned i = 0; i < 150000; ++i) compare(host, hostile(i));
    world_units = true;
    for (unsigned i = 0; i < 400000; ++i) compare(world, i & 1 ? realistic(0, 2.5) : realistic(0, 0.6));
    for (unsigned i = 0; i < 20000; ++i) near_tangent(world_tangent, nullptr);
    world_units = false;
    report("realistic", real);
    report("near_tangent", tangent);
    report("degenerate", degen);
    report("hostile", host);
    report("world_scale", world);
    report("world_near_tangent", world_tangent);
    check(
        real.cases == 1200000 && real.both_keep > 100000 && real.both_prune > 100000 && tangent.cases > 300000 &&
            tangent.both_keep > 100000 && tangent.both_prune > 50000 && degen.both_keep > 10000 &&
            degen.both_prune > 10000 && host.cases == 150000 && world.both_keep > 30000 && world.both_prune > 100000 &&
            world_tangent.cases > 150000 && world_tangent.both_keep > 50000 && world_tangent.both_prune > 25000,
        "population: > 1e6 realistic pairs, > 3e5 near-tangent, the observed world magnitudes, both verdicts well represented");
    const Tally* all[6] = {&real, &tangent, &degen, &host, &world, &world_tangent};
    unsigned long total = 0, violations = 0, outside = 0, earlier = 0, unexplained = 0;
    for (const Tally* t : all) {
        total += t->cases;
        violations += t->violations;
        outside += t->outside_band;
        earlier += t->axis_earlier;
        unexplained += t->axis_unexplained;
    }
    check(violations == 0,
          "(i) replica keeps the pair => SSE2 keeps it, every category, NaN / inf / negative extents included");
    check(
        outside == 0,
        "(ii) every pair SSE2 keeps and the replica prunes lies inside the 2^-20 margin (pruned once T is stretched by 2^-18)");
    check(
        host.sse_keeps + host.axis_mismatch <= host.cases / 1000 && real.sse_keeps <= real.cases / 10000 &&
            world.sse_keeps <= world.cases / 10000,
        "(ii) the extra-keep rate is margin-sized on random pairs, and non-finite inputs get the engine's verdict and axis");
    check(earlier == 0 && unexplained == 0,
          "(iii) same separating axis, or a later one only inside the margin; never an earlier one");
    std::printf(
        "KEEP total=%lu sse_keeps_realistic=%lu sse_keeps_world=%lu sse_keeps_near_tangent=%lu sse_keeps_world_near_tangent=%lu sse_keeps_degenerate=%lu sse_keeps_hostile=%lu axis_mismatch_near_tangent=%lu axis_mismatch_hostile=%lu\n",
        total, real.sse_keeps, world.sse_keeps, tangent.sse_keeps, world_tangent.sse_keeps, degen.sse_keeps,
        host.sse_keeps, tangent.axis_mismatch + world_tangent.axis_mismatch, host.axis_mismatch);
    // Other x87 precision controls on the near-tangent sample (FEX's reduced-precision x87 computes in double whatever
    // PC says; a native 24-bit x87 differs from double by ~2^-24, which the geometric argument of 12.8 covers, not this
    // comparison).
    {
        Tally extended, single;
        fx_set_cw(0x037f);
        for (const Case& c : tangent_sample) compare(extended, c);
        fx_set_cw(0x007f);
        for (const Case& c : tangent_sample) compare(single, c);
        fx_set_cw(0x027f);
        std::printf(
            "PRECISION sample=%lu cw037f_violations=%lu cw037f_sse_keeps=%lu cw007f_violations=%lu cw007f_sse_keeps=%lu\n",
            extended.cases, extended.violations, extended.sse_keeps, single.violations, single.sse_keeps);
        check(extended.cases == tangent_sample.size() && extended.violations == 0,
              "(i) holds on the near-tangent sample at 64-bit x87 precision too");
    }

    // ---- (iv) direct call: every register, x87 environment and stack, MXCSR (hostile values) across the thunk ----
    const std::uint32_t mxcsrs[5] = {0x1f80, 0x3fbf, 0x5f80, 0x7fc0, 0x0f80}; // default; round down + sticky; round up;
                                                                              // chop + DAZ; precision exception
                                                                              // unmasked
    // Calibration with a callee that does nothing: an emulator that does not track the sticky exception flags (FEX)
    // cannot round-trip them through ldmxcsr/stmxcsr at all; those bits are then left out of every MXCSR comparison
    // below, and said so.
    std::uint32_t mxcsr_mask = 0xffffffffu;
    {
        const Case c = realistic(0, 1);
        State s{};
        s.control_word = 0x027f;
        s.mxcsr_in = 0x3fbf;
        fx_call_state(reinterpret_cast<void*>(&fx_null_sat), c.R, c.b, c.T, c.a, &s);
        if (s.mxcsr_out == 0x3f80) mxcsr_mask = ~0x3fu;
        std::printf("MXCSR roundtrip in=3fbf out=%lx sticky_flags_tracked=%u\n", (unsigned long)s.mxcsr_out,
                    mxcsr_mask == 0xffffffffu ? 1u : 0u);
        check(s.mxcsr_out == 0x3fbf || s.mxcsr_out == 0x3f80, "MXCSR control bits round-trip in this environment");
    }
    {
        unsigned bad_regs = 0, bad_env = 0, bad_mxcsr = 0, bad_stack = 0, bad_result = 0, bad_rounding = 0,
                 shared_rounding = 0, chop_verdicts = 0;
        for (unsigned i = 0; i < 20000; ++i) {
            const Case& c = tangent_sample[i % tangent_sample.size()];
            State s{};
            s.control_word = (i & 1) ? 0x027f : 0x0f7f;
            s.mxcsr_in = mxcsrs[i % 5];
            const std::uint32_t expected = call(thunk_fn, c);
            const std::uint32_t got = fx_call_state(thunk_fn, c.R, c.b, c.T, c.a, &s);
            // FEX keeps one host rounding mode: with the x87 control word written last and set to chop, SSE arithmetic
            // chops too whatever MXCSR says, and so does the engine's own x87 test. Counted apart; the verdict must not
            // move otherwise.
            if (s.regs[7] != got || got > 15)
                ++bad_result;
            else if (got != expected) {
                if (s.control_word & 0x0c00)
                    ++chop_verdicts;
                else
                    ++bad_result;
            }
            if (s.regs[0] != addr(c.b) || s.regs[1] != addr(c.R) || s.regs[2] != 0xebebebebu ||
                s.regs[4] != 0xb0b0b0b0u || s.regs[5] != 0xddddddddu || s.regs[6] != 0xc1c1c1c1u)
                ++bad_regs;
            if (std::memcmp(s.env_before, s.env_after, sizeof s.env_before)) ++bad_env;
            const bool default_control = (s.mxcsr_in & 0xffc0u) == 0x1f80u; // the thunk's no-write path: control bits
                                                                            // compared, sticky flags may accumulate
            if ((s.mxcsr_out ^ s.mxcsr_in) & (default_control ? 0xffc0u : mxcsr_mask) && ++bad_mxcsr <= 2)
                std::printf("DETAIL mxcsr in=%lx out=%lx\n", (unsigned long)s.mxcsr_in, (unsigned long)s.mxcsr_out);
            if (s.st0 != 1.0 || s.st1 != 3.141592653589793) ++bad_stack;
            State n{};
            n.control_word = s.control_word;
            n.mxcsr_in = s.mxcsr_in;
            fx_call_state(reinterpret_cast<void*>(&fx_null_sat), c.R, c.b, c.T, c.a, &n);
            if (std::memcmp(&n.third, &s.third, 8) && !default_control)
                ++shared_rounding; // FEX: one host rounding mode, the restoring ldmxcsr re-imposes MXCSR's on x87
            else if (std::memcmp(&n.third, &s.third, 8) && ++bad_rounding <= 2)
                std::printf("DETAIL x87 rounding after the call cw=%lx mxcsr=%lx null=%a thunk=%a\n",
                            (unsigned long)s.control_word, (unsigned long)s.mxcsr_in, n.third, s.third);
        }
        check(
            bad_result == 0,
            "thunk: the verdict does not depend on the caller's MXCSR (rounding mode, DAZ, masks) with x87 rounding to nearest");
        check(bad_regs == 0, "thunk: ECX, EDX, EBX, EBP, ESI, EDI unchanged, EAX = the verdict");
        check(bad_env == 0 && bad_stack == 0, "thunk: the whole fnstenv image and both live x87 registers unchanged");
        check(
            bad_mxcsr == 0,
            "thunk: MXCSR control bits untouched on the default path, the whole register restored on the bracketed path");
        check(
            bad_rounding == 0,
            "thunk, default MXCSR control: x87 arithmetic after the call rounds as after a callee that touches nothing, whatever the x87 rounding mode");
        std::printf(
            "ROUNDING bracketed_path_x87_rounding_follows_mxcsr=%u of 16000 (emulator artefact: 0 on hardware with separate x87 and SSE rounding) near_tangent_verdicts_moved_by_x87_chop=%u of 10000\n",
            shared_rounding, chop_verdicts);
        if (bad_regs || bad_env || bad_mxcsr || bad_stack || bad_result)
            std::printf("DETAIL state regs=%u env=%u mxcsr=%u stack=%u result=%u\n", bad_regs, bad_env, bad_mxcsr,
                        bad_stack, bad_result);
    }

    // ---- native replay through the layout-preserving caller ----
    struct Scenario {
        Case c;
        float scale;
        std::uint32_t control_word, mxcsr;
    };
    std::vector<Scenario> scenarios;
    const std::uint32_t words[4] = {0x027f, 0x037f, 0x007f, 0x0f7f};
    for (unsigned i = 0; i < 3000; ++i)
        scenarios.push_back(Scenario{i % 2 == 0 ? realistic(0, 0.3) : realistic(0.5, 4), float(uniform(0.5, 2)),
                                     words[i % 4], mxcsrs[i % 5]});
    std::vector<Result> native;
    native.reserve(scenarios.size());
    unsigned exits[3] = {0, 0, 0};
    bool model = true;
    for (unsigned i = 0; i < scenarios.size(); ++i) {
        const Scenario& s = scenarios[i];
        native.push_back(run(s.c, s.scale, s.control_word, s.mxcsr, i));
        const Result& r = native.back();
        ++exits[r.code < 3 ? r.code : 0];
        Case scaled = s.c;
        for (unsigned k = 0; k < 3; ++k) scaled.b[k] = s.c.b[k] * s.scale;
        fx_set_cw(s.control_word);
        const std::uint32_t direct = call(replica_fn, scaled);
        fx_set_cw(0x027f);
        float seen[3];
        std::memcpy(seen, &r.locals[8], 12); // [esp+0x20..0x2c): the scaled extents the head stores
        const bool was = model;
        model = model && r.visits == 1 && r.last_error == 0x5150 + i && !((r.mxcsr ^ s.mxcsr) & mxcsr_mask) &&
                (r.code == 2 ? r.eax == 0x7e57 && r.site[7] == 0 : r.eax == 0) &&
                ((s.control_word & 0x0c00) || (s.mxcsr & 0x6000) ||
                 (r.code == 2 ? direct == 0 && !std::memcmp(seen, scaled.b, 12) : direct != 0)) // the head scales b
                                                                                                // under the x87
                                                                                                // rounding mode:
                                                                                                // modelled for
                                                                                                // round-to-nearest only
                                                                                                // (FEX derives the x87
                                                                                                // rounding mode from
                                                                                                // the last of fldcw /
                                                                                                // ldmxcsr)
                && r.ret[6] == 0xc1c1c1c1u && r.ret[5] == 0xddddddddu && r.ret[4] == 0xb0b0b0b0u &&
                r.ret[1] == 0x51515151u && r.ret[0] == 0xd1d1d1d1u && (r.env[2] & 0xffff) == 0xffff;
        if (was && !model)
            std::printf(
                "DETAIL replay %u code=%lu eax=%lx direct=%lu visits=%lu mxcsr=%lx/%lx error=%lx tag=%lx ecx=%lx edx=%lx ebx=%lx esi=%lx edi=%lx site_eax=%lx\n",
                i, (unsigned long)r.code, (unsigned long)r.eax, (unsigned long)direct, (unsigned long)r.visits,
                (unsigned long)r.mxcsr, (unsigned long)s.mxcsr, (unsigned long)r.last_error, (unsigned long)r.env[2],
                (unsigned long)r.ret[6], (unsigned long)r.ret[5], (unsigned long)r.ret[4], (unsigned long)r.ret[1],
                (unsigned long)r.ret[0], (unsigned long)r.site[7]);
    }
    std::printf("REPLAY pruned=%u descend=%u other=%u\n", exits[1], exits[2], exits[0]);
    check(exits[1] > 500 && exits[2] > 500 && exits[0] == 0,
          "native replay reaches both the pruned tail and the descend path");
    check(
        model,
        "native replay: the engine bytes scale b, count the visit, pass R/b/T/a as decoded, leave the x87 stack empty and ECX/EDX untouched");

    // ---- option off / refusals ----
    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", nullptr);
    check(!sat::initialize() && !std::strcmp(sat::state(), "disabled") && region_original(),
          "variable unset: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", L"0");
    check(!sat::initialize() && !std::strcmp(sat::state(), "disabled") && region_original(),
          "value 0: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", L"1");
    check(!sat::initialize() && region_original() && !std::strcmp(sat::state(), "callee_mismatch"),
          "value 1 without the engine image: body absent, nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", nullptr);
    check(!sat::install_at(sat::Addresses{0, addr(fx_sat_replica)}) && !std::strcmp(sat::state(), "invalid_site"),
          "null site refused");
    synthetic_descent[site_offset - 22] ^= 0x01; // push eax -> push ecx: another first argument
    check(!sat::install_at(addresses()) && !std::strcmp(sat::state(), "bytes_mismatch"),
          "changed argument set-up refused");
    synthetic_descent[site_offset - 22] ^= 0x01;
    synthetic_descent[site_offset + 7] ^= 0x04; // add esp,8 -> add esp,0xc
    check(!sat::install_at(addresses()) && !std::strcmp(sat::state(), "bytes_mismatch"),
          "changed return window refused");
    synthetic_descent[site_offset + 7] ^= 0x04;
    sat::Addresses bad = addresses();
    bad.target += 3;
    check(!sat::install_at(bad) && !std::strcmp(sat::state(), "target_mismatch") && region_original(),
          "a call to another callee refused by the call-site claim, bytes intact");
    bad = addresses();
    bad.site += 1;
    check(!sat::install_at(bad) && region_original(), "a site that is not the call refused, bytes intact");

    // ---- install next to the narrow census's site-7 claim on the same function (both options on) ----
    engine_patch::Site n7{};
    engine_patch::SiteSpec spec{};
    static std::uint32_t census_visits = 0;
    {
        spec.name = "collide_narrow_census_n7";
        spec.address = addr(synthetic_descent);
        spec.length = 5;
        spec.ret_pop = 0;
        spec.rel32_offset = 0;
        std::memcpy(spec.expected, original_region, 5);
        engine_patch::Emitter e(census::stub_capacity);
        unsigned char code[census::stub_capacity];
        const std::uint32_t at = addr(e.here());
        const unsigned length = census::encode_n7_stub(at, addr(&census_visits), addr(&fx_hits),
                                                       addr(synthetic_descent) + 5, code);
        e.bytes(code, length);
        check(e.ok() && e.finish() && engine_patch::claim(n7, spec) &&
                  engine_patch::push_front(n7, reinterpret_cast<void*>(std::uintptr_t(at))),
              "census site-7 claim on the same function installed first");
    }
    SetLastError(0x1234);
    check(sat::install_at(addresses()), "install with the census claim live");
    check(GetLastError() == 0x1234 && !std::strcmp(sat::state(), "ok"), "install preserves LastError, state ok");
    {
        std::uint32_t rel = 0;
        std::memcpy(&rel, synthetic_descent + site_offset + 1, 4);
        check(synthetic_descent[site_offset] == 0xe8 &&
                  addr(synthetic_descent) + site_offset + 5 + rel == addr(thunk_fn) && synthetic_descent[0] == 0xe9 &&
                  !std::memcmp(synthetic_descent + 5, original_region + 5, site_offset + 1 - 5) &&
                  !std::memcmp(synthetic_descent + site_offset + 5, original_region + site_offset + 5,
                               region_length - site_offset - 5),
              "only the call's rel32 (-> thunk) and the census's five entry bytes changed");
    }
    check(!sat::install_at(addresses()) && !std::strcmp(sat::state(), "already_installed"), "second install refused");

    // ---- (iv) patched replay: identical machine state wherever the verdicts agree ----
    char why[320];
    unsigned mismatches = 0, disagreements = 0, reversed = 0;
    const std::uint32_t census_before = census_visits;
    for (unsigned i = 0; i < scenarios.size(); ++i) {
        const Scenario& s = scenarios[i];
        const Result r = run(s.c, s.scale, s.control_word, s.mxcsr, i);
        if (r.code != native[i].code) {
            ++disagreements;
            if (r.code == 1) ++reversed;
            continue;
        }
        if (!same(native[i], r, why) && ++mismatches <= 8) std::printf("DETAIL scenario %u: %s\n", i, why);
    }
    check(
        mismatches == 0,
        "patched: identical exit, EAX, ECX/EDX and callee-saved registers, EFLAGS, x87 control/tag/TOP, MXCSR, engine locals, visit counter and LastError");
    check(
        reversed == 0 && disagreements <= 2,
        "patched: no pair pruned that the replica keeps on the replay set (a kept pair inside the margin is allowed)");
    check(census_visits - census_before == scenarios.size(),
          "census site-7 stub counted every visit with the SSE2 SAT on");

    // ---- (v) bench: replica against SSE2, both mixes (direct calls; harness cost measured with a null callee) ----
    while (early.size() < 4096) {
        const Case c = realistic(3, 8);
        if (call(replica_fn, c) && call(thunk_fn, c)) early.push_back(c);
    }
    while (overlap.size() < 4096) {
        const Case c = realistic(0, 0.2);
        if (!call(replica_fn, c)) overlap.push_back(c);
    }
    double axis_sum = 0;
    for (const Case& c : early) axis_sum += call(replica_fn, c);
    std::uint64_t s0, s1, s2, s3, s4, s5, s6;
    const double null_ns = bench_ns(reinterpret_cast<void*>(&fx_null_sat), early, 600, &s0);
    const double early_replica = bench_ns(replica_fn, early, 300, &s1),
                 early_sse2 = bench_ns(thunk_fn, early, 300, &s2),
                 early_bracketed = bench_ns(reinterpret_cast<void*>(&fx_thunk_bracketed), early, 300, &s3);
    const double full_replica = bench_ns(replica_fn, overlap, 150, &s4),
                 full_sse2 = bench_ns(thunk_fn, overlap, 150, &s5),
                 full_bracketed = bench_ns(reinterpret_cast<void*>(&fx_thunk_bracketed), overlap, 150, &s6);
    check(s1 == s2 && s2 == s3 && s4 == 0 && s5 == 0 && s6 == 0 && s0 == 0,
          "bench mixes: identical verdict sums, the overlap mix all zero");
    std::printf(
        "COLLIDE SAT SSE2 BENCH null_ns=%.2f early_axis_mean=%.2f early_replica_ns=%.2f early_sse2_ns=%.2f early_bracketed_ns=%.2f full_replica_ns=%.2f full_sse2_ns=%.2f full_bracketed_ns=%.2f\n",
        null_ns, axis_sum / double(early.size()), early_replica, early_sse2, early_bracketed, full_replica, full_sse2,
        full_bracketed);

    // ---- restore in either order, reinstall, closed window ----
    check(sat::shutdown() && !std::strcmp(sat::state(), "restored") && synthetic_descent[0] == 0xe9 &&
              !std::memcmp(synthetic_descent + 5, original_region + 5, region_length - 5),
          "shutdown restores the call exactly, the census claim stays live");
    {
        const Result r = run(scenarios[5].c, scenarios[5].scale, scenarios[5].control_word, scenarios[5].mxcsr, 5);
        check(same(native[5], r, why), "restored call replays natively under the census stub");
    }
    check(sat::install_at(addresses()) && engine_patch::restore(n7) && synthetic_descent[0] == original_region[0] &&
              synthetic_descent[site_offset + 1] != original_region[site_offset + 1],
          "reinstall; the census claim restored first leaves the SAT call patched");
    {
        const Result r = run(scenarios[7].c, scenarios[7].scale, scenarios[7].control_word, scenarios[7].mxcsr, 7);
        check(same(native[7], r, why), "SAT alone replays identically");
    }
    check(sat::shutdown() && region_original(), "second shutdown: the whole region byte-exact");
    mismatches = 0;
    for (unsigned i = 0; i < scenarios.size(); i += 5) {
        const Scenario& s = scenarios[i];
        const Result r = run(s.c, s.scale, s.control_word, s.mxcsr, i);
        if (!same(native[i], r, why)) ++mismatches;
    }
    check(mismatches == 0, "restored bytes replay natively");
    engine_patch::close_install_window("fixture");
    check(!sat::install_at(addresses()) && !std::strcmp(sat::state(), "late_claim") && region_original(),
          "closed install window refused");
    std::printf("COLLIDE SAT SSE2 CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
