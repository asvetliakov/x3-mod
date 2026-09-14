#include "lod_scale.h"
#include "lod_scale_core.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 absolute-address operand only");
namespace {
using namespace x3m::lod_scale::core;
// The FMUL operand: one aligned 32-bit word in this image, so a disp32 reaches
// it. The render thread reads it through the patched instruction; refresh()
// writes it with one aligned store (never torn on x86).
alignas(4) std::atomic<std::uint32_t> mirror{float_to_bits(1.0f)};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the mirror must be a plain word");
double factor = 0;
bool patched_ = false, seen_ = false;
std::uint32_t last_game_bits = 0;
unsigned value_lines = 0;
unsigned char original[site_length]{}, replacement[site_length]{};
DWORD site_protection = 0;
const char* state = "disabled";

// X3M_LOD_SCALE as printable ASCII (anything else is invalid and logged as "?").
// Returns false when the variable is unset or empty.
bool read_setting(char* out, unsigned capacity) {
    wchar_t text[32]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_LOD_SCALE", text, 32);
    if (length == 0) return false;
    if (length >= 32 || length + 1 > capacity) { out[0] = '?'; out[1] = 0; return true; }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0; return true;
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
// Writes the six bytes and flushes; reports which write_code path was taken.
bool swap(void* code, const unsigned char* bytes, bool* atomic) {
    const bool used_atomic = x3m::engine_patch::write_code(reinterpret_cast<std::uintptr_t>(code), bytes, site_length);
    if (atomic) *atomic = used_atomic;
    return FlushInstructionCache(GetCurrentProcess(), code, site_length) != FALSE;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the absolute operand the game executes can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&mirror), &module) != FALSE && module != nullptr;
}
}

namespace x3m::lod_scale {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    char setting[32]{};
    if (!read_setting(setting, sizeof setting)) { state = "disabled"; SetLastError(error); return false; }
    double f = 0;
    const char* reason = nullptr;
    const char* write = "none";
    std::uint32_t game_bits = 0; bool game_read = false;
    unsigned char window[window_length]{};
    if (!parse_factor(setting, &f) || !valid_factor(f)) reason = "invalid_factor";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!object_trace::executable_verified()) reason = "executable_mismatch";
    else if (!engine_patch::read_code(window_va, window, window_length) || std::memcmp(window, expected_window, window_length)) reason = "bytes_mismatch";
    else if (!(game_read = read_game_bits(&game_bits))) reason = "config_unreadable";
    else if (!pin_self()) reason = "pin_failed";
    if (!reason) {
        factor = f;
        std::memcpy(original, expected_site, site_length);
        // The multiplier is written by the device bring-up (004d8f10), after
        // this path: the mirror carries the game's current bits (the vanilla
        // operand) until refresh() sees a value in band. Stored before the
        // instruction can read it.
        const bool scaled = publish(game_bits);
        seen_ = true; last_game_bits = game_bits;
        encode_replacement(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&mirror)), replacement);
        auto* code = reinterpret_cast<unsigned char*>(site_va);
        if (!protect(code, PAGE_EXECUTE_READWRITE, &site_protection)) reason = "protect_failed";
        else {
            patched_ = true; // ownership published before the mutation, so a failed rollback stays registered
            bool atomic = false;
            const bool flushed = swap(code, replacement, &atomic);
            write = atomic ? "atomic" : "plain";
            unsigned char current[site_length]{};
            const bool verified = flushed && engine_patch::read_code(site_va, current, site_length) && !std::memcmp(current, replacement, site_length);
            DWORD unused = 0;
            const bool protected_again = protect(code, site_protection, &unused);
            if (!verified || !protected_again) {
                DWORD writable = 0;
                if (protect(code, PAGE_EXECUTE_READWRITE, &writable) && swap(code, original, nullptr) && protect(code, site_protection, &unused)) {
                    patched_ = false; reason = "patch_rolled_back";
                } else reason = "rollback_failed"; // live bytes kept registered for shutdown()
            } else reason = scaled ? "ok" : "game_value_pending";
        }
    }
    state = reason;
    const float game_value = game_read ? bits_to_float(game_bits) : 0.0f;
    const float proxy_value = patched_ ? bits_to_float(mirror.load(std::memory_order_relaxed)) : 0.0f;
    const bool applied = patched_ && !std::strcmp(reason, "ok");
    log("lod_scale requested=%s applied=%.4g game_value=%.6g proxy_value=%.6g patched=%u reason=%s write=%s",
        setting, applied ? f : 0.0, static_cast<double>(game_value), static_cast<double>(proxy_value), patched_ ? 1u : 0u, reason, write);
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
    const bool flushed = swap(code, original, nullptr);
    const bool protected_again = protect(code, site_protection, &unused);
    patched_ = false;
    state = flushed && protected_again ? "restored" : "restore_failed";
    SetLastError(error);
    return flushed && protected_again;
}
}
