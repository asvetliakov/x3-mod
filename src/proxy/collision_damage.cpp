// X3M_COLLISION_DAMAGE (collision_damage.h; docs/reverse-engineering/collision-damage.md sections 4, 5 and
// "Implementation"). One source for both editions: the AP build binds core::ap at 0x400000 behind the AP executable
// identity, the FL build core::fl at X3FL.exe's module base. Backend-load path only, inside the engine_patch install
// window (later: late_claim). Order: setting, identity, install window, every window of the edition compared whole,
// pin, the stub's return bound, contact site, planet site (a planet failure takes the contact site back). LastError
// preserved throughout. Per Present while applied: the flight row, at most once per second and only after contacts.
#include "collision_damage.h"
#include "collision_damage_stubs.h"
#include "config.h"
#include "engine_patch.h"
#if X3M_EDITION_FL
#include "../engine/fl/executable_identity_fl.h"
#else
#include "object_trace.h"
#endif
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace core = x3m::collision_damage::core;
namespace engine_patch = x3m::engine_patch;
#if X3M_EDITION_FL
const core::Edition& edition_ = core::fl;
#else
const core::Edition& edition_ = core::ap;
#endif
const char* state_ = "disabled";
std::uintptr_t base_ = 0;
core::Installed installed_{};
core::Mismatch mismatch_{};
std::uint32_t logged_ = 0;
unsigned long long last_row_ms_ = 0;

struct CodeOps {
    static constexpr std::uint32_t writable = PAGE_EXECUTE_READWRITE;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { return engine_patch::read_code(at, out, n); }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous) {
        DWORD old = 0;
        const bool ok = VirtualProtect(reinterpret_cast<void*>(at), n, protection, &old) != FALSE;
        if (ok) *previous = old;
        return ok;
    }
    // One lock cmpxchg8b when the span lies inside one aligned qword (FL contact, both planet bytes), else a plain copy
    // (AP contact): no other thread runs the collision pass on the backend-load path.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
std::uintptr_t verified_base() {
#if X3M_EDITION_FL
    return x3m::fl::module_base();
#else
    return x3m::object_trace::executable_verified() ? core::preferred_base : 0;
#endif
}
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&installed_), &module) != FALSE &&
           module != nullptr;
}
void hex(const unsigned char* bytes, unsigned n, char* out) {
    static const char digits[] = "0123456789abcdef";
    if (!n) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * n] = 0;
}
const char* install(std::uintptr_t base) {
    if (!base) return "executable_mismatch";
    if (!engine_patch::install_window_open()) return "late_claim";
    const auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) {
        return engine_patch::read_code(at, out, n);
    };
    if (!core::compare_windows(read, base, edition_, mismatch_)) return "window_mismatch";
    if (!pin_self()) return "pin_failed"; // the engine jumps into this module's stub
    x3m::collision_damage::bind(edition_, base);
    volatile X3mCollisionDamageStats& s = x3m_collision_damage_stats;
    s.events = 0;
    s.max_a = 0;
    s.max_b = 0;
    CodeOps ops;
    return core::install(ops, base, edition_, x3m::collision_damage::stub_address(edition_), installed_);
}
const char* write_path(bool live, bool atomic) {
    return live ? (atomic ? "atomic" : "plain") : "none";
}
}

namespace x3m::collision_damage {
bool initialize() {
    const DWORD error = GetLastError();
    if (installed_.contact || installed_.planet) {
        SetLastError(error);
        return !std::strcmp(state_, "ok");
    }
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_COLLISION_DAMAGE", setting, 4);
    const bool one = length == 1 && setting[0] == L'1', zero = length == 1 && setting[0] == L'0';
    const char* setting_text = !length ? "-" : one ? "1" : zero ? "0" : "?";
    mismatch_ = core::Mismatch{};
    base_ = 0;
    if (!length || one) {
        state_ = "vanilla";
    } else if (!zero) {
        state_ = "invalid_value";
    } else {
        base_ = verified_base();
        state_ = install(base_);
    }
    const bool applied = !std::strcmp(state_, "ok");
    char expected[2 * core::shown_bytes + 1], got[2 * core::shown_bytes + 1];
    const core::Window* w = mismatch_.window;
    hex(w ? w->bytes : nullptr, w ? mismatch_.shown : 0, expected);
    hex(mismatch_.got, w && mismatch_.readable ? mismatch_.shown : 0, got);
    const std::uintptr_t base = base_ ? base_ : core::preferred_base;
    log("collision_damage edition=%s status=%s reason=%s setting=%s base=0x%08lx contact_site=0x%08lx contact_len=%u "
        "contact_write=%s planet_site=0x%08lx planet_byte=%02x->%02x planet_write=%s stub=0x%08lx resume=0x%08lx "
        "windows=%u window=%s expected=%s got=%s",
        edition_.name, applied ? "applied" : "skipped", state_, setting_text, static_cast<unsigned long>(base_),
        static_cast<unsigned long>(base + core::rva(edition_.contact_va)), edition_.contact_length,
        write_path(installed_.contact, installed_.contact_atomic),
        static_cast<unsigned long>(base + core::rva(edition_.planet_va)), unsigned(edition_.planet_original),
        unsigned(edition_.planet_patched), write_path(installed_.planet, installed_.planet_atomic),
        static_cast<unsigned long>(installed_.contact ? stub_address(edition_) : 0),
        static_cast<unsigned long>(installed_.contact ? base + core::rva(edition_.contact_va) + edition_.contact_length
                                                      : 0),
        edition_.window_count, w ? w->name : "-", expected, got);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!installed_.contact && !installed_.planet) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    state_ = core::restore(ops, base_, edition_, installed_);
    SetLastError(error);
    return !installed_.contact && !installed_.planet;
}
const char* state() {
    return state_;
}
void present(unsigned long long frame) {
    if (!installed_.contact) return;
    volatile X3mCollisionDamageStats& s = x3m_collision_damage_stats;
    const std::uint32_t events = s.events;
    if (events == logged_) return;
    const DWORD error = GetLastError();
    const unsigned long long now = GetTickCount64();
    if (core::row_due(events, logged_, now, last_row_ms_)) {
        log("collision_damage_suppressed edition=%s frame=%llu events=%lu total=%lu last_a=%ld last_b=%ld max_a=%ld "
            "max_b=%ld last=%lu/%lu c=%ld v=%ld player=%lu",
            edition_.name, frame, static_cast<unsigned long>(events - logged_), static_cast<unsigned long>(events),
            static_cast<long>(s.last_a), static_cast<long>(s.last_b), static_cast<long>(s.max_a),
            static_cast<long>(s.max_b), static_cast<unsigned long>(s.class_a), static_cast<unsigned long>(s.class_b),
            static_cast<long>(s.c), static_cast<long>(s.v), static_cast<unsigned long>(s.player));
        logged_ = events;
        last_row_ms_ = now;
        s.max_a = 0;
        s.max_b = 0;
    }
    SetLastError(error);
}
}
