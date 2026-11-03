// Native fixture of the FL SETA manual flight (src/engine/fl/seta_manual_flight_fl.cpp, seta_manual_flight_sites_fl.h;
// docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)"): built with -DX3M_EDITION_FL=1
// -DX3M_SETA_MANUAL_FLIGHT_FL_FIXTURE by verification/analysis/test_fl_seta_manual_flight.py (the production module is a
// separate object with that define) and run natively through WSL interop. No game launch, no Wine, nothing written to
// X3FL.exe: the exe passed as argv[1] is mapped read-only into a private 64 KB-aligned image with its relocations
// applied (fl_seta_image.h), so the windows, the stop rows and the five SSE spans are the real relocated bytes.
//
// Covered: the install (mode 1 and 2) and its rows; exact-byte refusal (a flipped byte in each of the 20 windows, a
// relocated dword pointing elsewhere) before any write; read-back faults on a stop row and on each claim rolled back
// (the plain-written pitch row and strafe Y jump included), the late-window refusal; part A executed on the real rows
// (each flag alone: before the install every set flag reaches the stop, after it only the speed flag; the look row's
// JE->JMP); part B through the real spans at every 4-byte ESP skew with DF/flags set and clear: the original path
// against a reference copy of the span (GPRs, EFLAGS, ESP, XMM0-7, x87 CW/stack, MXCSR, LastError), the apply path
// against an independent reference of the scaled step / target / strafe limit (real-time rates checked), the mouse-delta
// and register-contract skips; shutdown restores every byte.
#include "fl_seta_image.h"
#include "fl_seta_harness.h"
#include "../../src/engine/fl/seta_manual_flight_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/seta_manual_flight.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <emmintrin.h>

namespace F = x3m::fl::seta_manual_flight_fl;
namespace core = x3m::seta_manual_flight::core;
namespace smf = x3m::seta_manual_flight;
using std::uint32_t;
namespace x3m::seta_manual_flight {
void fixture_set_base(std::uintptr_t base);
void fixture_fault_row(unsigned row);
void fixture_fault_claim(unsigned axis);
void fixture_reset();
}
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[2048], install_row[2048];
unsigned key_latch_calls = 0;
bool quiet = false;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    if (!std::strncmp(last_row, "seta_manual_flight installed=", 29)) std::memcpy(install_row, last_row, sizeof install_row);
    if (!quiet) std::printf("LOG %s\n", last_row);
}
namespace fl {
std::uintptr_t module_base() {
    return 0; // the fixture binds through fixture_set_base
}
}
namespace seta_restore {
const char* key_latch_enable(bool) {
    ++key_latch_calls;
    return "seta_restore_disabled";
}
}
}

FlImage im;
// The fake cfg the image's cfg slot points at (factor 0xbc, governor 0xc0, game ms 0x704) and a fake ship.
alignas(16) unsigned char cfg[0x800], ship[0x100];
void set_cfg(std::int32_t factor, std::int32_t governor, std::int32_t game_ms) {
    std::memcpy(cfg + F::cfg_factor, &factor, 4);
    std::memcpy(cfg + F::cfg_governor, &governor, 4);
    std::memcpy(cfg + F::cfg_game_ms, &game_ms, 4);
}
bool initialize(const char* mode) {
    SetEnvironmentVariableW(L"X3M_SETA_MANUAL_FLIGHT", nullptr);
    wchar_t wide[4] = {wchar_t(mode[0]), 0};
    SetEnvironmentVariableW(L"X3M_SETA_MANUAL_FLIGHT", wide);
    return smf::initialize();
}
void reset() {
    smf::shutdown();
    smf::fixture_reset();
}
bool image_matches(std::uint32_t rva, unsigned n) {
    return !std::memcmp(im.base + rva, im.pristine.data() + rva, n);
}
bool all_sites_original() {
    bool ok = image_matches(F::stop_rows_rva, F::stop_row_count * F::stop_row_length);
    for (const auto& s : F::sites) ok = ok && image_matches(s.rva, s.length);
    return ok;
}
// Part A on the real rows: enter at the first row with only `flag` set (or none); the stop 0x0d21d0 and the join
// 0x0d21fb carry `jmp h_record`, EAX tells which (the fixture's marker: the stop's `mov eax,[cfg]` never runs).
extern "C" void stop_landing();
extern "C" void join_landing();
asm(".text\n\t.globl _stop_landing\n_stop_landing:\n\tmovl $1,%eax\n\tjmp _h_record\n\t"
    ".globl _join_landing\n_join_landing:\n\tmovl $2,%eax\n\tjmp _h_record\n\t");
