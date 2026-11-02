// FL edition of x3m::lod_occlusion (src/proxy/lod_occlusion.h; the ap target keeps src/proxy/lod_occlusion.cpp):
// X3M_LOD_OCCLUSION=record0|all (unset = record0; the launcher sends all, marked X3M_LOD_OCCLUSION_DEFAULT=1) on
// X3FL.exe's material submission. all rewrites the LOD gate's JNE rel8 at base+0x0e56c1 (6f -> 00, `jne 0x0e56c2`), so
// every LOD record takes the LOD-0 path and binds its material's occlusion map; record0 leaves the engine's bytes. It
// removes no draw. Site and decode: lod_patch_sites_fl.h; docs/reverse-engineering/fl/cull-chain.md, "Verified
// 2026-11-02 against X3FL.exe" section 3. Backend-load path only, inside the engine_patch install window (late_claim
// after the first Present), X3FL.exe identity (fl::module_base()), the 20-byte window compared with its one fixup equal
// to base+0x1a9014, VirtualProtect, one lock cmpxchg8b, FlushInstructionCache, read-back, rollback judged by a read-back
// of the original byte. One row with edition=fl. Nothing per frame; saves unchanged. shutdown() (fixture only: the FL
// loader's unload calls are AP only) puts 6f back only over 00.
#include "lod_patches_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/lod_occlusion_sites.h"
#include "executable_identity_fl.h"
#include "lod_patch_sites_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::lod_occlusion::sites; // the AP parser (record0|all)
namespace L = x3m::fl::lod_patch_sites_fl;
bool patched_ = false;
std::uintptr_t write_at_ = 0;
std::uint32_t site_protection = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";
#ifdef X3M_LOD_PATCHES_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif
struct CodeOps {
    static constexpr std::uint32_t writable = PAGE_EXECUTE_READWRITE;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { return engine_patch::read_code(at, out, n); }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous) {
        DWORD old = 0;
        const bool ok = VirtualProtect(reinterpret_cast<void*>(at), n, protection, &old) != FALSE;
        if (ok) *previous = old;
        return ok;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= sites::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
}

namespace x3m::lod_occlusion {
bool install_base(std::uintptr_t base) {
    const DWORD error = GetLastError();
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    CodeOps ops;
    bool live = false, atomic = false;
    const char* reason =
        L::install(ops, L::occlusion, base, engine_patch::install_window_open(), &live, &atomic, &site_protection);
    const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") || !std::strcmp(reason, "rollback_unprotected");
    write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
    if (live) {
        patched_ = true; // "ok", or rollback_failed kept registered for shutdown()
        write_at_ = base + L::occlusion.rva + L::occlusion.edit_offset;
    }
    state_ = reason;
    SetLastError(error);
    return patched_ && !std::strcmp(reason, "ok");
}
bool install_at(std::uintptr_t window) {
    return install_base(window >= L::occlusion.rva ? window - L::occlusion.rva : 0);
}
#ifdef X3M_LOD_PATCHES_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return true;
    }
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_LOD_OCCLUSION", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    wchar_t marker[2]{};
    const bool from_default = length > 0 && x3m::config::get(L"X3M_LOD_OCCLUSION_DEFAULT", marker, 2) == 1 &&
                              marker[0] == L'1';
#ifdef X3M_LOD_PATCHES_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    sites::Mode mode = sites::default_mode;
    bool applied = false, parsed = true;
    if (length >= sites::setting_capacity) {
        state_ = "too_long";
        parsed = false;
    } else if (length && !sites::parse_mode(text, &mode)) {
        state_ = "invalid_setting";
        parsed = false;
    } else if (mode == sites::Mode::record0)
        state_ = "record0";
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base);
    // patched_unverified: a rollback failed, the byte may still be 00 (registered).
    const char* status = applied                                  ? "patched"
                         : patched_                               ? "patched_unverified"
                         : parsed && mode == sites::Mode::record0 ? "off"
                                                                  : "refused";
    log("lod_occlusion site=%08lx rva=%06lx edition=fl state=%s reason=%s mode=%s setting=%s write=%s default=%u",
        static_cast<unsigned long>(base ? base + L::occlusion.rva + L::occlusion.edit_offset : 0),
        static_cast<unsigned long>(L::occlusion.rva + L::occlusion.edit_offset), status, state_,
        parsed ? sites::mode_name(mode) : "-", setting, write_, unsigned(from_default));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    unsigned char found = 0;
    bool found_read = false;
    const char* reason = L::restore(ops, L::occlusion, write_at_, site_protection, &found, &found_read);
    if (!std::strcmp(reason, "restored")) patched_ = false; // restore_not_owned / restore_failed stay registered
    state_ = reason;
    SetLastError(error);
    return !patched_;
}
const char* state() {
    return state_;
}
const char* write_path() {
    return write_;
}
bool patched() {
    return patched_;
}
}
