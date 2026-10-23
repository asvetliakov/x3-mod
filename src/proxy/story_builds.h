#pragma once
#include <cstdint>

// The known story builds (docs/architecture/story-builds.md): the sites story_detect.h derives at run time from the
// loaded L/x3story.obj, as decoded from the two objects on disk. Not the run-time source: a detection on a build listed
// here must reproduce its entry exactly or the feature stays inert (the last-resort check, Fail::table_mismatch); a
// build not listed here relies on the detection alone. The host tests require the detector to reproduce both entries
// from the objects on disk. The VM [*0x006085e4] holds the build id of the KC object it loaded last at +0x143c (loader
// 0x0049d2a7): the story, or one of the other KC objects the same VM runs (x3config at process init, x3intro behind
// the main menu, x3galedit); other_objects names those for the rows. Pure data and lookups: no Windows header, no
// allocation, no floating point. verification/results/seta-script-toggle/verify_seta_restore_sites.py (SETA fields)
// and verification/results/music-script-driver/verify_music_queue_kc.py (the queue's fields) pin every entry against
// its object on disk, for each build present under the game directory.
namespace x3m::story {
constexpr unsigned warp_window_length = 6; // the KC patch window: op6f; jt <continue> (memory form)
constexpr unsigned queue_cell_count = 6;   // old, loop return, new, fade, loop, change return

struct Build {
    const char* name;
    std::uint32_t build;
    // seta_restore: StopFastForward's TI_SetTimeWarpFactor (the task IP after it) and StartGateWarp's StopFastForward call
    // (the return offset after it)
    std::uint32_t seta_stop_ip, seta_gate_return;
    // music_queue, the KC patch: 150::WarpToSector's entry, its StopMusic(500) fork window and the continuation
    std::uint32_t warp_method_entry, warp_fork_offset, warp_continue_offset;
    const unsigned char* warp_window_memory; // warp_window_length bytes, the vanilla window in memory (operand LE)
    const unsigned char* warp_patch_memory;  // warp_window_length bytes, `jmp <continue>` plus the filler
    // music_queue, the deferral site: PlayMusicWithFade's MOV_StopMovie (IP after it), MusicFadeOut's MOV_StopMovie,
    // PlayMusicWithFade's MOV_PlayMovie, LoopMusic's return into it and ChangeTrackMusic's return into LoopMusic
    std::uint32_t kc_play_stop_ip, kc_fade_stop_ip, kc_play_ip, kc_loop_music_return, kc_change_track_return;
};
template <class T, unsigned N> constexpr unsigned count_of(const T (&)[N]) noexcept {
    return N;
}

// ---- Mayhem: addon/07.cat L/x3story.obj, 2,182,503 B, decoded SHA-256 2c3101a8...199298 (the installed story) ----
// Decoded from the object; the SETA sites and the queue's stop IPs and stack depth are flight-measured (Run 189-191).
inline constexpr std::uint32_t mayhem_build = 1462141839u;
inline constexpr std::uint32_t mayhem_seta_stop_ip = 0x1558d, mayhem_seta_gate_return = 0x15ef8;
inline constexpr std::uint32_t mayhem_warp_method_entry = 0x16185, mayhem_warp_fork_offset = 0x161c3,
                               mayhem_warp_continue_offset = 0x161d7;
inline constexpr unsigned char mayhem_warp_window_memory[warp_window_length] = {0x6f, 0x33, 0xd7, 0x61, 0x01, 0x00};
inline constexpr unsigned char mayhem_warp_patch_memory[warp_window_length] = {0x32, 0xd7, 0x61, 0x01, 0x00, 0x01};
inline constexpr std::uint32_t mayhem_kc_play_stop_ip = 0x0db54, mayhem_kc_fade_stop_ip = 0x0da17, mayhem_kc_play_ip = 0x0dbf7,
                               mayhem_kc_loop_music_return = 0x0dc84, mayhem_kc_change_track_return = 0x0dcdb;

// ---- stock AP 3.3: addon/04.cat L/x3story.obj, 2,312,979 B, decoded SHA-256 ed5786a0...faff7a ----
// Decoded from the object (static; no stock flight exists). The five music methods are equal to Mayhem's modulo
// relocation, so the fork child's stack depth and cells (story_detect.h) carry over.
inline constexpr std::uint32_t stock_build = 1510658513u;
inline constexpr std::uint32_t stock_seta_stop_ip = 0x157a6, stock_seta_gate_return = 0x160d1;
inline constexpr std::uint32_t stock_warp_method_entry = 0x1635e, stock_warp_fork_offset = 0x1639c,
                               stock_warp_continue_offset = 0x163b0;
inline constexpr unsigned char stock_warp_window_memory[warp_window_length] = {0x6f, 0x33, 0xb0, 0x63, 0x01, 0x00};
inline constexpr unsigned char stock_warp_patch_memory[warp_window_length] = {0x32, 0xb0, 0x63, 0x01, 0x00, 0x01};
inline constexpr std::uint32_t stock_kc_play_stop_ip = 0x0d273, stock_kc_fade_stop_ip = 0x0d136, stock_kc_play_ip = 0x0d316,
                               stock_kc_loop_music_return = 0x0d3a3, stock_kc_change_track_return = 0x0d3fa;

// clang-format off
inline constexpr Build builds[] = {
    {"mayhem", mayhem_build, mayhem_seta_stop_ip, mayhem_seta_gate_return, mayhem_warp_method_entry, mayhem_warp_fork_offset, mayhem_warp_continue_offset, mayhem_warp_window_memory, mayhem_warp_patch_memory, mayhem_kc_play_stop_ip, mayhem_kc_fade_stop_ip, mayhem_kc_play_ip, mayhem_kc_loop_music_return, mayhem_kc_change_track_return},
    {"stock_ap", stock_build, stock_seta_stop_ip, stock_seta_gate_return, stock_warp_method_entry, stock_warp_fork_offset, stock_warp_continue_offset, stock_warp_window_memory, stock_warp_patch_memory, stock_kc_play_stop_ip, stock_kc_fade_stop_ip, stock_kc_play_ip, stock_kc_loop_music_return, stock_kc_change_track_return},
};
// clang-format on
inline constexpr unsigned build_count = count_of(builds);

// The other KC objects the VM loads (same catalogues; their build ids are not stories): +0x143c holds one of these
// whenever no story is loaded. 0 = nothing loaded yet.
struct Object {
    std::uint32_t build;
    const char* name;
};
inline constexpr Object other_objects[] = {
    {0u, "none"},
    {1462141340u, "mayhem_x3config"},    // addon/07.cat L/x3config.obj: process init 0x004031fd (frame 0)
    {1462141842u, "mayhem_x3intro"},     // addon/07.cat L/x3intro.obj: the main menu (session mode 1)
    {1462141841u, "mayhem_x3galedit"},   // addon/07.cat L/x3galedit.obj: -galedit
    {1510658518u, "stock_ap_x3config_x3intro"}, // addon/04.cat L/x3config.obj and L/x3intro.obj (one build id)
    {1510658517u, "stock_ap_x3galedit"}, // addon/04.cat L/x3galedit.obj
};

inline const Build* find(std::uint32_t build) noexcept {
    for (const Build& b : builds)
        if (b.build == build) return &b;
    return nullptr;
}
inline const Object* find_other(std::uint32_t build) noexcept {
    for (const Object& o : other_objects)
        if (o.build == build) return &o;
    return nullptr;
}
enum class Kind : unsigned { story = 0, other = 1, unknown = 2 };
inline Kind classify(std::uint32_t build) noexcept {
    return find(build) ? Kind::story : find_other(build) ? Kind::other : Kind::unknown;
}
// The name a row prints for a build id: the story entry, the other object, or "unknown".
inline const char* object_name(std::uint32_t build) noexcept {
    if (const Build* b = find(build)) return b->name;
    if (const Object* o = find_other(build)) return o->name;
    return "unknown";
}
} // namespace x3m::story
