#pragma once
#include <cstdint>
#include <cstring>

// SETA and manual flight (X3M_SETA_MANUAL_FLIGHT; docs/architecture/seta-manual-flight.md, engine facts in
// docs/reverse-engineering/seta-manual-flight.md): the engine sites, the byte windows the installer compares whole, and
// the per-axis decision of the controller stubs, as pure code the host test compiles
// (verification/probe/seta_manual_flight_host.cpp) and the native stub fixture executes
// (verification/probe/seta_manual_flight_stub_fixture.cpp). No Windows header, no allocation; the decision uses SSE2
// doubles (the DLL's -mfpmath=sse) and never x87. verification/analysis/test_seta_manual_flight.py pins every window
// and site against X3AP.exe.
namespace x3m::seta_manual_flight::core {
// ---- engine globals and fields (engine-effects.md section 8, seta-manual-flight.md sections 1, 3, 5) ----
constexpr std::uint32_t cfg_ptr_va = 0x00606f34; // cfg = *(u32*)0x00606f34
constexpr unsigned cfg_factor = 0xcc, cfg_governor = 0xd0, cfg_game_ms = 0x714; // 16.16 warp, 16.16 governor, game ms
constexpr std::uint32_t mouse_type_x_va = 0x00608e40, mouse_type_y_va = 0x00608e44; // ship-view mouse axis types
// The pair the dispatcher 0x0040fec0 reads instead while the caller's view flag ([esp+0xc], read at 0x00411b3c) is set;
// types 1..3 there feed the ship records too.
constexpr std::uint32_t mouse_turret_x_va = 0x00608e48, mouse_turret_y_va = 0x00608e4c;
constexpr unsigned record_target = 0x24, ship_yaw_rate = 0x24; // record command (binary angle / game s), obj+0x24
constexpr std::int32_t factor_one = 0x10000;
// The effective rate R = (cfg+0xcc * cfg+0xd0 + 0x8000) >> 16 drives the game clock; the scale needs R / 65536 = k.
// The scale applies only while the game clock runs faster than real time, R > 0x10000 (k > 1). The governor is
// 0.3..1.0, so during the SETA ramp under load R can be below 1.0 with the warp above it (factor 1.5 x governor 0.4:
// k = 0.6); scaling then would raise the target above the ship's clamped maximum (1/k) and the step (1/k^2), so those
// frames take the original path. An R past int32 is not a state this engine produces and is left alone too.
constexpr std::int64_t rate_min = 0x10001, rate_max = 0x7fffffff;

// ---- part A: the input stop (seta-manual-flight.md section 2) ----
// 0x0041210d mov edx,[cfg]; 0x00412113 cmp [edx+0xcc],0x10000; 0x0041211d je 0x00412136 -> jmp 0x00412136 (one
// byte); 0x0041211f..0x00412135 the StopFastForward call; 0x00412136 the autopilot-cancel block (join).
struct Patch {
    const char* name;
    std::uint32_t va;
    unsigned char original[1];
    unsigned length;
    unsigned char patched;
};
constexpr Patch stop_patch = {"seta_manual_flight_stop", 0x0041211d, {0x74}, 1, 0xeb};
constexpr std::uint32_t stop_compare_va = 0x00412113; // the 12 bytes cmp + je, inside stop_block
constexpr unsigned stop_compare_length = 12;

// ---- part B: the controller spans and the strafe step loads (sections 3 and 5) ----
// Rotation: `mov eax,[0x00606f34]; fimul [eax+0x714]; mov ecx,[rec+0x24]` (14 bytes, three whole position-independent
// instructions, entered only at the first byte). In: st0 = accel; yaw/pitch EDI = record, ESI = ship; roll ESI =
// record, EDI = ship. Out: st0 = accel * game ms (the engine's next instruction multiplies by 0.001), ECX = target,
// EAX = cfg. EDX and EFLAGS dead (sub esp,8 follows; 0x0040e9a0 never reads EDX).
// Strafe: `fild [esi+0x714]` (6 bytes, one whole instruction) with ESI = cfg; the per-frame strafe limit
// 20000 * ms * 0.001 is computed from it, so scaling the loaded ms by 1/k gives clamp(command, +-20 * game_ms / k).
enum Axis : unsigned { yaw = 0, pitch = 1, roll = 2, strafe_y = 3, strafe_x = 4, axis_count = 5 };
struct Site {
    const char* name;
    std::uint32_t va;
    unsigned char bytes[16];
    unsigned length;
    std::uint32_t record; // the record the stub's register must hold (0: none checked)
};
constexpr Site sites[axis_count] = {
    {"seta_manual_flight_yaw", 0x0040ebde, {0xa1, 0x34, 0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4f, 0x24}, 14, 0x00608e50},
    {"seta_manual_flight_pitch", 0x0040ee3e, {0xa1, 0x34, 0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4f, 0x24}, 14, 0x00608e78},
    {"seta_manual_flight_roll", 0x0040f04d, {0xa1, 0x34, 0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4e, 0x24}, 14, 0x00608ea0},
    {"seta_manual_flight_strafe_y", 0x00411e0c, {0xdb, 0x86, 0x14, 0x07, 0x00, 0x00}, 6, 0},
    {"seta_manual_flight_strafe_x", 0x00411eb2, {0xdb, 0x86, 0x14, 0x07, 0x00, 0x00}, 6, 0},
};
constexpr const char* axis_names[axis_count] = {"yaw", "pitch", "roll", "strafe_y", "strafe_x"};
// The mouse type whose delta feeds the axis's controller directly (0x0040fec0 jump table 0x00410024: type 1 -> yaw
// record 0x00608e50, 2 -> pitch 0x00608e78, 3 -> roll 0x00608ea0; 4/5 feed the turret records, which no stub scales).
// Those delta modes divide by the game step and are already warp-invariant (section 4): the axis keeps its original
// behaviour while any of the four mouse type words (ship-view x/y, view-flag x/y) has that type.
constexpr std::uint32_t mouse_delta_type[axis_count] = {1, 2, 3, 0, 0};

// ---- the installer's comparison: every window read whole (engine_patch::verify_bytes caps at 16 bytes: Run 189) ----
// Each window holds a claim and the code its register and x87 contract depends on: the stop block from the cfg load to
// the join; each controller from the accel load before the join through the call of the slew 0x0040e9a0; both strafe
// blocks through their ftol calls; the mouse dispatcher's type load and the first five jump-table entries.
// stop_block 0x0041210d, 48 bytes
constexpr unsigned char stop_block_bytes[48] = {
    0x8b, 0x15, 0x34, 0x6f, 0x60, 0x00, 0x81, 0xba, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x74, 0x17, 0x53, 0x6a, 0x01, 0x68, 0x1c, 0x5e, 0x55, 0x00, 0x68, 0x96, 0x00, 0x00, 0x00, 0x53,
    0x53, 0xe8, 0x8d, 0xd3, 0x08, 0x00, 0x83, 0xc4, 0x18, 0xf7, 0x47, 0x40, 0x00, 0x00, 0x08, 0x00,
};
// yaw_controller 0x0040ebd0, 58 bytes
constexpr unsigned char yaw_controller_bytes[58] = {
    0xd8, 0x0d, 0xe0, 0x54, 0x56, 0x00, 0xeb, 0x06, 0xd9, 0x05, 0xa4, 0x56, 0x56, 0x00, 0xa1, 0x34,
    0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4f, 0x24, 0x83, 0xec, 0x08, 0xdc,
    0x0d, 0x00, 0x55, 0x56, 0x00, 0xd9, 0x07, 0xd9, 0x5c, 0x24, 0x08, 0xd9, 0x54, 0x24, 0x04, 0xd9,
    0x1c, 0x24, 0x51, 0x8b, 0xcf, 0xe8, 0x96, 0xfd, 0xff, 0xff,
};
// pitch_controller 0x0040ee30, 58 bytes
constexpr unsigned char pitch_controller_bytes[58] = {
    0xd8, 0x0d, 0xe0, 0x54, 0x56, 0x00, 0xeb, 0x06, 0xd9, 0x05, 0xa4, 0x56, 0x56, 0x00, 0xa1, 0x34,
    0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4f, 0x24, 0x83, 0xec, 0x08, 0xdc,
    0x0d, 0x00, 0x55, 0x56, 0x00, 0xd9, 0x07, 0xd9, 0x5c, 0x24, 0x08, 0xd9, 0x54, 0x24, 0x04, 0xd9,
    0x1c, 0x24, 0x51, 0x8b, 0xcf, 0xe8, 0x36, 0xfb, 0xff, 0xff,
};
// roll_controller 0x0040f03f, 58 bytes
constexpr unsigned char roll_controller_bytes[58] = {
    0xd8, 0x0d, 0xe0, 0x54, 0x56, 0x00, 0xeb, 0x06, 0xd9, 0x05, 0xa4, 0x56, 0x56, 0x00, 0xa1, 0x34,
    0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4e, 0x24, 0x83, 0xec, 0x08, 0xdc,
    0x0d, 0x00, 0x55, 0x56, 0x00, 0xd9, 0x06, 0xd9, 0x5c, 0x24, 0x0c, 0xd9, 0x54, 0x24, 0x04, 0xd9,
    0x1c, 0x24, 0x51, 0x8b, 0xce, 0xe8, 0x27, 0xf9, 0xff, 0xff,
};
// strafe_block 0x00411dd0, 331 bytes
constexpr unsigned char strafe_block_bytes[331] = {
    0xd9, 0x05, 0x50, 0x56, 0x56, 0x00, 0x83, 0x3d, 0x84, 0x8f, 0x60, 0x00, 0x00, 0xdd, 0x05, 0x00,
    0x55, 0x56, 0x00, 0x8b, 0x35, 0x34, 0x6f, 0x60, 0x00, 0x75, 0x09, 0xd9, 0xee, 0x33, 0xc0, 0xe9,
    0x8b, 0x00, 0x00, 0x00, 0x83, 0x3d, 0x80, 0x8f, 0x60, 0x00, 0x00, 0x75, 0x09, 0xd9, 0xee, 0x33,
    0xc0, 0xe9, 0x79, 0x00, 0x00, 0x00, 0xdb, 0x05, 0x84, 0x8f, 0x60, 0x00, 0xdb, 0x86, 0x14, 0x07,
    0x00, 0x00, 0xd8, 0xcb, 0xd8, 0xca, 0xd8, 0xf1, 0xdb, 0x05, 0x80, 0x8f, 0x60, 0x00, 0xd8, 0xf2,
    0xd9, 0xee, 0xd8, 0xd1, 0xdf, 0xe0, 0xf6, 0xc4, 0x05, 0x7a, 0x13, 0xd9, 0xc9, 0xd8, 0xd2, 0xdf,
    0xe0, 0xf6, 0xc4, 0x05, 0x7a, 0x04, 0xdd, 0xda, 0xeb, 0x32, 0xdd, 0xd8, 0xeb, 0x2e, 0xd8, 0xd1,
    0xdf, 0xe0, 0xf6, 0xc4, 0x41, 0x75, 0x1d, 0xd9, 0xc9, 0xd9, 0xe0, 0xd8, 0xd2, 0xdf, 0xe0, 0xf6,
    0xc4, 0x05, 0x7a, 0x08, 0xdd, 0xda, 0xd9, 0xc9, 0xd9, 0xe0, 0xeb, 0x0e, 0xdd, 0xd8, 0xd9, 0xc9,
    0xd9, 0xe0, 0xeb, 0x06, 0xdd, 0xd9, 0xdd, 0xd9, 0xd9, 0xc0, 0xd9, 0xc9, 0xd9, 0xca, 0xc7, 0x05,
    0x6c, 0x8f, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0xde, 0xc9, 0xe8, 0x51, 0x97, 0x11, 0x00, 0x83,
    0x3d, 0x58, 0x8f, 0x60, 0x00, 0x00, 0x89, 0x44, 0x24, 0x10, 0x75, 0x0d, 0xdd, 0xd8, 0x33, 0xc0,
    0xdd, 0xd9, 0xdd, 0xd8, 0xe9, 0x82, 0x00, 0x00, 0x00, 0x83, 0x3d, 0x54, 0x8f, 0x60, 0x00, 0x00,
    0x75, 0x0a, 0xdd, 0xd8, 0x33, 0xc0, 0xdd, 0xd9, 0xdd, 0xd8, 0xeb, 0x6f, 0xdb, 0x05, 0x58, 0x8f,
    0x60, 0x00, 0xdb, 0x86, 0x14, 0x07, 0x00, 0x00, 0xde, 0xcc, 0xd9, 0xcb, 0xde, 0xca, 0xd9, 0xc9,
    0xd8, 0xf2, 0xdb, 0x05, 0x54, 0x8f, 0x60, 0x00, 0xd8, 0xf3, 0xd8, 0xd2, 0xdf, 0xe0, 0xf6, 0xc4,
    0x41, 0x75, 0x0f, 0xdd, 0xda, 0xd8, 0xd1, 0xdf, 0xe0, 0xf6, 0xc4, 0x41, 0x74, 0x2a, 0xdd, 0xd9,
    0xeb, 0x28, 0xd8, 0xd2, 0xdf, 0xe0, 0xf6, 0xc4, 0x05, 0x7a, 0x1b, 0xdd, 0xda, 0xd9, 0xc9, 0xd9,
    0xe0, 0xd8, 0xd1, 0xdf, 0xe0, 0xf6, 0xc4, 0x05, 0x7a, 0x06, 0xdd, 0xd9, 0xd9, 0xe0, 0xeb, 0x0a,
    0xdd, 0xd8, 0xd9, 0xe0, 0xeb, 0x04, 0xdd, 0xd8, 0xdd, 0xd8, 0xde, 0xc9, 0xc7, 0x05, 0x40, 0x8f,
    0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0xe8, 0xb5, 0x96, 0x11, 0x00,
};
// mouse_dispatch 0x0040fec0, 105 bytes
constexpr unsigned char mouse_dispatch_bytes[105] = {
    0x83, 0x7c, 0x24, 0x04, 0x00, 0x53, 0x56, 0x8b, 0xf1, 0x74, 0x09, 0x8b, 0x1c, 0x85, 0x48, 0x8e,
    0x60, 0x00, 0xeb, 0x07, 0x8b, 0x1c, 0x85, 0x40, 0x8e, 0x60, 0x00, 0x85, 0xf6, 0x0f, 0x84, 0x19,
    0x01, 0x00, 0x00, 0x8d, 0x43, 0xff, 0x83, 0xf8, 0x0f, 0x0f, 0x87, 0x0d, 0x01, 0x00, 0x00, 0xff,
    0x24, 0x85, 0x24, 0x00, 0x41, 0x00, 0x57, 0x56, 0xbe, 0x50, 0x8e, 0x60, 0x00, 0xe8, 0x9e, 0xeb,
    0xff, 0xff, 0xe9, 0xf5, 0x00, 0x00, 0x00, 0x57, 0x56, 0xbe, 0x78, 0x8e, 0x60, 0x00, 0xe8, 0xad,
    0xed, 0xff, 0xff, 0xe9, 0xe4, 0x00, 0x00, 0x00, 0x57, 0x56, 0xbe, 0xa0, 0x8e, 0x60, 0x00, 0xe8,
    0xdc, 0xef, 0xff, 0xff, 0xe9, 0xd3, 0x00, 0x00, 0x00,
};
// mouse_table 0x00410024, 20 bytes
constexpr unsigned char mouse_table_bytes[20] = {
    0xf6, 0xfe, 0x40, 0x00, 0x07, 0xff, 0x40, 0x00, 0x18, 0xff, 0x40, 0x00, 0x29, 0xff, 0x40, 0x00,
    0x3d, 0xff, 0x40, 0x00,
};

struct Window {
    const char* name;
    std::uint32_t va;
    const unsigned char* bytes;
    unsigned length;
    unsigned part; // 1: needed by part A (mode >= 1), 2: by part B (mode 2)
};
constexpr Window windows[] = {{"stop_block", 0x0041210d, stop_block_bytes, sizeof stop_block_bytes, 1},
                              {"yaw_controller", 0x0040ebd0, yaw_controller_bytes, sizeof yaw_controller_bytes, 2},
                              {"pitch_controller", 0x0040ee30, pitch_controller_bytes, sizeof pitch_controller_bytes, 2},
                              {"roll_controller", 0x0040f03f, roll_controller_bytes, sizeof roll_controller_bytes, 2},
                              {"strafe_block", 0x00411dd0, strafe_block_bytes, sizeof strafe_block_bytes, 2},
                              {"mouse_dispatch", 0x0040fec0, mouse_dispatch_bytes, sizeof mouse_dispatch_bytes, 2},
                              {"mouse_table", 0x00410024, mouse_table_bytes, sizeof mouse_table_bytes, 2}};
constexpr unsigned window_count = sizeof windows / sizeof windows[0];
constexpr unsigned window_max = sizeof strafe_block_bytes;
constexpr bool contains(const Window& w, std::uint32_t va, unsigned length) {
    return w.va <= va && va + length <= w.va + w.length;
}
constexpr bool span_matches(const Window& w, std::uint32_t va, const unsigned char* bytes, unsigned length) {
    for (unsigned i = 0; i < length; ++i)
        if (w.bytes[va - w.va + i] != bytes[i]) return false;
    return true;
}
// Every claimed span lies inside its window with the same bytes, so the window compare covers the claim's own check.
constexpr bool sites_inside_windows() {
    if (!contains(windows[0], stop_patch.va, 1) || !span_matches(windows[0], stop_patch.va, stop_patch.original, 1) ||
        !contains(windows[0], stop_compare_va, stop_compare_length))
        return false;
    for (unsigned a = 0; a < axis_count; ++a) {
        const Window& w = windows[a < strafe_y ? 1 + a : 4];
        if (!contains(w, sites[a].va, sites[a].length) || !span_matches(w, sites[a].va, sites[a].bytes, sites[a].length))
            return false;
    }
    return true;
}
static_assert(sites_inside_windows(), "every claim lies inside its compared window");
constexpr unsigned shown_bytes = 8; // the refusal row's expected=/got= prefix of the window
struct Mismatch {
    const Window* window = nullptr; // the first window that differs or cannot be read; null when all match
    bool readable = false;
    unsigned shown = 0;
    unsigned char got[shown_bytes]{};
};
// Read: bool(std::uint32_t va, void* out, unsigned n) reads code bytes. Only the windows of the parts the mode installs.
template <class Read> bool compare_windows(Read&& read, unsigned mode, Mismatch& out) noexcept {
    out = Mismatch{};
    for (const Window& w : windows) {
        if (w.part > mode) continue;
        unsigned char actual[window_max];
        const bool readable = read(w.va, actual, w.length);
        if (readable && !std::memcmp(actual, w.bytes, w.length)) continue;
        out.window = &w;
        out.readable = readable;
        out.shown = w.length < shown_bytes ? w.length : shown_bytes;
        if (readable) std::memcpy(out.got, actual, out.shown);
        return false;
    }
    return true;
}

// ---- the per-axis decision ----
enum Skip : unsigned { applied = 0, warp_off, rate_out_of_range, mouse_delta, contract, skip_count };
constexpr const char* skip_names[skip_count] = {"applied", "warp_off", "rate_out_of_range", "mouse_delta", "contract"};
struct Inputs {
    std::int32_t factor = factor_one, governor = factor_one, game_ms = 0, target = 0;
    std::uint32_t mouse_x = 0, mouse_y = 0, mouse_turret_x = 0, mouse_turret_y = 0;
};
struct Out {
    double step_factor = 0;         // rotation: replaces fimul [cfg+0x714]: game_ms / k^2
    double inv_k = 1;               // strafe: multiplies the loaded game ms: 1 / k
    std::int32_t target = 0;        // rotation: round(target / k), half away from zero
    std::int32_t rate = factor_one; // R (int32 after the range check: an int64 -> double conversion is x87 code on i686)
};
constexpr std::int64_t effective_rate(std::int32_t factor, std::int32_t governor) {
    return (std::int64_t(factor) * governor + 0x8000) >> 16;
}
inline std::int32_t round_scaled(std::int32_t value, double scale) noexcept {
    double q = double(value) * scale;
    if (q > 2147483647.0) q = 2147483647.0;
    if (q < -2147483647.0) q = -2147483647.0;
    return q >= 0 ? std::int32_t(q + 0.5) : -std::int32_t(-q + 0.5);
}
inline Skip decide(unsigned axis, const Inputs& in, Out& out) noexcept {
    out = Out{};
    if (axis >= axis_count) return contract;
    if (in.factor <= factor_one) return warp_off; // the gate: SETA engaged (the engine's own compare is == 0x10000)
    const std::uint32_t delta = mouse_delta_type[axis];
    // conservative: either pair (the ship-view one or the one the view flag selects) may be the one read this frame
    if (delta && (in.mouse_x == delta || in.mouse_y == delta || in.mouse_turret_x == delta || in.mouse_turret_y == delta))
        return mouse_delta;
    const std::int64_t rate = effective_rate(in.factor, in.governor);
    if (rate < rate_min || rate > rate_max) return rate_out_of_range;
    out.rate = std::int32_t(rate);
    out.inv_k = 65536.0 / double(out.rate);
    out.step_factor = double(in.game_ms) * out.inv_k * out.inv_k;
    out.target = round_scaled(in.target, out.inv_k);
    return applied;
}
// The engine reads of one stub call, on the main thread at the span, where the original instructions read the same
// cfg pointer, cfg+0x714 and record+0x24 (so these plain loads are as valid as theirs; cfg+0xcc/0xd0 sit in the same
// structure, the mouse types are globals). The record register is checked against the axis's record first (another
// value: contract, original behaviour). 32-bit process only.
inline std::uint32_t load32(std::uint32_t va) noexcept {
    std::uint32_t v;
    std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(va)), 4);
    return v;
}
inline Skip evaluate(unsigned axis, std::uint32_t record, Inputs& in, Out& out) noexcept {
    out = Out{};
    in = Inputs{};
    if (axis >= axis_count || (sites[axis].record && record != sites[axis].record)) return contract;
    const std::uint32_t cfg = load32(cfg_ptr_va);
    if (!cfg) return contract;
    in.factor = std::int32_t(load32(cfg + cfg_factor));
    in.governor = std::int32_t(load32(cfg + cfg_governor));
    in.game_ms = std::int32_t(load32(cfg + cfg_game_ms));
    in.target = sites[axis].record ? std::int32_t(load32(record + record_target)) : 0;
    in.mouse_x = load32(mouse_type_x_va);
    in.mouse_y = load32(mouse_type_y_va);
    in.mouse_turret_x = load32(mouse_turret_x_va);
    in.mouse_turret_y = load32(mouse_turret_y_va);
    return decide(axis, in, out);
}
}
