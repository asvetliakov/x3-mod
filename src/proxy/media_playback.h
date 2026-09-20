#pragma once
#include "../media/playback_runtime.h"
#include <cstddef>
#include <cstdint>

namespace x3m::media_playback {
// Never enable this until ALL consumer, worker, callback and destination seams
// are qualified and installed atomically. There are no hooks in this module.
inline constexpr bool production_admission_enabled = false;
using media::SessionHandle;
struct EngineKey { std::uint32_t address = 0; std::uint64_t generation = 0; };
inline bool operator==(EngineKey a, EngineKey b) noexcept {
    return a.address == b.address && a.generation == b.generation;
}
// All pointer-shaped fields are x86 values, never native host pointers.
struct Shell32 {
    std::uint32_t source;
    std::uint32_t inert_04_88[34];
    std::uint32_t flags;
    std::int32_t start_ms, end_ms;
    std::uint32_t inert_98_ac[6];
    std::uint32_t pump_state;
};
struct Record32 {
    std::uint32_t next, previous, unknown_08, unknown_0c, source;
    std::uint32_t callback_context;
    std::int32_t callback_index, start_ms, end_ms;
    std::uint32_t shell, destination, flags, slot, unknown_34, unknown_38, unknown_3c;
};
static_assert(sizeof(Shell32) == 0xb4 && alignof(Shell32) == 4);
static_assert(offsetof(Shell32, flags) == 0x8c && offsetof(Shell32, start_ms) == 0x90);
static_assert(offsetof(Shell32, end_ms) == 0x94 && offsetof(Shell32, pump_state) == 0xb0);
static_assert(sizeof(Record32) == 0x40 && alignof(Record32) == 4);
static_assert(offsetof(Record32, source) == 0x10 && offsetof(Record32, callback_context) == 0x14);
static_assert(offsetof(Record32, callback_index) == 0x18 && offsetof(Record32, start_ms) == 0x1c);
static_assert(offsetof(Record32, end_ms) == 0x20 && offsetof(Record32, shell) == 0x24);
static_assert(offsetof(Record32, destination) == 0x28 && offsetof(Record32, flags) == 0x2c);
static_assert(offsetof(Record32, slot) == 0x30);
struct AdmittedIdentity { SessionHandle session{}; media::SourceKey source = 0; };
// Caller supplies a live 0xb4-byte allocation from the matched engine family.
// This routine neither allocates nor writes a session pointer into engine memory.
bool initialize_shell(void* storage, std::size_t bytes, AdmittedIdentity, std::uint32_t flags) noexcept;
struct CallbackKey { std::uint32_t context = 0; std::int32_t index = 0; };
struct Request { media::Request playback{}; CallbackKey callback{}; };
struct PlayValues32 {
    std::int32_t callback_index;
    std::uint32_t callback_context, source;
    std::uint32_t start_minutes, start_seconds, start_milliseconds;
    std::uint32_t end_minutes, end_seconds, end_milliseconds, loop;
};
Request decode_play(PlayValues32) noexcept;
enum class SeekCaller : std::uint8_t { explicit_play, speech_play, manager_loop, other };
bool decode_seek_caller(std::uint32_t return_address, SeekCaller&, media::SeekIntent&) noexcept;
inline constexpr std::uint32_t manager_abort = 0x004984be, stop_all_abort = 0x00498362;
inline constexpr std::uint32_t stop_all_owned_tail = 0x00498322, rate_result_tail = 0x00498697;
enum class Traversal : std::uint8_t { manager, stop_all };
enum class Continuation : std::uint8_t { live, abort_manager, abort_stop_all };
struct TraversalTicket { std::uint64_t generation = 0; Traversal kind = Traversal::manager; };
struct Notification { CallbackKey key{}; std::uint32_t status = 0; media::OperationId operation = 0; };
class Adapter;
// Move-only, locally consumed before invoking ANY external dispatch. No retained
// callback registry pointer or VM context ownership is introduced here.
class Transition {
public:
    Transition() noexcept = default;
    Transition(Transition&&) noexcept;
    Transition& operator=(Transition&&) noexcept;
    Transition(const Transition&) = delete;
    Transition& operator=(const Transition&) = delete;
    bool accepted = false;
    media::OperationId operation = 0;
    media::Epoch epoch = 0;
private:
    friend class Adapter;
    const Adapter* owner_ = nullptr;
    std::uint64_t scope_ = 0, session_scope_ = 0;
    SessionHandle session_{};
    Notification notification_{};
    bool pending_ = false;
};
class PreparedPlay {
public:
    PreparedPlay() noexcept = default;
    PreparedPlay(PreparedPlay&&) noexcept;
    PreparedPlay& operator=(PreparedPlay&&) noexcept;
    PreparedPlay(const PreparedPlay&) = delete;
    PreparedPlay& operator=(const PreparedPlay&) = delete;
    explicit operator bool() const noexcept { return bool(runtime_); }
    Transition rejection{};
private:
    friend class Adapter;
    media::PreparedPlay runtime_{};
    SessionHandle session_{};
    Request request_{};
    std::uint64_t scope_ = 0;
    const Adapter* owner_ = nullptr;
};
// A registry miss is not proof of an unowned live engine object. Future hook
// admission must establish that independently before choosing legacy COM replay.
enum class Ownership : std::uint8_t { unknown, owned_live, owned_stale };
struct Lookup { Ownership ownership = Ownership::unknown; SessionHandle session{}; };
// Contract for the canonical clock adapter: apply is synchronous, allocation-free,
// cannot reenter the engine, and mutates nothing on failure. It commits elapsed
// old-rate time and the supplied slope atomically on success. No backend HRESULT.
struct ClockTransaction {
    void* clock = nullptr;
    bool (*apply)(void*, std::int32_t input, double exact_rate) noexcept = nullptr;
};
inline constexpr std::int32_t rate_ok = 0, rate_invalid = -2147024809, rate_failed = -2147467259;
bool decode_rate(std::int32_t input, double& result) noexcept;
std::uint32_t rate_tail_boolean(std::int32_t hresult) noexcept;

// Single engine-thread state; all registry lookups compare VALUES only. No engine
// memory is dereferenced. Thread admission and memory observations belong to the
// future verified stubs; a caller must never manufacture keys from stale memory.
class Adapter {
public:
    explicit Adapter(std::uint32_t sessions = 2, std::uint32_t commands = 8) noexcept : runtime_(sessions, commands) {}
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    AdmittedIdentity try_admit(EngineKey shell, media::SourceKey) noexcept;
    Lookup find_owned(std::uint32_t shell_key, std::uint64_t generation) const noexcept;
    bool associate_record(SessionHandle, EngineKey record) noexcept;
    bool publish_record_id(SessionHandle, EngineKey record, media::SourceKey) noexcept;
    bool replace_binding(SessionHandle, std::uint32_t slot) noexcept;
    PreparedPlay prepare_play(SessionHandle, Request) noexcept;
    Transition commit_play(PreparedPlay&&) noexcept;
    Transition seek(SessionHandle, std::int32_t, media::SeekIntent) noexcept;
    Transition loop_seek(SessionHandle, std::int32_t start_ms, std::int32_t end_ms,
                         bool* backpressure = nullptr) noexcept;
    Transition run(SessionHandle) noexcept;
    bool set_end(SessionHandle, media::Epoch, std::int32_t) noexcept;
    Transition stop(SessionHandle) noexcept;
    Transition retire(SessionHandle) noexcept;
    Transition consume_event(SessionHandle, media::OperationId, media::Epoch, media::Event) noexcept;
    bool take_notification(Transition&, Notification&) noexcept;
    std::int32_t set_rate(SessionHandle, std::int32_t, ClockTransaction) noexcept;
    TraversalTicket begin_traversal(Traversal) const noexcept;
    void invalidate_traversal() noexcept;
    void invalidate_dispatch_scope() noexcept;
    Continuation classify_continuation(TraversalTicket) const noexcept;
    // Mandatory observers cover ALL records, including unowned current/cached-next.
    void observe_record_retirement(EngineKey) noexcept;
    void clear() noexcept;
    bool snapshot(SessionHandle h, media::Snapshot& out) const noexcept { return runtime_.snapshot(h, out); }
    bool publication(std::uint32_t slot, media::Publication& out) const noexcept { return runtime_.publication(slot, out); }
    bool accepts_publication(SessionHandle h, media::OperationId op, media::Epoch epoch) const noexcept {
        return runtime_.accepts_publication(h, op, epoch);
    }
    // Same non-reentrant submit/ack contract as Runtime; no intermediate queue.
    bool peek_command(SessionHandle h, media::CommandOffer& out) noexcept { return runtime_.peek_command(h, out); }
    bool offer_current(const media::CommandOffer& offer) const noexcept { return runtime_.offer_current(offer); }
    bool acknowledge_command(media::CommandOffer& offer) noexcept { return runtime_.acknowledge_command(offer); }
    bool pop_command(media::Command& out) noexcept { return runtime_.pop_command(out); }
    std::uint32_t occupied_commands() const noexcept { return runtime_.occupied_commands(); }
private:
    struct Sidecar {
        SessionHandle session{};
        EngineKey shell{}, record{};
        CallbackKey callback{};
        media::SourceKey source = 0;
        std::uint64_t binding_generation = 1, dispatch_serial = 1;
        std::uint32_t slot = 0;
        bool live = false, id_published = false;
    };
    Sidecar* find(SessionHandle) noexcept;
    Transition transition(media::Transition, CallbackKey, SessionHandle = {}) noexcept;
    Transition reject_play(Request) noexcept;
    media::Runtime runtime_;
    Sidecar sidecars_[media::Runtime::max_sessions]{};
    std::uint64_t traversal_generation_ = 1, dispatch_generation_ = 1;
};
} // namespace x3m::media_playback
