// Native fixture of the FL combat trigger (src/engine/fl/music_alert_fl.cpp, music_alert_sites_fl.h;
// docs/reverse-engineering/fl/music-alert.md, "Port (2026-11-03)"). Built with -DX3M_EDITION_FL=1
// -DX3M_MUSIC_ALERT_FL_FIXTURE by verification/analysis/test_fl_music_alert.py (the module force-includes
// fl_music_shim.h) and run natively (WSL interop): ./fl_music_alert.exe <X3FL.exe>. The installed X3FL.exe is mapped and
// relocated to a private base; the harness enters the real seam bytes at base+0x02fada with EBP = a fake registry and
// EBX = a fake player (the image's M and cfg slots point at fake blocks), and leaves at a planted exit on the
// continuation base+0x02fae0, so the claim tail's replay of `mov ecx,[ebp+8]; mov eax,[ebp+4]` is what the harness sees.
#include "fl_music_fixture.h"
#include "../../src/engine/fl/music_alert_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/music_alert.h"
namespace F = x3m::fl::music_alert_fl;
namespace W = x3m::fl::music_windows;
namespace engine_patch = x3m::engine_patch;
using std::uint32_t;
extern std::uintptr_t fl_music_fault_read_at, fl_music_fault_push_at;
extern unsigned fl_music_fault_read_count, fl_music_faults_taken;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fx::row(format, args);
    va_end(args);
}
namespace music_alert {
void fixture_base(std::uintptr_t base);
const char* fixture_window();
const engine_patch::Site* fixture_site();
}
}
namespace ma = x3m::music_alert;

