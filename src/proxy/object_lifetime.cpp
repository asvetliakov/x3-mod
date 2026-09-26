#include "object_lifetime.h"
#include "config.h"
#include "engine_memory.h"
#include "executable_identity.h"
#include <wincrypt.h>
#include <excpt.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <limits>

static_assert(sizeof(void*) == 4, "Reviewed x86 registry ABI only");
namespace {
using x3m::object_lifetime::Reason;
constexpr unsigned MaxEntries = 16384;
enum Kind : unsigned { Insert, Remove, Destroy, Load };
struct Entry {
    std::uint32_t key = 0;
    std::uintptr_t value = 0;
    std::uint64_t serial = 0;
    unsigned state = 0;
};
struct Scope {
    void* previous_seh;
    void* handler;
    unsigned kind;
    std::uintptr_t args[2];
    std::uintptr_t map;
    std::uint32_t key, value;
    bool entered;
    std::uint64_t registry_epoch, load_epoch;
};
static_assert(offsetof(Scope, args) == 12 && sizeof(Scope) <= 128);
// PUSHFD/PUSHAD snapshot, followed by dispatch kind and the untouched caller frame.
struct Registers {
    std::uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags, kind, return_address, args[2];
};
static_assert(offsetof(Registers, kind) == 36 && offsetof(Registers, args) == 44);
std::atomic_flag model_lock = ATOMIC_FLAG_INIT;
struct Lock {
    Lock() {
        while (model_lock.test_and_set(std::memory_order_acquire)) {}
    }
    ~Lock() { model_lock.clear(std::memory_order_release); }
};
std::array<Entry, MaxEntries> entries{};
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
unsigned capacity = MaxEntries, in_flight = 0;
std::uintptr_t engine_slot = 0, registry = 0;
bool registry_dead = false;
std::uint64_t observer_epoch = 0, load_epoch = 0, registry_epoch = 0, revision = 0, next_serial = 0;
std::atomic<bool> observation{false};
std::atomic<const char*> diagnostic{"disabled"};
Reason disabled_reason = Reason::Disabled;
struct Patch {
    unsigned char* site = nullptr;
    void* trampoline = nullptr;
    std::array<unsigned char, 6> before{}, ours{};
    unsigned size = 0;
    DWORD protection = 0;
    bool owned = false;
};
std::array<Patch, 4> patches{};
bool retain_retired_dispatch = true, dispatch_published = false, retired_dispatch = false;

bool read_memory(std::uintptr_t address, void* out, std::size_t size) {
    return x3m::engine_memory::read(address, out, size);
}
void fail(Reason reason, const char* text) {
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
// Retirement journal: written only under model_lock and only for a registered consumer.
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
    journal_append(JournalKind::FlushAll, 0);
}
bool read_registry(std::uintptr_t& result) {
    std::uintptr_t engine = 0;
    result = 0;
    return read_memory(engine_slot, &engine, 4) && engine && engine <= 0xfffffff3u &&
           read_memory(engine + 12, &result, 4) && result;
}
bool bind_registry(std::uintptr_t value) {
    if (!value) return false;
    if (registry != value) {
        registry = value;
        registry_dead = false;
        clear_entries();
        return increment(registry_epoch) && increment(revision);
    }
    return true;
}
Entry* find(std::uint32_t key, bool create) {
    if (!key) return nullptr;
    const unsigned first = (key * 2654435761u) % capacity;
    Entry* vacant = nullptr;
    for (unsigned n = 0; n < capacity; ++n) {
        auto& entry = entries[(first + n) % capacity];
        if (entry.state == 1 && entry.key == key) return &entry;
        if (entry.state == 2 && !vacant) vacant = &entry;
        if (entry.state == 0) return create ? (vacant ? vacant : &entry) : nullptr;
    }
    return create ? vacant : nullptr;
}
void retire(std::uint32_t key) {
    if (auto* entry = find(key, false)) {
        journal_append(JournalKind::Retired, entry->serial);
        entry->state = 2;
        entry->value = 0;
        entry->serial = 0;
    }
}
enum class Lookup { Found, Missing, Unavailable };
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
    // A corrupt/cyclic chain is unavailable, never evidence of a missing birth.
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
bool initial_snapshot() {
    baseline_complete = false;
    baseline_count = 0;
    clear_entries();
    std::uintptr_t map = 0;
    std::uint32_t header[4]{}, again[4]{};
    if (!read_registry(map) || !bind_registry(map) || !read_memory(map, header, sizeof header)) return false;
    const auto bucket_count = header[1], expected_count = header[3];
    if (!bucket_count || bucket_count > baseline_buckets.size() || (bucket_count & (bucket_count - 1)) ||
        expected_count > capacity)
        return false;
    if (!header[0]) {
        baseline_complete = expected_count == 0 && read_memory(map, again, sizeof again) &&
                            !std::memcmp(header, again, sizeof header);
        return baseline_complete;
    }
    if (!read_memory(header[0], baseline_buckets.data(), bucket_count * 4)) return false;
    baseline_pointers.fill(0);
    unsigned count = 0;
    auto reject = []() {
        clear_entries();
        baseline_count = 0;
        return false;
    };
    for (unsigned bucket = 0; bucket < bucket_count; ++bucket) {
        auto address = baseline_buckets[bucket];
        while (address) {
            if (count >= expected_count || count >= capacity) return reject();
            auto& raw = baseline_records[count];
            raw.address = address;
            if (!read_memory(address, raw.words, sizeof raw.words)) return reject();
            const auto key = raw.words[1], value = raw.words[2];
            std::uint32_t handle = 0;
            if (!key || !value || value > 0xffffffd7u || (key & (bucket_count - 1)) != bucket ||
                !read_memory(value + 0x28, &handle, 4) || handle != key || find(key, false))
                return reject();
            unsigned position = ((value >> 2) * 2654435761u) % capacity;
            while (baseline_pointers[position] && baseline_pointers[position] != value)
                position = (position + 1) % capacity;
            if (baseline_pointers[position]) return reject();
            baseline_pointers[position] = value;
            auto* entry = find(key, true);
            if (!entry || !increment(next_serial)) return reject();
            *entry = {key, value, next_serial, 1};
            ++count;
            address = raw.words[0];
        }
    }
    if (count != expected_count || !read_memory(map, again, sizeof again) || std::memcmp(header, again, sizeof header))
        return reject();
    // Recheck every visited entry and bucket after traversal. Caller quiescence
    // remains mandatory; these checks detect inconsistency, not a concurrent lock.
    for (unsigned i = 0; i < count; ++i) {
        std::uint32_t words[3]{}, handle = 0;
        if (!read_memory(baseline_records[i].address, words, sizeof words) ||
            std::memcmp(words, baseline_records[i].words, sizeof words) || !read_memory(words[2] + 0x28, &handle, 4) ||
            handle != words[1])
            return reject();
    }
    if (std::uint64_t(header[0]) + std::uint64_t(bucket_count) * 4 > 0x100000000ull) return reject();
    for (unsigned i = 0; i < bucket_count; ++i) {
        std::uintptr_t head = 0;
        if (!read_memory(header[0] + i * 4, &head, 4) || head != baseline_buckets[i]) return reject();
    }
    baseline_count = count;
    baseline_complete = true;
    return true;
}
bool verify_ownership() {
    for (const auto& p : patches) {
        unsigned char bytes[6]{};
        if (!p.owned || !read_memory(reinterpret_cast<std::uintptr_t>(p.site), bytes, p.size) ||
            std::memcmp(bytes, p.ours.data(), p.size)) {
            clear_entries();
            increment(revision);
            fail(Reason::ObserverFailure, "hook_ownership_lost");
            return false;
        }
    }
    return true;
}
void finish(Scope* scope, bool normal) {
    if (!scope->entered) return;
    Lock lock;
    scope->entered = false;
    if (in_flight)
        --in_flight;
    else {
        fail(Reason::ObserverFailure, "scope_underflow");
        return;
    }
    if (!normal) {
        clear_entries();
        increment(load_epoch);
        increment(revision);
        return;
    }
    if (!observation || !verify_ownership() || scope->kind != Insert || scope->registry_epoch != registry_epoch ||
        scope->load_epoch != load_epoch || registry_dead || registry != scope->map || !scope->key || !scope->value)
        return;
    std::uintptr_t current = 0;
    if (lookup(scope->map, scope->key, current) != Lookup::Found || current != scope->value) return;
    auto* entry = find(scope->key, true);
    if (!entry) {
        clear_entries();
        increment(revision);
        fail(Reason::CapacityExhausted, "capacity_exhausted");
        return;
    }
    if (!increment(next_serial)) return;
    *entry = {scope->key, scope->value, next_serial, 1};
}
}

