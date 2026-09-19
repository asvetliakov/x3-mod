#include "collide_memo.h"
#include "collide_sat_sse2.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <xmmintrin.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
static_assert(sizeof(x3m_collide_memo_args) == 40, "the ten words at the site");
namespace {
using namespace x3m::collide_memo::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false, verify_ = false, advance_ = false;
const char* state_ = "disabled";
engine_patch::CallSite site_{};
Table table_;
Counters counters_{}, logged_{};
volatile std::uint32_t frame_ = 0;            // written by present() only
volatile LONG owner_thread_ = 0;              // the first thread through the thunk; every other thread goes straight to the engine
volatile LONG clear_requested_ = 0;           // device Reset / stuck query: consumed by the owner at its next query
volatile LONG foreign_thread_ = 0;            // counted by the foreign threads themselves
// Owner thread only from here on.
volatile bool busy_ = false;                           // between lookup() returning 0 and store()
std::uint32_t seen_frame_ = 0, queries_since_tick_ = 0;
// Between lookup and store of the one query in flight (the thunk's busy flag keeps it to one).
Key pending_key_;
bool pending_eligible_ = false;
Entry* pending_verify_ = nullptr;
int pending_miss_ = -1;
template <class T> T& engine(std::uintptr_t va);
// Conservative advancement: the SAT module's pruning count when the engine run started, the entry an advance answer is being
// verified against (verify mode), and log2 histograms of the gaps and displacements tested in this window (bucket k: [2^(k-16), 2^(k-15))).
std::uint32_t pending_prunes_ = 0;
Entry* pending_advance_ = nullptr;
double pending_gap_ = 0.0, pending_displacement_ = 0.0;
std::uint32_t gap_histogram_[48]{}, displacement_histogram_[48]{};
// Exponent and power of two through the bit pattern: frexp/ldexp are library calls that may use x87.
void tally(std::uint32_t* histogram, double v) { std::uint64_t b; std::memcpy(&b, &v, 8); const int k = v > 0.0 ? int((b >> 52) & 0x7ff) - 1022 + 16 : 0; ++histogram[k < 0 ? 0 : k > 47 ? 47 : k]; }
// The median's bucket as a power of two (value in [2^e, 2^(e+1)) model units), -99 when nothing was tested. An integer: a double
// passed through varargs would be copied with x87 instructions.
int median(std::uint32_t* histogram) {
    std::uint32_t total = 0, seen = 0;
    for (unsigned k = 0; k < 48; ++k) total += histogram[k];
    int value = -99;
    for (unsigned k = 0; k < 48 && total != 0; ++k) { seen += histogram[k]; if (2 * seen >= total) { value = int(k) - 17; break; } }
    std::memset(histogram, 0, 48 * sizeof *histogram);
    return value;
}
long milli(double v) { return v < 2.0e6 ? static_cast<long>(v * 1000.0) : 2000000000L; }   // thousandths of a model unit, clamped; cvttsd2si
// Exactly what a no-contact query writes (collide_memo_core.h); the contact record and *minimum stay as they are. On an advance
// answer the node-pair count and the root block are the stored pose's, not this pose's: nothing outside the collider reads them
// (verify_collide_memo_site.py, `replayed_globals_private`) and the collider rewrites all of them before it reads any.
void replay(const Entry& entry, std::uint32_t flags, std::uint32_t cap, const x3m_collide_memo_args& args) {
    engine<std::uint32_t>(flags_va) = flags;
    engine<std::uint32_t>(cap_va) = cap;
    engine<std::uint32_t>(tolerance_va) = entry.outputs.tolerance_integer;
    engine<std::uint32_t>(minimum_va) = reinterpret_cast<std::uintptr_t>(args.minimum);
    engine<std::uint32_t>(visits_va) = entry.outputs.visits;
    engine<std::uint32_t>(triangles_va) = entry.outputs.triangles;
    engine<std::uint32_t>(contacts_va) = 0;
    std::memcpy(&engine<std::uint32_t>(root_block_va), entry.outputs.root_block, 4 * root_block_words);
}
// The moved query against its reference (SSE2 double arithmetic; only with MXCSR at its default, which is read, never written).
[[gnu::noinline]] Entry* advance_answer(const Key& key, std::uint32_t frame) {
    if (!advance_ || !x3m::collide_sat_sse2::installed() || (_mm_getcsr() & 0xffc0u) != 0x1f80u) return nullptr;
    Entry* const e = table_.advance_candidate(key, frame);
    if (e == nullptr) return nullptr;
    double margin = 0.0;
    const bool holds = advance_holds(e->key, e->gap, key, pending_displacement_, margin);
    pending_gap_ = e->gap;
    tally(gap_histogram_, e->gap); tally(displacement_histogram_, pending_displacement_);
    if (!holds) { ++counters_.advance_refused; return nullptr; }
    return e;
}
   // the class of the miss in flight, for the visits it goes on to cost

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
        && body_matches(leaf_va, leaf_length, leaf_fnv1a, true, false) && body_matches(triangle_va, triangle_length, triangle_fnv1a, false, false)
        && body_matches(sat_va, sat_length, sat_fnv1a, false, false) && body_matches(matrix_helpers_va, matrix_helpers_length, matrix_helpers_fnv1a, false, false)
        && body_matches(vector_helpers_va, vector_helpers_length, vector_helpers_fnv1a, false, false) && body_matches(ftol_va, ftol_length, ftol_fnv1a, false, false);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
// False when a model is not in the built state: such a query returns early inside the engine and is left to it.
bool build_key(Key& key, std::uint32_t flags, std::uint32_t cap, const x3m_collide_memo_args& a) {
    if (a.model_a == nullptr || a.model_b == nullptr) return false;   // the caller tests both before the site; never dereferenced on trust
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
std::uint32_t x3m_collide_memo_target = 0, x3m_collide_memo_return = 0;

int __cdecl x3m_collide_memo_lookup(std::uint32_t flags, std::uint32_t cap, const x3m_collide_memo_args* args) {
    // Single-thread gate, before anything of the memo's state is read: the table, the pending key and the saved return
    // address belong to the first thread that came through. Any other thread, and a re-entered call (busy: also what
    // an unwind past the thunk leaves behind), is the engine's alone.
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());   // no LastError, no x87
    LONG owner = owner_thread_;
    if (owner == 0) owner = InterlockedCompareExchange(&owner_thread_, thread, 0) == 0 ? thread : owner_thread_;
    if (owner != thread) { InterlockedIncrement(&foreign_thread_); return 2; }
    if (busy_) { ++counters_.reentered; return 2; }
    busy_ = true;
    const std::uint32_t frame = frame_;
    if (frame != seen_frame_) { seen_frame_ = frame; queries_since_tick_ = 0; }
    if (InterlockedExchange(&clear_requested_, 0) != 0 || ++queries_since_tick_ > queries_without_tick_limit) { table_.clear(); queries_since_tick_ = 0; ++counters_.clears; }
    pending_verify_ = nullptr;
    pending_miss_ = -1;
    pending_advance_ = nullptr;
    pending_eligible_ = build_key(pending_key_, flags, cap, *args);
    if (!pending_eligible_) { ++counters_.ineligible; return 0; }
    Entry* const entry = table_.find(pending_key_, frame);
    if (entry == nullptr) {
        if (Entry* const reference = advance_answer(pending_key_, frame)) {
            if (verify_) { pending_advance_ = reference; pending_prunes_ = x3m_collide_sat_prunes; return 0; }   // the engine runs too; store() checks it found no contact
            reference->frame = frame;
            replay(*reference, flags, cap, *args);
            ++counters_.advance_hits; counters_.advance_skipped_visits += reference->outputs.visits;
            busy_ = false;
            return 1;
        }
        ++counters_.misses; pending_miss_ = table_.classify(pending_key_); ++counters_.miss_count[pending_miss_];
        x3m_collide_sat_min_gap = 1e300; pending_prunes_ = x3m_collide_sat_prunes;   // the engine's run reports its smallest pruning gap
        return 0;
    }
    if (verify_) { pending_verify_ = entry; return 0; }   // the engine runs too; store() compares
    entry->frame = frame;
    replay(*entry, flags, cap, *args);
    ++counters_.hits;
    if (entry->key.words[minimum_value_word] != pending_key_.words[minimum_value_word]) ++counters_.min_relaxed_hits;   // answered although the running minimum moved: no leaf was reached
    counters_.skipped_visits += entry->outputs.visits;
    counters_.skipped_triangles += entry->outputs.triangles;
    busy_ = false;
    return 1;
}
void __cdecl x3m_collide_memo_store() {   // owner thread only: reached on lookup() == 0 alone
    busy_ = false;
    if (!pending_eligible_) return;
    pending_eligible_ = false;
    Outputs now;
    now.visits = engine<std::uint32_t>(visits_va); now.triangles = engine<std::uint32_t>(triangles_va); now.tolerance_integer = engine<std::uint32_t>(tolerance_va);
    std::memcpy(now.root_block, &engine<std::uint32_t>(root_block_va), 4 * root_block_words);
    const bool contact = engine<std::uint32_t>(contacts_va) != 0;
    if (pending_miss_ >= 0) counters_.miss_visits[pending_miss_] += now.visits;
    if (Entry* const reference = pending_advance_) {   // verify mode: an advance answer must never meet a contact
        pending_advance_ = nullptr;
        if (!contact) { ++counters_.advance_verified; reference->frame = frame_; }
        else {
            ++counters_.advance_mismatches; reference->valid = false;
            const DWORD error = GetLastError();
            x3m::log("collide_memo_unsound model_a=0x%08lx model_b=0x%08lx gap_milli=%ld displacement_milli=%ld contacts=%lu", static_cast<unsigned long>(pending_key_.words[model_words_begin]),
                     static_cast<unsigned long>(pending_key_.words[model_words_begin + 1]), milli(pending_gap_), milli(pending_displacement_), static_cast<unsigned long>(engine<std::uint32_t>(contacts_va)));
            SetLastError(error);
        }
        return;
    }
    if (Entry* const expected = pending_verify_) {
        pending_verify_ = nullptr;
        if (!contact && !std::memcmp(&now, &expected->outputs, sizeof now)) { ++counters_.verified; expected->frame = frame_; }
        else { ++counters_.verify_mismatches; expected->valid = false; }
        return;
    }
    if (contact) { ++counters_.contacts; return; }   // never stored: a contact is always computed
    // A gap goes on record only when no leaf was reached and every pruning test of this run was seen: a contact-free run that
    // reaches no leaf makes 1 + 2 * descents node pairs, so exactly (pairs + 1) / 2 of them were pruned.
    double gap = 0.0;
    if (advance_ && now.triangles == 0 && x3m::collide_sat_sse2::installed() && x3m_collide_sat_prunes - pending_prunes_ == (now.visits + 1) / 2 && x3m_collide_sat_min_gap > 0.0
        && x3m_collide_sat_min_gap < 1e299) { gap = x3m_collide_sat_min_gap; ++counters_.advance_rearm; }
    ++counters_.stored;
    if (table_.store(pending_key_, now, frame_, gap)) ++counters_.evictions;
}
}
// In: ECX = flags, EAX = cap, [ESP] = the engine's return address, [ESP+4..] = x3m_collide_memo_args.
// lookup() = 1: answered, EAX = 0 and a plain return. 2: not this thread's or re-entered, straight to the engine with
// nothing of the memo touched. 0: the engine's return address is taken off and 0x004e29f0 is called with the engine's
// exact stack (it reads its tenth argument above the nine), then store() runs with EAX/ECX/EDX kept and control
// returns to the engine's address. The caller pops the arguments in every case.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collide_memo_thunk
_x3m_collide_memo_thunk:
    push ecx
    push eax
    lea edx, [esp+12]
    push edx
    push eax
    push ecx
    call _x3m_collide_memo_lookup
    add esp, 12
    cmp eax, 1
    je 1f
    cmp eax, 2
    pop eax
    pop ecx
    je 2f
    pop dword ptr [_x3m_collide_memo_return]
    call dword ptr [_x3m_collide_memo_target]
    push eax
    push ecx
    push edx
    call _x3m_collide_memo_store
    pop edx
    pop ecx
    pop eax
    jmp dword ptr [_x3m_collide_memo_return]
