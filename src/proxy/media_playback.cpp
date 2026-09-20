#include "media_playback.h"
#include <cstring>
#include <utility>

namespace x3m::media_playback {
bool initialize_shell(void* storage, std::size_t bytes, AdmittedIdentity identity, std::uint32_t flags) noexcept {
    if (!storage || bytes < sizeof(Shell32) || !identity.session) return false;
    Shell32 shell{};
    // 0x4cf4f4 zeroes all bytes; 0x4cf522/524/52a/534 install these values.
    shell.source = identity.source;
    shell.flags = flags | ((flags & 0x10) ? 0x40 : 0);
    shell.end_ms = -1;
    shell.pump_state = (flags & 0x10) ? 0 : 1; // Original 0x4cf56b video write.
    std::memcpy(storage, &shell, sizeof shell);
    return true;
}
static std::int32_t signed_bits(std::uint32_t value) noexcept {
    std::int32_t result; std::memcpy(&result, &value, sizeof result); return result;
}
Request decode_play(PlayValues32 v) noexcept {
    // x86 LEA/IMUL/ADD use low 32 bits, including on malformed large operands.
    const auto start = signed_bits((v.start_minutes * 60u + v.start_seconds) * 1000u + v.start_milliseconds);
    const auto end = signed_bits((v.end_minutes * 60u + v.end_seconds) * 1000u + v.end_milliseconds);
    return {{v.source, start, end > 0 ? end : -1, v.loop != 0}, {v.callback_context, v.callback_index}};
}
bool decode_seek_caller(std::uint32_t ret, SeekCaller& caller, media::SeekIntent& intent) noexcept {
    intent = media::SeekIntent::preserve;
    switch (ret) {
    case 0x0049840f: caller = SeekCaller::manager_loop; intent = media::SeekIntent::loop_restart; return true;
    case 0x00498d59: caller = SeekCaller::explicit_play; return true;
    case 0x00498f5a: caller = SeekCaller::speech_play; return true;
    default: caller = SeekCaller::other; return false;
    }
}
bool decode_rate(std::int32_t input, double& result) noexcept {
    if (input <= 0) return false;
    // Exact float bits0x3727c5ac, widened before multiplication as in accepted policy.
    result = double(input) * (2748779.0 / 274877906944.0);
    return true;
}
std::uint32_t rate_tail_boolean(std::int32_t hresult) noexcept { return hresult >= 0 ? 1u : 0u; }
Transition::Transition(Transition&& other) noexcept { *this = std::move(other); }
Transition& Transition::operator=(Transition&& other) noexcept {
    if (this == &other) return *this;
    accepted = other.accepted; operation = other.operation; epoch = other.epoch;
    owner_ = other.owner_; scope_ = other.scope_; notification_ = other.notification_;
    session_ = other.session_; session_scope_ = other.session_scope_;
    pending_ = other.pending_; other.pending_ = false; other.owner_ = nullptr;
    return *this;
}
PreparedPlay::PreparedPlay(PreparedPlay&& other) noexcept { *this = std::move(other); }
PreparedPlay& PreparedPlay::operator=(PreparedPlay&& other) noexcept {
    if (this == &other) return *this;
    runtime_ = std::move(other.runtime_); session_ = other.session_; request_ = other.request_;
    scope_ = other.scope_; owner_ = other.owner_; other.owner_ = nullptr;
    rejection = std::move(other.rejection);
    return *this;
}
Adapter::Sidecar* Adapter::find(SessionHandle h) noexcept {
    if (!h || h.slot >= runtime_.session_capacity()) return nullptr;
    auto& s = sidecars_[h.slot];
    return s.live && s.session == h ? &s : nullptr;
}
AdmittedIdentity Adapter::try_admit(EngineKey shell, media::SourceKey source) noexcept {
    if (!shell.address || !shell.generation || dispatch_generation_ == UINT64_MAX) return {};
    for (const auto& s : sidecars_)
        if (s.live && s.shell.address == shell.address) return {};
    auto h = runtime_.try_admit(source);
    if (!h) return {};
    auto& s = sidecars_[h.slot]; s = {};
    s.session = h; s.shell = shell; s.source = source; s.live = true;
    return {h, source};
}
Lookup Adapter::find_owned(std::uint32_t key, std::uint64_t generation) const noexcept {
    // Prefer a live allocation over a retired tombstone at a reused address.
    const Sidecar* retired = nullptr;
    for (const auto& s : sidecars_) {
        if (!s.session || s.shell.address != key) continue;
        if (s.live) return {s.shell.generation == generation ? Ownership::owned_live : Ownership::owned_stale, s.session};
        retired = &s;
    }
    return retired ? Lookup{Ownership::owned_stale, retired->session} : Lookup{};
}
bool Adapter::associate_record(SessionHandle h, EngineKey record) noexcept {
    auto* s = find(h);
    if (!s || !record.address || !record.generation || s->record.address) return false;
    for (const auto& candidate : sidecars_)
        if (candidate.live && candidate.record.address == record.address) return false;
    s->record = record; // No ID read: original ID store0x4982a3 follows publication.
    return true;
}
bool Adapter::publish_record_id(SessionHandle h, EngineKey record, media::SourceKey source) noexcept {
    auto* s = find(h);
    if (!s || !(s->record == record) || s->source != source) return false;
    s->id_published = true; return true;
}
bool Adapter::replace_binding(SessionHandle h, std::uint32_t slot) noexcept {
    auto* s = find(h);
    if (!s || s->binding_generation == UINT64_MAX) return false;
    ++s->binding_generation; s->slot = slot;
    invalidate_traversal();
    return true;
}
PreparedPlay Adapter::prepare_play(SessionHandle h, Request request) noexcept {
    PreparedPlay p;
    auto* s = find(h);
    if (!s || !s->id_published || dispatch_generation_ == UINT64_MAX) {
        p.rejection = reject_play(request); return p;
    }
    p.runtime_ = runtime_.prepare_play(h, request.playback);
    if (!p.runtime_) { p.rejection = reject_play(request); return p; }
    p.session_ = h; p.request_ = request; p.scope_ = dispatch_generation_; p.owner_ = this;
    return p;
}
Transition Adapter::transition(media::Transition value, CallbackKey old, SessionHandle h) noexcept {
    Transition result;
    result.accepted = value.accepted; result.operation = value.operation; result.epoch = value.epoch;
    result.owner_ = this; result.scope_ = dispatch_generation_;
    result.session_ = h;
    if (h) result.session_scope_ = sidecars_[h.slot].dispatch_serial;
    // Original termination additionally requires nonzero context and index.
    result.pending_ = value.terminal && old.context && old.index > 0 && old.index < 32;
    result.notification_ = {old, 1, value.previous_operation};
    return result;
}
Transition Adapter::reject_play(Request request) noexcept {
    Transition result;
    result.owner_ = this; result.scope_ = dispatch_generation_;
    // Registry presence/enabled/function eligibility still belongs to live dispatch.
    result.pending_ = request.callback.index >= 0 && request.callback.index < 32;
    result.notification_ = {request.callback, 0, 0};
    return result;
}
Transition Adapter::commit_play(PreparedPlay&& p) noexcept {
    if (p.owner_ != this) return {};
    auto* s = find(p.session_);
    if (!s || p.scope_ != dispatch_generation_) {
        p.runtime_.reset(); p.owner_ = nullptr;
        // Scope invalidation suppresses use of the incoming callback context too.
        return {};
    }
    const auto result = runtime_.commit_play(std::move(p.runtime_));
    p.owner_ = nullptr;
    if (!result.accepted) return reject_play(p.request_);
    const auto old = s->callback;
    s->callback = p.request_.callback;
    return transition(result, old, p.session_);
}
Transition Adapter::seek(SessionHandle h, std::int32_t start, media::SeekIntent intent) noexcept {
    if (!find(h)) return {};
    return transition(runtime_.seek(h, start, intent), {});
}
Transition Adapter::run(SessionHandle h) noexcept {
    if (!find(h)) return {};
    return transition(runtime_.run(h), {});
}
bool Adapter::set_end(SessionHandle h, media::Epoch epoch, std::int32_t end) noexcept {
    return find(h) && runtime_.set_end(h, epoch, end);
}
Transition Adapter::stop(SessionHandle h) noexcept {
    auto* s = find(h); if (!s) return {};
    const auto old = s->callback; s->callback = {};
    return transition(runtime_.stop(h), old, h);
}
Transition Adapter::retire(SessionHandle h) noexcept {
    auto* s = find(h); if (!s) return {};
    invalidate_traversal();
    if (s->dispatch_serial != UINT64_MAX) ++s->dispatch_serial;
    const auto old = s->callback; s->callback = {}; s->live = false;
    return transition(runtime_.retire(h), old, h);
}
Transition Adapter::consume_event(SessionHandle h, media::OperationId op, media::Epoch epoch, media::Event event) noexcept {
    auto* s = find(h); if (!s) return {};
    const auto result = runtime_.consume_event(h, op, epoch, event);
    const auto old = s->callback;
    if (result.terminal) s->callback = {};
    return transition(result, old, h);
}
bool Adapter::take_notification(Transition& transition, Notification& out) noexcept {
    if (transition.owner_ != this || !transition.pending_) return false;
    transition.pending_ = false; // Clear BEFORE any caller can dispatch/reenter.
    if (transition.scope_ != dispatch_generation_ || dispatch_generation_ == UINT64_MAX) return false;
    if (transition.session_) {
        const auto& s = sidecars_[transition.session_.slot];
        if (!(s.session == transition.session_) || s.dispatch_serial != transition.session_scope_ ||
            s.dispatch_serial == UINT64_MAX) return false;
    }
    out = transition.notification_; return true;
}
std::int32_t Adapter::set_rate(SessionHandle h, std::int32_t input, ClockTransaction transaction) noexcept {
    double rate;
    if (!decode_rate(input, rate)) return rate_invalid;
    if (!find(h) || !transaction.apply || !transaction.clock) return rate_failed;
    return transaction.apply(transaction.clock, input, rate) ? rate_ok : rate_failed;
}
TraversalTicket Adapter::begin_traversal(Traversal kind) const noexcept { return {traversal_generation_, kind}; }
void Adapter::invalidate_traversal() noexcept { if (traversal_generation_ != UINT64_MAX) ++traversal_generation_; }
void Adapter::invalidate_dispatch_scope() noexcept {
    if (dispatch_generation_ != UINT64_MAX) ++dispatch_generation_;
    // An invalid context must never reappear on a later worker terminal event.
    for (auto& s : sidecars_) s.callback = {};
}
Continuation Adapter::classify_continuation(TraversalTicket ticket) const noexcept {
    if (ticket.generation == traversal_generation_ && traversal_generation_ != UINT64_MAX) return Continuation::live;
    return ticket.kind == Traversal::manager ? Continuation::abort_manager : Continuation::abort_stop_all;
}
void Adapter::observe_record_retirement(EngineKey record) noexcept {
    // Unowned retirement is equally capable of destroying a cached traversal node.
    invalidate_traversal();
    for (auto& s : sidecars_) {
        if (s.record == record) {
            if (s.dispatch_serial != UINT64_MAX) ++s.dispatch_serial;
            // Observer revokes pending dispatch. A retirement delivery, if needed,
            // must be claimed at the qualified callback seam before this observer.
            s.callback = {}; s.live = false; runtime_.retire(s.session);
        }
    }
}
void Adapter::clear() noexcept {
    invalidate_traversal(); invalidate_dispatch_scope();
    for (auto& s : sidecars_) if (s.live) {
        s.callback = {}; s.live = false; runtime_.retire(s.session);
    }
}
} // namespace x3m::media_playback
