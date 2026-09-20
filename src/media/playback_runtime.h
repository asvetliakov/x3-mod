#pragma once
#include <cstddef>
#include <cstdint>

namespace x3m::media {
using SourceKey = std::uint32_t;
using OperationId = std::uint64_t;
using Epoch = std::uint64_t;
struct SessionHandle {
    std::uint32_t slot = UINT32_MAX;
    std::uint64_t generation = 0;
    explicit operator bool() const noexcept { return generation != 0; }
};
inline bool operator==(SessionHandle a, SessionHandle b) noexcept {
    return a.slot == b.slot && a.generation == b.generation;
}
struct Request {
    SourceKey source = 0;
    std::int32_t start_ms = 0, end_ms = -1;
    bool loop = false;
};
enum class Intent : std::uint8_t { stopped, preparing, playing };
enum class SeekIntent : std::uint8_t { preserve, loop_restart };
enum class CommandKind : std::uint8_t { construct, play, seek, run };
struct Command {
    SessionHandle session{};
    OperationId operation = 0;
    Epoch epoch = 0;
    Request request{};
    CommandKind kind = CommandKind::construct;
    bool playing = false;
};
// Values copied by the future transport owner, NOT an atomic cross-thread mailbox.
// Cancellation uses this separate channel even when every command cell is occupied.
struct Publication {
    SessionHandle session{};
    OperationId operation = 0;
    Epoch epoch = 0;
    bool live = false, playing = false;
};
struct Snapshot {
    Publication publication{};
    Request request{};
    Intent intent = Intent::stopped;
    bool operation_active = false;
};
enum class Event : std::uint8_t { ready, failed, presentation_complete };
struct Transition {
    bool accepted = false, terminal = false;
    OperationId previous_operation = 0, operation = 0;
    Epoch epoch = 0;
};
class Runtime;
// Engine-thread offer of one existing command cell, not a second queue. Copies
// may be retained for comparison, but only the first valid acknowledgement can
// remove the cell. Runtime must outlive every offer (including across placement
// construction); offers do not extend its lifetime.
class CommandOffer {
public:
    explicit operator bool() const noexcept { return owner_ != nullptr; }
    const Command& command() const noexcept { return command_; }
private:
    friend class Runtime;
    const Runtime* owner_ = nullptr;
    std::uint32_t cell_ = 0;
    std::uint64_t serial_ = 0;
    Command command_{};
};
// A reservation is engine-thread local. Runtime must outlive it. No callback,
// engine address, frame bytes or backend ownership can enter this token.
class PreparedPlay {
public:
    PreparedPlay() noexcept = default;
    ~PreparedPlay();
    PreparedPlay(PreparedPlay&&) noexcept;
    PreparedPlay& operator=(PreparedPlay&&) noexcept;
    PreparedPlay(const PreparedPlay&) = delete;
    PreparedPlay& operator=(const PreparedPlay&) = delete;
    explicit operator bool() const noexcept { return owner_ != nullptr; }
    void reset() noexcept;
private:
    friend class Runtime;
    Runtime* owner_ = nullptr;
    SessionHandle session_{};
    OperationId operation_ = 0;
    Epoch epoch_ = 0;
    std::uint64_t reservation_ = 0;
    std::uint32_t cell_ = 0;
    Request request_{};
};

// Engine-thread-only CPU state. Thread publication, workers, frames and canonical
// clock integration are deliberately absent. Admission at the proxy remains off.
// Limits are explicit service parameters, bounded by static backing storage.
class Runtime {
public:
    static constexpr std::uint32_t max_sessions = 8, max_commands = 32;
    explicit Runtime(std::uint32_t sessions = 2, std::uint32_t commands = 8) noexcept;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    SessionHandle try_admit(SourceKey) noexcept;
    PreparedPlay prepare_play(SessionHandle, Request) noexcept;
    Transition commit_play(PreparedPlay&&) noexcept;
    Transition seek(SessionHandle, std::int32_t, SeekIntent) noexcept;
    // Manager loop: reserve first, then publish one epoch with complete bounds.
    // Requires the still-active operation (including endpoint awaiting retry).
    // Pressure is a nonterminal rejection; caller may retry on its next pass.
    Transition loop_seek(SessionHandle, std::int32_t start_ms, std::int32_t end_ms) noexcept;
    Transition run(SessionHandle) noexcept;
    bool set_end(SessionHandle, Epoch, std::int32_t end_ms) noexcept;
    Transition stop(SessionHandle) noexcept;
    Transition retire(SessionHandle) noexcept;
    Transition consume_event(SessionHandle, OperationId, Epoch, Event) noexcept;
    bool snapshot(SessionHandle, Snapshot&) const noexcept;
    bool publication(std::uint32_t slot, Publication&) const noexcept;
    bool accepts_publication(SessionHandle, OperationId, Epoch) const noexcept;
    // Production transport uses this per-session seam, never pop-then-submit.
    // A false try_submit leaves the offer queued. Check offer_current immediately
    // before submitting a retained offer; submit and acknowledge must be adjacent
    // engine-thread operations with no reentry. A successful submit followed by
    // failed ack means invalidation, NOT permission to retry the accepted command.
    // The worker independently validates the full tuple against cancellation.
    //
    // peek prunes superseded commands, then offers the oldest current cell for h.
    // Consequently play/seek/run each ensure a graph for request.source if absent;
    // construct is optional eager preparation. A fresh-identity run also seeks to
    // request.start_ms instead of assuming a superseded command was consumed.
    bool peek_command(SessionHandle, CommandOffer&) noexcept;
    bool offer_current(const CommandOffer&) const noexcept;
    bool acknowledge_command(CommandOffer&) noexcept;
    // Destructive diagnostic drain retained for the existing state fixture only.
    bool pop_command(Command&) noexcept;
    std::uint32_t occupied_commands() const noexcept;
    std::uint32_t session_capacity() const noexcept { return session_limit_; }
private:
    friend class PreparedPlay;
    struct Session {
        Snapshot state{};
        std::uint64_t reservation = 0;
    };
    enum class CellState : std::uint8_t { free, reserved, ready };
    struct Cell {
        Command command{};
        std::uint64_t serial = 0;
        CellState state = CellState::free;
    };
    Session* find(SessionHandle) noexcept;
    const Session* find(SessionHandle) const noexcept;
    std::uint32_t reserve_cell() noexcept;
    void abandon(PreparedPlay&) noexcept;
    void release_reservation(Session&) noexcept;
    void discard_commands(SessionHandle) noexcept;
    void enqueue(std::uint32_t, Session&, CommandKind) noexcept;
    Transition terminate(Session&, bool retire) noexcept;
    Transition seek_impl(SessionHandle, std::int32_t start_ms, std::int32_t end_ms, SeekIntent) noexcept;
    Session sessions_[max_sessions]{};
    Cell commands_[max_commands]{};
    std::uint32_t session_limit_, command_limit_;
    std::uint64_t next_generation_ = 1, next_operation_ = 1, next_serial_ = 1;
};
} // namespace x3m::media
