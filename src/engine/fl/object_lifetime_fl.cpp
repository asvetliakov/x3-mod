// FL edition of x3m::object_lifetime (src/proxy/object_lifetime.h): X3FL.exe's render-node registry observer
// (docs/architecture/farnhams-legacy-lifetime.md; docs/reverse-engineering/fl/object-lifetimes.md). The model,
// table, baseline, current() and failure semantics mirror src/proxy/object_lifetime.cpp (the AP file); what
// differs is the site set, the ABI and the patch mechanics:
//
// - Sites (generated object_lifetime_fl_inc.h, every address fl::module_base() + RVA): the whole entries of
//   insert, remove and destroy (six bytes each, claimed through engine_patch::claim: a byte-checked JMP into
//   the engine_patch arena, whose tail replays the six bytes and jumps back; this file's stub is pushed in
//   front of the tail) and the renderer-load `call rel32` (engine_patch::claim_call). The destroy tail replays
//   `cmp dword [ebx],0` itself, so the ZF the continuation's JE reads is the replayed one.
// - ABI: insert ECX = map, EDX = key, [esp+4] = value; remove ECX = map, EDX = key; destroy ECX = map; load
//   ECX = stream. All four end in a plain RET (caller cleanup); the dispatcher forwards ECX/EDX/every GPR and
//   EFLAGS, copies one stack word (the insert value) and returns with a plain RET.
// - CFG: the game reaches the arena by a direct JMP/CALL rel32 and the arena reaches the stub by `jmp [entry]`
//   inside our own emitted code; X3FL.exe's guard table is empty and not enforced (farnhams-legacy-build.md).
//
// The dispatcher saves EFLAGS, all GPRs and an FXSAVE image around enter()/leave(), so the engine call sees
// its own register, x87, MXCSR and XMM state and the caller receives the target's output state; the bookkeeping
// may call Windows (engine_memory's VirtualQuery) inside that envelope. This file is built
// -fno-exceptions -mno-sse -mno-mmx -mfpmath=387 (CMakeLists.txt) and executes no floating point.
// Publication of an insert additionally waits for mutation depth zero: an insert finishing while another
// insert/remove/destroy of the bound registry is in flight (an allocator-recovery reentry, another thread's
// rehash) stays unpublished (the node is UnknownNodeBirth until reinserted), never published from a header
// another mutation may be rewriting.
// Retirement journal (object_lifetime.h, AP semantics): Retired(serial) in retire(), FlushAll in clear_entries(),
// both under model_lock and only while a consumer is registered. The unpublished-insert rule: a consumer retains a
// node only from a Known current() result, which needs a published serial, so a key that was never published (an
// insert finishing nested or with a failed membership lookup, a dormant or other-map birth) has nothing to retire
// and gets no entry; its later remove finds no key and appends nothing. Every published serial leaves the table
// only through retire() (insert/remove entry, failed membership), clear_entries() or occupy() over a live key,
// and all three journal it; occupy() over a live key is unreachable under the mutation-depth rule (an insert in
// flight holds mutations > 0, so neither another publish nor the load re-baseline can make its key live before it
// finishes) and appends Retired(old serial) regardless. The load path needs no extra FlushAll: enter() already
// appends one with the clear before forwarding, and the survivors are absent from the table until the re-baseline
// adopts them with fresh serials under the same lock that ends the load's in_flight (current() reports
// MutationInProgress until then), so no consumer can hold a serial issued between the flush and the adoption.
// A renderer load clears the table and advances load_epoch before forwarding (AP); after its normal return the
// registry entries that survived it are adopted again with fresh serials (FL identities survive a load,
// registry-binding.md), the keys born during the load keeping the serials the insert hook gave them.
#include "../../proxy/object_lifetime.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include <excpt.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 registry ABI only");
namespace {
namespace layout {
#include "object_lifetime_fl_inc.h"
}
static_assert(sizeof layout::insert_entry == 6 && sizeof layout::remove_entry == 6 && sizeof layout::destroy_entry == 6,
              "six-byte entry spans");
static_assert(layout::load_anchor[layout::load_site_offset] == 0xe8, "the load site is a call rel32");
using x3m::object_lifetime::Reason;
constexpr unsigned MaxEntries = x3m::object_lifetime::RegistryCapacity;
static_assert(MaxEntries && !(MaxEntries & (MaxEntries - 1)) && MaxEntries <= (1u << 24));
constexpr unsigned ReasonCount = unsigned(Reason::ObserverFailure) + 1;
static_assert(ReasonCount == 13, "reason names below");
constexpr const char* reason_names[ReasonCount] = {
    "known",           "disabled",           "mutation_in_progress", "registry_unavailable", "registry_mismatch",
    "unknown_node",    "unknown_camera",     "pointer_mismatch",     "lookup_unavailable",   "registry_changed",
    "capacity",        "counter_exhausted",  "observer_failure"};
enum Kind : unsigned { Insert, Remove, Destroy, Load };
struct Entry {
    std::uint32_t key = 0;
    std::uintptr_t value = 0;
    std::uint64_t serial = 0;
    unsigned state = 0;
};
static_assert(sizeof(Entry) == 24);
// One per dispatcher frame; the first two words are a real x86 SEH registration record.
struct Scope {
    void* previous_seh;
    void* handler;
    unsigned kind;
    std::uintptr_t args[1]; // the forwarded stack word: the insert value, 0 for the others
    std::uintptr_t map;
    std::uint32_t key, value;
    bool entered;
    std::uint64_t registry_epoch, load_epoch;
};
static_assert(offsetof(Scope, args) == 12 && offsetof(Scope, entered) == 28 && sizeof(Scope) <= 64,
              "the dispatcher's frame offsets");
// PUSHFD/PUSHAD snapshot, then the stub's kind word and the untouched caller frame.
struct Registers {
    std::uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags, kind, return_address, args[1];
};
static_assert(offsetof(Registers, flags) == 32 && offsetof(Registers, kind) == 36 && offsetof(Registers, args) == 44);
std::atomic_flag model_lock = ATOMIC_FLAG_INIT;
struct Lock {
    Lock() {
        while (model_lock.test_and_set(std::memory_order_acquire)) {}
    }
    ~Lock() { model_lock.clear(std::memory_order_release); }
};
std::array<Entry, MaxEntries> entries{};
unsigned live = 0, peak_live = 0, max_probe = 0;
// Installation-only scratch, never used to adopt an individual draw lazily.
std::array<std::uintptr_t, 65536> baseline_buckets{};
std::array<std::uintptr_t, MaxEntries> baseline_pointers{};
struct BaselineEntry {
    std::uintptr_t address;
    std::uint32_t words[3];
};
std::array<BaselineEntry, MaxEntries> baseline_records{};
bool baseline_complete = false;
unsigned baseline_count = 0;
const char* baseline_reason = "not_run"; // no_engine, no_registry, complete, rejected, not_run
unsigned capacity = MaxEntries, in_flight = 0, mutations = 0;
std::uintptr_t engine_slot = 0, registry = 0;
bool registry_dead = false;
std::uint64_t observer_epoch = 0, load_epoch = 0, registry_epoch = 0, revision = 0, next_serial = 0;
std::atomic<bool> observation{false};
std::atomic<const char*> diagnostic{"disabled"};
const char* failed_site = "none";
Reason disabled_reason = Reason::Disabled;
x3m::object_lifetime::DisableEvent disable_event{};
std::atomic<bool> disable_event_pending{false};
x3m::engine_patch::Site entry_sites[3];
x3m::engine_patch::CallSite load_site;
std::uintptr_t install_base = 0, install_sites[5]{};
// fl.lifetime_sample counters, written under model_lock and taken (and reset) per row.
struct Counters {
    unsigned long inserts, removes, destroys, loads, unwinds, other_maps, dormant, published, unpublished_nested,
        unpublished_lookup, binds, rebind_without_destroy, current_calls, rebaselined, rebaseline_rejected,
        rebaseline_skipped;
    unsigned long long rebaseline_ticks_max; // the longest post-load re-baseline of the period (QPC ticks)
    unsigned long reasons[ReasonCount];
};
Counters counters{};
long long sample_period = 0, sample_due = 0;
bool sample_force = false;
unsigned long sample_rows = 0;
std::uintptr_t sample_registry = 0;
bool sample_have_registry = false;

bool read_memory(std::uintptr_t address, void* out, std::size_t size) {
    return x3m::engine_memory::read(address, out, size);
}
// Callers hold model_lock (AP semantics: the first event per installation is kept until taken).
void fail(Reason reason, const char* text) {
    if (observation && !disable_event_pending.load(std::memory_order_relaxed)) {
        disable_event = {reason, text, live, peak_live, capacity, load_epoch, registry_epoch, revision, false};
        disable_event_pending.store(true, std::memory_order_release);
    }
    disabled_reason = reason;
    observation = false;
    diagnostic = text;
}
bool increment(std::uint64_t& value) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        fail(Reason::CounterExhausted, "counter_exhausted");
        return false;
    }
    ++value;
    return true;
}
// Retirement journal (AP file): written only under model_lock and only for a registered consumer. Integer only.
using x3m::object_lifetime::JournalCapacity;
using x3m::object_lifetime::JournalKind;
static_assert(JournalCapacity && !(JournalCapacity & (JournalCapacity - 1)));
std::array<x3m::object_lifetime::JournalEntry, JournalCapacity> journal{};
std::uint64_t journal_head = 0;
unsigned journal_consumers = 0;
void journal_append(JournalKind kind, std::uint64_t serial) {
    if (!journal_consumers) return;
    journal[static_cast<unsigned>(journal_head) & (JournalCapacity - 1)] = {kind, 0, load_epoch, registry_epoch,
                                                                            serial};
    ++journal_head;
}
void clear_entries() {
    for (auto& entry : entries) entry = {};
    live = 0;
    journal_append(JournalKind::FlushAll, 0);
}
bool read_registry(std::uintptr_t& result) {
    std::uintptr_t engine = 0;
    result = 0;
    return read_memory(engine_slot, &engine, 4) && engine && engine <= 0xfffffff3u &&
           read_memory(engine + layout::registry_offset, &result, 4) && result;
}
bool bind_registry(std::uintptr_t value) {
    if (!value) return false;
    if (registry != value) {
        // A registry that changes without a destroy of the bound one (an engine freed by the factory's
        // constructor-unwind cleanup, never by destroy) is counted: rebind_without_destroy.
        if (registry && !registry_dead) ++counters.rebind_without_destroy;
        ++counters.binds;
        registry = value;
        registry_dead = false;
        clear_entries();
        return increment(registry_epoch) && increment(revision);
    }
    return true;
}
unsigned home(std::uint32_t key) {
    return (key * 2654435761u) % capacity;
}
// Linear probing without tombstones (AP file): state 0 empty, 1 live; a search ends at the first empty slot.
Entry* find(std::uint32_t key, bool create) {
    if (!key) return nullptr;
    const unsigned first = home(key);
    for (unsigned n = 0; n < capacity; ++n) {
        auto& entry = entries[(first + n) % capacity];
        if (entry.state == 0 || entry.key == key) {
            if (n >= max_probe) max_probe = n + 1;
            if (entry.state) return &entry;
            return create ? &entry : nullptr;
        }
    }
    max_probe = capacity;
    return nullptr;
}
void occupy(Entry* entry, std::uint32_t key, std::uintptr_t value, std::uint64_t serial) {
    if (entry->state == 1)
        journal_append(JournalKind::Retired, entry->serial); // never reached (header comment); never lose a serial
    else if (++live > peak_live)
        peak_live = live;
    *entry = {key, value, serial, 1};
}
// Backward-shift deletion (Knuth 6.4, Algorithm R), as the AP file.
void erase(Entry* entry) {
    unsigned hole = static_cast<unsigned>(entry - entries.data()), slot = hole;
    entries[hole] = {};
    for (;;) {
        slot = slot + 1 == capacity ? 0 : slot + 1;
        auto& next = entries[slot];
        if (next.state == 0) return;
        const unsigned h = home(next.key);
        const bool stays = hole <= slot ? (hole < h && h <= slot) : (hole < h || h <= slot);
        if (stays) continue;
        entries[hole] = next;
        next = {};
        hole = slot;
    }
}
void retire(std::uint32_t key) {
    if (auto* entry = find(key, false)) {
        journal_append(JournalKind::Retired, entry->serial);
        erase(entry);
        --live;
    }
}
enum class Lookup { Found, Missing, Unavailable };
// FL map: +0 bucket array, +4 bucket count (power of two), +8 key counter, +0xc entry count; 12-byte hash nodes
// {next, key, value}; bucket = key & (count - 1) (object-lifetimes.md, "Registry ownership, layout").
Lookup lookup(std::uintptr_t map, std::uint32_t key, std::uintptr_t& value) {
    value = 0;
    std::uint32_t header[4]{};
    if (!key || !read_memory(map, header, sizeof header)) return Lookup::Unavailable;
    const auto buckets = header[1];
    if (!buckets || buckets > (1u << 24) || (buckets & (buckets - 1))) return Lookup::Unavailable;
    if (!header[0]) return Lookup::Missing;
    const std::uint64_t slot = std::uint64_t(header[0]) + std::uint64_t(key & (buckets - 1)) * 4;
    std::uint32_t address = 0;
    if (slot > 0xffffffffu || !read_memory(static_cast<std::uintptr_t>(slot), &address, 4)) return Lookup::Unavailable;
    for (unsigned n = 0; address && n < 512; ++n) {
        std::uint32_t item[3]{};
        if (!read_memory(address, item, sizeof item)) return Lookup::Unavailable;
        if (item[1] == key) {
            value = item[2];
            return Lookup::Found;
        }
        address = item[0];
    }
    return address ? Lookup::Unavailable : Lookup::Missing;
}
// The AP baseline walk over the FL map layout, without touching the table: bounded header and bucket reads,
// every key in its masked bucket, value+0x28 == key, unique pointers, the exact count, then the header, the
// bucket heads and every visited node read again. The records land in baseline_records[0, count).
bool collect(std::uintptr_t map, unsigned& count) {
    count = 0;
    std::uint32_t header[4]{}, again[4]{};
    if (!read_memory(map, header, sizeof header)) return false;
    const auto bucket_count = header[1], expected_count = header[3];
    if (!bucket_count || bucket_count > baseline_buckets.size() || (bucket_count & (bucket_count - 1)) ||
        expected_count > capacity)
        return false;
    if (!header[0])
        return expected_count == 0 && read_memory(map, again, sizeof again) && !std::memcmp(header, again, sizeof header);
    if (!read_memory(header[0], baseline_buckets.data(), bucket_count * 4)) return false;
    baseline_pointers.fill(0);
    unsigned n = 0;
    for (unsigned bucket = 0; bucket < bucket_count; ++bucket) {
        auto address = baseline_buckets[bucket];
        while (address) {
            if (n >= expected_count || n >= capacity) return false;
            auto& raw = baseline_records[n];
            raw.address = address;
            if (!read_memory(address, raw.words, sizeof raw.words)) return false;
            const auto key = raw.words[1], value = raw.words[2];
            std::uint32_t handle = 0;
            if (!key || !value || value > 0xffffffd7u || (key & (bucket_count - 1)) != bucket ||
                !read_memory(value + layout::node_handle, &handle, 4) || handle != key)
                return false;
            unsigned position = ((value >> 2) * 2654435761u) % capacity;
            while (baseline_pointers[position] && baseline_pointers[position] != value)
                position = (position + 1) % capacity;
            if (baseline_pointers[position]) return false;
            baseline_pointers[position] = value;
            ++n;
            address = raw.words[0];
        }
    }
    if (n != expected_count || !read_memory(map, again, sizeof again) || std::memcmp(header, again, sizeof header))
        return false;
    for (unsigned i = 0; i < n; ++i) {
        std::uint32_t words[3]{}, handle = 0;
        if (!read_memory(baseline_records[i].address, words, sizeof words) ||
            std::memcmp(words, baseline_records[i].words, sizeof words) ||
            !read_memory(words[2] + layout::node_handle, &handle, 4) || handle != words[1])
            return false;
    }
    if (std::uint64_t(header[0]) + std::uint64_t(bucket_count) * 4 > 0x100000000ull) return false;
    for (unsigned i = 0; i < bucket_count; ++i) {
        std::uintptr_t head = 0;
        if (!read_memory(header[0] + i * 4, &head, 4) || head != baseline_buckets[i]) return false;
    }
    count = n;
    return true;
}
// Installation baseline (quiescent, AP semantics): every collected key adopted under the new observer epoch, a
// duplicate key rejects the whole baseline. With no engine yet (the DLL loads before X3FL.exe constructs it) there
// is nothing to adopt: every key of the registry is later born through the insert hook.
bool initial_snapshot() {
    baseline_complete = false;
    baseline_count = 0;
    baseline_reason = "rejected";
    clear_entries();
    std::uintptr_t engine = 0, map = 0;
    if (!read_memory(engine_slot, &engine, 4) || !engine) {
        baseline_reason = "no_engine";
        return false;
    }
    if (!read_registry(map)) {
        baseline_reason = "no_registry";
        return false;
    }
    unsigned count = 0;
    if (!bind_registry(map) || !collect(map, count)) {
        clear_entries();
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto key = baseline_records[i].words[1];
        Entry* entry = find(key, false) ? nullptr : find(key, true);
        if (!entry || !increment(next_serial)) {
            clear_entries();
            return false;
        }
        occupy(entry, key, baseline_records[i].words[2], next_serial);
    }
    baseline_count = count;
    baseline_complete = true;
    baseline_reason = "complete";
    return true;
}
// After a renderer load returned normally (docs/reverse-engineering/fl/registry-binding.md, "Registry replacement,
// load"): identities that survive the load (the view camera among them) are adopted again under the load epoch
// enter() already advanced, with fresh serials; keys the insert hook published during the load keep theirs (a
// key already live is skipped), keys removed by the load are absent from the map. Caller holds model_lock with no
// registry mutation in flight: every hooked writer blocks in enter() before forwarding until this returns.
void rebaseline_after_load() {
    std::uintptr_t current = 0;
    if (!read_registry(current) || !bind_registry(current) || registry_dead) {
        ++counters.rebaseline_rejected;
        return;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    unsigned count = 0, adopted = 0;
    if (!collect(registry, count)) {
        ++counters.rebaseline_rejected;
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto key = baseline_records[i].words[1];
        Entry* entry = find(key, true);
        if (!entry) {
            fail(Reason::CapacityExhausted, "capacity_exhausted");
            clear_entries();
            increment(revision);
            return;
        }
        if (entry->state) continue; // published by the insert hook during the load
        if (!increment(next_serial)) return;
        occupy(entry, key, baseline_records[i].words[2], next_serial);
        ++adopted;
    }
    if (adopted) increment(revision);
    QueryPerformanceCounter(&end);
    counters.rebaselined += adopted;
    const auto ticks = static_cast<unsigned long long>(end.QuadPart - begin.QuadPart);
    if (ticks > counters.rebaseline_ticks_max) counters.rebaseline_ticks_max = ticks;
}
// The four sites still hold this module's five patch bytes. Per current() call, so read directly: the sites are
// code of the main image (mapped and readable for the process lifetime), never heap; a patched_in site is one
// this module wrote and has not restored. (engine_memory::read costs about 30 ns per call: four of the twelve
// reads of a current() call; fixture TIMING.)
bool sites_owned() {
    for (const auto& site : entry_sites)
        if (!site.patched_in || std::memcmp(reinterpret_cast<const void*>(site.spec.address), site.patched, 5))
            return false;
    return load_site.patched_in && !std::memcmp(reinterpret_cast<const void*>(load_site.address), load_site.patched, 5);
}
bool verify_ownership() {
    if (sites_owned()) return true;
    fail(Reason::ObserverFailure, "hook_ownership_lost");
    clear_entries();
    increment(revision);
    return false;
}
void finish(Scope* scope, bool normal) {
    if (!scope->entered) return;
    Lock lock;
    scope->entered = false;
    if (scope->kind != Load && mutations) --mutations;
    if (in_flight)
        --in_flight;
    else {
        fail(Reason::ObserverFailure, "scope_underflow");
        return;
    }
    if (!normal) {
        ++counters.unwinds;
        clear_entries();
        increment(load_epoch);
        increment(revision);
        return;
    }
    if (scope->kind == Load) {
        // A normal return of the load: the survivors are adopted again (rebaseline_after_load). Another load or an
        // unwind since this one began (load_epoch moved) or a registry mutation still in flight skips it.
        if (observation && verify_ownership() && scope->load_epoch == load_epoch) {
            if (mutations)
                ++counters.rebaseline_skipped;
            else
                rebaseline_after_load();
        }
        return;
    }
    if (!observation || !verify_ownership() || scope->kind != Insert || scope->registry_epoch != registry_epoch ||
        scope->load_epoch != load_epoch || registry_dead || registry != scope->map || !scope->key || !scope->value)
        return;
    if (mutations) {
        ++counters.unpublished_nested;
        return;
    }
    std::uintptr_t current = 0;
    if (lookup(scope->map, scope->key, current) != Lookup::Found || current != scope->value) {
        ++counters.unpublished_lookup;
        return;
    }
    auto* entry = find(scope->key, true);
    if (!entry) {
        fail(Reason::CapacityExhausted, "capacity_exhausted");
        clear_entries();
        increment(revision);
        return;
    }
    if (!increment(next_serial)) return;
    occupy(entry, scope->key, scope->value, next_serial);
    ++counters.published;
}
}

