#include "collide_descent_sse2.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <xmmintrin.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
// The census's monotonic counters (collide_narrow_census.h); [1] is what its site-7 stub on 0x004e2530 counts.
extern "C" volatile std::uint32_t x3m_collide_narrow_counters[5];
namespace {
using namespace x3m::collide_descent_sse2::core;
namespace engine_patch = x3m::engine_patch;
using x3m::collide_sat_sse2::core::fnv1a;
bool patched_ = false;
const char* state_ = "disabled";
engine_patch::CallSite site_{};
constexpr unsigned default_mxcsr = 0x1f80;   // round to nearest, all exceptions masked, no DAZ/FZ

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[64]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
// The bodies this module stands in for (descent, helpers) or calls (leaf), outside the windows install_at compares.
bool bodies_match() {
    static unsigned char body[leaf_body_length];
    static_assert(descent_body_length <= sizeof body, "buffer");
    if (!engine_patch::read_code(descent_target_va, body, descent_body_length)) return false;
    std::memset(body, 0, entry_hole);
    std::memset(body + sat_rel32_offset, 0, sat_rel32_length);
    if (fnv1a(body, descent_body_length) != descent_body_fnv1a) return false;
    if (!engine_patch::read_code(leaf_va, body, leaf_body_length)) return false;
    std::memset(body, 0, entry_hole);
    if (fnv1a(body, leaf_body_length) != leaf_body_fnv1a) return false;
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const Range& r : helper_ranges) {
        if (!engine_patch::read_code(r.va, body, r.length)) return false;
        h = fnv1a(body, r.length, h);
    }
    return h == helpers_fnv1a;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
template <class T> T& engine(std::uintptr_t va) { return *reinterpret_cast<T*>(va); }
// The engine's per-query globals at their fixed addresses (non-relocatable image, verified executable).
template <bool Bracket> struct EngineEnv {
    unsigned saved_mxcsr;
    std::int32_t contacts() const { return engine<std::int32_t>(contacts_va); }
    std::int32_t first_contact() const { return engine<std::int32_t>(first_contact_va); }
    unsigned flags() const { return engine<unsigned char>(flags_va); }
    std::int32_t cap() const { return engine<std::int32_t>(cap_va); }
    void add_visits(std::uint32_t n) const { engine<std::uint32_t>(visits_va) += n; }
    void add_entries(std::uint32_t n) const { x3m_collide_narrow_counters[1] = x3m_collide_narrow_counters[1] + n; }
    void visit(const Pair&, const float*) const {}
    int leaf(const Node* a, const Node* b) const {
        if (!Bracket) return x3m_collide_descent_leaf(a, b);
        _mm_setcsr(saved_mxcsr);   // the engine's x87 leaf under the caller's own rounding state
        const int result = x3m_collide_descent_leaf(a, b);
        _mm_setcsr(default_mxcsr);
        return result;
    }
};
}

extern "C" int __cdecl x3m_collide_descent_sse2(const Node* a, const Node* b, const float* R, const float* T, float s) {
    // MXCSR: with default control bits (the state an x87-only game runs in) it is only read; the common path holds
    // no `ldmxcsr` because FEX keeps one host rounding mode for x87 and SSE (sector-collide.md 12.8).
    const unsigned mxcsr = _mm_getcsr();
    if ((mxcsr & 0xffc0u) == default_mxcsr) {
        EngineEnv<false> env{mxcsr};
        return descend<double>(a, b, R, T, s, env);
    }
    _mm_setcsr(default_mxcsr);
    EngineEnv<true> env{mxcsr};
    const int result = descend<double>(a, b, R, T, s, env);
    _mm_setcsr(mxcsr);
    return result;
}
// Thunk: cdecl (a, b, R, T, s) as pushed by 0x004e293e..0x004e2946, the caller pops 0x14. ECX/EDX are kept: the
// original leaves them untouched when the root pair is pruned. EFLAGS are dead at the return (`add esp,0x14`).
// Leaf: the engine's register convention, with every callee-saved register kept on this side as well.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collide_descent_thunk
_x3m_collide_descent_thunk:
    push ecx
    push edx
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    call _x3m_collide_descent_sse2
    add esp, 20
    pop edx
    pop ecx
    ret
    .p2align 4
    .globl _x3m_collide_descent_leaf
_x3m_collide_descent_leaf:
    push ebx
    push ebp
    push esi
    push edi
    mov esi, dword ptr [esp+20]
    mov eax, dword ptr [esp+24]
    mov edx, 0x004e2190
    call edx
    pop edi
    pop esi
    pop ebp
    pop ebx
    ret
    .att_syntax
)");

namespace x3m::collide_descent_sse2 {
bool install_at(const Addresses& a) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.site || !a.target) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!bytes_match(a.site - descent_pre_length, descent_pre_window, descent_pre_length) || !bytes_match(a.site + call_length, descent_post_window, descent_post_length)) return done("bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", false);
    site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site_, a.site, a.target, reinterpret_cast<void*>(&x3m_collide_descent_thunk))) {
        if (site_.patched_in && !engine_patch::restore_call(site_)) { patched_ = true; return done("rollback_failed", false); }   // registered: shutdown() tries again
        return done(site_.status, false);
    }
    patched_ = true;
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_COLLIDE_DESCENT_SSE2", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else if (!bodies_match()) state_ = "body_mismatch";
    else applied = install_at(Addresses{descent_site_va, descent_target_va});
    log("collide_descent_sse2 requested=%u patched=%u reason=%s site=0x%08lx target=0x%08lx write=%s handler=0x%08lx",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(descent_site_va), static_cast<unsigned long>(descent_target_va),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_collide_descent_thunk)));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    const bool back = engine_patch::restore_call(site_);
    patched_ = false;
    state_ = back ? "restored" : "restore_failed";
    SetLastError(error);
    return back;
}
const char* state() { return state_; }
}