extern "C" {
std::uintptr_t x3m_lifetime_originals[4]{};
__attribute__((force_align_arg_pointer)) void __cdecl x3m_lifetime_enter(Scope* scope, const Registers* registers) {
    const DWORD error = GetLastError();
    scope->entered = false;
    scope->kind = registers->kind;
    scope->args[0] = scope->args[1] = 0;
    if (scope->kind == Insert) {
        scope->args[0] = registers->args[0];
        scope->args[1] = registers->args[1];
    } else if (scope->kind == Destroy || scope->kind == Load)
        scope->args[0] = registers->args[0];
    scope->map = (scope->kind == Destroy) ? scope->args[0] : registers->edi;
    scope->key = (scope->kind == Insert) ? scope->args[0] : registers->edx;
    scope->value = scope->args[1];
    if (observation) {
        Lock lock;
        if (observation) {
            std::uintptr_t current = 0;
            if (scope->kind == Load) {
                clear_entries();
                increment(load_epoch);
                increment(revision);
            } else if (!read_registry(current)) {
                // Generic maps may be used before the renderer registry exists.
                // No trusted state exists yet in that dormant case. Losing a
                // previously bound registry is different and must fail closed.
                if (registry) {
                    clear_entries();
                    increment(registry_epoch);
                    increment(revision);
                    fail(Reason::RegistryUnavailable, "registry_unavailable");
                }
            } else if (bind_registry(current) && scope->map == registry) {
                // Registry destruction is engine teardown (0x004710f0 frees the engine
                // object next): no cached engine region is trusted until a frame advances.
                if (scope->kind == Destroy) {
                    clear_entries();
                    registry_dead = true;
                    increment(registry_epoch);
                    x3m::engine_memory::begin_shutdown("registry_destroy");
                } else {
                    if (registry_dead && scope->kind == Insert) {
                        registry_dead = false;
                        clear_entries();
                        increment(registry_epoch);
                    }
                    retire(scope->key);
                }
                increment(revision);
            }
            if (observation && (scope->kind == Load || (registry && scope->map == registry))) {
                if (in_flight == std::numeric_limits<unsigned>::max())
                    fail(Reason::CounterExhausted, "scope_exhausted");
                else {
                    ++in_flight;
                    scope->entered = true;
                    scope->registry_epoch = registry_epoch;
                    scope->load_epoch = load_epoch;
                }
            }
        }
    }
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_lifetime_leave(Scope* scope) {
    const DWORD error = GetLastError();
    finish(scope, true);
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl x3m_lifetime_unwind(EXCEPTION_RECORD* record,
                                                                                           void* frame, CONTEXT*,
                                                                                           void*) {
    alignas(16) unsigned char floating_state[512];
    __asm__ __volatile__("fxsave %0" : "=m"(floating_state));
    const DWORD error = GetLastError();
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        finish(static_cast<Scope*>(frame), false);
    SetLastError(error);
    __asm__ __volatile__("fxrstor %0" ::"m"(floating_state));
    return ExceptionContinueSearch;
}
// All bookkeeping runs behind PUSHFD/PUSHAD and an aligned 512-byte FXSAVE area.
// The original target receives the untouched register state and copied stack
// arguments. Its complete output register/flag/FP state survives leave(). Scope
// at fx+512 is a real Win32 x86 SEH record covering foreign exception unwinds.
__attribute__((naked)) void x3m_lifetime_dispatch() {
    __asm__ __volatile__("pushfl\n\tpushal\n\tmovl %esp,%ebp\n\tsubl $672,%esp\n\tandl $-16,%esp\n\t"
                         "movl %esp,-4(%ebp)\n\tfxsave (%esp)\n\tleal 512(%esp),%eax\n\tmovl %eax,-8(%ebp)\n\t"
                         "movl %fs:0,%edx\n\tmovl %edx,0(%eax)\n\tmovl $_x3m_lifetime_unwind,4(%eax)\n\t"
                         "movb $0,32(%eax)\n\tmovl %eax,%fs:0\n\tpushl %ebp\n\tpushl %eax\n\t"
                         "call _x3m_lifetime_enter\n\taddl $8,%esp\n\tfxrstor (%esp)\n\t"
                         "movl 36(%ebp),%eax\n\tpushl _x3m_lifetime_originals(,%eax,4)\n\tpushl %ebp\n\t"
                         "movl -8(%ebp),%eax\n\tpushl 16(%eax)\n\tpushl 12(%eax)\n\t"
                         "pushl 32(%ebp)\n\tpopfl\n\tmovl 0(%ebp),%edi\n\tmovl 4(%ebp),%esi\n\t"
                         "movl 16(%ebp),%ebx\n\tmovl 20(%ebp),%edx\n\tmovl 24(%ebp),%ecx\n\t"
                         "movl 28(%ebp),%eax\n\tmovl 8(%ebp),%ebp\n\tcall *12(%esp)\n\t"
                         "pushfl\n\tpushal\n\tmovl 44(%esp),%ebp\n\tmovl %esp,-12(%ebp)\n\t"
                         "movl -4(%ebp),%eax\n\tfxsave (%eax)\n\tpushl -8(%ebp)\n\t"
                         "call _x3m_lifetime_leave\n\taddl $4,%esp\n\tmovl -8(%ebp),%eax\n\t"
                         "movl 0(%eax),%eax\n\tmovl %eax,%fs:0\n\tmovl -4(%ebp),%eax\n\tfxrstor (%eax)\n\t"
                         "movl -12(%ebp),%esp\n\tpopal\n\tpopfl\n\tmovl 8(%esp),%esp\n\tleal 40(%esp),%esp\n\tret\n\t");
}
__attribute__((naked)) void x3m_lifetime_insert() {
    __asm__ __volatile__("pushl $0\n\tjmp _x3m_lifetime_dispatch");
}
__attribute__((naked)) void x3m_lifetime_remove() {
    __asm__ __volatile__("pushl $1\n\tjmp _x3m_lifetime_dispatch");
}
__attribute__((naked)) void x3m_lifetime_destroy() {
    __asm__ __volatile__("pushl $2\n\tjmp _x3m_lifetime_dispatch");
}
__attribute__((naked)) void x3m_lifetime_load() {
    __asm__ __volatile__("pushl $3\n\tjmp _x3m_lifetime_dispatch");
}
}

namespace x3m::object_lifetime {
namespace {
bool any_owned() {
    for (const auto& p : patches)
        if (p.owned) return true;
    return false;
}
void release_trampolines() {
    if (any_owned() || (retain_retired_dispatch && dispatch_published)) return;
    for (auto& p : patches) {
        if (p.trampoline) VirtualFree(p.trampoline, 0, MEM_RELEASE);
        p = {};
    }
    for (auto& target : x3m_lifetime_originals) target = 0;
}
bool restore_all(unsigned fail_stage = 0, unsigned fail_site = 0) {
    observation = false;
    bool ok = true;
    for (unsigned i = 4; i--;) {
        auto& p = patches[i];
        if (!p.owned) continue;
        const unsigned fault = (i == fail_site) ? fail_stage : 0;
        unsigned char current[6]{};
        if (!read_memory(reinterpret_cast<std::uintptr_t>(p.site), current, p.size) ||
            (std::memcmp(current, p.before.data(), p.size) && std::memcmp(current, p.ours.data(), p.size))) {
            diagnostic = "foreign_patch";
            ok = false;
            continue;
        }
        DWORD old = 0, unused = 0;
        if (fault == 1 || !VirtualProtect(p.site, p.size, PAGE_EXECUTE_READWRITE, &old)) {
            diagnostic = "restore_protect_failed";
            ok = false;
            continue;
        }
        std::memcpy(p.site, p.before.data(), p.size);
        const bool flushed = fault != 2 && FlushInstructionCache(GetCurrentProcess(), p.site, p.size);
        const bool protected_again = fault != 3 && VirtualProtect(p.site, p.size, p.protection, &unused);
        if (flushed && protected_again)
            p.owned = false;
        else {
            diagnostic = "restore_incomplete";
            ok = false;
        }
    }
    if (ok) {
        if (retain_retired_dispatch && dispatch_published) retired_dispatch = true;
        release_trampolines();
        diagnostic = retired_dispatch ? "retired" : "disabled";
    }
    return ok;
}
struct Sites {
    void* insert;
    void* remove;
    void* destroy;
    void* load;
    void* target;
    std::uintptr_t engine;
};
bool install(const Sites& sites, unsigned requested_capacity, unsigned fault, unsigned fault_site, bool snapshot = true,
             bool retain = true) {
    if (retired_dispatch) {
        diagnostic = "retired_no_reinstall";
        return false;
    }
    if (any_owned() || !requested_capacity || requested_capacity > MaxEntries) {
        diagnostic = "invalid_install";
        return false;
    }
    release_trampolines();
    retain_retired_dispatch = retain;
    dispatch_published = false;
    const void* addresses[] = {sites.insert, sites.remove, sites.destroy, sites.load};
    const void* dispatchers[] = {
        reinterpret_cast<void*>(&x3m_lifetime_insert), reinterpret_cast<void*>(&x3m_lifetime_remove),
        reinterpret_cast<void*>(&x3m_lifetime_destroy), reinterpret_cast<void*>(&x3m_lifetime_load)};
    const unsigned sizes[] = {5, 6, 5, 5};
    const unsigned char expected[3][6] = {
        {0x55, 0x8b, 0x6c, 0x24, 0x08, 0}, {0x8b, 0x4f, 0x04, 0x83, 0xe9, 0x01}, {0x53, 0x8b, 0x5c, 0x24, 0x08, 0}};
    for (unsigned i = 0; i < 4; ++i) {
        auto& p = patches[i];
        p.site = static_cast<unsigned char*>(const_cast<void*>(addresses[i]));
        p.size = sizes[i];
        if (!read_memory(reinterpret_cast<std::uintptr_t>(p.site), p.before.data(), p.size)) {
            diagnostic = "unreadable_site";
            release_trampolines();
            return false;
        }
        if (i < 3 && std::memcmp(p.before.data(), expected[i], p.size)) {
            diagnostic = "site_mismatch";
            release_trampolines();
            return false;
        }
        if (i == 3) {
            std::uint32_t displacement = 0;
            std::memcpy(&displacement, p.before.data() + 1, 4);
            if (p.before[0] != 0xe8 || reinterpret_cast<std::uintptr_t>(p.site) + 5 + displacement !=
                                           reinterpret_cast<std::uintptr_t>(sites.target)) {
                diagnostic = "load_target_mismatch";
                release_trampolines();
                return false;
            }
            x3m_lifetime_originals[i] = reinterpret_cast<std::uintptr_t>(sites.target);
        } else {
            p.trampoline = VirtualAlloc(nullptr, 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!p.trampoline) {
                diagnostic = "trampoline_allocation_failed";
                release_trampolines();
                return false;
            }
            auto* code = static_cast<unsigned char*>(p.trampoline);
            std::memcpy(code, p.before.data(), p.size);
            code[p.size] = 0xe9;
            const std::uint32_t displacement = reinterpret_cast<std::uintptr_t>(p.site) + p.size -
                                               (reinterpret_cast<std::uintptr_t>(code) + p.size + 5);
            std::memcpy(code + p.size + 1, &displacement, 4);
            DWORD unused = 0;
            if (!VirtualProtect(code, 16, PAGE_EXECUTE_READ, &unused) ||
                !FlushInstructionCache(GetCurrentProcess(), code, 16)) {
                diagnostic = "trampoline_protection_failed";
                release_trampolines();
                return false;
            }
            x3m_lifetime_originals[i] = reinterpret_cast<std::uintptr_t>(code);
        }
        p.ours.fill(0x90);
        p.ours[0] = i == 3 ? 0xe8 : 0xe9;
        const std::uint32_t displacement = reinterpret_cast<std::uintptr_t>(dispatchers[i]) -
                                           (reinterpret_cast<std::uintptr_t>(p.site) + 5);
        std::memcpy(p.ours.data() + 1, &displacement, 4);
    }
    for (unsigned i = 0; i < 4; ++i) {
        auto& p = patches[i];
        const unsigned failure = i == fault_site ? fault : 0;
        if (failure == 1 || !VirtualProtect(p.site, p.size, PAGE_EXECUTE_READWRITE, &p.protection)) {
            restore_all();
            diagnostic = "patch_protect_failed";
            return false;
        }
        p.owned = true;
        dispatch_published = true;
        std::memcpy(p.site, p.ours.data(), p.size);
        const bool flushed = failure != 2 && failure < 4 && FlushInstructionCache(GetCurrentProcess(), p.site, p.size);
        DWORD unused = 0;
        const bool protected_again = failure != 3 && VirtualProtect(p.site, p.size, p.protection, &unused);
        if (!flushed || !protected_again) {
            const bool restored = restore_all(failure >= 4 ? failure - 3 : 0, i);
            diagnostic = restored ? "patch_rolled_back" : "rollback_incomplete";
            return false;
        }
    }
    {
        Lock lock;
        capacity = requested_capacity;
        engine_slot = sites.engine;
        registry = 0;
        registry_dead = false;
        in_flight = 0;
        clear_entries();
        disabled_reason = Reason::Disabled;
        if (!increment(observer_epoch) || !increment(load_epoch) || !increment(registry_epoch) ||
            !increment(revision)) {
            restore_all();
            return false;
        }
        baseline_complete = false;
        baseline_count = 0;
        if (snapshot) initial_snapshot();
        if (disabled_reason == Reason::CounterExhausted) {
            restore_all();
            return false;
        }
        observation = true;
        diagnostic = baseline_complete ? "active" : "active_without_baseline";
    }
    return true;
}
bool digest_bytes(const void* bytes, DWORD size, const char* expected) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    unsigned char digest[32]{};
    DWORD count = 32;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
              CryptHashData(hash, static_cast<const BYTE*>(bytes), size, 0) &&
              CryptGetHashParam(hash, HP_HASHVAL, digest, &count, 0) && count == 32;
    if (ok)
        for (unsigned i = 0; i < 32; ++i) {
            constexpr char hex[] = "0123456789abcdef";
            if (expected[i * 2] != hex[digest[i] >> 4] || expected[i * 2 + 1] != hex[digest[i] & 15]) {
                ok = false;
                break;
            }
        }
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}
bool code_fingerprints(std::uintptr_t base) {
    struct Region {
        unsigned rva, size;
        const char* hash;
    };
    // clang-format off
    constexpr Region regions[]={
        {0xefbf0,208,"a45fd7284120eca546a620bfdd0078500570a753131edcf2c8ea7be17b38a305"},
        {0xefd30,112,"e271ea3ed40667b2a4ee5f872af4b14c2a9fe4bb315fbecd5b56c919ae14ad24"},
        {0xefe10,160,"da9c85a554fc0b76fec6ef9df8a2d714a9f7541d952414ec045a5ae762b686be"},
        {0xefeb0,240,"c482731ef6467d7e0f67ba7f7d9bb9a99cd070d97c9d834f08b595cdd679c3f5"},
        {0x5081,32,"286c345c62aad08a3c8651bfc72e2e4e0a9079c6e80d9e6068a32e7741716159"},
        {0x7a720,32,"e24d561bd8354b3f70bc36fedd7c973343a99e35306e506fa124ba5caa2858be"}};
    // clang-format on
    unsigned char data[256];
    for (const auto& region : regions)
        if (!read_memory(base + region.rva, data, region.size) || !digest_bytes(data, region.size, region.hash))
            return false;
    return true;
}
}
bool initialize() {
    const DWORD error = GetLastError();
    if (any_owned()) {
        const bool result = observation;
        SetLastError(error);
        return result;
    }
    if (retired_dispatch) {
        diagnostic = "retired_no_reinstall";
        SetLastError(error);
        return false;
    }
    wchar_t setting[4]{};
    if (x3m::config::get(L"X3M_OBJECT_LIFETIME", setting, 4) != 1 || setting[0] != L'1') {
        diagnostic = "disabled";
        SetLastError(error);
        return false;
    }
    // The shared executable identity (executable_identity.h: structure, global
    // anchors, file size; no file hash, so the 4GB-patched image verifies) and
    // this observer's own site fingerprints before anything is patched.
    const auto module = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const bool valid = base == executable_identity::image_base && executable_identity::known_structure(read_memory) &&
                       executable_identity::anchors_match(read_memory) &&
                       executable_identity::known_file_size(module) && code_fingerprints(base);
    bool result = false;
    if (valid)
        result = install({reinterpret_cast<void*>(base + 0xefbf0), reinterpret_cast<void*>(base + 0xefd39),
                          reinterpret_cast<void*>(base + 0xefe10), reinterpret_cast<void*>(base + 0x508d),
                          reinterpret_cast<void*>(base + 0x7a720), base + 0x208518},
                         MaxEntries, 0, 0);
    else
        diagnostic = "executable_mismatch";
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
    return {baseline_complete, baseline_count};
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
                    clear_entries();
                    increment(registry_epoch);
                    increment(revision);
                    fail(Reason::RegistryUnavailable, "registry_unavailable");
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
    }
    SetLastError(error);
    return out->known;
}
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
    SetLastError(error);
    return result;
}
#ifdef X3M_OBJECT_LIFETIME_FIXTURE
bool fixture_install(const FixtureSites& s, unsigned count, unsigned failure, unsigned site, bool snapshot,
                     bool retain) {
    const DWORD error = GetLastError();
    const bool result = install(
        {s.insert_entry, s.remove_nonempty, s.destroy_entry, s.load_call, s.load_target, s.engine_slot}, count, failure,
        site, snapshot, retain);
    SetLastError(error);
    return result;
}
bool fixture_shutdown(unsigned failure, unsigned site) {
    const DWORD error = GetLastError();
    {
        Lock lock;
        observation = false;
        clear_entries();
        increment(revision);
        disabled_reason = Reason::Disabled;
    }
    const bool result = restore_all(failure, site);
    SetLastError(error);
    return result;
}
void fixture_journal_consumers(unsigned count) {
    Lock lock;
    journal_consumers = count;
}
#endif
} // namespace x3m::object_lifetime
