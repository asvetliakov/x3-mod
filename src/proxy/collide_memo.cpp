#include "collide_memo.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
static_assert(sizeof(x3m_collide_memo_args) == 40, "the ten words at the site");
namespace {
using namespace x3m::collide_memo::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false, verify_ = false;
const char* state_ = "disabled";
engine_patch::CallSite site_{};
Table table_;
Counters counters_{}, logged_{};
volatile std::uint32_t frame_ = 0;
// Between lookup and store of the one query in flight (the thunk's busy flag keeps it to one).
Key pending_key_;
bool pending_eligible_ = false;
Entry* pending_verify_ = nullptr;

template <class T> T& engine(std::uintptr_t va) { return *reinterpret_cast<T*>(va); }
inline std::uint64_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[64]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool body_matches(std::uintptr_t va, unsigned length, std::uint64_t expected, bool entry_claimable, bool sat_call) {
    static unsigned char body[triangle_length];
    if (length > sizeof body || !engine_patch::read_code(va, body, length)) return false;
    if (entry_claimable) std::memset(body, 0, entry_hole);
    if (sat_call) std::memset(body + sat_rel32_offset, 0, sat_rel32_length);
    return fnv1a(body, length) == expected;
}
// Everything the enumeration of a no-contact query's writes rests on (section 14.2), outside the windows install_at compares.
bool bodies_match() {
    return body_matches(caller_va, caller_length, caller_fnv1a, false, false) && body_matches(memo_target_va, target_length, target_fnv1a, false, false)
        && body_matches(query_va, query_length, query_fnv1a, false, false) && body_matches(descent_va, descent_length, descent_fnv1a, true, true)
        && body_matches(leaf_va, leaf_length, leaf_fnv1a, true, false) && body_matches(triangle_va, triangle_length, triangle_fnv1a, false, false);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
// False when a model is not in the built state: such a query returns early inside the engine and is left to it.
bool build_key(Key& key, std::uint32_t flags, std::uint32_t cap, const x3m_collide_memo_args& a) {
    if (a.model_a[model_state_word] != model_built || a.model_b[model_state_word] != model_built || a.model_a[0] == 0 || a.model_b[0] == 0) return false;
    std::uint32_t* w = key.words;
    std::memcpy(w, a.R1, 36); std::memcpy(w + 9, a.T1, 12); w[12] = a.s1;
    std::memcpy(w + 13, a.R2, 36); std::memcpy(w + 22, a.T2, 12); w[25] = a.s2;
    w[26] = a.tolerance; w[27] = flags; w[28] = cap;
    w[29] = a.minimum != nullptr; w[30] = a.minimum != nullptr ? *a.minimum : 0u;
    w[31] = reinterpret_cast<std::uintptr_t>(a.model_a); w[32] = reinterpret_cast<std::uintptr_t>(a.model_b);
    std::memcpy(w + 33, a.model_a, 4 * header_words); std::memcpy(w + 33 + header_words, a.model_b, 4 * header_words);
    std::memcpy(w + 33 + 2 * header_words, reinterpret_cast<const void*>(a.model_a[0]), 4 * box_words);
    std::memcpy(w + 33 + 2 * header_words + box_words, reinterpret_cast<const void*>(a.model_b[0]), 4 * box_words);
    return true;
}
}

extern "C" {
volatile unsigned char x3m_collide_memo_busy = 0;
std::uint32_t x3m_collide_memo_target = 0, x3m_collide_memo_return = 0;

int __cdecl x3m_collide_memo_lookup(std::uint32_t flags, std::uint32_t cap, const x3m_collide_memo_args* args) {
    pending_verify_ = nullptr;
    pending_eligible_ = build_key(pending_key_, flags, cap, *args);
    if (!pending_eligible_) { ++counters_.ineligible; return 0; }
    Entry* const entry = table_.find(pending_key_, frame_);
    if (entry == nullptr) { ++counters_.misses; return 0; }
    if (verify_) { pending_verify_ = entry; return 0; }   // the engine runs too; store() compares
    entry->frame = frame_;
    // Exactly what the query would have written (collide_memo_core.h); the contact record and *minimum stay as they are.
    engine<std::uint32_t>(flags_va) = flags;
    engine<std::uint32_t>(cap_va) = cap;
    engine<std::uint32_t>(tolerance_va) = entry->outputs.tolerance_integer;
    engine<std::uint32_t>(minimum_va) = reinterpret_cast<std::uintptr_t>(args->minimum);
    engine<std::uint32_t>(visits_va) = entry->outputs.visits;
    engine<std::uint32_t>(triangles_va) = entry->outputs.triangles;
    engine<std::uint32_t>(contacts_va) = 0;
    std::memcpy(&engine<std::uint32_t>(root_block_va), entry->outputs.root_block, 4 * root_block_words);
    ++counters_.hits;
    counters_.skipped_visits += entry->outputs.visits;
    counters_.skipped_triangles += entry->outputs.triangles;
    return 1;
}
void __cdecl x3m_collide_memo_store() {
    if (!pending_eligible_) return;
    pending_eligible_ = false;
    Outputs now;
    now.visits = engine<std::uint32_t>(visits_va); now.triangles = engine<std::uint32_t>(triangles_va); now.tolerance_integer = engine<std::uint32_t>(tolerance_va);
    std::memcpy(now.root_block, &engine<std::uint32_t>(root_block_va), 4 * root_block_words);
    const bool contact = engine<std::uint32_t>(contacts_va) != 0;
    if (Entry* const expected = pending_verify_) {
        pending_verify_ = nullptr;
        if (!contact && !std::memcmp(&now, &expected->outputs, sizeof now)) { ++counters_.verified; expected->frame = frame_; }
        else { ++counters_.verify_mismatches; expected->valid = false; }
        return;
    }
    if (contact) { ++counters_.contacts; return; }   // never stored: a contact is always computed
    ++counters_.stored;
    if (table_.store(pending_key_, now, frame_)) ++counters_.evictions;
}
}
// In: ECX = flags, EAX = cap, [ESP] = the engine's return address, [ESP+4..] = x3m_collide_memo_args.
// Run path: the engine's return address is taken off and 0x004e29f0 is called with the engine's exact stack (it reads
// its tenth argument above the nine pushed ones), then store() runs with EAX/ECX/EDX kept and control returns to the
// engine's address. A re-entered thunk (busy) goes straight to the engine; an unwind past it leaves busy set, which
// fails safe to that. Hit path: EAX = 0 and a plain return; the caller pops the arguments in both cases.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collide_memo_thunk
_x3m_collide_memo_thunk:
    cmp byte ptr [_x3m_collide_memo_busy], 0
    jne 2f
    push ecx
    push eax
    lea edx, [esp+12]
    push edx
    push eax
    push ecx
    call _x3m_collide_memo_lookup
    add esp, 12
    test eax, eax
    jne 1f
    pop eax
    pop ecx
    mov byte ptr [_x3m_collide_memo_busy], 1
    pop dword ptr [_x3m_collide_memo_return]
    call dword ptr [_x3m_collide_memo_target]
    push eax
    push ecx
    push edx
    call _x3m_collide_memo_store
    pop edx
    pop ecx
    pop eax
    mov byte ptr [_x3m_collide_memo_busy], 0
    jmp dword ptr [_x3m_collide_memo_return]
1:  add esp, 8
    xor eax, eax
    ret
2:  jmp dword ptr [_x3m_collide_memo_target]
    .att_syntax
)");

