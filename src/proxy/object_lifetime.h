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
// ring (2048 entries x 32 bytes = 64 KB static), no allocation; appended under
// the observer's own lock at the existing retirement points (removal, overwrite,
// failed membership in current()) and at every whole-table invalidation
// (load/registry epoch bump, registry rebind, destruction or loss, foreign
// unwind, ownership loss, capacity exhaustion, install, shutdown). Nothing is
// written while no consumer is registered. Same calling contract as current():
// any thread, never from inside an observed engine call.
// Consumer contract:
// - Revalidate fully at registration and at every re-registration: a fresh
//   cursor starts at the head and says nothing about earlier retirements.
// - Revalidate fully when a drain reports overflow or available=false.
// - FlushAll is an unconditional drop of everything retained. Epoch comparisons
//   use JournalDrain's fields, never the entry's.
// - Retired means the observer dropped the key (including a membership lookup
//   that was merely unavailable), not proof that the node died.
constexpr std::uint32_t JournalCapacity = 2048; // power of two
enum class JournalKind : std::uint32_t { Retired, FlushAll };
struct JournalEntry {
    JournalKind kind = JournalKind::Retired;
    std::uint32_t reserved = 0;
    // Epochs at the append, informative only: a FlushAll is appended with the
    // table clear, which may precede the bump it belongs to.
    std::uint64_t load_epoch = 0, registry_epoch = 0;
    std::uint64_t node_serial = 0; // Retired only; any registry entry, cameras included
};
struct JournalCursor {
    static constexpr std::uint64_t Invalid = ~std::uint64_t{0};
    std::uint64_t sequence = Invalid;
    bool valid() const { return sequence != Invalid; }
};
struct JournalDrain {
    std::uint32_t count = 0; // entries copied, oldest first
    bool overflow = false;   // entries were lost or the cursor is foreign: a valid cursor was moved
                             // to the head; revalidate every retained node through current()
    bool available = false;  // false: observer disabled, no consumer or invalid cursor; revalidate
    bool more = false;       // the output buffer was smaller than the backlog; drain again
    bool invalid = false;    // out == nullptr or capacity == 0: nothing done, cursor unmoved, more=false
    std::uint64_t load_epoch = 0, registry_epoch = 0, mutation_revision = 0;
};
struct JournalStats { std::uint64_t head = 0; std::uint32_t consumers = 0; };
// Starts journaling with the first consumer. Refused (invalid cursor, no count
// taken, so no unregister is owed) when the consumer count is saturated.
JournalCursor journal_register();
void journal_unregister(); // once per valid registration; its cursor later drains as overflow
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
void fixture_journal_consumers(unsigned count); // saturation seam only
#endif
} // namespace x3m::object_lifetime
