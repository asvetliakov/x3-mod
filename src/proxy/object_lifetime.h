#pragma once
#include <windows.h>
#include <cstdint>

// Exact-version, opt-in lifetime evidence for X3's render-node registry.
// Installation/rollback must be quiescent and outside DllMain. Storage lifetime
// is distinct from camera-cut policy. Existing entries may be adopted only by a
// complete validated quiescent installation snapshot; never lazily during draws.
namespace x3m::object_lifetime {
enum class Reason : std::uint32_t {
    Known, Disabled, MutationInProgress, RegistryUnavailable, RegistryMismatch,
    UnknownNodeBirth, UnknownCameraBirth, PointerMismatch, LookupUnavailable,
    RegistryChanged, CapacityExhausted, CounterExhausted, ObserverFailure
};
struct Snapshot {
    bool known = false;
    Reason reason = Reason::Disabled;
    std::uint64_t observer_epoch = 0, load_epoch = 0, registry_epoch = 0;
    std::uint64_t node_serial = 0, camera_serial = 0, mutation_revision = 0;
};
struct Stats { bool baseline_complete=false; std::uint32_t baseline_entries=0; };
bool initialize(); // X3M_OBJECT_LIFETIME=1, exact executable + in-memory code gate
bool active();
bool recovery_required(); // code/protection ownership remains while disabled
const char* status();
Stats stats();
bool current(std::uintptr_t registry, std::uintptr_t node, std::uint32_t node_handle,
             std::uintptr_t camera, std::uint32_t camera_handle, Snapshot* out);
bool shutdown(); // does not overwrite a foreign replacement; retry is supported

// Bounded retirement journal (shadow-caster-retention.md, "Retirement"). Fixed
// ring, no allocation; appended under the observer's own lock at the existing
// retirement points (removal, overwrite, failed membership in current()) and at
// every whole-table invalidation (load/registry epoch bump, registry
// destruction or loss, foreign unwind, ownership loss, capacity exhaustion,
// shutdown). Nothing is written while no consumer is registered. Same calling
// contract as current(): any thread, never from inside an observed engine call.
constexpr std::uint32_t JournalCapacity = 512; // power of two
enum class JournalKind : std::uint32_t { Retired, FlushAll };
struct JournalEntry {
    JournalKind kind = JournalKind::Retired;
    std::uint32_t reserved = 0;
    // Epochs at the append. A FlushAll is appended with the table clear, which
    // may precede the bump it belongs to; JournalDrain carries the current ones.
    std::uint64_t load_epoch = 0, registry_epoch = 0;
    std::uint64_t node_serial = 0; // Retired only; any registry entry, cameras included
};
struct JournalCursor { std::uint64_t sequence = 0; };
struct JournalDrain {
    std::uint32_t count = 0; // entries copied, oldest first
    bool overflow = false;   // entries were lost or the cursor is foreign: cursor was moved to
                             // the head; revalidate every retained node through current()
    bool available = false;  // false: observer disabled or no consumer; revalidate as well
    bool more = false;       // the output buffer was smaller than the backlog; drain again
    std::uint64_t load_epoch = 0, registry_epoch = 0, mutation_revision = 0;
};
struct JournalStats { std::uint64_t head = 0; std::uint32_t consumers = 0; };
JournalCursor journal_register(); // starts journaling with the first consumer; cursor at the head
void journal_unregister();        // a cursor from an ended registration later drains as overflow
JournalDrain journal_drain(JournalCursor& cursor, JournalEntry* out, std::uint32_t capacity);
JournalStats journal_stats();
// Once production detours have been published, successful shutdown retains the
// tiny forwarding trampolines until process exit and forbids reinstallation.

#ifdef X3M_OBJECT_LIFETIME_FIXTURE
// Original synthetic code only; absent from production. Every site uses the
// reviewed original instruction layout/ABI, but no game bytes are redistributed.
struct FixtureSites {
    void* insert_entry = nullptr;
    void* remove_nonempty = nullptr;
    void* destroy_entry = nullptr;
    void* load_call = nullptr;
    void* load_target = nullptr;
    std::uintptr_t engine_slot = 0;
};
bool fixture_install(const FixtureSites&, unsigned capacity = 16384,
                     unsigned fail_stage = 0, unsigned fail_site = 0, bool initial_snapshot = true,
                     bool retain_dispatch = false); // false: fixture guarantees no retained callers
bool fixture_shutdown(unsigned fail_stage = 0, unsigned fail_site = 0);
#endif
} // namespace x3m::object_lifetime
