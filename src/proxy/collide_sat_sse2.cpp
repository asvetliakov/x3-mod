#include "collide_sat_sse2.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::collide_sat_sse2::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
const char* state_ = "disabled";
engine_patch::CallSite site_{};

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[64]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool callee_matches() {
    static unsigned char body[sat_callee_length];
    return engine_patch::read_code(sat_target_va, body, sat_callee_length) && fnv1a(body, sat_callee_length) == sat_callee_fnv1a;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
}

extern "C" {
const std::uint32_t x3m_collide_sat_mxcsr = 0x1f80;   // round to nearest, all exceptions masked, no DAZ/FZ
// For the collision memo's conservative advancement (collide_memo.cpp): the smallest gap of any pruning test since the
// memo last reset it, and how many pruning tests there were (so the memo can tell that it saw every one of a query).
// A NaN gap sticks as -1. One comparison and at most two stores per pruning call; nothing reads them otherwise.
double x3m_collide_sat_min_gap = 0.0;
std::uint32_t x3m_collide_sat_prunes = 0;
int __cdecl x3m_collide_sat_sse2(const float* R, const float* b_extents, const float* T, const float* a_extents) {
    return obb_disjoint_gap(R, b_extents, T, a_extents, [](double g) {
        ++x3m_collide_sat_prunes;
        if (!(g >= x3m_collide_sat_min_gap)) x3m_collide_sat_min_gap = g == g ? g : -1.0;
    });
}
}
// In: ESI = R, EDI = b extents, [ESP+4] = T, [ESP+8] = a extents (the caller pops 8). Out: EAX.
// ECX/EDX are kept (the original never writes them); EBX/EBP/ESI/EDI are callee-saved in the
// C function; EFLAGS are dead at the return (`add esp,8; test eax,eax`).
// MXCSR: when its control bits are already the default (round to nearest, all masked, no
// DAZ/FZ: the state an x87-only game runs in) the body runs as is and MXCSR is never
// written; only the sticky exception flags can accumulate, which nothing in the process
// reads. Any other value is saved, replaced by the default for the body and restored
// bit-exact. The common path deliberately holds no `ldmxcsr`: FEX keeps one host rounding
// mode for x87 and SSE, so an `ldmxcsr` re-imposes MXCSR's rounding mode on the engine's
// later x87 arithmetic (measured in the fixture); a plain read cannot.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collide_sat_thunk
_x3m_collide_sat_thunk:
    push ecx
    push edx
    push eax
    stmxcsr dword ptr [esp]
    mov eax, dword ptr [esp]
    and eax, 0xffc0
    cmp eax, 0x1f80
    jne 1f
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push edi
    push esi
    call _x3m_collide_sat_sse2
    lea esp, [esp+20]
    pop edx
    pop ecx
    ret
1:  ldmxcsr dword ptr [_x3m_collide_sat_mxcsr]
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

namespace x3m::collide_sat_sse2 {
bool install_at(const Addresses& a) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.site || !a.target) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!bytes_match(a.site - sat_pre_length, sat_pre_window, sat_pre_length) || !bytes_match(a.site + call_length, sat_post_window, sat_post_length)) return done("bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", false);
    site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site_, a.site, a.target, reinterpret_cast<void*>(&x3m_collide_sat_thunk))) {
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
    const DWORD length = GetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else if (!callee_matches()) state_ = "callee_mismatch";   // the body being replaced, outside the windows install_at compares
    else applied = install_at(Addresses{sat_site_va, sat_target_va});
    log("collide_sat_sse2 requested=%u patched=%u reason=%s site=0x%08lx target=0x%08lx write=%s handler=0x%08lx",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(sat_site_va), static_cast<unsigned long>(sat_target_va),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_collide_sat_thunk)));
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
bool installed() { return patched_; }
}
