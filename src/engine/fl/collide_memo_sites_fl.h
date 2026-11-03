#pragma once
#include "../../proxy/collide_memo_core.h"
#include "collide_windows_fl.h"
#include <cstdint>
#include <cstring>

// FL edition of the no-contact memo's engine boundary (the AP twin is src/proxy/collide_memo_core.h, whose 81-word Key,
// hash, minimum relaxation, 4-way Table, sliding one-frame expiry, MissClass and Counters are shared unchanged). Verified
// facts: docs/reverse-engineering/fl/collision-memo.md (static contract, "Implemented FP context correction", "Port
// (2026-11-03)"; verifier verification/probe/verify_collide_memo_sites_fl.py). Every value is an RVA added to
// x3m::fl::module_base(), or a field offset. The fourteen bodies the enumeration rests on (caller, query, descent, leaf,
// triangle test and projection, SAT, both matrix products, the five model mutators) are the memo table of
// collide_windows_fl_inc.h, each relocated dword bound to base + its target.
//
// FL inlines AP's mode-setting wrapper into the transform caller 0x0982d0 and passes the transforms in registers:
// 0984bf  a3 <base+0x1aa0e0>          MOV [cap],EAX              ; the caller writes cap, flags, tolerance (CVTTSS2SI)
// 0984c4  89 0d <base+0x1a9044>       MOV [flags],ECX            ; and the minimum pointer before the call
// 0984f3  a3 <base+0x1aa09c> / 0984ff a3 <base+0x1aa084>
// 098539  53                          PUSH EBX                   ; model A
// 09853a  e8 a1 25 08 00              CALL 0x11aae0              <- the site (sole encoded caller of the query)
// 09853f  83 c4 18 / 33 c0 / 39 05 <base+0x1aa0a0> / 0f 95 c0   result = contacts != 0
// At the call: ECX = &R1 (9 floats), EDX = &T1 (3), XMM2 low = s1 (float bits), [ESP+4..+0x18] = model A, &R2, &T2, s2,
// model B, flags & ~0xc. The caller drops 0x18 and reads EAX/ECX/EDX/XMM/EFLAGS no more; EBX/ESI/EDI/EBP are callee-saved.
// The query 0x11aae0: bypasses nothing while the init flag 0x21b9bc is 0 (one-time writes), returns 3 for an unbuilt
// model (+0x14 != 3), else computes the root-relative transform (T 0x21b988..90, R 0x21b994..b4, scale 0x21b9b8), the
// first-contact flag 0x21b980 = ((flags & ~0xc) == 2), resets visits 0x1a9048, triangle tests 0x1aa0d0 and contacts
// 0x1aa0a0 and descends. A no-contact query writes exactly those (collision-memo.md section 3); the contact record
// 0x1aa0a8..bc and *minimum are written on a contact only. 0x21b984 is not root state and is never touched.
namespace x3m::fl::collide_memo_fl {
namespace core = x3m::collide_memo::core;
namespace W = x3m::fl::collide_windows;

constexpr std::uint32_t caller_rva = 0x0982d0, site_rva = 0x09853a, return_rva = 0x09853f, query_rva = 0x11aae0;
constexpr std::uint32_t flags_rva = 0x1a9044, cap_rva = 0x1aa0e0, tolerance_rva = 0x1aa09c, minimum_rva = 0x1aa084;
constexpr std::uint32_t visits_rva = 0x1a9048, triangles_rva = 0x1aa0d0, contacts_rva = 0x1aa0a0;
constexpr std::uint32_t first_contact_rva = 0x21b980, root_t_rva = 0x21b988, root_r_rva = 0x21b994,
                        root_scale_rva = 0x21b9b8, init_rva = 0x21b9bc, untouched_rva = 0x21b984;
constexpr std::uint32_t mode_mask = ~std::uint32_t(0xc); // the sixth stack word is flags & ~0xc
constexpr std::uint32_t mxcsr_masks = 0x1f80, mxcsr_controls = 0xffc0;
constexpr unsigned stack_words = 6, call_length = 5;
constexpr unsigned char expected_call[call_length] = {0xe8, 0xa1, 0x25, 0x08, 0x00};

// The six stack words of the native call ([ESP+4..+0x18] at the thunk's entry).
struct Args {
    const std::uint32_t* model_a;
    const float* R2;
    const float* T2;
    std::uint32_t s2;
    const std::uint32_t* model_b;
    std::uint32_t flags_masked;
};
// The register inputs the thunk saves before its call into C: ECX, EDX and XMM2's low word.
struct Regs {
    const float* R1;
    const float* T1;
    std::uint32_t s1;
};
// The literal inputs the caller wrote before the call (read at lookup, never replayed).
struct Mode {
    std::uint32_t flags, cap, tolerance, minimum, init;
};

// The 81-word key in AP's word order: R1 T1 s1 R2 T2 s2 (26 float words), the tolerance (FL: the integer the caller
// already converted), flags, cap, minimum pointer null-ness and the float behind it, both model pointers, both 24-byte
// headers, both 0x48-byte root boxes. False (the engine runs, nothing is stored) when the init flag is still 0, the stack
// mode word is not the caller's flags & ~0xc, or a model is null, not built (+0x14 != 3) or has no root.
inline bool build_key(core::Key& key, const Regs& r, const Args& a, const Mode& m) {
    if (!m.init || a.flags_masked != (m.flags & mode_mask)) return false;
    if (a.model_a == nullptr || a.model_b == nullptr) return false;
    if (a.model_a[core::model_state_word] != core::model_built || a.model_b[core::model_state_word] != core::model_built ||
        a.model_a[0] == 0 || a.model_b[0] == 0)
        return false;
    std::uint32_t* w = key.words;
    std::memcpy(w, r.R1, 36);
    std::memcpy(w + 9, r.T1, 12);
    w[12] = r.s1;
    std::memcpy(w + 13, a.R2, 36);
    std::memcpy(w + 22, a.T2, 12);
    w[25] = a.s2;
    w[26] = m.tolerance;
    w[27] = m.flags;
    w[28] = m.cap;
    w[29] = m.minimum != 0;
    w[30] = m.minimum != 0 ? *reinterpret_cast<const std::uint32_t*>(std::uintptr_t(m.minimum)) : 0u;
    w[31] = std::uint32_t(reinterpret_cast<std::uintptr_t>(a.model_a));
    w[32] = std::uint32_t(reinterpret_cast<std::uintptr_t>(a.model_b));
    std::memcpy(w + 33, a.model_a, 4 * core::header_words);
    std::memcpy(w + 33 + core::header_words, a.model_b, 4 * core::header_words);
    std::memcpy(w + 33 + 2 * core::header_words, reinterpret_cast<const void*>(std::uintptr_t(a.model_a[0])),
                4 * core::box_words);
    std::memcpy(w + 33 + 2 * core::header_words + core::box_words,
                reinterpret_cast<const void*>(std::uintptr_t(a.model_b[0])), 4 * core::box_words);
    return true;
}
// Outputs in AP's root_block order (T[3], first-contact flag, scale, R[9]); `at(rva)` is the engine's uint32 at base +
// rva (production) or a word of the fixture's image.
template <class At> void read_outputs(core::Outputs& out, At at) {
    out.visits = at(visits_rva);
    out.triangles = at(triangles_rva);
    out.tolerance_integer = at(tolerance_rva);
    for (unsigned i = 0; i < 3; ++i) out.root_block[i] = at(root_t_rva + 4 * i);
    out.root_block[3] = at(first_contact_rva);
    out.root_block[4] = at(root_scale_rva);
    for (unsigned i = 0; i < 9; ++i) out.root_block[5 + i] = at(root_r_rva + 4 * i);
}
// Exactly what a no-contact query writes, from the stored outputs; mode, cap, tolerance, minimum, the init flag and
// 0x21b984 stay as the caller and the engine left them.
template <class At> void replay_outputs(const core::Outputs& out, At at) {
    at(visits_rva) = out.visits;
    at(triangles_rva) = out.triangles;
    at(contacts_rva) = 0;
    for (unsigned i = 0; i < 3; ++i) at(root_t_rva + 4 * i) = out.root_block[i];
    at(first_contact_rva) = out.root_block[3];
    at(root_scale_rva) = out.root_block[4];
    for (unsigned i = 0; i < 9; ++i) at(root_r_rva + 4 * i) = out.root_block[5 + i];
}
// The FP context (collision-memo.md section 6): the native query inherits the caller's MXCSR, and rounding control can
// change even signed-zero bits of the root scratch. An entry is reused only under the controls it was stored with: a
// change of bits 0xffc0 clears the table; any unmasked SIMD exception bypasses (the native trap behaviour stays).
class ControlContext {
public:
    enum Verdict { accept, accept_cleared, bypass };
    Verdict judge(std::uint32_t mxcsr) {
        if ((mxcsr & mxcsr_masks) != mxcsr_masks) return bypass;
        const std::uint32_t controls = mxcsr & mxcsr_controls;
        if (valid_ && controls == controls_) return accept;
        const bool had = valid_;
        controls_ = controls;
        valid_ = true;
        return had ? accept_cleared : accept;
    }
    void reset() { valid_ = false; }

private:
    std::uint32_t controls_ = 0;
    bool valid_ = false;
};

static_assert(sizeof(Args) == 4 * stack_words, "the six native stack words");
static_assert(sizeof(Regs) == 12, "ECX, EDX, XMM2 low");
static_assert(site_rva + call_length == return_rva && caller_rva < site_rva, "the call inside the caller");
static_assert(std::uint32_t(site_rva + call_length + 0x000825a1u) == query_rva, "the call's rel32 reaches the query");
static_assert((site_rva & 7u) + call_length <= 8u, "the five call bytes lie in one aligned qword (atomic)");
static_assert(W::memo_windows[0].rva == caller_rva && W::memo_windows[1].rva == query_rva, "caller and query pinned");
static_assert(root_r_rva + 4 * 9 == root_scale_rva && root_scale_rva + 4 == init_rva && root_t_rva + 12 == root_r_rva,
              "the root scratch spans");
}
