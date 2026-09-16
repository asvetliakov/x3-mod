#pragma once
#include <cstdint>
#include "engine_patch.h"

// One byte-verified gate on the media-record allocator
// `media_record* __cdecl 0x00498140(int media_id /*[esp+4]*/)`, EAX = the
// constructor flag word in, the linked 0x40-byte record or 0 out
// (X3M_MEDIA_CUE_TRACE=1 / X3M_MEDIA_CUE_CACHE=1). The 5-byte entry span
// `push ebx; mov ebx,[esp+8]` is two whole instructions with no incoming edge,
// no data reference and no relative branch, so the claim tail is the original
// bytes verbatim, replayed at the game's exact ESP after the gate's proceed
// arm. Exact instruction boundaries, the five `call` sites and their cdecl
// `add esp,4` cleanups, the two plain `ret`s, the routine's ESP contract, the
// play-helper frame that exposes the selector's return address and cue kind at
// [esp+0xc]/[esp+0x10], and the disjointness from every installed stamp table
// are checked by verification/probe/verify_media_cue_site.py. Owning study:
// docs/reverse-engineering/media-cue-playback.md.
namespace x3m::media_cue::sites {
enum Index : unsigned { MediaCreateEnter = 0, Count = 1 };
constexpr unsigned kSiteCount = Count;
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"media_create_enter",0x00498140,{0x53,0x8b,0x5c,0x24,0x08},5,0,0},
};
static_assert(sizeof(kSites)/sizeof(kSites[0]) == Count, "One gate site");
// The return addresses at [esp] on entry, one per direct caller (the complete
// entry set: no data reference to 0x00498140 exists in the image), the
// selector's return address at [esp+0xc] behind the play helper 0x004f65f0
// (`push ecx` frame slot + `push esi` id between its own return address and
// the call) and the cue kind it pushes (`0x0045c605 push 0x5a`) at [esp+0x10].
inline constexpr std::uint32_t kQueryReturn = 0x0049873f;      // 0x00498730 stop/query helper
inline constexpr std::uint32_t kSavegameReturn = 0x00498bb3;   // 0x00498ad0 savegame MOVI restore
inline constexpr std::uint32_t kScriptReturn = 0x00498cdd;     // 0x00498c90 play-by-id (script VM)
inline constexpr std::uint32_t kSpeechReturn = 0x00498efd;     // 0x00498e30 speech cue
inline constexpr std::uint32_t kHelperReturn = 0x004f6615;     // 0x004f65f0 track/emitter play helper
inline constexpr std::uint32_t kSelectorReturn = 0x0045c60c;   // 0x0045b720 sector selector, `call 0x004f65f0` at 0x0045c607
inline constexpr std::uint32_t kSelectorKind = 0x5a;
}
