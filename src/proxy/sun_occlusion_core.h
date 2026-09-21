#pragma once
// Partial sun occlusion, step 1: the pure part (docs/architecture/sun-partial-occlusion.md;
// sites, layouts and formulas from docs/reverse-engineering/lens-flare-visibility.md,
// sections 11-16). No Windows/D3D dependency, no allocation, no engine call: the
// host double verification/probe/sun_occlusion_host.cpp drives exactly this header.
//
// Two halves with different CPU contracts:
//   * decide(), latch(), main_view(): integer only. They run inside the probe
//     thunk at 0x00471630, in the middle of the engine's record loop, where the
//     x87 stack and MXCSR belong to the engine.
//   * footprint(), smoothing_alpha(), classify_blend(): float (SSE2 in the DLL).
//     They run inside the lens bracket under a full CPU-state boundary.
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace x3m::sun_occlusion::core {
// ---- Sites (preferred base 0x00400000; X3AP.exe SHA-256 fdbf3418...f8ab) ----
constexpr std::uintptr_t probe_site_va = 0x00471630, probe_target_va = 0x00488720;
constexpr std::uintptr_t lens_site_va = 0x00472491, lens_target_va = 0x0047e6e0;
// Whole instructions around the probe call, from 0x0047162a: mov ecx,[esp+0x14]; push ecx;
// push esi; CALL; add esp,8; test eax,eax; jz +3; mov [esi+0x30],ebp.
constexpr std::uintptr_t probe_context_va = 0x0047162a;
constexpr unsigned char probe_context[] = {0x8b,0x4c,0x24,0x14, 0x51, 0x56, 0xe8,0xeb,0x70,0x01,0x00,
                                           0x83,0xc4,0x08, 0x85,0xc0, 0x74,0x03, 0x89,0x6e,0x30};
// From 0x00472490: push esi; CALL; mov ecx,[0x608518]; mov eax,[ecx+0x6304]; add esp,4.
constexpr std::uintptr_t lens_context_va = 0x00472490;
constexpr unsigned char lens_context[] = {0x56, 0xe8,0x4a,0xc2,0x00,0x00, 0x8b,0x0d,0x18,0x85,0x60,0x00,
                                          0x8b,0x81,0x04,0x63,0x00,0x00, 0x83,0xc4,0x04};
// The probe's prologue and its three early tests, 0x00488720..0x004887b3 (148 bytes): the
// override replicates exactly these, so they are compared whole. FNV-1a 64 of the span.
constexpr std::uintptr_t probe_gates_va = 0x00488720;
constexpr unsigned probe_gates_length = 0x94;
constexpr std::uint64_t probe_gates_fnv1a = 0x0e5f40888a926996ull;
// push ebp; mov ebp,esp; and esp,-16; sub esp,0x44 (entry boundary; cdecl, plain ret).
constexpr unsigned char probe_entry[] = {0x55, 0x8b,0xec, 0x83,0xe4,0xf0, 0x83,0xec,0x44};
// mov eax,[0x608518]; push ebx; push ebp; mov ebp,[esp+0xc] (one stack argument, caller cleans).
constexpr unsigned char lens_entry[] = {0xa1,0x18,0x85,0x60,0x00, 0x53, 0x55, 0x8b,0x6c,0x24,0x0c};
// The X3M_SUBMIT_PHASES diagnostic claims 0x00472490 (six bytes: push esi + this call).
constexpr std::uintptr_t conflicting_submit_phase_va = 0x00472490;

// ---- Engine layout ----
constexpr std::uintptr_t config_global_va = 0x00606f34;   // *this + 0xfc = VideoD3DFlags
constexpr unsigned config_video_flags = 0xfc;
constexpr std::uint32_t video_flag_flares = 0x8000;
constexpr std::uintptr_t mode_global_va = 0x00606f38;     // *this + 0x28/+0x2c = fallback view scale pair
constexpr std::uintptr_t cockpit_registry_va = 0x00608504;
constexpr unsigned record_view = 0x08, record_accumulator = 0x10, record_x = 0x20, record_y = 0x24,
                   record_visible = 0x30, record_size = 0x34, record_group = 0x38;
constexpr unsigned view_flags = 0x270, view_rect_top = 0x288, view_rect_bottom = 0x28c, view_rect_left = 0x290,
                   view_rect_right = 0x294, view_fov = 0x298, view_scale_x = 0x300, view_scale_y = 0x304;
constexpr std::uint32_t view_flag_probe_hidden = 0x8000000, view_flag_sector_camera = 0x10000;

// ---- The probe decision ----
enum Decision : int { Visible = 0, Hidden = 1, Original = 2 };
struct ProbeInputs {
    std::uintptr_t record_owner = 0, view = 0, main_view = 0;
    bool ready = false;                 // the GPU side is healthy for this record (Ready below)
    std::uint32_t video_flags = 0, view_flags = 0;
    std::int32_t x = 0, y = 0;          // record +0x20 / +0x24
    std::int32_t top = 0, bottom = 0, left = 0, right = 0;
};
// The main-view guard, then the three vanilla tests in vanilla order (0x00488742, 0x00488763..
// 0x004887a2, 0x004887a8). Signed compares with inclusive bounds (jl / jg); the 0x8000 offsets
// wrap as the engine's lea / sub do. Whatever vanilla hides for one of these reasons stays hidden.
inline Decision decide(const ProbeInputs& in) noexcept {
    if (!in.ready || !in.main_view || in.view != in.main_view || in.record_owner != in.view) return Original;
    if (!(in.video_flags & video_flag_flares)) return Hidden;
    const std::int32_t sx = std::int32_t(std::uint32_t(in.x) + 0x8000u);
    if (sx < in.left || sx > in.right) return Visible;
    const std::int32_t sy = std::int32_t(0x8000u - std::uint32_t(in.y));
    if (sy < in.top || sy > in.bottom) return Visible;
    if (in.view_flags & view_flag_probe_hidden) return Hidden;
    return Visible;
}

// ---- Frame readiness ----
// One record of the main view per frame, the same one as in the previous frame, with the
// visibility pass having run for it in the previous frame. `begin_frame` is Present; `probe`
// is one main-view probe call; `pass_ran` is the lens bracket's report.
struct Ready {
    std::uintptr_t record = 0, previous_record = 0;
    unsigned records = 0, previous_records = 0;
    bool pass_ok = false, previous_pass_ok = false;
    bool blocked = false;               // sticky for the process, across reset(): a lens draw that can never carry the fraction was seen
    bool answered = false;              // the override answered in this frame
    void begin_frame() noexcept {
        previous_record = record; previous_records = records; previous_pass_ok = pass_ok;
        record = 0; records = 0; pass_ok = false; answered = false;
    }
    void reset() noexcept { const bool keep = blocked; *this = Ready{}; blocked = keep; } // device Reset: the refusal that blocked is a property of the program or the material, not of the device generation
    // Registers the probe call; true when the override may answer for it.
    bool probe(std::uintptr_t rec, bool enabled) noexcept {
        if (rec != record) { ++records; if (records == 1) record = rec; }
        return enabled && !blocked && records == 1 && rec == record && previous_records == 1 && previous_record == rec && previous_pass_ok;
    }
    bool single() const noexcept { return records == 1 && record != 0; }
};

// ---- A frame whose pass did not run ----
// A transient skip (a query open, a block recording, the scene end late) must not make the chain flicker: the
// last smoothed fraction stays in use for at most hold_limit consecutive frames, the override keeps answering,
// and only then the fraction is dropped and the engine's probe decides again. A failed pass (the texture may be
// half written), another record or no fraction at all is never held.
constexpr unsigned hold_limit = 4;
struct Hold {
    unsigned frames = 0;
    bool exhausted = false;             // dropped: nothing is held again before a pass has run
    // `ran`: the pass ran in this frame. `holdable`: it was skipped untouched and the fraction of the same record
    // is still valid. True when lens draws may use the fraction in this frame.
    bool step(bool ran, bool holdable) noexcept {
        if (ran) { frames = 0; exhausted = false; return true; }
        if (holdable && !exhausted && frames < hold_limit) { ++frames; return true; }
        exhausted = true;
        return false;
    }
};

// ---- What the thunk latches for the pass (integers only) ----
struct Latch {
    std::uintptr_t record = 0;
    std::int32_t x = 0, y = 0, size = 0, accumulator = 0;
    std::int32_t top = 0, bottom = 0, left = 0, right = 0, scale_x = 0;
    std::uint32_t fov = 0;
    bool valid = false;
};

// ---- Record -> back-buffer footprint (RE note section 11) ----
struct Footprint { float u = .5f, v = .5f, radius_u = 0.f, radius_v = 0.f; bool radius_known = false, valid = false; };
constexpr float radius_min_pixels = 1.5f, radius_max_u = .25f;
// u = 0.5 + x / 65536, v = 0.5 - y / 65536. record+0x34 = ((ratio * avg) >> 16) * acc / 200 with
// ratio = r / z in 16.16 and avg the rect's mean normalised extent, so the disc's half-width as a
// fraction of the back-buffer width is ratio * cot(fov / 2) / (2 Wn) * rect_width, the same cot and
// Wn the position uses. Pixels are square: radius_v = radius_u * width / height. A record on its
// first frame (accumulator 0, size 0) has no radius: radius_known stays false and the caller
// supplies the last known or the configured one.
inline Footprint footprint(const Latch& in, unsigned width, unsigned height, float radius_scale) noexcept {
    Footprint out;
    if (!in.valid || !width || !height) return out;
    out.u = .5f + float(in.x) / 65536.f; out.v = .5f - float(in.y) / 65536.f;
    out.valid = std::isfinite(out.u) && std::isfinite(out.v);
    const float rect_w = float(in.right - in.left) / 65536.f, rect_h = float(in.bottom - in.top) / 65536.f;
    const float avg = .5f * (rect_w + rect_h), wn = float(in.scale_x) / 65536.f;
    const std::uint32_t half = (in.fov >> 1) & 0xffffu;
    if (in.accumulator <= 0 || in.accumulator > 200 || in.size <= 0 || !(avg > 0.f) || !(wn > 0.f) || !(rect_w > 0.f) || half == 0 || half >= 0x4000u) return out;
    const float angle = float(half) * (6.2831853f / 65536.f);
    const float cot = std::cos(angle) / std::sin(angle);
    const float ratio = float(in.size) / 65536.f * (200.f / float(in.accumulator)) / avg;
    float r = ratio * cot / (2.f * wn) * rect_w * radius_scale;
    if (!std::isfinite(r) || !(r > 0.f)) return out;
    const float floor_u = radius_min_pixels / float(width);
    r = r < floor_u ? floor_u : (r > radius_max_u ? radius_max_u : r);
    out.radius_u = r; out.radius_v = r * float(width) / float(height); out.radius_known = true;
    return out;
}
inline void apply_radius(Footprint& f, float radius_u, unsigned width, unsigned height) noexcept {
    const float floor_u = radius_min_pixels / float(width ? width : 1u);
    const float r = radius_u < floor_u ? floor_u : (radius_u > radius_max_u ? radius_max_u : radius_u);
    f.radius_u = r; f.radius_v = height ? r * float(width) / float(height) : r;
}
// 1 - exp(-dt / tau), dt clamped to [0, 250 ms]; tau = 80 ms (design, "Visibility pass").
constexpr float smoothing_tau_seconds = .080f;
inline float smoothing_alpha(double dt_seconds) noexcept {
    if (!(dt_seconds > 0.)) return 0.f;
    if (dt_seconds > .25) dt_seconds = .25;
    return 1.f - std::exp(-float(dt_seconds) / smoothing_tau_seconds);
}

// ---- How a lens draw carries f exactly (raw D3DBLEND / D3DCMPFUNC codes) ----
// out = src * S + dst * D. Scaling the emitted light by f and returning the background by the
// same amount is exact when: S = SRCALPHA and D in {ONE, INVSRCALPHA, ZERO-free}: scale alpha;
// S = ONE and D in {ONE, INVSRCCOLOR}: scale rgb; S = ONE and D = INVSRCALPHA (premultiplied):
// scale both. Blending off, another factor or op, a separate alpha blend that a scaled alpha
// would change visibly (irrelevant: the 8-bit target's alpha is not read back by the lens
// scene), sRGB write, or an alpha test that a scaled alpha would cross: Refuse.
enum class Scale : std::uint8_t { Refuse = 0, Rgb = 1, Alpha = 2, Both = 3 };
constexpr std::uint32_t blend_one = 2, blend_srccolor = 3, blend_invsrccolor = 4, blend_srcalpha = 5, blend_invsrcalpha = 6;
constexpr std::uint32_t blendop_add = 1, cmp_greater = 5, cmp_greaterequal = 7, cmp_always = 8;
// The alpha test keeps alpha > ref (or >=). A scaled alpha drops fragments whose contribution was
// at most ref / 255 of the source colour; admitted up to this reference, refused above it.
constexpr std::uint32_t alpha_ref_admitted = 8;
// Fixed-function fog is applied after the pixel shader (fog colour lerped in before the blend), so a
// scaled shader output is not a scaled draw and f = 0 would still show fog colour: refused.
struct BlendState { std::uint32_t enable = 0, src = 0, dst = 0, op = 0, srgb_write = 0, alpha_test = 0, alpha_ref = 0, alpha_func = 0, fog = 0; };
inline Scale classify_blend(const BlendState& s) noexcept {
    if (!s.enable || s.op != blendop_add || s.srgb_write || s.fog) return Scale::Refuse;
    Scale scale = Scale::Refuse;
    if (s.src == blend_srcalpha && (s.dst == blend_one || s.dst == blend_invsrcalpha)) scale = Scale::Alpha;
    else if (s.src == blend_one && (s.dst == blend_one || s.dst == blend_invsrccolor)) scale = Scale::Rgb;
    else if (s.src == blend_one && s.dst == blend_invsrcalpha) scale = Scale::Both;
    if (scale == Scale::Refuse || scale == Scale::Rgb || !s.alpha_test || s.alpha_func == cmp_always) return scale;
    const bool keeps_high = s.alpha_func == cmp_greater || s.alpha_func == cmp_greaterequal;
    return keeps_high && s.alpha_ref <= alpha_ref_admitted ? scale : Scale::Refuse;
}

// ---- The main 3D view: *(cockpit + 0x58) of the active control cockpit ----
// The registry walk of sector_background.h (layout proof: sector-fog.md section 11): registry
// *0x00608504, handle registry+0x10, power-of-two bucket array, {next, handle, cockpit} links.
// Additionally requires the sector-camera marker view+0x270 & 0x10000 that the cockpit update
// sets every frame (0x0042157c). 0 on any doubt: the override then never answers.
template<class Read> std::uintptr_t main_view(Read& read) noexcept {
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
        if (row[1] == handle) { cockpit = row[2]; break; }
        link = row[0];
    }
    if (!word(cockpit, 0x58, camera) || !word(camera, view_flags, flags) || !(flags & view_flag_sector_camera)) return 0;
    return camera;
}
inline std::uint64_t fnv1a(const unsigned char* p, std::size_t n) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
} // namespace x3m::sun_occlusion::core
