// Native fixture of the FL media-cue gate (src/engine/fl/media_cue_fl.cpp, media_cue_sites_fl.h;
// docs/reverse-engineering/fl/media-cue.md, "Port (2026-11-03)"). Built with -DX3M_EDITION_FL=1
// -DX3M_MEDIA_CUE_FL_FIXTURE by verification/analysis/test_fl_media_cue.py (the module force-includes fl_music_shim.h)
// and run natively (WSL interop): ./fl_media_cue.exe <X3FL.exe>. The installed X3FL.exe is mapped and relocated to a
// private 64 KB-aligned base (fl_music_fixture.h); the real allocator and its callers' bytes run, with the allocation
// helper, the graph constructor and the free replaced by recording fakes after the install.
#include "fl_music_fixture.h"
#include <initializer_list>
#include "../../src/engine/fl/media_cue_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/media_cue.h"
namespace F = x3m::fl::media_cue_fl;
namespace W = x3m::fl::music_windows;
namespace detail = x3m::media_cue::detail;
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
HANDLE log_handle() noexcept {
    return reinterpret_cast<HANDLE>(std::uintptr_t(4));
}
namespace session_log {
void vlog(const char* format, va_list args) noexcept {
    fx::row(format, args);
}
}
namespace telemetry {
bool enabled() {
    return true;
}
}
namespace media_cue {
void fixture_base(std::uintptr_t base);
void fixture_reinitialize();
bool fixture_uninstall();
const char* fixture_status();
const char* fixture_window();
const engine_patch::Site* fixture_site();
void* fixture_trampoline();
const detail::NegativeCache* fixture_cache();
const detail::PendingStack* fixture_pending();
bool fixture_pop_trace(detail::Entry* out);
void fixture_set_modes(bool trace, bool cache, std::uint64_t ticks);
}
}
namespace mc = x3m::media_cue;

// ---- The engine callees' fakes (the allocator's own ABI): helper ECX = size, EDX = 0; constructor ECX = id,
// EDX = flags; free cdecl. ----
alignas(16) unsigned char records[16][0x40];
alignas(16) unsigned char media[0xb4];
unsigned record_next = 0, alloc_calls = 0, ctor_calls = 0, free_calls = 0;
uint32_t ctor_id = 0, ctor_flags = 0, fail_id = 0;
extern "C" uint32_t __cdecl fake_alloc_c(uint32_t size, uint32_t) {
    ++alloc_calls;
    unsigned char* r = records[record_next++ % 16];
    std::memset(r, 0, size <= 0x40 ? size : 0x40);
    return fx::u32(r);
}
extern "C" uint32_t __cdecl fake_ctor_c(uint32_t id, uint32_t flags) {
    ++ctor_calls;
    ctor_id = id;
    ctor_flags = flags;
    return id == fail_id ? 0u : fx::u32(media);
}
extern "C" void __cdecl fake_free_c(uint32_t) {
    ++free_calls;
}
extern "C" void fake_alloc();
extern "C" void fake_ctor();
extern "C" void fake_free();
asm(".text\n\t"
    ".globl _fake_alloc\n_fake_alloc:\n\tpushl %edx\n\tpushl %ecx\n\tcall _fake_alloc_c\n\taddl $8,%esp\n\tret\n\t"
    ".globl _fake_ctor\n_fake_ctor:\n\tpushl %edx\n\tpushl %ecx\n\tcall _fake_ctor_c\n\taddl $8,%esp\n\tret\n\t"
    ".globl _fake_free\n_fake_free:\n\tpushl 4(%esp)\n\tcall _fake_free_c\n\taddl $4,%esp\n\tret\n\t");
