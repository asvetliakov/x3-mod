#include "point_light_admission.h"
#include "point_light_admission_core.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <atomic>
#include <cstring>

// Compiled with -mno-sse -mno-mmx -mfpmath=387 and -fno-exceptions (CMake
// source property; the fixture build compiles it the same way): the handler
// runs on the engine's submission thread between two of its own helper calls
// with no CPU-state boundary, so nothing here may touch an XMM/MMX register,
// and it contains no floating-point arithmetic, so no x87 instruction either
// (verification/probe/check_no_x87.py walks it from x3m_point_light_root_admits).
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::point_light_admission::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
std::uintptr_t site_ = 0, detour_ = 0;
unsigned char original[site_length]{}, replacement[site_length]{};
DWORD site_protection = 0;
const char* state_ = "disabled";
const char* write_ = "none";
std::atomic<std::uint32_t> outcomes[outcome_count]{};
// Per-(node, light) per-frame memo of the root verdict: the reject path runs
// per submitted mesh part per populated light slot per view, and every part
// of one node repeats the same test, so the walk is done once per node and
// light per frame. Direct-mapped, fixed size, written only by the submission
// thread; an entry is valid only for the current frame serial, which Present
// and Reset bump (next_frame). A stale or colliding entry costs one walk.
constexpr unsigned memo_size = 256;
struct Memo { std::uint32_t node, light, frame; std::uint32_t admit; };
Memo memo[memo_size]{};
std::atomic<std::uint32_t> frame_serial{1};
std::atomic<std::uint32_t> memo_hits{0}, walks{0};
inline unsigned memo_slot(std::uint32_t node, std::uint32_t light) {
    return ((node >> 4) * 0x9e3779b1u ^ (light >> 4) * 0x85ebca6bu) >> 24;
}

bool protect(void* code, DWORD protection, DWORD* previous) {
    return VirtualProtect(code, site_length, protection, previous) != FALSE;
}
// Writes the six bytes and flushes; reports which write_code path was taken.
// The span 0x004c27af..0x004c27b5 straddles an 8-byte word, so the engine
// site takes the plain copy: the install window guarantees no thread executes
// it, and the read-back compare below is what the log line reports.
bool swap(void* code, const unsigned char* bytes, bool* atomic) {
    const bool used_atomic = engine_patch::write_code(reinterpret_cast<std::uintptr_t>(code), bytes, site_length);
    if (atomic) *atomic = used_atomic;
    return FlushInstructionCache(GetCurrentProcess(), code, site_length) != FALSE;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the detour's CALL can never land in freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[window_length]{};
    return length <= window_length && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool read_engine(std::uint32_t address, void* out, unsigned size) {
    return x3m::engine_memory::read(address, out, size);
}
}

extern "C" int x3m_point_light_root_admits(std::uint32_t node, std::uint32_t light) {
    const std::uint32_t frame = frame_serial.load(std::memory_order_relaxed);
    Memo& slot = memo[memo_slot(node, light)];
    if (slot.frame == frame && slot.node == node && slot.light == light) {
        memo_hits.fetch_add(1, std::memory_order_relaxed);
        return int(slot.admit);
    }
    const DWORD error = GetLastError();
    const Outcome outcome = root_admission(node, light, read_engine);
    outcomes[unsigned(outcome)].fetch_add(1, std::memory_order_relaxed);
    walks.fetch_add(1, std::memory_order_relaxed);
    SetLastError(error);
    const bool admit = outcome == Outcome::admitted;
    slot.frame = 0; // key and verdict first, the frame last: a reader on another thread never pairs a stale verdict with a fresh key
    slot.node = node; slot.light = light; slot.admit = admit ? 1u : 0u;
    slot.frame = frame;
    return admit;
}

