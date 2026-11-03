#pragma once
#include <cstdint>
#include <cstring>

// The write / read-back / rollback / restore sequence of an in-place FL instruction patch (no stub, no emitted code),
// independent of the memory API: src/engine/fl/pause_key_only_fl.cpp (13 bytes) and run_in_background_fl.cpp (2 bytes)
// drive it with VirtualProtect / engine_patch::write_code / FlushInstructionCache, the native fixtures with a fault-
// injecting buffer. The shape of lod_patch_sites_fl.h's one-byte install(), widened to a span of up to 16 bytes. Ops
// provides read(at, out, n), protect(at, n, protection, &previous), write(at, bytes, n, &atomic) and `writable`. No
// Windows dependency.
namespace x3m::fl::inline_patch_fl {
constexpr unsigned max_span = 16;
// Writes `patched` over `original` at `at` (the caller verified its windows, the original bytes included). "ok" or the
// reason; *live says whether the patched bytes may be in place (true after "ok" and "rollback_failed": the caller keeps
// the site registered); *protection is the page protection found before the write. A failed read-back or re-protect
// puts the original bytes back, judged by a read-back of the original bytes, not by the protection calls.
template <class Ops>
inline const char* install(Ops& ops, std::uintptr_t at, const unsigned char* original, const unsigned char* patched,
                           unsigned n, bool* live, bool* atomic, std::uint32_t* protection) {
    *live = false;
    *atomic = false;
    if (!n || n > max_span) return "invalid_site";
    if (!ops.protect(at, n, Ops::writable, protection)) return "protect_failed";
    *live = true; // ownership published before the mutation, so a failed rollback stays registered
    const bool flushed = ops.write(at, patched, n, atomic);
    unsigned char readback[max_span]{};
    const bool verified = flushed && ops.read(at, readback, n) && !std::memcmp(readback, patched, n);
    std::uint32_t unused = 0;
    const bool protected_again = ops.protect(at, n, *protection, &unused);
    if (verified && protected_again) return "ok";
    std::uint32_t writable = 0;
    bool reprotected = false, ignored = false;
    if (!protected_again || ops.protect(at, n, Ops::writable, &writable)) {
        ops.write(at, original, n, &ignored);
        reprotected = ops.protect(at, n, *protection, &unused);
    }
    unsigned char back[max_span]{};
    if (ops.read(at, back, n) && !std::memcmp(back, original, n)) {
        *live = false;
        return reprotected ? "patch_rolled_back" : "rollback_unprotected";
    }
    return "rollback_failed";
}
// Puts the original bytes back only over the patched ones (engine_patch::restore's rule): "restored" when the span held
// the patched bytes and reads the original afterwards (or already held the original: no write), "restore_not_owned"
// when it held anything else or was unreadable (nothing written), "restore_failed" when the protection or the store
// failed.
template <class Ops>
inline const char* restore(Ops& ops, std::uintptr_t at, const unsigned char* original, const unsigned char* patched,
                           unsigned n, std::uint32_t protection) {
    unsigned char found[max_span]{};
    if (!n || n > max_span) return "restore_not_owned";
    const bool found_read = ops.read(at, found, n);
    if (found_read && !std::memcmp(found, original, n)) return "restored";
    if (!found_read || std::memcmp(found, patched, n)) return "restore_not_owned";
    std::uint32_t previous = 0, unused = 0;
    bool ignored = false;
    if (!ops.protect(at, n, Ops::writable, &previous)) return "restore_failed";
    const bool flushed = ops.write(at, original, n, &ignored);
    unsigned char back[max_span]{};
    const bool verified = flushed && ops.read(at, back, n) && !std::memcmp(back, original, n);
    const bool protected_again = ops.protect(at, n, protection, &unused);
    return verified && protected_again ? "restored" : "restore_failed";
}
}