namespace x3m::collide_memo {
bool install_at(const Addresses& a, bool verify_mode) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.site || !a.target) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!bytes_match(a.site - memo_pre_length, memo_pre_window, memo_pre_length) || !bytes_match(a.site + call_length, memo_post_window, memo_post_length)) return done("bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", false);
    table_.clear();
    verify_ = verify_mode;
    x3m_collide_memo_target = static_cast<std::uint32_t>(a.target);
    x3m_collide_memo_busy = 0;
    site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site_, a.site, a.target, reinterpret_cast<void*>(&x3m_collide_memo_thunk))) {
        if (site_.patched_in && !engine_patch::restore_call(site_)) { patched_ = true; return done("rollback_failed", false); }   // registered: shutdown() tries again
        return done(site_.status, false);
    }
    patched_ = true;
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{}, verify[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    const bool verify_mode = GetEnvironmentVariableW(L"X3M_COLLIDE_MEMO_VERIFY", verify, 4) == 1 && verify[0] == L'1';
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else if (!bodies_match()) state_ = "body_mismatch";
    else applied = install_at(Addresses{memo_site_va, memo_target_va}, verify_mode);
    log("collide_memo requested=%u patched=%u verify=%u reason=%s site=0x%08lx target=0x%08lx write=%s handler=0x%08lx entries=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, patched_ && verify_ ? 1u : 0u, state_, static_cast<unsigned long>(memo_site_va), static_cast<unsigned long>(memo_target_va),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_collide_memo_thunk)), ways * sets);
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
core::Counters counters() { return counters_; }
void present(unsigned long long device, unsigned long long frame, bool) {
    if (!patched_) return;
    const std::uint32_t now = frame_ + 1;
    frame_ = now;
    if (now % 300u != 0) return;
    const Counters c = counters_;   // written by the engine's thread only; a torn read costs one window's accuracy, never a decision
    const auto d = [](std::uint32_t a, std::uint32_t b) { return static_cast<unsigned long>(a - b); };
    const std::uint32_t queries = (c.hits - logged_.hits) + (c.misses - logged_.misses) + (c.ineligible - logged_.ineligible) + (c.verified - logged_.verified) + (c.verify_mismatches - logged_.verify_mismatches);
    log("collide_memo device=%llu frame=%llu frames=300 verify=%u queries=%lu hits=%lu misses=%lu stored=%lu contacts=%lu ineligible=%lu evictions=%lu skipped_visits=%lu "
        "skipped_triangles=%lu verified=%lu verify_mismatches=%lu",
        device, frame, verify_ ? 1u : 0u, static_cast<unsigned long>(queries), d(c.hits, logged_.hits), d(c.misses, logged_.misses), d(c.stored, logged_.stored), d(c.contacts, logged_.contacts),
        d(c.ineligible, logged_.ineligible), d(c.evictions, logged_.evictions), d(c.skipped_visits, logged_.skipped_visits), d(c.skipped_triangles, logged_.skipped_triangles),
        d(c.verified, logged_.verified), d(c.verify_mismatches, logged_.verify_mismatches));
    logged_ = c;
}
}