extern "C" {
// The forwarding targets by kind: the three engine_patch tails (replayed entry bytes + jmp back) and the
// renderer-load target. Written before the matching stub goes live.
std::uintptr_t x3m_fl_lifetime_originals[4]{};
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_lifetime_enter(Scope* scope, const Registers* registers) {
    const DWORD error = GetLastError();
    scope->entered = false;
    scope->kind = registers->kind;
    scope->args[0] = scope->kind == Insert ? registers->args[0] : 0;
    scope->map = scope->kind == Load ? 0 : registers->ecx;
    scope->key = (scope->kind == Insert || scope->kind == Remove) ? registers->edx : 0;
    scope->value = scope->args[0];
    if (observation) {
        Lock lock;
        if (observation) {
            std::uintptr_t current = 0;
            if (scope->kind == Load) {
                ++counters.loads;
                clear_entries();
                increment(load_epoch);
                increment(revision);
            } else if (!read_registry(current)) {
                // Generic maps are used before the engine exists (install precedes it): dormant, nothing trusted
                // yet. Losing a bound registry fails closed.
                ++counters.dormant;
                if (registry) {
                    fail(Reason::RegistryUnavailable, "registry_unavailable");
                    clear_entries();
                    increment(registry_epoch);
                    increment(revision);
                }
            } else if (bind_registry(current) && scope->map == registry) {
                if (scope->kind == Destroy) {
                    // Engine teardown (RVA 0x8abad passes engine+0xc, then frees the engine): nothing cached is
                    // trusted until a frame advances; the dead registry is never read again for a lookup.
                    ++counters.destroys;
                    clear_entries();
                    registry_dead = true;
                    increment(registry_epoch);
                    x3m::engine_memory::begin_shutdown("fl_registry_destroy");
                } else {
                    if (scope->kind == Insert)
                        ++counters.inserts;
                    else
                        ++counters.removes;
                    if (registry_dead && scope->kind == Insert) {
                        registry_dead = false;
                        clear_entries();
                        increment(registry_epoch);
                    }
                    retire(scope->key); // before the engine unlinks (remove) or replaces (insert) the entry
                }
                increment(revision);
            } else
                ++counters.other_maps;
            if (observation && (scope->kind == Load || (registry && scope->map == registry))) {
                if (in_flight == std::numeric_limits<unsigned>::max())
                    fail(Reason::CounterExhausted, "scope_exhausted");
                else {
                    ++in_flight;
                    if (scope->kind != Load) ++mutations;
                    scope->entered = true;
                    scope->registry_epoch = registry_epoch;
                    scope->load_epoch = load_epoch;
                }
            }
        }
    }
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_lifetime_leave(Scope* scope) {
    const DWORD error = GetLastError();
    finish(scope, true);
    SetLastError(error);
}
// A foreign unwind through the engine call: the whole table is dropped and load_epoch advances (AP semantics).
__attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl x3m_fl_lifetime_unwind(EXCEPTION_RECORD* record,
                                                                                              void* frame, CONTEXT*,
                                                                                              void*) {
    alignas(16) unsigned char fx_state[512];
    __asm__ __volatile__("fxsave %0" : "=m"(fx_state));
    const DWORD error = GetLastError();
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        finish(static_cast<Scope*>(frame), false);
    SetLastError(error);
    __asm__ __volatile__("fxrstor %0" ::"m"(fx_state));
    return ExceptionContinueSearch;
}
// Entry from a stub: [esp] kind, [esp+4] return into the engine, [esp+8] the caller's first stack word (the
// insert value). PUSHFD/PUSHAD form Registers at EBP; below it an aligned 512-byte FXSAVE area, the Scope at
// fx+512 (a real SEH record for the duration of the engine call) and three frame slots at EBP-4/-8/-12. The
// target gets every input GPR and EFLAGS from the snapshot, the FX state restored and one copied stack word
// (it ends in a plain RET and reads at most that word); its output GPRs, EFLAGS and FX state reach the caller.
// The copied word and the stub's kind are dropped on return; the caller's own words stay (caller cleanup).
__attribute__((naked)) void x3m_fl_lifetime_dispatch() {
    __asm__ __volatile__("pushfl\n\tpushal\n\tmovl %esp,%ebp\n\tsubl $608,%esp\n\tandl $-16,%esp\n\t"
                         "movl %esp,-4(%ebp)\n\tfxsave (%esp)\n\tleal 512(%esp),%eax\n\tmovl %eax,-8(%ebp)\n\t"
                         "movl %fs:0,%edx\n\tmovl %edx,0(%eax)\n\tmovl $_x3m_fl_lifetime_unwind,4(%eax)\n\t"
                         "movb $0,28(%eax)\n\tmovl %eax,%fs:0\n\tpushl %ebp\n\tpushl %eax\n\t"
                         "call _x3m_fl_lifetime_enter\n\taddl $8,%esp\n\tfxrstor (%esp)\n\t"
                         "movl 36(%ebp),%eax\n\tpushl _x3m_fl_lifetime_originals(,%eax,4)\n\tpushl %ebp\n\t"
                         "movl -8(%ebp),%eax\n\tpushl 12(%eax)\n\t"
                         "pushl 32(%ebp)\n\tpopfl\n\tmovl 0(%ebp),%edi\n\tmovl 4(%ebp),%esi\n\t"
                         "movl 16(%ebp),%ebx\n\tmovl 20(%ebp),%edx\n\tmovl 24(%ebp),%ecx\n\t"
                         "movl 28(%ebp),%eax\n\tmovl 8(%ebp),%ebp\n\tcall *8(%esp)\n\t"
                         "pushfl\n\tpushal\n\tmovl 40(%esp),%ebp\n\tmovl %esp,-12(%ebp)\n\t"
                         "movl -4(%ebp),%eax\n\tfxsave (%eax)\n\tpushl -8(%ebp)\n\t"
                         "call _x3m_fl_lifetime_leave\n\taddl $4,%esp\n\tmovl -8(%ebp),%eax\n\t"
                         "movl 0(%eax),%eax\n\tmovl %eax,%fs:0\n\tmovl -4(%ebp),%eax\n\tfxrstor (%eax)\n\t"
                         "movl -12(%ebp),%esp\n\tpopal\n\tpopfl\n\tmovl 4(%esp),%esp\n\tleal 40(%esp),%esp\n\tret\n\t");
}
__attribute__((naked)) void x3m_fl_lifetime_insert() {
    __asm__ __volatile__("pushl $0\n\tjmp _x3m_fl_lifetime_dispatch");
}
__attribute__((naked)) void x3m_fl_lifetime_remove() {
    __asm__ __volatile__("pushl $1\n\tjmp _x3m_fl_lifetime_dispatch");
}
__attribute__((naked)) void x3m_fl_lifetime_destroy() {
    __asm__ __volatile__("pushl $2\n\tjmp _x3m_fl_lifetime_dispatch");
}
__attribute__((naked)) void x3m_fl_lifetime_load() {
    __asm__ __volatile__("pushl $3\n\tjmp _x3m_fl_lifetime_dispatch");
}
}

namespace x3m::object_lifetime {
namespace {
constexpr const char* site_names[4] = {"insert", "remove", "destroy", "load"};
bool any_owned() {
    for (const auto& site : entry_sites)
        if (site.patched_in) return true;
    return load_site.patched_in;
}
// Restores every site this module still holds, load first. A thread already inside a stub keeps running: the
// arena and this pinned DLL stay mapped.
bool restore_all() {
    observation = false;
    bool ok = !load_site.patched_in || x3m::engine_patch::restore_call(load_site);
    for (unsigned i = 3; i--;)
        if (entry_sites[i].patched_in && !x3m::engine_patch::restore(entry_sites[i])) ok = false;
    return ok;
}
void rollback() {
    const char* reason = diagnostic.load();
    diagnostic = restore_all() ? reason : "rollback_incomplete";
}
bool pin_self() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&x3m_fl_lifetime_originals), &self) != FALSE &&
           self != nullptr;
}
// X3M_FL_LIFETIME_LOG (fl_lifetime_log, as fl_trace_log): seconds between rows, 0 off, unset 10, at most 3600.
unsigned sample_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_LIFETIME_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
void arm_sample() {
    sample_period = sample_due = 0;
    sample_force = false;
    sample_rows = 0;
    sample_have_registry = false;
    sample_registry = 0;
    const unsigned seconds = sample_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        sample_period = frequency.QuadPart * static_cast<long long>(seconds);
        sample_due = now.QuadPart + sample_period;
    }
}
struct Sites {
    std::uintptr_t insert, remove, destroy, load_call, load_target, engine;
};
// The sites are claimed in order insert, remove, destroy, load; each stub goes live only after its forwarding
// target is written; any refusal restores what was claimed (rollback_incomplete keeps recovery_required). The
// observer starts only after all four are live, under a new observer epoch, with the baseline.
bool install(const Sites& s, unsigned requested_capacity, bool snapshot) {
    failed_site = "none";
    if (any_owned() || !requested_capacity || requested_capacity > MaxEntries) {
        diagnostic = "invalid_install";
        return false;
    }
    if (!x3m::engine_patch::install_window_open()) {
        diagnostic = "late_claim";
        return false;
    }
    if (!pin_self()) {
        diagnostic = "pin_failed";
        return false;
    }
    observation = false;
    install_sites[0] = s.insert;
    install_sites[1] = s.remove;
    install_sites[2] = s.destroy;
    install_sites[3] = s.load_call;
    install_sites[4] = s.load_target;
    const std::uintptr_t addresses[3] = {s.insert, s.remove, s.destroy};
    const std::uint8_t* expected[3] = {layout::insert_entry, layout::remove_entry, layout::destroy_entry};
    void* const stubs[3] = {reinterpret_cast<void*>(&x3m_fl_lifetime_insert),
                            reinterpret_cast<void*>(&x3m_fl_lifetime_remove),
                            reinterpret_cast<void*>(&x3m_fl_lifetime_destroy)};
    constexpr const char* spec_names[3] = {"fl_lifetime_insert", "fl_lifetime_remove", "fl_lifetime_destroy"};
    for (unsigned i = 0; i < 3; ++i) {
        auto& site = entry_sites[i];
        site = {};
        x3m::engine_patch::SiteSpec spec{};
        spec.name = spec_names[i];
        spec.address = addresses[i];
        std::memcpy(spec.expected, expected[i], 6);
        spec.length = 6;
        if (!x3m::engine_patch::claim(site, spec)) {
            failed_site = site_names[i];
            diagnostic = site.status;
            rollback();
            return false;
        }
        x3m_fl_lifetime_originals[i] = reinterpret_cast<std::uintptr_t>(site.tail);
        if (x3m::engine_patch::push_front(site, stubs[i]) != site.tail) {
            failed_site = site_names[i];
            diagnostic = "chain_failed";
            rollback();
            return false;
        }
    }
    load_site = {};
    x3m_fl_lifetime_originals[Load] = s.load_target;
    if (!x3m::engine_patch::claim_call(load_site, s.load_call, s.load_target,
                                       reinterpret_cast<void*>(&x3m_fl_lifetime_load))) {
        failed_site = site_names[3];
        diagnostic = load_site.status;
        rollback();
        return false;
    }
    {
        Lock lock;
        capacity = requested_capacity;
        engine_slot = s.engine;
        registry = 0;
        registry_dead = false;
        in_flight = mutations = 0;
        clear_entries();
        peak_live = max_probe = 0;
        if (disable_event_pending.load(std::memory_order_relaxed)) disable_event.before_reinstall = true;
        disabled_reason = Reason::Disabled;
        if (!increment(observer_epoch) || !increment(load_epoch) || !increment(registry_epoch) ||
            !increment(revision)) {
            rollback();
            return false;
        }
        baseline_complete = false;
        baseline_count = 0;
        baseline_reason = "not_run";
        if (snapshot) initial_snapshot();
        if (disabled_reason == Reason::CounterExhausted) {
            rollback();
            return false;
        }
        counters = {};
        observation = true;
        diagnostic = baseline_complete ? "active" : "active_without_baseline";
    }
    arm_sample();
    return true;
}
// The production byte gate at `base`: the three entry spans, the load call (exact, its rel32 to the load target)
// and the load target's entry, and the four engine anchors with their engine operand equal to base + slot.
bool anchor_matches(std::uintptr_t base, std::uint32_t rva, const std::uint8_t* bytes, unsigned length,
                    unsigned operand) {
    unsigned char actual[32]{};
    if (length > sizeof actual || !x3m::engine_patch::read_code(base + rva, actual, length)) return false;
    if (operand != 0xff) {
        std::uint32_t value = 0;
        std::memcpy(&value, actual + operand, 4);
        if (value != base + layout::engine_slot_rva) return false;
        std::memset(actual + operand, 0, 4);
    }
    return !std::memcmp(actual, bytes, length);
}
const char* gate(std::uintptr_t base) {
    using x3m::engine_patch::verify_bytes;
    if (!verify_bytes(base + layout::insert_rva, layout::insert_entry, 6)) return "insert_entry_mismatch";
    if (!verify_bytes(base + layout::remove_rva, layout::remove_entry, 6)) return "remove_entry_mismatch";
    if (!verify_bytes(base + layout::destroy_rva, layout::destroy_entry, 6)) return "destroy_entry_mismatch";
    unsigned char call[sizeof layout::load_anchor]{};
    if (!x3m::engine_patch::read_code(base + layout::load_anchor_rva, call, sizeof call) ||
        std::memcmp(call, layout::load_anchor, sizeof call))
        return "load_site_mismatch";
    std::uint32_t displacement = 0;
    std::memcpy(&displacement, call + layout::load_site_offset + 1, 4);
    if (base + layout::load_anchor_rva + layout::load_site_offset + 5 + displacement != base + layout::load_target_rva)
        return "load_target_mismatch";
    if (!verify_bytes(base + layout::load_target_rva, layout::load_target_entry, sizeof layout::load_target_entry))
        return "load_entry_mismatch";
    if (!anchor_matches(base, layout::remove_anchor_rva, layout::remove_anchor, sizeof layout::remove_anchor,
                        layout::remove_anchor_operand) ||
        !anchor_matches(base, layout::destroy_anchor_rva, layout::destroy_anchor, sizeof layout::destroy_anchor,
                        layout::destroy_anchor_operand) ||
        !anchor_matches(base, layout::birth_anchor_rva, layout::birth_anchor, sizeof layout::birth_anchor,
                        layout::birth_anchor_operand) ||
        !anchor_matches(base, layout::allocate_insert_anchor_rva, layout::allocate_insert_anchor,
                        sizeof layout::allocate_insert_anchor, layout::allocate_insert_anchor_operand))
        return "engine_anchor_mismatch";
    return nullptr;
}
const char* write_mode(bool patched, bool atomic) {
    return patched ? (atomic ? "atomic" : "plain") : "none";
}
void log_status() {
    std::uintptr_t engine = 0, current = 0;
    if (engine_slot && read_memory(engine_slot, &engine, 4) && engine)
        read_memory(engine + layout::registry_offset, &current, 4);
    unsigned baseline_entries = 0, table_capacity = 0;
    const char* baseline_text = "not_run";
    {
        Lock lock;
        baseline_entries = baseline_count;
        table_capacity = capacity;
        baseline_text = baseline_reason;
    }
    x3m::log("fl.lifetime status=%s base=0x%08lx insert=0x%08lx remove=0x%08lx destroy=0x%08lx load_site=0x%08lx "
             "load_target=0x%08lx engine_slot=0x%08lx engine=0x%08lx registry=0x%08lx baseline=%s baseline_entries=%u "
             "capacity=%u write=%s,%s,%s,%s failed_site=%s",
             diagnostic.load(), static_cast<unsigned long>(install_base),
             static_cast<unsigned long>(install_sites[0]), static_cast<unsigned long>(install_sites[1]),
             static_cast<unsigned long>(install_sites[2]), static_cast<unsigned long>(install_sites[3]),
             static_cast<unsigned long>(install_sites[4]), static_cast<unsigned long>(engine_slot),
             static_cast<unsigned long>(engine), static_cast<unsigned long>(current), baseline_text, baseline_entries,
             table_capacity, write_mode(entry_sites[0].patched_in, entry_sites[0].atomic_write),
             write_mode(entry_sites[1].patched_in, entry_sites[1].atomic_write),
             write_mode(entry_sites[2].patched_in, entry_sites[2].atomic_write),
             write_mode(load_site.patched_in, load_site.atomic_write), failed_site);
}
}
// X3M_OBJECT_LIFETIME=1 (schema default on), X3FL.exe identity and the byte gate; inside the engine_patch install
// window only (load_backend, before the device and before X3FL.exe constructs its engine). One fl.lifetime row.
bool initialize() {
    const DWORD error = GetLastError();
    if (any_owned()) {
        const bool result = observation;
        SetLastError(error);
        return result;
    }
    wchar_t setting[4]{};
    bool result = false;
    if (x3m::config::get(L"X3M_OBJECT_LIFETIME", setting, 4) != 1 || setting[0] != L'1')
        diagnostic = "disabled";
    else if (!x3m::fl::verified())
        diagnostic = "executable_mismatch";
    else {
        const std::uintptr_t base = x3m::fl::module_base();
        install_base = base;
        engine_slot = base + layout::engine_slot_rva;
        if (const char* refusal = gate(base))
            diagnostic = refusal;
        else
            result = install({base + layout::insert_rva, base + layout::remove_rva, base + layout::destroy_rva,
                              base + layout::load_anchor_rva + layout::load_site_offset, base + layout::load_target_rva,
                              base + layout::engine_slot_rva},
                             MaxEntries, true);
    }
    log_status();
    SetLastError(error);
    return result;
}
bool active() {
    return observation;
}
bool recovery_required() {
    return any_owned() && !observation;
}
const char* status() {
    return diagnostic.load();
}
Stats stats() {
    Lock lock;
    return {baseline_complete, baseline_count, any_owned(), observation.load(), live, peak_live, max_probe,
            capacity, load_epoch};
}
bool disable_pending() {
    return disable_event_pending.load(std::memory_order_relaxed);
}
bool take_disable_event(DisableEvent* out) {
    if (!out || !disable_event_pending.load(std::memory_order_acquire)) return false;
    Lock lock;
    if (!disable_event_pending.load(std::memory_order_relaxed)) return false;
    *out = disable_event;
    disable_event_pending.store(false, std::memory_order_relaxed);
    return true;
}
// The insert-caller census is AP-only (scene_graph_census.cpp is not in the fl link).
void set_insert_callers(bool) {}
bool take_insert_callers(scene_graph_census::core::CallerTable*) {
    return false;
}
bool current(std::uintptr_t map, std::uintptr_t node, std::uint32_t node_handle, std::uintptr_t camera,
             std::uint32_t camera_handle, Snapshot* out) {
    if (!out) return false;
    const DWORD error = GetLastError();
    *out = {};
    {
        Lock lock;
        out->observer_epoch = observer_epoch;
        out->load_epoch = load_epoch;
        out->registry_epoch = registry_epoch;
        out->mutation_revision = revision;
        auto evaluate = [&]() -> Reason {
            if (!observation) return disabled_reason;
            if (!verify_ownership()) return disabled_reason;
            if (in_flight) return Reason::MutationInProgress;
            std::uintptr_t expected = 0;
            if (!read_registry(expected)) {
                if (registry) {
                    fail(Reason::RegistryUnavailable, "registry_unavailable");
                    clear_entries();
                    increment(registry_epoch);
                    increment(revision);
                }
                return Reason::RegistryUnavailable;
            }
            if (!map || map != expected) return Reason::RegistryMismatch;
            if (map != registry) {
                bind_registry(map);
                return Reason::RegistryChanged;
            }
            if (registry_dead) return Reason::RegistryUnavailable;
            const auto* n = find(node_handle, false);
            const auto* c = find(camera_handle, false);
            if (!n) return Reason::UnknownNodeBirth;
            if (!c) return Reason::UnknownCameraBirth;
            if (!node || !camera || n->value != node || c->value != camera) return Reason::PointerMismatch;
            std::uintptr_t nvalue = 0, cvalue = 0;
            if (lookup(map, node_handle, nvalue) != Lookup::Found ||
                lookup(map, camera_handle, cvalue) != Lookup::Found) {
                retire(node_handle);
                retire(camera_handle);
                increment(revision);
                return Reason::LookupUnavailable;
            }
            if (nvalue != node || cvalue != camera) {
                retire(node_handle);
                retire(camera_handle);
                increment(revision);
                return Reason::PointerMismatch;
            }
            out->node_serial = n->serial;
            out->camera_serial = c->serial;
            return Reason::Known;
        };
        out->reason = evaluate();
        out->known = out->reason == Reason::Known;
        out->load_epoch = load_epoch;
        out->registry_epoch = registry_epoch;
        out->mutation_revision = revision;
        ++counters.current_calls;
        ++counters.reasons[unsigned(out->reason) < ReasonCount ? unsigned(out->reason) : 0];
    }
    SetLastError(error);
    return out->known;
}
// The journal functions (AP file): same calling contract as current(), any thread, never inside an engine call.
JournalCursor journal_register() {
    Lock lock;
    if (journal_consumers == std::numeric_limits<unsigned>::max()) return {};
    // Skipping more than a whole ring makes every cursor of an earlier registration drain as overflow.
    if (!journal_consumers) journal_head += JournalCapacity + 1;
    ++journal_consumers;
    return {journal_head};
}
void journal_unregister() {
    Lock lock;
    if (journal_consumers) --journal_consumers;
}
JournalStats journal_stats() {
    Lock lock;
    return {journal_head, journal_consumers};
}
JournalDrain journal_drain(JournalCursor& cursor, JournalEntry* out, std::uint32_t limit) {
    JournalDrain result{};
    Lock lock;
    result.available = journal_consumers && observation && cursor.valid();
    result.load_epoch = load_epoch;
    result.registry_epoch = registry_epoch;
    result.mutation_revision = revision;
    if (!out || !limit) {
        result.invalid = true;
        return result;
    }
    if (!cursor.valid()) {
        result.overflow = true;
        return result;
    }
    if (!journal_consumers || cursor.sequence > journal_head || journal_head - cursor.sequence > JournalCapacity) {
        result.overflow = true;
        cursor.sequence = journal_head;
        return result;
    }
    const std::uint64_t pending = journal_head - cursor.sequence;
    const std::uint32_t count = pending < limit ? static_cast<std::uint32_t>(pending) : limit;
    for (std::uint32_t i = 0; i < count; ++i)
        out[i] = journal[static_cast<unsigned>(cursor.sequence + i) & (JournalCapacity - 1)];
    cursor.sequence += count;
    result.count = count;
    result.more = cursor.sequence != journal_head;
    return result;
}
bool shutdown() {
    const DWORD error = GetLastError();
    {
        Lock lock;
        observation = false;
        clear_entries();
        increment(revision);
        disabled_reason = Reason::Disabled;
    }
    const bool result = restore_all();
    diagnostic = result ? "disabled" : "restore_incomplete";
    SetLastError(error);
    return result;
}
void fl_sample_row() {
    if (!sample_period) return;
    const DWORD error = GetLastError();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (!sample_force && now.QuadPart < sample_due) {
        SetLastError(error);
        return;
    }
    sample_force = false;
    sample_due = now.QuadPart + sample_period;
    Counters c{};
    unsigned live_now = 0, peak = 0, probe = 0, table = 0, flight = 0;
    std::uintptr_t bound = 0;
    bool dead = false;
    std::uint64_t loads_epoch = 0, registries_epoch = 0;
    {
        Lock lock;
        c = counters;
        counters = {};
        live_now = live;
        peak = peak_live;
        probe = max_probe;
        table = capacity;
        flight = in_flight;
        bound = registry;
        dead = registry_dead;
        loads_epoch = load_epoch;
        registries_epoch = registry_epoch;
    }
    unsigned top = 0; // the most frequent non-Known reason of the period, 0: none
    for (unsigned i = 1; i < ReasonCount; ++i)
        if (c.reasons[i] && (!top || c.reasons[i] > c.reasons[top])) top = i;
    unsigned long unknown = 0;
    for (unsigned i = 1; i < ReasonCount; ++i) unknown += c.reasons[i];
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const bool changed = sample_have_registry && bound != sample_registry;
    sample_have_registry = true;
    sample_registry = bound;
    ++sample_rows;
    x3m::log("fl.lifetime_sample row=%lu status=%s inserts=%lu removes=%lu destroys=%lu loads=%lu unwinds=%lu "
             "other_maps=%lu dormant=%lu published=%lu unpublished_nested=%lu unpublished_lookup=%lu live=%u "
             "peak_live=%u capacity=%u max_probe=%u current=%lu known=%lu unknown=%lu top_reason=%s top_count=%lu "
             "registry=0x%08lx registry_changed=%u binds=%lu rebind_without_destroy=%lu registry_dead=%u in_flight=%u "
             "load_epoch=%llu registry_epoch=%llu rebaselined=%lu rebaseline_rejected=%lu rebaseline_skipped=%lu "
             "rebaseline_us_max=%llu",
             sample_rows, diagnostic.load(), c.inserts, c.removes, c.destroys, c.loads, c.unwinds, c.other_maps,
             c.dormant, c.published, c.unpublished_nested, c.unpublished_lookup, live_now, peak, table, probe,
             c.current_calls, c.reasons[0], unknown, top ? reason_names[top] : "none", top ? c.reasons[top] : 0ul,
             static_cast<unsigned long>(bound), unsigned(changed), c.binds, c.rebind_without_destroy, unsigned(dead),
             flight, static_cast<unsigned long long>(loads_epoch), static_cast<unsigned long long>(registries_epoch),
             c.rebaselined, c.rebaseline_rejected, c.rebaseline_skipped,
             frequency.QuadPart > 0 ? c.rebaseline_ticks_max * 1000000ull / static_cast<unsigned long long>(frequency.QuadPart)
                                    : 0ull);
    SetLastError(error);
}
#ifdef X3M_OBJECT_LIFETIME_FIXTURE
bool fixture_install(const FixtureSites& s, unsigned count, bool snapshot) {
    const DWORD error = GetLastError();
    install_base = 0;
    const bool result = install({reinterpret_cast<std::uintptr_t>(s.insert_entry),
                                 reinterpret_cast<std::uintptr_t>(s.remove_entry),
                                 reinterpret_cast<std::uintptr_t>(s.destroy_entry),
                                 reinterpret_cast<std::uintptr_t>(s.load_call),
                                 reinterpret_cast<std::uintptr_t>(s.load_target), s.engine_slot},
                                count, snapshot);
    log_status();
    SetLastError(error);
    return result;
}
bool fixture_shutdown() {
    return shutdown();
}
const char* fixture_gate(std::uintptr_t base) {
    return gate(base);
}
void fixture_force_sample() {
    sample_force = true;
}
#endif
} // namespace x3m::object_lifetime
