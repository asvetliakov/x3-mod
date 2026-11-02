#pragma once
// The own sun's flare-probe hook: the pure part (docs/architecture/own-sun-pass.md section 5;
// sites, layouts and the probe's ABI from docs/reverse-engineering/lens-flare-visibility.md, sections 11-16). No
// Windows/D3D dependency, no allocation, no engine call, integer only: eligible() and main_view() run inside the probe
// thunk at 0x00471630, in the middle of the engine's record loop, where the x87 stack and MXCSR belong to the engine.
// The host double verification/probe/own_sun_hook_host.cpp drives exactly this header.
#include <cstddef>
#include <cstdint>

namespace x3m::own_sun_hook::core {
// ---- Sites (preferred base 0x00400000; X3AP.exe per executable_identity.h, shipped SHA-256 fdbf3418...f8ab) ----
constexpr std::uintptr_t probe_site_va = 0x00471630, probe_target_va = 0x00488720;
// The lens-scene call: never written since the lens bracket's deletion (2026-11-02); its bytes and the traversal's
// entry stay in the install's identity check.
constexpr std::uintptr_t lens_site_va = 0x00472491, lens_target_va = 0x0047e6e0;
// Whole instructions around the probe call, from 0x0047162a: mov ecx,[esp+0x14]; push ecx;
// push esi; CALL; add esp,8; test eax,eax; jz +3; mov [esi+0x30],ebp.
constexpr std::uintptr_t probe_context_va = 0x0047162a;
constexpr unsigned char probe_context[] = {0x8b, 0x4c, 0x24, 0x14, 0x51, 0x56, 0xe8, 0xeb, 0x70, 0x01, 0x00,
                                           0x83, 0xc4, 0x08, 0x85, 0xc0, 0x74, 0x03, 0x89, 0x6e, 0x30};
// From 0x00472490: push esi; CALL; mov ecx,[0x608518]; mov eax,[ecx+0x6304]; add esp,4.
constexpr std::uintptr_t lens_context_va = 0x00472490;
constexpr unsigned char lens_context[] = {0x56, 0xe8, 0x4a, 0xc2, 0x00, 0x00, 0x8b, 0x0d, 0x18, 0x85, 0x60,
                                          0x00, 0x8b, 0x81, 0x04, 0x63, 0x00, 0x00, 0x83, 0xc4, 0x04};
// The probe's prologue and its three early tests, 0x00488720..0x004887b3 (148 bytes), compared whole as the install
// identity of the probe (FNV-1a 64 of the span). The hidden answer itself needs none of the three gates.
constexpr std::uintptr_t probe_gates_va = 0x00488720;
constexpr unsigned probe_gates_length = 0x94;
constexpr std::uint64_t probe_gates_fnv1a = 0x0e5f40888a926996ull;
// push ebp; mov ebp,esp; and esp,-16; sub esp,0x44 (entry boundary; cdecl, plain ret).
constexpr unsigned char probe_entry[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0, 0x83, 0xec, 0x44};
// mov eax,[0x608518]; push ebx; push ebp; mov ebp,[esp+0xc] (one stack argument, caller cleans).
constexpr unsigned char lens_entry[] = {0xa1, 0x18, 0x85, 0x60, 0x00, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c};
// The X3M_SUBMIT_PHASES diagnostic claims 0x00472490 (six bytes: push esi + this call): the lens call is not claimed
// any more, but its bytes are part of the install identity, so the pair is refused by name (submit_phases_conflict).
constexpr std::uintptr_t conflicting_submit_phase_va = 0x00472490;

// ---- Engine layout ----
constexpr std::uintptr_t config_global_va = 0x00606f34; // *this + 0xfc = VideoD3DFlags (FL: +0xec)
constexpr unsigned config_video_flags = 0xfc;
constexpr std::uint32_t video_flag_flares = 0x8000;
constexpr std::uintptr_t mode_global_va = 0x00606f38; // *this + 0x28/+0x2c = fallback view scale pair
constexpr std::uintptr_t cockpit_registry_va = 0x00608504;
constexpr unsigned record_view = 0x08, record_accumulator = 0x10, record_x = 0x20, record_y = 0x24,
                   record_visible = 0x30, record_size = 0x34, record_group = 0x38;
constexpr unsigned view_flags = 0x270, view_rect_top = 0x288, view_rect_bottom = 0x28c, view_rect_left = 0x290,
                   view_rect_right = 0x294, view_fov = 0x298, view_scale_x = 0x300, view_scale_y = 0x304;
constexpr std::uint32_t view_flag_probe_hidden = 0x8000000, view_flag_sector_camera = 0x10000;

// ---- The probe answer ----
// Eligible probe (run223, docs/verification/sun-occlusion.md): the MAIN view's cross-view re-probe of a
// record OWNED BY A BACKGROUND-REGIME VIEW (owner+0x270 & 0x400000: the layer-15 view that holds the
// sun's lens record; its rect and camera coincide with the main view's, so that re-probe is the one
// that hides the sun). The owner's own probe, later views' re-probes and every record the main view
// owns itself (ship flares, group 25) run the original and are not counted as suns.
constexpr std::uint32_t view_flag_background = 0x400000;
// The probe's answers: 0 / 1 = the probe's own EAX ("visible" / "hide"), 2 = run the original on the caller's frame.
// The own sun answers Hidden for the eligible record in an own-sun frame (own_sun_core.h answer()); every other probe
// is the original's. Visible is never chosen by the hook: the log mode returns the original's own 0 under that name.
enum Decision : int { Visible = 0, Hidden = 1, Original = 2 };
inline bool eligible(std::uintptr_t record_owner, std::uintptr_t view, std::uintptr_t main_view,
                     std::uint32_t owner_flags) noexcept {
    return main_view != 0 && view == main_view && record_owner != 0 && record_owner != view &&
           (owner_flags & view_flag_background) != 0;
}

// ---- The main 3D view: *(cockpit + 0x58) of the active control cockpit ----
// The registry walk of sector_background.h (layout proof: sector-fog.md section 11): registry
// *0x00608504, handle registry+0x10, power-of-two bucket array, {next, handle, cockpit} links.
// Additionally requires the sector-camera marker view+0x270 & 0x10000 that the cockpit update
// sets every frame (0x0042157c). 0 on any doubt: the own sun then never answers.
template <class Read> std::uintptr_t main_view(Read& read) noexcept {
    auto word = [&](std::uint32_t base, std::uint32_t offset, std::uint32_t& out) {
        return base && !(base & 3) && read(std::uintptr_t(base) + offset, &out, sizeof out);
    };
    std::uint32_t registry = 0, handle = 0, header = 0, bucket[2]{}, link = 0, cockpit = 0, camera = 0, flags = 0;
    if (!read(cockpit_registry_va, &registry, 4) || !word(registry, 0x10, handle) || !handle) return 0;
    if (!word(registry, 0, header) || !header || (header & 3) || !read(header, bucket, sizeof bucket)) return 0;
    const std::uint32_t n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !word(bucket[0], 4 * ((n - 1) & handle), link)) return 0;
    for (unsigned walked = 0; link && walked < 32; ++walked) {
        std::uint32_t row[3]{};
        if ((link & 3) || !read(link, row, sizeof row)) return 0;
        if (row[1] == handle) {
            cockpit = row[2];
            break;
        }
        link = row[0];
    }
    if (!word(cockpit, 0x58, camera) || !word(camera, view_flags, flags) || !(flags & view_flag_sector_camera))
        return 0;
    return camera;
}
inline std::uint64_t fnv1a(const unsigned char* p, std::size_t n) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
} // namespace x3m::own_sun_hook::core
