#pragma once
#include <cstdint>
#include <cstring>

// FL edition of the chase camera's pose-seam contract (the AP site is the constant block of src/proxy/chase_camera.cpp).
// Facts: docs/reverse-engineering/fl/chase-camera.md (offline study, then "Verified 2026-11-02 against X3FL.exe";
// verifier verification/probe/verify_chase_camera_site.py --edition fl). X3FL.exe is relocatable: every value below is
// an RVA added to x3m::fl::module_base(), or a field offset. No Windows dependency (the native fixture compiles it).
//
// Cockpit updater RVA 0x021f50 (ECX = cockpit -> EBX at 0x021f5e, stored once to [esp+0x1c] at 0x021f63; the only
// EBX writes before the seam sit in the loop 0x02268d..0x0226d2, whose sole exit reloads EBX from [esp+0x1c] at
// 0x0226d8). Plain RET; the handler calls no native function, so that ABI is not used.
//
// 0227a4  0f 10 44 24 20        movups xmm0,[esp+0x20]                 <- seam window (66 bytes, no fixup)
// 0227a9  0f 11 41 30           movups [ecx+0x30],xmm0                 ; last native position copy: the pose is final
// 0227ad  83 7b 54 00           cmp dword [ebx+0x54],0                 <- site (ten bytes, whole instructions)
// 0227b1  0f 84 71 01 00 00     je 0x022928                            ; rel32 at site+6, re-based in the tail
// 0227b7  33 c9                 xor ecx,ecx                            <- continuation
// ...
// 0227e3  8b 4b 58              mov ecx,[ebx+0x58]                     ; first camera-relative load after the seam
// 022928  8b cb / e8 81 06 00 00  mov ecx,ebx / call 0x022fb0          <- branch-target window (7 bytes)
// 0dec8c  a1 <0x5a82f8> / 8b 78 28 / 8b 70 2c   the default view plane +0x28/+0x2c (fixup at +1 = base + 0x1a82f8)
//
// Seven direct edges target the site start (0x022141, 0x022597, 0x0225a6, 0x0225af, 0x0225b8, 0x0225c1, 0x0225ca), none
// its interior; no HIGHLOW fixup inside the span. The five-byte jump crosses the aligned qword at 0x0227b0 (plain copy,
// install window only).
namespace x3m::fl::chase_camera_fl {
constexpr std::uint32_t updater_rva = 0x021f50, updater_end_rva = 0x022fb0;
constexpr std::uint32_t site_rva = 0x0227ad, continuation_rva = 0x0227b7, branch_target_rva = 0x022928;
constexpr std::uint32_t last_position_copy_rva = 0x0227a9, first_camera_load_rva = 0x0227e3;
constexpr unsigned site_length = 10, rel32_offset = 6;
constexpr unsigned char expected_site[site_length] = {0x83, 0x7b, 0x54, 0x00, 0x0f, 0x84, 0x71, 0x01, 0x00, 0x00};
constexpr std::uint32_t inbound_edge_rvas[7] = {0x022141, 0x022597, 0x0225a6, 0x0225af, 0x0225b8, 0x0225c1, 0x0225ca};

constexpr std::uint32_t seam_window_rva = 0x0227a4;
constexpr unsigned seam_window_length = 66, seam_site_offset = 9;
constexpr unsigned char expected_seam_window[seam_window_length] = {
    0x0f, 0x10, 0x44, 0x24, 0x20, 0x0f, 0x11, 0x41, 0x30, 0x83, 0x7b, 0x54, 0x00, 0x0f, 0x84, 0x71, 0x01,
    0x00, 0x00, 0x33, 0xc9, 0xba, 0x20, 0x00, 0x00, 0x00, 0x89, 0x4c, 0x24, 0x14, 0x89, 0x54, 0x24, 0x18,
    0x8b, 0x43, 0x54, 0x8b, 0x40, 0x50, 0x8b, 0x04, 0x01, 0x89, 0x44, 0x24, 0x10, 0x83, 0x38, 0x00, 0x0f,
    0x84, 0x38, 0x01, 0x00, 0x00, 0x0f, 0x1f, 0x40, 0x00, 0x8b, 0x78, 0x70, 0x8b, 0x4b, 0x58};
constexpr std::uint32_t target_window_rva = branch_target_rva;
constexpr unsigned target_window_length = 7;
constexpr unsigned char expected_target_window[target_window_length] = {0x8b, 0xcb, 0xe8, 0x81, 0x06, 0x00, 0x00};
// The default view plane (the handler's fallback when the camera's own +0x300/+0x304 are not positive).
constexpr std::uint32_t default_view_plane_slot_rva = 0x1a82f8;
constexpr std::uint32_t plane_window_rva = 0x0dec8c;
constexpr unsigned plane_window_length = 11, plane_window_fixup_offset = 1;
// The fixup dword (+1) is zeroed here; at the live base it must equal base + default_view_plane_slot_rva.
constexpr unsigned char expected_plane_window[plane_window_length] = {0xa1, 0x00, 0x00, 0x00, 0x00, 0x8b,
                                                                      0x78, 0x28, 0x8b, 0x70, 0x2c};
constexpr std::uint32_t seam_window_fnv1a = 0x391b90a2, target_window_fnv1a = 0x466a42e2,
                        plane_window_fnv1a = 0x2df2f1ce; // plane with the fixup zeroed

// Field offsets the shared handler reads and writes (src/proxy/chase_camera.cpp, chase_camera_native.h); each is an
// FL anchor of the study's section 2 tables, re-decoded on 2026-11-02 (the verifier's field anchors).
constexpr unsigned cockpit_scene_camera = 0x08, cockpit_ref_object = 0x0c, cockpit_view_object = 0x10,
                   cockpit_sector_object = 0x54, cockpit_camera = 0x58, cockpit_view_rel = 0xf0,
                   cockpit_view_mode = 0x150, cockpit_flags = 0x1a0, cockpit_connect_mode = 0x1c0,
                   cockpit_aim_gun = 0x1d8, cockpit_tracked = 0x1e0, cockpit_tracking_mode = 0x1e4,
                   cockpit_sector = 0x1fc;
constexpr unsigned camera_position = 0x30, camera_basis = 0x40, camera_fov = 0x298, camera_plane_w = 0x300,
                   camera_plane_h = 0x304, plane_w = 0x28, plane_h = 0x2c;

constexpr std::uint32_t fnv1a(const unsigned char* p, unsigned n) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
constexpr std::uint32_t le32(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 |
           std::uint32_t(bytes[3]) << 24;
}
// The install decision on the three windows read at base + *_window_rva: nullptr when they are exactly the engine's
// (the plane fixup equal to base + default_view_plane_slot_rva), else the refusal.
inline const char* plan(const unsigned char seam[seam_window_length], const unsigned char target[target_window_length],
                        const unsigned char plane[plane_window_length], std::uintptr_t base) {
    if (std::memcmp(seam, expected_seam_window, seam_window_length)) return "seam_mismatch";
    if (std::memcmp(target, expected_target_window, target_window_length)) return "target_mismatch";
    if (le32(plane + plane_window_fixup_offset) != std::uint32_t(base + default_view_plane_slot_rva))
        return "plane_slot_mismatch";
    unsigned char masked[plane_window_length];
    std::memcpy(masked, plane, plane_window_length);
    std::memset(masked + plane_window_fixup_offset, 0, 4);
    return std::memcmp(masked, expected_plane_window, plane_window_length) ? "plane_window_mismatch" : nullptr;
}

static_assert(fnv1a(expected_seam_window, seam_window_length) == seam_window_fnv1a, "the seam window is the measured one");
static_assert(fnv1a(expected_target_window, target_window_length) == target_window_fnv1a,
              "the branch-target window is the measured one");
static_assert(fnv1a(expected_plane_window, plane_window_length) == plane_window_fnv1a,
              "the plane window (fixup zeroed) is the measured one");
static_assert(seam_window_rva + seam_site_offset == site_rva && site_rva + site_length == continuation_rva &&
                  seam_window_rva + 5 == last_position_copy_rva &&
                  first_camera_load_rva + 3 == seam_window_rva + seam_window_length,
              "the seam window runs from the last position copy through the first camera load");
constexpr bool site_in_window() {
    for (unsigned i = 0; i < site_length; ++i)
        if (expected_seam_window[seam_site_offset + i] != expected_site[i]) return false;
    return true;
}
static_assert(site_in_window(), "the site is the window's ten bytes at +9");
static_assert(expected_site[0] == 0x83 && expected_site[1] == 0x7b && expected_site[2] == cockpit_sector_object &&
                  expected_site[3] == 0x00,
              "cmp dword [ebx+0x54],0: EBX is the cockpit");
static_assert(expected_site[4] == 0x0f && expected_site[5] == 0x84 &&
                  continuation_rva + le32(expected_site + rel32_offset) == branch_target_rva,
              "je rel32 to the branch target");
static_assert(expected_seam_window[first_camera_load_rva - seam_window_rva + 2] == cockpit_camera,
              "mov ecx,[ebx+0x58] after the seam");
static_assert(expected_target_window[0] == 0x8b && expected_target_window[1] == 0xcb, "mov ecx,ebx at the target");
// The module base is 64 KB aligned, so the site's qword geometry is its RVA's.
static_assert((site_rva & 7u) + 5u > 8u, "the five-byte jump crosses an aligned 8-byte word (plain write)");
static_assert(updater_rva < seam_window_rva && branch_target_rva + target_window_length <= updater_end_rva,
              "both windows inside the cockpit updater");
}
