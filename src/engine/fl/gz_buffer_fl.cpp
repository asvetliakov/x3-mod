// FL edition of the gz read-ahead buffer's import rows: the AP policy (src/proxy/gz_buffer.cpp, shared unchanged) on
// X3FL.exe's six zlib1.dll import slots. docs/reverse-engineering/fl/loading-hooks.md, "Port (2026-11-03)"; sites in
// gz_buffer_sites_fl.h. The slots point straight at gz_buffer's cdecl functions (as AP's rows forward to them with
// telemetry off): no stub, no displaced instruction, nothing emitted. The buffer keeps zlib's serialized per-handle
// contract, unregisters a handle before zlib's own gzclose frees it, and passes every unknown or write handle through.
// The FL loader has no unload path (cull_small_parts_fl.cpp): the slots stay for the process lifetime.
#include "gz_buffer_fl.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/gz_buffer.h"
#include "executable_identity_fl.h"
#include "gz_buffer_sites_fl.h"
#include "gz_close_share_fl.h"
#include "import_slots_fl.h"
#include <windows.h>
#include <cstring>
#include <type_traits>

namespace x3m {
void log(const char* format, ...);
}
namespace {
namespace F = x3m::fl::gz_buffer_fl;
namespace W = x3m::fl::loading_windows;
namespace slots = x3m::fl::import_slots;
namespace share = x3m::fl::gz_close_share;
namespace gz = x3m::gz_buffer;
static_assert(std::is_same_v<decltype(&gz::open), gz::OpenFn> && std::is_same_v<decltype(&gz::read), gz::ReadFn> &&
              std::is_same_v<decltype(&gz::seek), gz::SeekFn> && std::is_same_v<decltype(&gz::tell), gz::TellFn> &&
              std::is_same_v<decltype(&gz::getc), gz::GetcFn> && std::is_same_v<decltype(&gz::close), gz::CloseFn>);
const char* state_ = "disabled";
const char* window_ = "-";
const char* close_route_ = "none";
bool active_ = false, initialized_ = false, rewind_ = false;
unsigned live_ = 0;
std::uintptr_t base_ = 0;
#ifdef X3M_GZ_BUFFER_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
void* fixture_exports_[7]{};
#endif
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
bool read_self(std::uintptr_t at, void* out, std::size_t n) {
    return x3m::engine_patch::read_code(at, static_cast<unsigned char*>(out), unsigned(n));
}
std::uint32_t load32(std::uintptr_t at) {
    std::uint32_t v = 0;
    return read_self(at, &v, 4) ? v : 0;
}
// gz_buffer's entry for each import, in gz_buffer_sites_fl.h's claim order.
std::uint32_t entry(unsigned i) {
    if (i == F::close_index) return address_of(reinterpret_cast<const void*>(&gz::close));
    if (i == F::tell_index) return address_of(reinterpret_cast<const void*>(&gz::tell));
    if (i == F::seek_index) return address_of(reinterpret_cast<const void*>(&gz::seek));
    if (i == F::getc_index) return address_of(reinterpret_cast<const void*>(&gz::getc));
    if (i == F::read_index) return address_of(reinterpret_cast<const void*>(&gz::read));
    return address_of(reinterpret_cast<const void*>(&gz::open));
}
// The close slot's route: the slot itself (it holds zlib's gzclose), or the music keep's forward word (the keep's
// stub holds the slot and still forwards to zlib's gzclose).
enum class CloseRoute { none, slot, keep };
CloseRoute route_ = CloseRoute::none;
bool write_close(std::uint32_t expected, std::uint32_t desired) {
    if (route_ == CloseRoute::slot)
        return slots::exchange(base_ + F::imports[F::close_index].slot_rva, expected, desired);
#ifdef X3M_LOADING_FL_FIXTURE
    if (slots::fixture_fail_exchange && !--slots::fixture_fail_exchange) return false;
#endif
    volatile std::uint32_t* forward = share::keep_forward;
    if (!forward) return false;
    const LONG seen =
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(forward), LONG(desired), LONG(expected));
    return std::uint32_t(seen) == expected && *forward == desired;
}
bool install() {
    window_ = "-";
    close_route_ = "none";
    route_ = CloseRoute::none;
    live_ = 0;
    if (!base_) {
        state_ = "executable_mismatch";
        return false;
    }
    if (!x3m::engine_patch::install_window_open()) {
        state_ = "late_claim";
        return false;
    }
    if (const char* bad = W::first_mismatch(read_self, base_, W::gz_windows, W::gz_fixups)) {
        window_ = bad;
        state_ = "bytes_mismatch";
        return false;
    }
    for (const F::Import& i : F::imports)
        if (slots::named_slot(base_, "zlib1.dll", i.name, read_self) != i.slot_rva) {
            window_ = i.name;
            state_ = "import_mismatch";
            return false;
        }
    // zlib1.dll's own exports (resolved by name, no DLL hash): every slot but gzclose must hold its export.
    std::uint32_t exports[F::import_count]{};
    void* rewind = nullptr;
#ifndef X3M_GZ_BUFFER_FL_FIXTURE
    const HMODULE zlib = GetModuleHandleW(L"zlib1.dll");
    for (unsigned i = 0; i < F::import_count; ++i)
        exports[i] = zlib ? address_of(reinterpret_cast<const void*>(GetProcAddress(zlib, F::imports[i].name))) : 0;
    rewind = zlib ? reinterpret_cast<void*>(GetProcAddress(zlib, "gzrewind")) : nullptr;
#else
    const unsigned order[F::import_count] = {5, 4, 2, 3, 1, 0}; // fixture_bind order: open read seek getc tell close
    for (unsigned i = 0; i < F::import_count; ++i) exports[i] = address_of(fixture_exports_[order[i]]);
    rewind = fixture_exports_[6];
#endif
    for (unsigned i = 0; i < F::import_count; ++i)
        if (!exports[i]) {
            state_ = "zlib_not_loaded";
            return false;
        }
    for (unsigned i = F::tell_index; i < F::import_count; ++i)
        if (load32(base_ + F::imports[i].slot_rva) != exports[i]) {
            window_ = F::imports[i].name;
            state_ = "original_mismatch";
            return false;
        }
    const std::uint32_t close_now = load32(base_ + F::imports[F::close_index].slot_rva);
    if (close_now == exports[F::close_index])
        route_ = CloseRoute::slot;
    else if (share::keep_stub && close_now == share::keep_stub && share::keep_forward &&
             *share::keep_forward == exports[F::close_index])
        route_ = CloseRoute::keep;
    else {
        window_ = "gzclose";
        state_ = "original_mismatch";
        return false;
    }
    gz::Originals real;
    real.open = reinterpret_cast<gz::OpenFn>(exports[F::open_index]);
    real.read = reinterpret_cast<gz::ReadFn>(exports[F::read_index]);
    real.seek = reinterpret_cast<gz::SeekFn>(exports[F::seek_index]);
    real.getc = reinterpret_cast<gz::GetcFn>(exports[F::getc_index]);
    real.tell = reinterpret_cast<gz::TellFn>(exports[F::tell_index]);
    real.close = reinterpret_cast<gz::CloseFn>(exports[F::close_index]);
    real.rewind = reinterpret_cast<gz::RewindFn>(rewind);
    rewind_ = rewind != nullptr;
    if (!gz::initialize(real, gz::requested_capacity())) {
        state_ = "buffer_refused";
        return false;
    }
    // Claim order: gzclose (the slot or the keep's forward word), gztell, gzseek, gzgetc, gzread, gzopen last; a failure
    // puts every earlier write back in reverse (no buffered handle can exist before gzopen is live).
    unsigned done = 0;
    for (; done < F::import_count; ++done) {
        const bool ok = done == F::close_index
                            ? write_close(exports[done], entry(done))
                            : slots::exchange(base_ + F::imports[done].slot_rva, exports[done], entry(done));
        if (!ok) break;
    }
    if (done == F::import_count) {
        live_ = done;
        close_route_ = route_ == CloseRoute::slot ? "slot" : "keep";
        share::buffer_forward = exports[F::close_index];
        share::buffer_holds_slot = route_ == CloseRoute::slot ? 1u : 0u;
        share::buffer_entry = entry(F::close_index);
        state_ = "ok";
        return true;
    }
    window_ = F::imports[done].name;
    bool rolled_back = true;
    for (unsigned k = done; k-- > 0;) {
        const bool back = k == F::close_index ? write_close(entry(k), exports[k])
                                              : slots::exchange(base_ + F::imports[k].slot_rva, entry(k), exports[k]);
        rolled_back = rolled_back && back;
        if (!back) ++live_;
    }
    state_ = rolled_back ? "slot_not_writable" : "rollback_failed";
    return false;
}
}

