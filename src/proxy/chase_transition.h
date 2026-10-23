#pragma once
#include <cstdint>

// Read-only native cockpit lifetime/update witnesses plus optional transition
// diagnostics. Install before the engine's first cockpit construction. Unseen,
// partial, destroyed and capacity-refused lifetimes fail closed (generation 0).
namespace x3m::chase_transition {
struct Update {
    std::uint64_t generation = 0, serial = 0;
    std::uint32_t thread = 0;
};
bool initialize();
bool installed();
bool diagnostics_active();
// X3M_CHASE_VIEW_RESTORE=1: the one-use rear-chase restore ticket (engine
// path: injection site 0x00410100 plus three VM cancel sites) is installed.
bool restore_installed();
// The restore engine path's injection site (0x00410100) and its three VM cancel sites, shared with one more consumer
// (X3M_SETA_RESTORE, docs/architecture/seta-restore.md): claimed once by whichever attaches first (the chase restore
// in initialize(), the consumer through inject_attach), never twice. The stub's prefilter word carries one bit per
// consumer, so an idle site costs one compare whoever owns it. At the site's entry the chase ticket runs first, then
// the consumer's poll; a call the poll hands out runs through the engine's by-name wrapper `target(0, 0, key, name,
// return_kind, argc[, value])` under the engine's incoming x87/MXCSR/LastError image (callback slot and cookie stay 0),
// and its AL goes back through result(). vm() runs at the VM clear (kind 1) and load (kind 2) sites, unwind() at the
// EH adapter (lock-free). No lock of this module is held across any callback.
struct InjectCall {
    std::uintptr_t target = 0;
    std::uint32_t key = 0;
    const char* name = nullptr;
    std::uint32_t return_kind = 1, argc = 0, value = 0;
};
struct InjectConsumer {
    bool (*poll)(std::uint32_t controlled, InjectCall& call) noexcept; // controlled = [ESP+4] at the site
    void (*result)(unsigned al) noexcept;
    void (*vm)(unsigned kind) noexcept;
    void (*unwind)() noexcept;
};
// Inside the engine_patch install window only for the first claim; *status = ok or the refusing site's status.
bool inject_attach(const InjectConsumer* consumer, const char** status) noexcept;
// Detaches the consumer; restores the four sites when the chase restore does not hold them.
bool inject_detach() noexcept;
// Sets or clears the consumer's prefilter bit (lock-free).
void inject_want(bool wanted) noexcept;
// Chase hook (0x00420e06) visit for the restore ticket: the fields the chase
// camera already read from the cockpit block. Cheap when nothing is armed.
struct AppliedFrame {
    std::uintptr_t cockpit = 0;
    std::uint32_t mode = 0, connect = 0, flags = 0, ref = 0, view = 0, sector = 0, sector54 = 0; // +0x1fc, +0x54
};
void restore_observe_frame(const AppliedFrame& frame) noexcept;
std::uint64_t generation(std::uintptr_t cockpit) noexcept;
// A nested/new updater permanently revokes the previous token on that thread.
// The caller still validates active registry, ship, camera and view identities.
Update current_update(std::uintptr_t cockpit) noexcept;
// run78 pose gap: a complete lifetime whose registry active-control handle
// (*0x00608504+0x10, the walk of the native resolver 0x0041cd20) maps to this
// cockpit. The chase camera uses it only while the cockpit's ref view object
// (+0x10) is still 0 on a fresh generation; anything unseen, partial,
// destroyed or foreign fails closed. LastError preserved.
bool active_control_cockpit(std::uintptr_t cockpit) noexcept;
void report(std::uint64_t frame);
// Called only after game-thread quiescence; executable arena remains allocated.
void shutdown();
}