constexpr uint32_t flags_rva[F::stop_row_count] = {0x21c6d0, 0x21c6f8, 0x21c720, 0x21c748, 0x21c77c,
                                                   0x21c7b0, 0x21c7dc, 0x21c808, 0x21c840};
unsigned run_rows(int flag) {
    for (uint32_t r : flags_rva) im.u32(r) = 0;
    if (flag >= 0) im.u32(flags_rva[flag]) = 1;
    im.jump(F::stop_rva, reinterpret_cast<void*>(&stop_landing));
    im.jump(F::stop_join_rva, reinterpret_cast<void*>(&join_landing));
    h_sentinels(0x77, false);
    h_in.eax = 0;
    DWORD e = 0;
    h_run(im.at(F::stop_rows_rva), 0, 0x1234, &e);
    im.restore(F::stop_rva, 5);
    im.restore(F::stop_join_rva, 5);
    return h_out.eax;
}
void test_part_a(bool installed) {
    for (int flag = -1; flag < int(F::stop_row_count); ++flag) {
        const unsigned got = run_rows(flag);
        const unsigned want = flag < 0 ? 2 : !installed || flag == int(F::stop_speed_row) ? 1 : 2;
        char d[96];
        std::snprintf(d, sizeof d, "flag=%d installed=%u got=%u want=%u", flag, unsigned(installed), got, want);
        check(got == want, "part_a_route", d);
    }
}
// ---- part B ----
bool same_cpu(const HCpu& a, const HCpu& b, unsigned xmm_skip_mask, const char* label) {
    bool ok = a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edx == b.edx && a.esi == b.esi &&
              a.edi == b.edi && a.ebp == b.ebp && a.esp == b.esp && a.flags == b.flags && a.fpucw == b.fpucw &&
              ((a.fpusw >> 11) & 7) == ((b.fpusw >> 11) & 7) && a.st0 == b.st0 && a.mxcsr == b.mxcsr;
    unsigned xmm_diff = 0;
    for (unsigned i = 0; i < 8; ++i)
        if (!(xmm_skip_mask >> i & 1) && std::memcmp(a.xmm + 16 * i, b.xmm + 16 * i, 16)) xmm_diff |= 1u << i;
    ok = ok && !xmm_diff;
    if (!ok) {
        char d[512];
        std::snprintf(d, sizeof d,
                      "xmm_diff=%02x tops %u/%u eax %08x/%08x ecx %08x/%08x edx %08x/%08x esi %08x/%08x edi %08x/%08x esp %08x/%08x fl %08x/%08x "
                      "cw %04x/%04x mx %08x/%08x st0 %g/%g",
                      xmm_diff, (a.fpusw >> 11) & 7, (b.fpusw >> 11) & 7, a.eax, b.eax, a.ecx, b.ecx, a.edx, b.edx, a.esi, b.esi, a.edi, b.edi, a.esp, b.esp, a.flags,
                      b.flags, a.fpucw, b.fpucw, a.mxcsr, b.mxcsr, a.st0, b.st0);
        check(false, label, d);
        return false;
    }
    check(true, label);
    return true;
}
void set_xmm_float(unsigned reg, float v) {
    std::memcpy(h_in.xmm + 16 * reg, &v, 4);
}
void set_xmm_double(unsigned reg, double v) {
    std::memcpy(h_in.xmm + 16 * reg, &v, 8);
}
float out_float(unsigned reg) {
    float v;
    std::memcpy(&v, h_out.xmm + 16 * reg, 4);
    return v;
}
// Inputs at a span: rotation EDI = record, ESI = ship, XMM0 = double step, XMM2 = current rate, XMM4 = 2^-16; strafe
// ESI = cfg, EDI = ship, XMM0 = float game ms, XMM6 = 20000.0, XMM7 = 0.001; the other lanes and registers sentinels.
void axis_inputs(unsigned axis, std::uint32_t seed, bool flags_set, double step, std::int32_t target, float game_ms) {
    h_sentinels(seed, flags_set);
    if (axis < core::strafe_y) {
        h_in.edi = im.va(F::sites[axis].record_rva);
        h_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
        im.u32(F::sites[axis].record_rva + F::record_target) = std::uint32_t(target);
        set_xmm_double(0, step);
        set_xmm_float(2, 0.25f);
        set_xmm_float(4, 1.0f / 65536.0f);
    } else {
        h_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
        h_in.edi = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
        set_xmm_float(0, game_ms);
        set_xmm_float(6, 20000.0f);
        set_xmm_double(7, 0.001);
    }
}
void* references[core::axis_count];
unsigned original_runs = 0, apply_runs = 0;
void test_original_path(unsigned axis, const char* why) {
    for (unsigned skew = 0; skew < 16; skew += 4)
        for (int f = 0; f < 2; ++f) {
            axis_inputs(axis, axis * 16 + skew + f, f != 0, 0.0123456789, 0x1234567, 16.0f);
            const HCpu in = h_in;
            DWORD e_ref = 0, e_hook = 0;
            h_run(reinterpret_cast<std::uintptr_t>(references[axis]), skew, 0xfeed0000 + skew, &e_ref);
            const HCpu ref = h_out;
            h_in = in;
            h_run(im.at(F::sites[axis].rva), skew, 0xfeed0000 + skew, &e_hook);
            char label[96];
            std::snprintf(label, sizeof label, "original_path %s axis=%u skew=%u flags=%d", why, axis, skew, f);
            same_cpu(h_out, ref, 0, label);
            check(e_ref == 0xfeed0000 + skew && e_hook == e_ref, "original_path_last_error", label);
            ++original_runs;
        }
}
// The independent reference of the apply path (seta.md section 4): k = R / 65536, R = (factor * governor + 0x8000) >> 16;
// rotation XMM3 = float(step / k^2), XMM0 = float(round_half_away(target / k)) * 2^-16; strafe out = float(double(
// float(game_ms * 20000)) * 0.001 / k). Computed under the same MXCSR the stub restores (default rounding here).
double k_of(std::int32_t factor, std::int32_t governor) {
    const long long r = (static_cast<long long>(factor) * governor + 0x8000) >> 16;
    return double(r) / 65536.0;
}
std::int32_t round_away(double v) {
    return v >= 0 ? std::int32_t(std::floor(v + 0.5)) : -std::int32_t(std::floor(-v + 0.5));
}
void test_apply_path(unsigned axis, std::int32_t factor, std::int32_t governor) {
    const double k = k_of(factor, governor);
    for (unsigned skew = 0; skew < 16; skew += 4)
        for (int f = 0; f < 2; ++f) {
            const double step = 0.0421875 * (1 + axis);
            const std::int32_t target = (axis & 1 ? -1 : 1) * (0x00c80000 + 977 * int(axis));
            const float game_ms = 16.0f * float(k);
            axis_inputs(axis, axis * 32 + skew + f, f != 0, step, target, game_ms);
            set_cfg(factor, governor, std::int32_t(game_ms));
            const HCpu in = h_in;
            DWORD e = 0;
            h_run(im.at(F::sites[axis].rva), skew, 0xabc00000 + skew, &e);
            char label[128];
            std::snprintf(label, sizeof label, "apply_path axis=%u factor=0x%08x governor=0x%08x skew=%u flags=%d", axis,
                          unsigned(factor), unsigned(governor), skew, f);
            const unsigned changed = axis < core::strafe_y ? (1u << 0 | 1u << 3) : (1u << 0 | 1u << (axis == core::strafe_y ? 4 : 1));
            HCpu expect = in;
            expect.esp = std::uint32_t(reinterpret_cast<std::uintptr_t>(h_stack + sizeof h_stack - 0x400 - skew));
            expect.mxcsr = h_out.mxcsr; // control bits compared below (the replay may raise the sticky PE flag)
            expect.fpusw = std::uint16_t(7u << 11); // one value on the x87 stack: TOP 7, as entered
            same_cpu(h_out, expect, changed, label);
            check((h_out.mxcsr & ~0x3fu) == (in.mxcsr & ~0x3fu), "apply_mxcsr_control", label);
            check(e == 0xabc00000 + skew, "apply_last_error", label);
            if (axis < core::strafe_y) {
                const float want_step = float(step / (k * k));
                const float want_target = float(round_away(double(target) / k)) * (1.0f / 65536.0f);
                const float got_step = out_float(3), got_target = out_float(0);
                char d[160];
                std::snprintf(d, sizeof d, "%s step %.9g/%.9g target %.9g/%.9g", label, double(got_step),
                              double(want_step), double(got_target), double(want_target));
                // the step within one float ulp (the module scales by (65536/R)^2, the reference divides by k^2)
                check(std::fabs(double(got_step) - double(want_step)) <=
                              double(std::nextafter(want_step, 1e30f) - want_step) &&
                          got_target == want_target,
                      "apply_rotation_math", d);
                // real time: the per-frame rate change is accel * real_dt / k in game-rate units, i.e. the unwarped
                // acceleration once the game clock's k is applied; the target's real-time rate is the unwarped one.
                check(std::fabs(double(got_step) * k * k - step) <= 1e-6 * step, "real_time_acceleration", d);
                check(std::fabs(double(got_target) * 65536.0 * k - target) <= k, "real_time_target", d);
                // the upper lanes of XMM0/XMM3 are what the native instructions leave: MOVD zeroes XMM0[127:32],
                // CVTPD2PS zeroes XMM3[127:64]
                unsigned char zero[12]{};
                check(!std::memcmp(h_out.xmm + 4, zero, 12) && !std::memcmp(h_out.xmm + 48 + 8, zero, 8),
                      "apply_rotation_upper_lanes", label);
                // XMM3 lane 1 is CVTPD2PS of XMM0's untouched upper double, as the native instruction leaves it
                const __m128 native = _mm_cvtpd_ps(_mm_loadu_pd(reinterpret_cast<const double*>(in.xmm)));
                float lane1;
                _mm_store_ss(&lane1, _mm_shuffle_ps(native, native, 1));
                check(!std::memcmp(h_out.xmm + 48 + 4, &lane1, 4), "apply_rotation_xmm3_lane1", label);
            } else {
                const unsigned out = axis == core::strafe_y ? 4 : 1;
                const double limit = double(float(game_ms * 20000.0f)) * 0.001;
                const float want = float(limit * (1.0 / k));
                const float got = out_float(out);
                char d[160];
                std::snprintf(d, sizeof d, "%s limit %.9g/%.9g", label, double(got), double(want));
                check(std::fabs(double(got) - double(want)) <= double(want) * 1e-6, "apply_strafe_math", d);
                // real time: the per-frame limit is 20000 * real_ms * 0.001, the unwarped one
                check(std::fabs(double(got) - 20000.0 * 16.0 * 0.001) <= 0.05, "real_time_strafe", d);
                double x0;
                std::memcpy(&x0, h_out.xmm, 8);
                check(std::fabs(x0 - limit / k) <= 1e-9 * limit, "apply_strafe_xmm0_double", d);
            }
            ++apply_runs;
        }
}
int main(int argc, char** argv) {
    if (argc < 2 || !map_fl_image(argv[1], im)) {
        std::printf("RESULT FAIL cannot map %s\n", argc > 1 ? argv[1] : "(no path)");
        return 2;
    }
    std::printf("IMAGE base=0x%08lx size=0x%08x timestamp=0x%08x relocated=%u\n",
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(im.base)), im.size, im.timestamp, im.relocated);
    check(im.timestamp == 0x618416c8 && im.size == 0x00391000, "fl_identity");
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(im.base);
    im.u32(F::cfg_slot_rva) = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
    im.pristine.assign(im.base, im.base + im.size); // the cfg slot is runtime state, not a window
    set_cfg(0x10000, 0x10000, 16);
    smf::fixture_set_base(base);
    for (unsigned a = 0; a < core::axis_count; ++a)
        references[a] = h_reference(im.base + F::sites[a].rva, F::sites[a].length);

    // ---- part A before any install: every set flag reaches the stop ----
    test_part_a(false);
    // ---- exact-byte refusal: one flipped byte per window (the middle) and one foreign fixup; nothing written ----
    unsigned flips = 0, refused = 0;
    quiet = true;
    for (unsigned w = 0; w < F::window_count; ++w) {
        const auto& win = F::windows[w];
        // flip a byte outside every fixup (from the middle on); a window that is all fixups (the jump table) refuses
        // the flipped byte as a fixup mismatch
        auto in_fixup = [&win](unsigned at) {
            for (unsigned f = 0; f < win.fixups; ++f)
                if (at >= win.fixup[f].offset && at < win.fixup[f].offset + 4) return true;
            return false;
        };
        unsigned at = win.length / 2;
        for (unsigned k = 0; k < win.length && in_fixup(at); ++k) at = (at + 1) % win.length;
        const char* want = in_fixup(at) ? "fixup_mismatch" : "window_mismatch";
        im.base[win.rva + at] ^= 0x5a;
        ++flips;
        const bool ok = initialize("2");
        const bool named = !std::strcmp(smf::state(), want);
        im.restore(win.rva + at, 1);
        const bool clean = !ok && named && all_sites_original(); // nothing written (the flip itself put back first)
        refused += clean;
        if (!clean) std::printf("FLIP_MISSED window=%s at=%u state=%s\n", win.name, at, smf::state());
        reset();
        if (win.fixups) {
            im.u32(win.rva + win.fixup[0].offset) += 4;
            ++flips;
            const bool ok2 = initialize("2");
            const bool named2 = !std::strcmp(smf::state(), "fixup_mismatch");
            im.restore(win.rva + win.fixup[0].offset, 4);
            const bool clean2 = !ok2 && named2 && all_sites_original();
            refused += clean2;
            if (!clean2) std::printf("FLIP_MISSED window=%s fixup state=%s\n", win.name, smf::state());
            reset();
        }
    }
    quiet = false;
    std::printf("FLIPS windows=%u flips=%u refused=%u\n", F::window_count, flips, refused);
    check(flips == refused, "every_flip_refused_before_any_write");
    // mode 1 compares part A's windows only: a part B flip does not refuse it
    im.base[F::windows[F::window_yaw_controller].rva] ^= 1;
    check(initialize("1") && !std::strcmp(smf::state(), "ok"), "mode1_ignores_part_b_windows");
    check(image_matches(F::sites[0].rva, F::sites[0].length), "mode1_leaves_spans");
    reset();
    im.restore(F::windows[F::window_yaw_controller].rva, 1);
    check(all_sites_original(), "mode1_shutdown_restores");

    // ---- read-back faults: rolled back ----
    for (unsigned row : {0u, 1u, 2u, 7u}) {
        smf::fixture_fault_row(row);
        const bool ok = initialize("2");
        char d[64];
        std::snprintf(d, sizeof d, "row=%u state=%s", row, smf::state());
        check(!ok && !std::strcmp(smf::state(), "patch_rolled_back") && all_sites_original(), "row_fault_rolled_back", d);
        reset();
    }
    for (unsigned a = 0; a < core::axis_count; ++a) {
        smf::fixture_fault_claim(a);
        const bool ok = initialize("2");
        char d[64];
        std::snprintf(d, sizeof d, "axis=%u state=%s", a, smf::state());
        check(!ok && !std::strcmp(smf::state(), "readback_failed") && all_sites_original(), "claim_fault_rolled_back", d);
        reset();
    }
    // ---- the install ----
    const unsigned latch_before = key_latch_calls;
    check(initialize("2") && !std::strcmp(smf::state(), "ok"), "install_mode2");
    check(std::strstr(install_row, "stop_write=atomic:7,plain:1 stop_plain_rva=0x0d218f") != nullptr, "row_stop_write",
          install_row);
    check(std::strstr(install_row, "yaw=active:atomic pitch=active:atomic roll=active:atomic strafe_y=active:plain "
                                "strafe_x=active:atomic") != nullptr,
          "row_sites", install_row);
    check(key_latch_calls == latch_before + 1 && std::strstr(last_row, "key_latch installed=0 reason=seta_restore_disabled") != nullptr,
          "key_latch_row", last_row);
    for (unsigned i = 0; i < F::stop_patch_count; ++i)
        check(!std::memcmp(im.base + F::stop_patches[i].rva, F::stop_patches[i].patched, 2), "row_written");
    check(image_matches(F::stop_rows_rva + F::stop_speed_row * F::stop_row_length, F::stop_row_length), "speed_row_kept");
    test_part_a(true);
    // landings at the five continuations (outside the claimed spans; after the install's window check)
    for (unsigned a = 0; a < core::axis_count; ++a) im.jump(F::sites[a].rva + F::sites[a].length, reinterpret_cast<void*>(&h_record));
    // SETA off: every axis takes the original path (the claim tail replays the span)
    set_cfg(0x10000, 0x10000, 16);
    for (unsigned a = 0; a < core::axis_count; ++a) test_original_path(a, "warp_off");
    // SETA on, governor holding the effective rate below 1.0: original path (rate_out_of_range)
    set_cfg(0x18000, 0x6000, 16);
    for (unsigned a = 0; a < core::axis_count; ++a) test_original_path(a, "rate_below_one");
    // mouse delta mode on the yaw/pitch/roll types: those axes keep the original path; strafe is not excluded
    set_cfg(0x40000, 0x10000, 64);
    for (unsigned a = 0; a < core::strafe_y; ++a) {
        im.u32(F::mouse_turret_x_rva) = a + 1;
        test_original_path(a, "mouse_delta");
        im.u32(F::mouse_turret_x_rva) = 0;
    }
    // apply path: k = 4, 10, 1.5 (governor 0.75 under factor 2), 1000
    for (unsigned a = 0; a < core::axis_count; ++a) {
        test_apply_path(a, 0x40000, 0x10000);
        test_apply_path(a, 0xa0000, 0x10000);
        test_apply_path(a, 0x20000, 0xc000);
        test_apply_path(a, 0x3e80000, 0x10000);
    }
    // register contract: a rotation with EDI not its record, a strafe with ESI not the cfg pointer: original path
    for (unsigned a = 0; a < core::axis_count; ++a) {
        set_cfg(0x40000, 0x10000, 64);
        axis_inputs(a, 0x99, true, 0.5, 0x100000, 64.0f);
        if (a < core::strafe_y)
            h_in.edi = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
        else
            h_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
        const HCpu in = h_in;
        DWORD e1 = 0, e2 = 0;
        // the reference copy reads [edi+0x24] from the ship buffer: same input both ways
        h_run(reinterpret_cast<std::uintptr_t>(references[a]), 4, 7, &e1);
        const HCpu ref = h_out;
        h_in = in;
        h_run(im.at(F::sites[a].rva), 4, 7, &e2);
        char label[64];
        std::snprintf(label, sizeof label, "contract axis=%u", a);
        same_cpu(h_out, ref, 0, label);
    }
    std::printf("CONTRACT original_runs=%u apply_runs=%u\n", original_runs, apply_runs);
    // ---- late window: a second initialize after shutdown with the window closed is refused, nothing written ----
    for (unsigned a = 0; a < core::axis_count; ++a) im.restore(F::sites[a].rva + F::sites[a].length, 5);
    reset();
    check(all_sites_original(), "shutdown_restores_every_byte");
    x3m::engine_patch::close_install_window("fixture_present");
    check(!initialize("2") && !std::strcmp(smf::state(), "late_claim") && all_sites_original(), "late_claim_refused");
    std::printf("ARENA used=%u capacity=%u\n", x3m::engine_patch::arena_used(), x3m::engine_patch::arena_capacity());
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
