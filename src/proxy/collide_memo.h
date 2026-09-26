#pragma once
#include "collide_memo_core.h"
#include <cstdint>

// Temporal no-contact memo of the engine's mesh-pair collision query (X3M_COLLIDE_MEMO=1; unset = vanilla, nothing
// patched), fix 1 of docs/reverse-engineering/sector-collide.md 11.6, designed and measured in section 14. The rel32
// of the sole `call 0x004e29f0` at 0x0047f329 (inside 0x0047f1b0, after it has turned both nodes' fixed-point
// transforms into the floats the collider reads) is redirected to x3m_collide_memo_thunk. A query whose complete
// input (collide_memo_core.h) equals, bit for bit, one that found no contact in this or the previous frame is not
// run: the globals such a query writes are written from the stored values and the call returns. Any other query runs
// in the engine unchanged, on the engine's own stack; a contact is never stored, so every contact is computed.
// X3M_COLLIDE_MEMO_VERIFY=1 (with the first) skips nothing: a would-be hit runs the engine too and the two are
// compared (`verify_mismatches` in the log line must stay 0).
//
// Independent of X3M_COLLIDE_SAT_SSE2, X3M_COLLIDE_BOX_CULL and X3M_COLLIDE_NARROW_CENSUS: the site is inside no
// window of theirs. The census keeps counting accepted pairs and mesh pairs (its sites are above this one); its
// node-pair and triangle counters see only the queries that ran, and `skipped_visits` here is the difference.
//
// Threads and clock. The memo's state (table, pending key, saved return address) belongs to ONE thread: the first
// that comes through the thunk, in the game its main loop, which is also the Present thread. lookup() tests that before
// it reads anything; a query from any other thread, and a re-entered one, goes straight to the engine
// (`foreign_thread`, `reentered`). Expiry counts Present calls; the table is also dropped after a device Reset, after
// 100,000 queries without a Present, and when a Present on the owner thread finds a query still marked in flight
// (`stuck_busy`: an unwind went past the thunk), so an entry never outlives one rendered frame or a loading stretch.
//
// X3M_COLLIDE_QUERY_PHASES=1 additionally selects a fully preserved diagnostic thunk and the
// sole root-descent call. Engine-query timing starts after key/classification and stops before
// output reads; cache hits read no clock. See collide_query_phases.h and sector-collide §14.10.
// The following cost/state contract describes the default path without that diagnostic.
// Path cost (per mesh-pair query, about 1e2 per frame): one key build and a 4-way set compare, no lock, no
// allocation, no log, no API call; the table is 1,024 static entries. The thunk and both C handlers hold no x87/MMX
// instruction and no floating-point arithmetic at all (the key is compared as words), MXCSR is not read or written,
// the x87 stack is empty at the site and stays so, LastError is never touched (GetCurrentThreadId does not set it). On
// the run path the engine's EAX/ECX/EDX and callee-saved registers reach the caller as the engine left them; on a hit
// EAX = 0, ECX/EDX and EFLAGS are dead at the return (`xor eax,eax` follows), EBX/EBP/ESI/EDI are untouched.
namespace x3m::collide_memo {
struct Addresses {
    std::uintptr_t site, target;
};
bool initialize(); // backend-load path only; logs one collide_memo line when the variable is set
bool shutdown();   // restores the call (dynamic-unload detach only); true when nothing is installed
// Verifies the windows before and after the call (offsets relative to the site) and redirects it.
bool install_at(const Addresses& addresses, bool verify_mode);
const char* state();
// Frame boundary: advances the memo's frame (entries expire after one frame without a store or a hit) and writes one
// `collide_memo` line per 300 frames. No-op when nothing is installed.
void present(unsigned long long device, unsigned long long frame, bool captured);
core::Counters counters(); // monotonic totals (fixture and tests)
void device_reset();       // after IDirect3DDevice9::Reset: the owner thread drops the whole table at its next query
}
extern "C" {
void x3m_collide_memo_thunk(); // the redirected call's target
// The words at the site: [ESP+4..] of the engine's call, the tenth (pushed first, `push edi`) included.
struct x3m_collide_memo_args {
    const float* R1;
    const float* T1;
    std::uint32_t s1;
    const std::uint32_t* model_a;
    const float* R2;
    const float* T2;
    std::uint32_t s2;
    const std::uint32_t* model_b;
    std::uint32_t tolerance;
    const std::uint32_t* minimum;
};
int __cdecl x3m_collide_memo_lookup(std::uint32_t flags, std::uint32_t cap,
                                    const x3m_collide_memo_args* args); // 1: answered; 0: run, then store(); 2: run,
                                                                        // not ours
void __cdecl x3m_collide_memo_abandon(); // diagnostic CPU-mode rejection: discard pending memo without outputs
void __cdecl x3m_collide_memo_store();   // after the engine ran
extern std::uint32_t x3m_collide_memo_target, x3m_collide_memo_return;
}
