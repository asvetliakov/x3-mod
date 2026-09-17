#pragma once
// Hook-free poll of the engine's brightest directional light node
// (docs/reverse-engineering/camera-and-lights.md, "Safe read contract for the
// proxy"): render context R = *(uint32_t*)0x00608518 of the verified X3AP.exe,
// the null-terminated light-candidate array at R+0x5e8c (at most 254 node
// pointers, rebuilt per rendered view by 0x0047c640), per node the flags at
// +0x12c, the integer world position at +0xb0/b4/b8 and the colour words at
// +0x150/152/154. No code patch; every read goes through the bounds-checked
// engine_memory::read, behind the same exact-executable identity gate the
// other engine readers use; a foreign executable, a missing context or a
// layout that does not validate leaves the caller on its portable source (the
// LightDir_Dir0 constant latch, shadow_replay_sun.h). The caller polls from
// the render thread while the sector view is current (at a routed main-scene
// draw), never at Present: the array then holds the last rendered view's
// lights. A node pointer is never kept across frames. No tearing: the game is
// single-threaded and the draws run on the thread that calls 0x0047c640
// (docs/reverse-engineering/voice-startup-sequence.md, section 1), so a poll
// taken inside a draw call cannot interleave with the array's rebuild.
//
// Layout validation per read: *(int32_t*)(R+0x6288) == 8 (the slot count the
// engine writes beside the array), a terminator within 255 entries, and the
// "is a light" bit (+0x12c & 4) on EVERY entry, which 0x0047c640 guarantees
// for the array it builds. Two selection rules are evaluated on every poll:
//  * engine (the result): the Dir-slot rule of 0x004c5030-0x004c508f on the
//    score of 0x0047d641: admitted when flags & 0x800000 (directional) or the
//    range at +0x158 exceeds 0x256250; score = round(0.299 R + 0.587 G + 0.114 B)
//    + 0x300 for a directional light. A long-range point light's distance term
//    (luma x d / (2 range), per node) is not reproduced: its score here is an
//    upper bound, still below any directional light's.
//  * admission (diagnostics, and the result while the engine rule admits
//    nothing: 0x800000 is set lazily by 0x004bdbf0): (flags & 4) &&
//    !(flags & 0x400010), 0x0047c640's own test, ranked by the same rounded luma.
// Ties go to the farther node (the 16 forced-directional nodes of the secondary
// scene 0x00420260 sit near the origin; the engine's qsort order on a tie is not
// established). Both winners are reported so a flight shows any divergence; the
// caller cross-checks the result against the shader constants before trusting
// it (shadow_replay_sun_point.h).
#include <cstdint>

namespace x3m::sun_light_poll {
enum class Status : std::uint32_t { Ok = 0, Disabled = 1, ExecutableMismatch = 2, SlotUnreadable = 3, NullContext = 4, Unreadable = 5, Layout = 6, NoDirectional = 7 };
const char* status_name(Status status);
struct Sample {
    Status status = Status::Disabled;
    std::int32_t position[3]{};      // engine integers (world units = x 0.01 in a gameplay view)
    std::uint32_t score = 0;         // the chosen node's engine score (rounded luma, + 0x300 when directional)
    std::uint32_t second_score = 0;  // the next candidate's under the same rule (0: none); equal scores are an engine tie
    bool engine_rule = false;        // the result is the engine rule's winner (else the admission rule's)
    bool rules_agree = false;        // both rules chose the same node
    std::int32_t admission_position[3]{}; // the admission rule's winner
    std::uint32_t candidates = 0, directional = 0, slot_admitted = 0; // array entries; admission-rule and engine-rule candidates
    std::uint32_t flags = 0;         // the chosen node's +0x12c
    float record_position[3]{};      // [node+0x16c]+0x34: the D3DLIGHT9 position the engine derived (context-scaled), diagnostics
    bool record_valid = false;
};
// X3M_MOTION_OUTPUT=1 with X3M_SHADOW_REPLAY_DEPTH=1 and a cascade list
// (X3M_SHADOW_CASCADES), unless X3M_SHADOW_SUN_POLL=0. Idempotent.
bool initialize();
bool available();
const char* status();
// One poll: one 4-byte and one 1,024-byte read of the context, one 192-byte
// read per candidate node, one 12-byte read of the chosen node's light record.
// Keeps LastError. Returns status == Ok.
bool read(Sample* out);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Fixture seam (absent from production): the fixture executable's own context
// pointer slot stands in for 0x00608518; the identity gate is bypassed. Null uninstalls.
void fixture_install(const std::uint32_t* context_slot);
#endif
}