namespace x3m::point_light_admission {
bool install_at(std::uintptr_t site) {
    if (patched_) { state_ = "already_installed"; return false; }
    const std::uintptr_t window = site - site_offset, admit = site + site_length, reject = admit + std::uint32_t(site_rel32);
    const char* reason = nullptr;
    write_ = "none";
    if (!site || window > site) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!bytes_match(window, expected_window, window_length) || !bytes_match(reject, expected_reject_prefix, reject_prefix_length)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    if (!reason) {
        engine_patch::Emitter e(detour_length + 4);
        if (!e.ok()) reason = "arena_full";
        else {
            unsigned char detour[detour_length];
            const std::uint32_t at = std::uint32_t(reinterpret_cast<std::uintptr_t>(e.here()));
            encode_detour(at, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_point_light_root_admits)), std::uint32_t(admit), std::uint32_t(reject), detour);
            e.bytes(detour, detour_length);
            if (!e.finish()) reason = "emit_failed";
            else detour_ = at;
        }
    }
    if (!reason) {
        std::memcpy(original, expected_site, site_length);
        encode_site_patch(std::uint32_t(site), std::uint32_t(detour_), replacement);
        auto* code = reinterpret_cast<unsigned char*>(site);
        if (!protect(code, PAGE_EXECUTE_READWRITE, &site_protection)) reason = "protect_failed";
        else {
            patched_ = true; site_ = site; // ownership published before the mutation, so a failed rollback stays registered
            bool atomic = false;
            const bool flushed = swap(code, replacement, &atomic);
            write_ = atomic ? "atomic" : "plain";
            unsigned char current[site_length]{};
            const bool verified = flushed && engine_patch::read_code(site, current, site_length) && !std::memcmp(current, replacement, site_length);
            DWORD unused = 0;
            const bool protected_again = protect(code, site_protection, &unused);
            if (!verified || !protected_again) {
                DWORD writable = 0;
                if (protect(code, PAGE_EXECUTE_READWRITE, &writable) && swap(code, original, nullptr) && protect(code, site_protection, &unused)) {
                    patched_ = false; reason = "patch_rolled_back";
                } else reason = "rollback_failed"; // live bytes kept registered for shutdown()
            } else reason = "ok";
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_POINT_LIGHT_ROOT_ADMISSION", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else applied = install_at(site_va);
    log("point_light_root_admission requested=%u patched=%u reason=%s write=%s site=0x%08lx detour=0x%08lx handler=0x%08lx",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, write_, static_cast<unsigned long>(site_va), static_cast<unsigned long>(patched_ ? detour_ : 0),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_point_light_root_admits)));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    unsigned char current[site_length]{};
    if (!engine_patch::read_code(site_, current, site_length) || std::memcmp(current, replacement, site_length)) {
        state_ = "restore_not_owned"; SetLastError(error); return false;
    }
    auto* code = reinterpret_cast<unsigned char*>(site_);
    DWORD previous = 0, unused = 0;
    if (!protect(code, PAGE_EXECUTE_READWRITE, &previous)) { state_ = "restore_protect_failed"; SetLastError(error); return false; }
    const bool flushed = swap(code, original, nullptr);
    const bool protected_again = protect(code, site_protection, &unused);
    patched_ = false; // the detour stays in the arena (a thread may still be inside it)
    state_ = flushed && protected_again ? "restored" : "restore_failed";
    SetLastError(error);
    return flushed && protected_again;
}
const char* state() { return state_; }
const char* write_path() { return write_; }
std::uintptr_t detour_address() { return patched_ ? detour_ : 0; }
void next_frame() { frame_serial.fetch_add(1, std::memory_order_relaxed); }
Stats stats() {
    Stats s{};
    for (unsigned i = 0; i < outcome_count; ++i) s.outcomes[i] = outcomes[i].load(std::memory_order_relaxed);
    s.walks = walks.load(std::memory_order_relaxed); s.memo_hits = memo_hits.load(std::memory_order_relaxed);
    s.frame = frame_serial.load(std::memory_order_relaxed);
    return s;
}
}
