#pragma once
#include <cstdint>

// Root-object point-light admission (X3M_POINT_LIGHT_ROOT_ADMISSION=1; unset =
// vanilla, nothing patched): the engine's per-node range test at 0x004c27af
// (`jg reject`, six bytes) becomes `jg detour`. A node in range runs the native
// instruction stream unchanged (the same not-taken branch); a rejected node
// enters the detour, which calls an integer-only cdecl handler that walks the
// node's parent chain (+0x18, at most 8 hops, every read bounds-checked through
// engine_memory) and applies the same predicate to the root node; the root's
// admission admits the node (docs/reverse-engineering/camera-and-lights.md,
// "Point-light admission site"; docs/architecture/original-shading-critique.md Q3).
// Written on the backend-load path inside the engine_patch install window
// after the exact-executable, 28-byte window and reject-target checks, with
// this module pinned so the detour and handler can never point into freed
// memory; VirtualProtect/FlushInstructionCache, read-back compare, rollback.
namespace x3m::point_light_admission {
bool initialize();  // backend-load path only; logs one point_light_root_admission line when the variable is set
bool shutdown();    // restores the six original bytes (dynamic-unload detach only); true when nothing is installed
// Verifies the window at `site` (the fixture emits a synthetic copy of the
// engine bytes), emits the detour and patches; the production initialize()
// calls it with the engine site. Returns whether the patch is live; state()
// carries the reason either way.
bool install_at(std::uintptr_t site);
const char* state();
const char* write_path();      // none|atomic|plain: which engine_patch::write_code path patched the site
std::uintptr_t detour_address();
// Bumps the frame serial of the per-(node, light) root-verdict memo: Present
// and Reset. One relaxed increment; no other work.
void next_frame();
struct Stats { std::uint32_t outcomes[9]; std::uint32_t walks, memo_hits, frame; }; // outcomes indexed by core::Outcome; walks = reject-path calls that walked, memo_hits = answered from the memo
Stats stats();
}
// The detour's cdecl target: non-zero when the root of `node` passes the
// range predicate against `light`. Preserves LastError; integer only.
extern "C" int x3m_point_light_root_admits(std::uint32_t node, std::uint32_t light);