alignas(16) uint32_t registry[0x20], root[0x20], cfg[0x800 / 4], player[0x80 / 4], sector[0x60 / 4], lists[32 * 3];
void set_player_hit(bool on) {
    player[0x44 / 4] = on ? 0x80000u : 0u;
}
void lay_out() {
    std::memset(registry, 0, sizeof registry);
    std::memset(root, 0, sizeof root);
    std::memset(cfg, 0, sizeof cfg);
    std::memset(player, 0, sizeof player);
    std::memset(sector, 0, sizeof sector);
    std::memset(lists, 0, sizeof lists);
    root[0x38 / 4] = fx::u32(player);
    player[0x3c / 4] = 10;      // group word: the player's own
    player[0x48 / 4] = 7;       // class word: a ship
    player[0x54 / 4] = fx::u32(sector);
    sector[0x48 / 4] = 1;       // class word: a sector
    sector[0x50 / 4] = fx::u32(lists);
    cfg[0x714 / 4] = 0x7fffffffu; // AP's clock offset holds garbage: the FL binding must read +0x708
    cfg[0x718 / 4] = 0x7fffffffu;
    cfg[0xbc / 4] = 0x10000;    // SETA 1.0 (Q16 operands)
    cfg[0xc0 / 4] = 0x10000;
    fx::put32(F::registry_slot_rva, fx::u32(registry));
    fx::put32(F::root_slot_rva, fx::u32(root));
    fx::put32(F::cfg_slot_rva, fx::u32(cfg));
}
uint32_t native_flags = 0;
// One pass over the seam: [R+4] = committed, [R+8] = level, the game clock now_ms; ECX out is the filtered level.
bool seam(uint32_t level, uint32_t committed, uint32_t now_ms, uint32_t expect, bool patched, const char* label) {
    registry[1] = committed;
    registry[2] = level;
    cfg[0x704 / 4] = 16;
    cfg[0x708 / 4] = now_ms;
    fx_df = 1;
    const fx::Call c = fx::enter(fx::va(F::site_rva), {0xeaeaeaea, 0x11111111, 0x22222222, fx::u32(player), 0x5e5e5e5e,
                                                       0xd1d1d1d1, fx::u32(registry)},
                                 {});
    if (!patched) native_flags = fx_out_flags;
    const bool ok = fx_out[fx::ecx] == expect && registry[2] == expect && fx_out[fx::eax] == committed &&
                    fx::kept({fx::edx, fx::ebx, fx::esi, fx::edi, fx::ebp}) && fx_out_esp == fx_esp_before &&
                    fx_out_flags == native_flags && fx::cpu_kept() && c.error == fx::sentinel_error;
    char d[200];
    std::snprintf(d, sizeof d, "level=%u now=%u ecx=%u/%u eax=%u flags=%03x/%03x esp=%08x/%08x err=%08lx", level, now_ms,
                  fx_out[fx::ecx], expect, fx_out[fx::eax], fx_out_flags, native_flags, fx_out_esp, fx_esp_before,
                  static_cast<unsigned long>(c.error));
    fx::check(ok, label, d);
    return ok;
}
bool init(const char* expect) {
    const unsigned before = fx::rows;
    SetLastError(fx::sentinel_error);
    const bool ok = ma::initialize();
    char d[400];
    std::snprintf(d, sizeof d, "state=%s window=%s row=%s", ma::state(), ma::fixture_window(), fx::last_row);
    fx::check(fx::rows == before + 1 && GetLastError() == fx::sentinel_error && !std::strcmp(ma::state(), expect) &&
                  std::strstr(fx::last_row, " edition=fl"),
              expect, d);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2 || !fx::map_exe(argv[1])) {
        std::printf("FAIL map_exe\n");
        return 2;
    }
    std::printf("IMAGE base=%08x size=%08x\n", fx::u32(fx::image), fx::image_size);
    lay_out();
    fx::jump_at(F::continue_rva, reinterpret_cast<void*>(&fx_exit)); // planted after the windows are checked below
    // ---- the native seam: the level passes through ----
    seam(1, 0, 1000, 1, false, "native_seam");
    fx::restore_bytes(F::continue_rva, 6);
    SetEnvironmentVariableW(L"X3M_MUSIC_COMBAT_HOLD_S", L"20");
    // ---- refusals: the settings, the executable, every window's change, the main loop's call, rollbacks ----
    SetEnvironmentVariableW(L"X3M_MUSIC_COMBAT_TRIGGER", L"vanilla");
    init("vanilla");
    SetEnvironmentVariableW(L"X3M_MUSIC_COMBAT_TRIGGER", L"sometimes");
    init("invalid_setting");
    SetEnvironmentVariableW(L"X3M_MUSIC_COMBAT_TRIGGER", nullptr); // the schema default: engaged
    init("executable_mismatch");
    ma::fixture_base(fx::base);
    unsigned flips = 0, refused = 0;
    for (const W::CodeWindow& w : W::alert_windows) {
        for (uint32_t at : {w.rva, w.rva + w.length / 2, w.rva + w.length - 1}) {
            fx::image[at] ^= 0x01;
            ma::initialize();
            ++flips;
            refused += !std::strcmp(ma::state(), "bytes_mismatch") && !std::strcmp(ma::fixture_window(), w.name) &&
                       !ma::fixture_site()->patched_in;
            fx::image[at] ^= 0x01;
        }
        for (unsigned k = 0; k < w.count; ++k) {
            const uint32_t at = w.rva + W::alert_fixups[w.first + k].offset;
            fx::put32(at, fx::get32(at) + 0x10000);
            ma::initialize();
            ++flips;
            refused += !std::strcmp(ma::state(), "bytes_mismatch") && !ma::fixture_site()->patched_in;
            fx::put32(at, fx::get32(at) - 0x10000);
        }
    }
    std::printf("FLIPS windows=%u cases=%u refused=%u\n", unsigned(sizeof W::alert_windows / sizeof W::alert_windows[0]),
                flips, refused);
    fx::check(flips == refused && fx::same_as_pristine(F::site_rva, 6), "every_window_change_refused");
    fx::put32(F::main_loop_call_rva + 1, fx::get32(F::main_loop_call_rva + 1) + 1);
    init("callers_mismatch");
    fx::put32(F::main_loop_call_rva + 1, fx::get32(F::main_loop_call_rva + 1) - 1);
    fl_music_fault_read_at = fx::base + F::site_rva;
    fl_music_fault_read_count = 5;
    init("readback_failed");
    fx::check(fx::same_as_pristine(F::site_rva, 6) && !ma::fixture_site()->patched_in, "readback_rolled_back");
    fl_music_fault_push_at = fx::base + F::site_rva;
    init("chain_failed");
    fx::check(fx::same_as_pristine(F::site_rva, 6) && !ma::fixture_site()->patched_in, "chain_rolled_back");
    // ---- the install ----
    init("ok");
    fx::check(ma::fixture_site()->patched_in && ma::fixture_site()->atomic_write && fx::image[F::site_rva] == 0xe9 &&
                  std::strstr(fx::last_row, "trigger=engaged hold_ms=20000 hold_source=setting patched=1 armed=1"),
              "installed_atomic", fx::last_row);
    fx::jump_at(F::continue_rva, reinterpret_cast<void*>(&fx_exit));
    // ---- the trigger and the hold clock (game ms at cfg+0x708; AP's +0x718 holds garbage) ----
    const unsigned rows0 = fx::rows;
    seam(1, 0, 1000, 0, true, "quiet_level_lowered");     // the first sample baselines; nothing engaged: suppressed
    set_player_hit(true);
    seam(1, 0, 2000, 1, true, "player_hit_keeps_level");   // (b): stamp at 2000
    set_player_hit(false);
    seam(1, 1, 2000 + 19999, 1, true, "hold_keeps_level"); // age 19999 < 20000
    seam(1, 1, 2000 + 20000, 0, true, "hold_expires");     // age 20000: lowered
    seam(2, 0, 30000, 2, true, "missile_level_kept");      // (d): vanilla 2
    seam(1, 2, 30000 + 5000, 1, true, "missile_stamp_holds");
    seam(1, 1, 30000 + 20000, 0, true, "missile_hold_expires");
    seam(0, 0, 60000, 0, true, "zero_stays_zero");         // never raised
    set_player_hit(true);
    seam(1, 0, 70000, 1, true, "hit_again");
    set_player_hit(false);
    seam(1, 1, 5000, 0, true, "clock_rewind_expires");     // a backward clock: no stale stamp holds
    std::printf("ROWS %u\n", fx::rows - rows0);
    // ---- restore, the native seam again, then the late window ----
    fx::check(ma::shutdown() && fx::same_as_pristine(F::site_rva, 6), "restored");
    seam(1, 0, 80000, 1, false, "native_after_restore_unfiltered");
    fx::restore_bytes(F::continue_rva, 6);
    engine_patch::close_install_window("fixture");
    init("late_claim");
    fx::check(fx::same_as_pristine(F::site_rva, 6), "late_claim_wrote_nothing");
    std::printf("FAULTS taken=%u\n", fl_music_faults_taken);
    return fx::result();
}