constexpr uint32_t alloc_helper_rva = 0x0d8fc0, ctor_rva = 0x0fda20, free_rva = 0x12f4e5;
alignas(16) uint32_t owner[0xfb0 / 4];
void fakes(bool on) {
    if (on) {
        fx::jump_at(alloc_helper_rva, reinterpret_cast<void*>(&fake_alloc));
        fx::jump_at(ctor_rva, reinterpret_cast<void*>(&fake_ctor));
        fx::jump_at(free_rva, reinterpret_cast<void*>(&fake_free));
    } else {
        fx::restore_bytes(alloc_helper_rva, 6);
        fx::restore_bytes(ctor_rva, 6);
        fx::restore_bytes(free_rva, 6);
    }
}
void reset_owner() {
    std::memset(owner, 0, sizeof owner);
    owner[0] = fx::u32(&owner[1]); // first node = the embedded sentinel (next 0, prev owner)
    owner[1] = 0;
    owner[2] = fx::u32(owner);
    fx::put32(F::owner_slot_rva, fx::u32(owner));
}
unsigned linked() {
    unsigned n = 0;
    for (uint32_t node = owner[0]; node && *reinterpret_cast<uint32_t*>(node); node = *reinterpret_cast<uint32_t*>(node)) ++n;
    return n;
}
constexpr uint32_t arith = 0x8d5; // CF PF AF ZF SF OF
uint32_t entry() {
    return fx::va(F::allocator_rva);
}
// A direct call (no wrapper frame): ECX = id, EDX = flags; EBX/ESI/EDI/EBP sentinels.
fx::Call direct(uint32_t id, uint32_t flags, bool df) {
    fx_df = df ? 1 : 0;
    return fx::enter(entry(), {0xeaeaeaea, id, flags, 0xb0b0b0b0, 0x5e5e5e5e, 0xd1d1d1d1, 0xebebebeb}, {});
}
// The wrapper's frame: [esp] = base+0x0b46c3 (planted exit), saved EDI/ESI, the wrapper caller's return, id, flags.
fx::Call wrapped(uint32_t id, uint32_t flags, uint32_t caller_rva) {
    fx_df = 0;
    return fx::enter(entry(), {0xeaeaeaea, id, flags, 0xb0b0b0b0, 0x5e5e5e5e, 0xd1d1d1d1, 0xebebebeb},
                     {0x77777777, 0x66666666, fx::va(caller_rva), id, flags, 0}, fx::va(F::wrapper_return_rva));
}
bool callee_saved_kept() {
    return fx::kept({fx::ebx, fx::esi, fx::edi, fx::ebp});
}
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
bool reinit(const char* expect_status) {
    mc::fixture_reinitialize();
    const unsigned before = fx::rows;
    SetLastError(fx::sentinel_error);
    const bool ok = mc::initialize();
    const bool rows_ok = fx::rows == before + 2 && std::strstr(fx::last_row, " edition=fl");
    char detail[300];
    std::snprintf(detail, sizeof detail, "status=%s window=%s row=%s", mc::fixture_status(), mc::fixture_window(),
                  fx::last_row);
    fx::check(rows_ok && GetLastError() == fx::sentinel_error && !std::strcmp(mc::fixture_status(), expect_status),
              expect_status, detail);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2 || !fx::map_exe(argv[1])) {
        std::printf("FAIL map_exe %s\n", argc > 1 ? argv[1] : "(no path)");
        return 2;
    }
    std::printf("IMAGE base=%08x size=%08x\n", fx::u32(fx::image), fx::image_size);
    reset_owner();
    // ---- natives (nothing installed): the fakes stand in for the allocator's callees ----
    fakes(true);
    fail_id = 2;
    direct(2, 0, false);
    const uint32_t native_fail_flags = fx_out_flags & arith;
    fx::check(fx_out[fx::eax] == 0 && ctor_calls == 1 && free_calls == 1 && callee_saved_kept(), "native_id2_fails_in_ctor");
    fail_id = 0;
    direct(5, 0, false);
    const uint32_t native_ok_flags = fx_out_flags & arith;
    fx::check(fx_out[fx::eax] && linked() == 1 && ctor_id == 5 && ctor_flags == 0 && callee_saved_kept() &&
                  fx_out_esp == fx_esp_before + 4,
              "native_record_linked");
    fakes(false);
    reset_owner();
    // ---- refusals before anything is written: every window's single-byte flip, the executable, the callers ----
    unsigned flips = 0, flips_refused = 0;
    for (const W::CodeWindow& w : W::media_windows) {
        for (uint32_t at : {w.rva, w.rva + w.length / 2, w.rva + w.length - 1}) {
            fx::image[at] ^= 0x01;
            mc::fixture_base(fx::base);
            mc::fixture_reinitialize();
            mc::initialize();
            ++flips;
            flips_refused += !std::strcmp(mc::fixture_status(), "bytes_mismatch") && !std::strcmp(mc::fixture_window(), w.name) &&
                             !mc::fixture_site()->patched_in && fx::image[F::allocator_rva] != 0xe9;
            fx::image[at] ^= 0x01;
        }
        for (unsigned k = 0; k < w.count; ++k) { // a fixup bound to another base
            const uint32_t at = w.rva + W::media_fixups[w.first + k].offset;
            fx::put32(at, fx::get32(at) + 0x10000);
            mc::fixture_reinitialize();
            mc::initialize();
            ++flips;
            flips_refused += !std::strcmp(mc::fixture_status(), "bytes_mismatch") && !mc::fixture_site()->patched_in;
            fx::put32(at, fx::get32(at) - 0x10000);
        }
    }
    std::printf("FLIPS windows=%u cases=%u refused=%u\n", unsigned(sizeof W::media_windows / sizeof W::media_windows[0]),
                flips, flips_refused);
    fx::check(flips == flips_refused && flips > 20, "every_window_change_refused");
    {
        const uint32_t call = F::wrapper_callers[2].call_rva; // a wrapper caller's rel32 off by one
        fx::put32(call + 1, fx::get32(call + 1) + 1);
        reinit("callers_mismatch");
        fx::put32(call + 1, fx::get32(call + 1) - 1);
    }
    mc::fixture_base(0);
    reinit("executable_mismatch"); // the fixture's main module is not X3FL.exe
    mc::fixture_base(fx::base);
    // ---- rollback: the jump's read-back flipped, then the chain refused: both restore the bytes ----
    fl_music_fault_read_at = fx::base + F::allocator_rva;
    fl_music_fault_read_count = 5;
    reinit("readback_failed");
    fx::check(fx::same_as_pristine(F::allocator_rva, 5) && !mc::fixture_site()->patched_in, "readback_rolled_back");
    fl_music_fault_push_at = fx::base + F::allocator_rva;
    reinit("chain_failed");
    fx::check(fx::same_as_pristine(F::allocator_rva, 5) && !mc::fixture_site()->patched_in, "chain_rolled_back");
    // ---- the install ----
    const bool installed = reinit("ok");
    fx::check(installed && mc::fixture_site()->patched_in && mc::fixture_site()->atomic_write &&
                  fx::image[F::allocator_rva] == 0xe9 && fx::same_as_pristine(F::continue_rva, 0x10),
              "installed_atomic");
    std::printf("SITE row=%s\n", fx::last_row);
    fakes(true);
    // ---- the default skip: ID2 with EDX 0 or 8, from any caller, before any state; DF and the rest preserved ----
    for (uint32_t flags : {0u, 8u}) {
        const unsigned c0 = ctor_calls, a0 = alloc_calls;
        const fx::Call r = direct(2, flags, true);
        char d[200];
        std::snprintf(d, sizeof d, "flags=%u eax=%08x fl=%03x esp=%08x/%08x err=%08lx", flags, fx_out[fx::eax],
                      fx_out_flags & arith, fx_out_esp, fx_esp_before + 4, static_cast<unsigned long>(r.error));
        fx::check(fx_out[fx::eax] == 0 && ctor_calls == c0 && alloc_calls == a0 && callee_saved_kept() &&
                      fx::kept({fx::ecx, fx::edx}) && fx_out_esp == fx_esp_before + 4 && fx::cpu_kept() &&
                      r.error == fx::sentinel_error && (fx_out_flags & arith) == native_fail_flags &&
                      (fx_out_flags & 0x44) == 0x44 && !(fx_out_flags & 0x881),
                  "id2_refused", d);
    }
    {
        const unsigned c0 = ctor_calls;
        direct(2, 0x10, false); // other incoming flags pass through
        fx::check(fx_out[fx::eax] && ctor_calls == c0 + 1 && ctor_flags == 0x10, "id2_other_flags_pass");
    }
    {
        const fx::Call r = direct(7, 0, false);
        fx::check(fx_out[fx::eax] && callee_saved_kept() && fx_out_esp == fx_esp_before + 4 && r.error == fx::sentinel_error &&
                      (fx_out_flags & arith) == native_ok_flags,
                  "pass_unobserved_same_as_native");
    }
    // ---- the cache and the trace: owner admitted, observed returns, selector scope ----
    mc::fixture_set_modes(true, true, ~0ull >> 2);
    mc::frame(1); // admits this thread
    fx::jump_at(F::wrapper_return_rva, reinterpret_cast<void*>(&fx_exit)); // the planted wrapper return
    fail_id = 77;
    const unsigned c0 = ctor_calls;
    wrapped(77, 0, 0x06612b); // selector: observed failure -> cached
    const bool first_fail = fx_out[fx::eax] == 0 && ctor_calls == c0 + 1 && mc::fixture_pending()->depth == 0 &&
                            fx_out_esp == fx_esp_before + 4;
    wrapped(77, 0, 0x06612b); // refused from the cache
    const bool refused = fx_out[fx::eax] == 0 && ctor_calls == c0 + 1 && fx_out_esp == fx_esp_before + 4;
    wrapped(77, 0, 0x0b4ca9); // script: outside the selector scope
    const bool script_passes = ctor_calls == c0 + 2;
    fail_id = 0;
    wrapped(77, 0, 0x0b4ca9); // a success from any caller clears the id
    wrapped(77, 0, 0x06612b);
    const bool cleared = ctor_calls == c0 + 4 && fx_out[fx::eax] != 0;
    const fx::Call ok_call = wrapped(9, 0, 0x06612b); // observed success: the outgoing flags are the allocator's
    const bool observed_flags = (fx_out_flags & arith) == native_ok_flags && callee_saved_kept() &&
                                ok_call.error == fx::sentinel_error && mc::fixture_pending()->depth == 0;
    std::printf("CACHE first_fail=%u refused=%u script=%u cleared=%u flags=%u used=%u\n", first_fail, refused, script_passes,
                cleared, observed_flags, mc::fixture_cache()->used);
    fx::check(first_fail && refused && script_passes && cleared && observed_flags, "cache_scope_and_observed_returns");
    detail::Entry e;
    unsigned entries = 0, selector = 0, script = 0, refusals = 0;
    while (mc::fixture_pop_trace(&e)) {
        ++entries;
        selector += e.caller == detail::selector && e.kind == 0x5a;
        script += e.caller == detail::script;
        refusals += e.outcome == detail::refused;
    }
    std::printf("TRACE entries=%u selector=%u script=%u refused=%u\n", entries, selector, script, refusals);
    fx::check(entries == 6 && selector == 4 && script == 2 && refusals == 1, "trace_classified");
    mc::frame(2);
    // ---- late window: restore, then a fresh initialize after the window closed writes nothing ----
    fx::restore_bytes(F::wrapper_return_rva, 6);
    fx::check(mc::fixture_uninstall() && fx::same_as_pristine(F::allocator_rva, 5), "restored");
    engine_patch::close_install_window("fixture");
    reinit("late_claim");
    fx::check(fx::same_as_pristine(F::allocator_rva, 5), "late_claim_wrote_nothing");
    fakes(false);
    std::printf("FAULTS taken=%u\n", fl_music_faults_taken);
    return fx::result();
}
