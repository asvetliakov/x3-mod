#pragma once
#include <cstdint>
#include "../../proxy/collide_memo.h"
#include "collide_memo_sites_fl.h"

// FL edition of x3m::collide_memo (src/engine/fl/collide_memo_fl.cpp provides collide_memo.h's namespace functions for
// X3FL.exe; the ap target keeps src/proxy/collide_memo.cpp). The AP thunk and handler symbols are not provided; the FL
// ones are below. FL edition only.
namespace x3m::collide_memo {
// Every memo window of collide_windows_fl_inc.h at base + its RVA, then the call at base+0x09853a redirected to the FL
// thunk and read back. initialize() calls it with fl::module_base(); the fixture with its own 64 KB-aligned image.
// install_at(a, verify) is the same with base = a.site - the site's RVA (a.target must be base + the query's RVA).
// LastError preserved.
bool install_base(std::uintptr_t base, bool verify_mode);
// FL-only counters (beside core::Counters): queries bypassed for an unmasked MXCSR exception, table clears for a
// changed MXCSR control word.
struct FpCounters {
    std::uint32_t unmasked, context_clears;
};
FpCounters fp_counters();
#ifdef X3M_COLLIDE_MEMO_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
extern "C" {
void x3m_collide_memo_fl_thunk(); // the redirected call's target
int __cdecl x3m_collide_memo_fl_lookup(const x3m::fl::collide_memo_fl::Regs* regs,
                                       const x3m::fl::collide_memo_fl::Args* args); // 1 answered, 0 run + store, 2 bypass
void __cdecl x3m_collide_memo_fl_store();                                           // after the engine ran
extern std::uint32_t x3m_collide_memo_fl_target, x3m_collide_memo_fl_return;
}
