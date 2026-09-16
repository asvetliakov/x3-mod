#pragma once
#include <atomic>
#include "engine_patch.h"

// The install transaction shared by the accumulate-only stamp groups
// (pass_phases.cpp, loop_phases.cpp), the same shape as
// frame_phases::install_group: refused outside the install window, every span
// preflighted before any claim, claims in table order, every patched site
// rolled back in reverse on the first failure (the claim's own reason, or
// stub_chain_failed, or rollback_failed_inert when a restore fails), and
// `installed` raised only after the last site chained its stub.
namespace x3m::stamp {
template <unsigned Count, class Emit>
bool install_group(engine_patch::Site (&patches)[Count], const engine_patch::SiteSpec* specs, Emit emit,
                   std::atomic<bool>& installed, const char*& status) {
    installed.store(false, std::memory_order_release);
    if (!engine_patch::install_window_open()) { status = "install_window_closed"; return false; }
    for (unsigned i = 0; i < Count; ++i)
        if (!engine_patch::verify_bytes(specs[i].address, specs[i].expected, specs[i].length)) { status = "preflight_bytes"; return false; }
    for (unsigned i = 0; i < Count; ++i) {
        if (engine_patch::claim(patches[i], specs[i])) {
            void** next = nullptr; void* stub = emit(i, &next);
            if (stub && next && engine_patch::store_pointer(next, *patches[i].entry) && engine_patch::push_front(patches[i], stub)) continue;
            status = "stub_chain_failed";
        } else status = patches[i].status; // the claim's own reason (bytes_mismatch, late_claim, arena_full, ...)
        bool restored = true;
        for (unsigned j = Count; j-- > 0;) if (patches[j].patched_in && !engine_patch::restore(patches[j])) restored = false;
        if (!restored) status = "rollback_failed_inert";
        return false;
    }
    status = "ok"; installed.store(true, std::memory_order_release); return true;
}
// Reverse-order restore of every patched site; the Site records are cleared.
template <unsigned Count>
bool uninstall_group(engine_patch::Site (&patches)[Count]) {
    bool restored = true;
    for (unsigned i = Count; i-- > 0;) {
        if (patches[i].patched_in && !engine_patch::restore(patches[i])) restored = false;
        patches[i] = engine_patch::Site{};
    }
    return restored;
}
}