namespace x3m::fl::gz_buffer_fl {
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return active_;
    }
    initialized_ = true;
    if (!gz::requested()) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
#ifdef X3M_GZ_BUFFER_FL_FIXTURE
    base_ = fixture_base_;
#else
    base_ = x3m::fl::module_base();
#endif
    active_ = install();
    log("gz_buffer requested=1 enabled=%u capacity_kb=%u telemetry=0 imports=%u rewind=%u slots=%u reason=%s window=%s "
        "close_route=%s live=%u slot_open=0x%08lx slot_close=0x%08lx edition=fl",
        active_ ? 1u : 0u, gz::requested_capacity() / 1024, std::strcmp(state_, "import_mismatch") ? 1u : 0u,
        rewind_ ? 1u : 0u, gz::slot_count, state_, window_, close_route_, live_,
        static_cast<unsigned long>(base_ + F::imports[F::open_index].slot_rva),
        static_cast<unsigned long>(base_ + F::imports[F::close_index].slot_rva));
    SetLastError(error);
    return active_;
}
bool active() {
    return active_;
}
const char* state() {
    return state_;
}
#ifdef X3M_GZ_BUFFER_FL_FIXTURE
void fixture_bind(std::uintptr_t base, void* const exports[7]) {
    fixture_base_ = base;
    for (unsigned i = 0; i < 7; ++i) fixture_exports_[i] = exports[i];
}
void fixture_reset() {
    initialized_ = active_ = false;
    state_ = "disabled";
    share::buffer_entry = share::buffer_forward = share::buffer_holds_slot = 0;
}
#endif
}