1:  add esp, 8
    xor eax, eax
    ret
2:  jmp dword ptr [_x3m_collide_memo_target]
    .att_syntax
)");

namespace x3m::collide_memo {
bool install_at(const Addresses& a, bool verify_mode, bool advance_mode) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.site || !a.target) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!bytes_match(a.site - memo_pre_length, memo_pre_window, memo_pre_length) || !bytes_match(a.site + call_length, memo_post_window, memo_post_length)) return done("bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", false);
    table_.clear();
    verify_ = verify_mode;
    advance_ = advance_mode;
    x3m_collide_memo_target = static_cast<std::uint32_t>(a.target);
    busy_ = false; owner_thread_ = 0; clear_requested_ = 0; seen_frame_ = frame_; queries_since_tick_ = 0;
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
    // Conservative advancement needs the SSE2 SAT (it measures the gaps); without it the option is simply not armed.
    const bool advance_mode = GetEnvironmentVariableW(L"X3M_COLLIDE_MEMO_ADVANCE", verify, 4) == 1 && verify[0] == L'1' && x3m::collide_sat_sse2::installed();
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else if (!bodies_match()) state_ = "body_mismatch";
    else applied = install_at(Addresses{memo_site_va, memo_target_va}, verify_mode, advance_mode);
    log("collide_memo requested=%u patched=%u verify=%u advance=%u reason=%s site=0x%08lx target=0x%08lx write=%s handler=0x%08lx entries=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, patched_ && verify_ ? 1u : 0u, patched_ && advance_ ? 1u : 0u, state_, static_cast<unsigned long>(memo_site_va), static_cast<unsigned long>(memo_target_va),
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
core::Counters counters() { core::Counters c = counters_; c.foreign_thread = static_cast<std::uint32_t>(foreign_thread_); return c; }
void device_reset() { if (patched_) InterlockedExchange(&clear_requested_, 1); }
void present(unsigned long long device, unsigned long long frame, bool) {
    if (!patched_) return;
    const std::uint32_t now = frame_ + 1;
    frame_ = now;
    // A query cannot be in flight on the thread that is presenting: busy here is what an unwind past the thunk left.
    if (busy_ && static_cast<LONG>(GetCurrentThreadId()) == owner_thread_) { busy_ = false; ++counters_.stuck_busy; InterlockedExchange(&clear_requested_, 1); }
    if (now % 300u != 0) return;
    const DWORD error = GetLastError();
    const Counters c = counters();   // written by the owner thread; a torn read costs one window's accuracy, never a decision
    const auto d = [](std::uint32_t a, std::uint32_t b) { return static_cast<unsigned long>(a - b); };
    const std::uint32_t queries = (c.hits - logged_.hits) + (c.misses - logged_.misses) + (c.ineligible - logged_.ineligible) + (c.verified - logged_.verified) + (c.verify_mismatches - logged_.verify_mismatches);
    log("collide_memo device=%llu frame=%llu frames=300 verify=%u queries=%lu hits=%lu misses=%lu stored=%lu contacts=%lu ineligible=%lu evictions=%lu skipped_visits=%lu "
        "skipped_triangles=%lu verified=%lu verify_mismatches=%lu foreign_thread=%lu reentered=%lu clears=%lu stuck_busy=%lu min_relaxed_hits=%lu "
        "miss_none_found=%lu miss_none_found_visits=%lu miss_xform_a=%lu miss_xform_a_visits=%lu miss_xform_b=%lu miss_xform_b_visits=%lu miss_scale=%lu miss_scale_visits=%lu miss_mode=%lu miss_mode_visits=%lu miss_models=%lu miss_models_visits=%lu miss_min_value=%lu miss_min_value_visits=%lu miss_expired=%lu miss_expired_visits=%lu advance_hits=%lu advance_skipped_visits=%lu advance_refused_gap=%lu advance_rearm=%lu advance_verified=%lu advance_mismatches=%lu advance_gap_median_log2=%d advance_displacement_median_log2=%d",
        device, frame, verify_ ? 1u : 0u, static_cast<unsigned long>(queries), d(c.hits, logged_.hits), d(c.misses, logged_.misses), d(c.stored, logged_.stored), d(c.contacts, logged_.contacts),
        d(c.ineligible, logged_.ineligible), d(c.evictions, logged_.evictions), d(c.skipped_visits, logged_.skipped_visits), d(c.skipped_triangles, logged_.skipped_triangles),
        d(c.verified, logged_.verified), d(c.verify_mismatches, logged_.verify_mismatches), d(c.foreign_thread, logged_.foreign_thread), d(c.reentered, logged_.reentered),
        d(c.clears, logged_.clears), d(c.stuck_busy, logged_.stuck_busy), d(c.min_relaxed_hits, logged_.min_relaxed_hits),
        d(c.miss_count[0], logged_.miss_count[0]), d(c.miss_visits[0], logged_.miss_visits[0]), d(c.miss_count[1], logged_.miss_count[1]), d(c.miss_visits[1], logged_.miss_visits[1]), d(c.miss_count[2], logged_.miss_count[2]), d(c.miss_visits[2], logged_.miss_visits[2]), d(c.miss_count[3], logged_.miss_count[3]), d(c.miss_visits[3], logged_.miss_visits[3]), d(c.miss_count[4], logged_.miss_count[4]), d(c.miss_visits[4], logged_.miss_visits[4]), d(c.miss_count[5], logged_.miss_count[5]), d(c.miss_visits[5], logged_.miss_visits[5]), d(c.miss_count[6], logged_.miss_count[6]), d(c.miss_visits[6], logged_.miss_visits[6]), d(c.miss_count[7], logged_.miss_count[7]), d(c.miss_visits[7], logged_.miss_visits[7]),
        d(c.advance_hits, logged_.advance_hits), d(c.advance_skipped_visits, logged_.advance_skipped_visits), d(c.advance_refused, logged_.advance_refused),
        d(c.advance_rearm, logged_.advance_rearm), d(c.advance_verified, logged_.advance_verified), d(c.advance_mismatches, logged_.advance_mismatches), median(gap_histogram_),
        median(displacement_histogram_));
    SetLastError(error);
    logged_ = c;
}
}
