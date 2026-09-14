#include "lod_scale.h"
#include "lod_scale_core.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <cwchar>

static_assert(sizeof(void*) == 4, "x86 absolute-address operand only");
namespace {
using namespace x3m::lod_scale::core;
// The FMUL operand: one aligned 32-bit word in this image, so a disp32 reaches
// it. The render thread reads it through the patched instruction; refresh()
// writes it with one aligned store (never torn on x86).
alignas(4) std::atomic<std::uint32_t> mirror{float_to_bits(1.0f)};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the mirror must be a plain word");
double factor = 0;
bool requested_ = false, patched_ = false, seen_ = false;
std::uint32_t last_game_bits = 0;
unsigned value_lines = 0;
unsigned char original[site_length]{}, replacement[site_length]{};
DWORD site_protection = 0;
const char* state = "disabled";

bool parse_factor(double* out) {
    wchar_t text[32]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_LOD_SCALE", text, 32);
    if (length == 0 || length >= 32) return false;
    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text || *end != L'\0') return false;
    *out = v; return true;
}
// The game's multiplier: *(0x606f34) + 0x760 through the bounded reader.
bool read_game_bits(std::uint32_t* bits) {
    std::uint32_t config = 0;
    if (!x3m::engine_memory::read(config_pointer_va, &config, 4) || !config) return false;
    return x3m::engine_memory::read(config + config_scale_offset, bits, 4);
}
// Stores the mirror for game_bits; returns whether the factor was applied.
bool publish(std::uint32_t game_bits) {
    std::uint32_t out = 0;
    const bool scaled = mirror_bits(game_bits, factor, &out);
    mirror.store(out, std::memory_order_relaxed);
    return scaled;
}
bool protect(void* code, DWORD protection, DWORD* previous) {
    return VirtualProtect(code, site_length, protection, previous) != FALSE;
}
bool swap(void* code, const unsigned char* bytes) {
    x3m::engine_patch::write_code(reinterpret_cast<std::uintptr_t>(code), bytes, site_length);
    return FlushInstructionCache(GetCurrentProcess(), code, site_length) != FALSE;
}
}

namespace x3m::lod_scale {
bool wanted() { double f = 0; return parse_factor(&f); }
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    double f = 0;
    if (!parse_factor(&f)) { state = "disabled"; SetLastError(error); return false; }
    requested_ = true;
    const char* reason = nullptr;
    std::uint32_t game_bits = 0; bool game_read = false;
    unsigned char window[window_length]{};
    if (!valid_factor(f)) reason = "factor_out_of_range";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!object_trace::executable_verified()) reason = "executable_mismatch";
    else if (!engine_patch::read_code(window_va, window, window_length) || std::memcmp(window, expected_window, window_length)) reason = "bytes_mismatch";
    if (!reason) {
        factor = f;
        std::memcpy(original, expected_site, site_length);
        // The multiplier is written by the device bring-up (004d8f10), after
        // this path: usually unreadable or unwritten here. The mirror then
        // carries the game's bits (or 1.0, the engine's own first constant,
        // when the struct is not there yet) until refresh() sees the value.
        game_read = read_game_bits(&game_bits);
        const bool scaled = publish(game_read ? game_bits : float_to_bits(1.0f));
        if (game_read) { seen_ = true; last_game_bits = game_bits; }
        encode_replacement(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&mirror)), replacement);
        auto* code = reinterpret_cast<unsigned char*>(site_va);
        if (!protect(code, PAGE_EXECUTE_READWRITE, &site_protection)) reason = "protect_failed";
        else {
            patched_ = true; // ownership published before the mutation, so a failed rollback stays registered
            const bool flushed = swap(code, replacement);
            unsigned char current[site_length]{};
            const bool verified = flushed && engine_patch::read_code(site_va, current, site_length) && !std::memcmp(current, replacement, site_length);
            DWORD unused = 0;
            const bool protected_again = protect(code, site_protection, &unused);
            if (!verified || !protected_again) {
                DWORD writable = 0;
                if (protect(code, PAGE_EXECUTE_READWRITE, &writable) && swap(code, original) && protect(code, site_protection, &unused)) {
                    patched_ = false; reason = "patch_rolled_back";
                } else reason = "rollback_failed"; // live bytes kept registered for shutdown()
            } else reason = scaled ? "ok" : "game_value_pending";
        }
    }
    state = reason;
    const float game_value = game_read ? bits_to_float(game_bits) : 0.0f;
    const float proxy_value = patched_ ? bits_to_float(mirror.load(std::memory_order_relaxed)) : 0.0f;
    const bool applied = patched_ && !std::strcmp(reason, "ok");
    log("lod_scale requested=%.4g applied=%.4g game_value=%.6g proxy_value=%.6g patched=%u reason=%s",
        f, applied ? f : 0.0, static_cast<double>(game_value), static_cast<double>(proxy_value), patched_ ? 1u : 0u, reason);
    SetLastError(error);
    return applied;
}
void refresh() {
    if (!patched_) return;
    const DWORD error = GetLastError();
    std::uint32_t game_bits = 0;
    if (read_game_bits(&game_bits) && (!seen_ || game_bits != last_game_bits)) {
        seen_ = true; last_game_bits = game_bits;
        const bool scaled = publish(game_bits);
        // The value changes at device bring-up and, at most, on a device
        // re-creation: a bounded line per change keeps the applied state visible.
        if (value_lines < 16) {
            ++value_lines;
            log("lod_scale_value game_value=%.6g proxy_value=%.6g applied=%.4g",
                static_cast<double>(bits_to_float(game_bits)), static_cast<double>(bits_to_float(mirror.load(std::memory_order_relaxed))), scaled ? factor : 0.0);
        }
    }
    SetLastError(error);
}
bool patched() { return patched_; }
const char* status() { return state; }
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    unsigned char current[site_length]{};
    if (!engine_patch::read_code(site_va, current, site_length) || std::memcmp(current, replacement, site_length)) {
        state = "restore_not_owned"; SetLastError(error); return false;
    }
    auto* code = reinterpret_cast<unsigned char*>(site_va);
    DWORD previous = 0, unused = 0;
    if (!protect(code, PAGE_EXECUTE_READWRITE, &previous)) { state = "restore_protect_failed"; SetLastError(error); return false; }
    const bool flushed = swap(code, original);
    const bool protected_again = protect(code, site_protection, &unused);
    patched_ = false;
    state = flushed && protected_again ? "restored" : "restore_failed";
    SetLastError(error);
    return flushed && protected_again;
}
}
