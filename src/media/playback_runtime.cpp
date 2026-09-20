#include "playback_runtime.h"
#include <utility>

namespace x3m::media {
PreparedPlay::~PreparedPlay() { reset(); }
PreparedPlay::PreparedPlay(PreparedPlay&& other) noexcept { *this = std::move(other); }
PreparedPlay& PreparedPlay::operator=(PreparedPlay&& other) noexcept {
    if (this == &other) return *this;
    reset();
    owner_ = other.owner_; session_ = other.session_; operation_ = other.operation_;
    epoch_ = other.epoch_; reservation_ = other.reservation_; cell_ = other.cell_;
    request_ = other.request_; other.owner_ = nullptr;
    return *this;
}
void PreparedPlay::reset() noexcept { if (owner_) owner_->abandon(*this); }
Runtime::Runtime(std::uint32_t sessions, std::uint32_t commands) noexcept
    : session_limit_(sessions <= max_sessions ? sessions : 0),
      command_limit_(commands <= max_commands ? commands : 0) {}
Runtime::Session* Runtime::find(SessionHandle h) noexcept {
    if (!h || h.slot >= session_limit_) return nullptr;
    auto& s = sessions_[h.slot];
    return s.state.publication.live && s.state.publication.session == h ? &s : nullptr;
}
const Runtime::Session* Runtime::find(SessionHandle h) const noexcept {
    if (!h || h.slot >= session_limit_) return nullptr;
    const auto& s = sessions_[h.slot];
    return s.state.publication.live && s.state.publication.session == h ? &s : nullptr;
}
std::uint32_t Runtime::reserve_cell() noexcept {
    if (next_serial_ == UINT64_MAX) return command_limit_;
    for (std::uint32_t i = 0; i < command_limit_; ++i) {
        auto& c = commands_[i];
        if (c.state == CellState::free) {
            c.state = CellState::reserved; c.serial = next_serial_++;
            return i;
        }
    }
    return command_limit_;
}
void Runtime::enqueue(std::uint32_t i, Session& s, CommandKind kind) noexcept {
    const auto& p = s.state.publication;
    commands_[i].command = {p.session, p.operation, p.epoch, s.state.request, kind, p.playing};
    commands_[i].state = CellState::ready;
}
SessionHandle Runtime::try_admit(SourceKey source) noexcept {
    if (next_generation_ == UINT64_MAX) return {};
    for (std::uint32_t i = 0; i < session_limit_; ++i) {
        auto& s = sessions_[i];
        if (s.state.publication.live) continue;
        const auto cell = reserve_cell();
        if (cell == command_limit_) return {};
        s = {};
        const SessionHandle h{i, next_generation_++};
        s.state.publication = {h, 0, 1, true, false};
        s.state.request.source = source;
        enqueue(cell, s, CommandKind::construct);
        return h;
    }
    return {};
}
PreparedPlay Runtime::prepare_play(SessionHandle h, Request request) noexcept {
    PreparedPlay p;
    auto* s = find(h);
    if (!s || s->reservation || s->state.request.source != request.source ||
        s->state.publication.epoch == UINT64_MAX || next_operation_ == UINT64_MAX) return p;
    const auto cell = reserve_cell();
    if (cell == command_limit_) return p;
    p.owner_ = this; p.session_ = h; p.request_ = request;
    p.operation_ = s->state.publication.operation; p.epoch_ = s->state.publication.epoch;
    p.cell_ = cell; p.reservation_ = commands_[cell].serial;
    s->reservation = p.reservation_;
    return p;
}
void Runtime::abandon(PreparedPlay& p) noexcept {
    if (auto* s = find(p.session_)) {
        if (s->reservation == p.reservation_) s->reservation = 0;
    }
    auto& c = commands_[p.cell_];
    if (c.state == CellState::reserved && c.serial == p.reservation_) c = {};
    p.owner_ = nullptr;
}
void Runtime::release_reservation(Session& s) noexcept {
    if (!s.reservation) return;
    for (std::uint32_t i = 0; i < command_limit_; ++i)
        if (commands_[i].state == CellState::reserved && commands_[i].serial == s.reservation)
            commands_[i] = {};
    s.reservation = 0;
}
Transition Runtime::commit_play(PreparedPlay&& p) noexcept {
    if (p.owner_ != this) return {};
    auto* s = find(p.session_);
    if (!s || s->reservation != p.reservation_ || s->state.publication.operation != p.operation_ ||
        s->state.publication.epoch != p.epoch_ || next_operation_ == UINT64_MAX) {
        p.reset(); return {};
    }
    auto& state = s->state;
    const auto previous = state.operation_active ? state.publication.operation : 0;
    state.publication.operation = next_operation_++;
    ++state.publication.epoch;
    state.publication.playing = true;
    state.operation_active = true; state.intent = Intent::preparing; state.request = p.request_;
    enqueue(p.cell_, *s, CommandKind::play);
    s->reservation = 0; p.owner_ = nullptr;
    return {true, previous != 0, previous, state.publication.operation, state.publication.epoch};
}
Transition Runtime::seek(SessionHandle h, std::int32_t start, SeekIntent intent) noexcept {
    auto* s = find(h);
    if (!s || s->state.publication.epoch == UINT64_MAX ||
        (intent == SeekIntent::loop_restart && !s->state.operation_active)) return {};
    const auto cell = reserve_cell();
    if (cell == command_limit_) return {};
    release_reservation(*s);
    auto& state = s->state;
    ++state.publication.epoch;
    state.request.start_ms = start;
    // Original seek clears the end; its caller writes the requested end later.
    state.request.end_ms = -1;
    if (intent == SeekIntent::loop_restart) state.publication.playing = true;
    state.intent = state.publication.playing ? Intent::preparing : Intent::stopped;
    enqueue(cell, *s, CommandKind::seek);
    return {true, false, 0, state.publication.operation, state.publication.epoch};
}
Transition Runtime::run(SessionHandle h) noexcept {
    auto* s = find(h);
    if (!s || !s->state.operation_active) return {};
    const auto cell = reserve_cell();
    if (cell == command_limit_) return {};
    release_reservation(*s);
    s->state.publication.playing = true; s->state.intent = Intent::preparing;
    enqueue(cell, *s, CommandKind::run);
    return {true, false, 0, s->state.publication.operation, s->state.publication.epoch};
}
bool Runtime::set_end(SessionHandle h, Epoch epoch, std::int32_t end) noexcept {
    auto* s = find(h);
    if (!s || s->state.publication.epoch != epoch) return false;
    s->state.request.end_ms = end; return true;
}
void Runtime::discard_commands(SessionHandle h) noexcept {
    for (std::uint32_t i = 0; i < command_limit_; ++i)
        if (commands_[i].state == CellState::ready && commands_[i].command.session == h)
            commands_[i] = {};
}
Transition Runtime::terminate(Session& s, bool retire) noexcept {
    auto& state = s.state;
    const auto previous = state.operation_active ? state.publication.operation : 0;
    release_reservation(s);
    discard_commands(state.publication.session);
    if (state.publication.epoch != UINT64_MAX) ++state.publication.epoch;
    else retire = true; // Fail closed instead of reviving an old epoch on overflow.
    state.publication.playing = false; state.operation_active = false;
    state.intent = Intent::stopped;
    if (retire) state.publication.live = false;
    return {true, previous != 0, previous, state.publication.operation, state.publication.epoch};
}
Transition Runtime::stop(SessionHandle h) noexcept {
    auto* s = find(h); return s ? terminate(*s, false) : Transition{};
}
Transition Runtime::retire(SessionHandle h) noexcept {
    auto* s = find(h); return s ? terminate(*s, true) : Transition{};
}
Transition Runtime::consume_event(SessionHandle h, OperationId op, Epoch epoch, Event event) noexcept {
    auto* s = find(h);
    if (!s || !s->state.operation_active || s->state.publication.operation != op ||
        s->state.publication.epoch != epoch) return {};
    if (event == Event::ready) {
        if (!s->state.publication.playing) return {};
        s->state.intent = Intent::playing;
        return {true, false, 0, op, epoch};
    }
    // presentation_complete is a clock/queue decision, never raw provider EOF.
    return terminate(*s, false);
}
bool Runtime::snapshot(SessionHandle h, Snapshot& out) const noexcept {
    const auto* s = find(h); if (!s) return false;
    out = s->state; return true;
}
bool Runtime::publication(std::uint32_t slot, Publication& out) const noexcept {
    if (slot >= session_limit_) return false;
    out = sessions_[slot].state.publication; return true;
}
bool Runtime::accepts_publication(SessionHandle h, OperationId op, Epoch epoch) const noexcept {
    const auto* s = find(h);
    return s && s->state.operation_active && s->state.publication.playing &&
        s->state.publication.operation == op && s->state.publication.epoch == epoch;
}
bool Runtime::peek_command(SessionHandle h, CommandOffer& out) noexcept {
    out = {};
    const auto* s = find(h);
    if (!s) return false;
    const auto& p = s->state.publication;
    std::uint32_t best = command_limit_;
    for (std::uint32_t i = 0; i < command_limit_; ++i) {
        auto& c = commands_[i];
        if (c.state != CellState::ready || !(c.command.session == h)) continue;
        // Only obsolete work is discarded. Callback termination ownership stays
        // in the engine adapter, independent of command-cell coalescing.
        if (c.command.operation != p.operation || c.command.epoch != p.epoch) {
            c = {}; continue;
        }
        if (best == command_limit_ || c.serial < commands_[best].serial) best = i;
    }
    if (best == command_limit_) return false;
    out.owner_ = this; out.cell_ = best; out.serial_ = commands_[best].serial;
    out.command_ = commands_[best].command;
    return true;
}
bool Runtime::offer_current(const CommandOffer& offer) const noexcept {
    if (offer.owner_ != this || offer.cell_ >= command_limit_) return false;
    const auto& c = commands_[offer.cell_];
    if (c.state != CellState::ready || c.serial != offer.serial_ ||
        !(c.command.session == offer.command_.session)) return false;
    const auto* s = find(offer.command_.session);
    if (!s || c.command.operation != s->state.publication.operation ||
        c.command.epoch != s->state.publication.epoch) return false;
    // Acknowledge only the oldest still-current cell of this session, even if a
    // copied token survives other transfers. Other sessions cannot head-block it.
    for (std::uint32_t i = 0; i < command_limit_; ++i) {
        const auto& earlier = commands_[i];
        if (earlier.state == CellState::ready && earlier.command.session == c.command.session &&
            earlier.command.operation == c.command.operation && earlier.command.epoch == c.command.epoch &&
            earlier.serial < c.serial) return false;
    }
    return true;
}
bool Runtime::acknowledge_command(CommandOffer& offer) noexcept {
    if (!offer_current(offer)) { offer = {}; return false; }
    commands_[offer.cell_] = {};
    offer = {};
    return true;
}
bool Runtime::pop_command(Command& out) noexcept {
    std::uint32_t best = command_limit_;
    for (std::uint32_t i = 0; i < command_limit_; ++i) {
        if (commands_[i].state != CellState::ready) continue;
        if (best == command_limit_ || commands_[i].serial < commands_[best].serial) best = i;
    }
    if (best == command_limit_) return false;
    out = commands_[best].command; commands_[best] = {}; return true;
}
std::uint32_t Runtime::occupied_commands() const noexcept {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < command_limit_; ++i) count += commands_[i].state != CellState::free;
    return count;
}
} // namespace x3m::media
