#include "../../src/proxy/media_playback.h"
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace m = x3m::media;
namespace p = x3m::media_playback;
static unsigned checks = 0, failures = 0;
static unsigned long long allocations = 0;
void* operator new(std::size_t n) { ++allocations; if (void* v = std::malloc(n)) return v; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "line%d: %s\n", __LINE__, #x); } } while (false)
static p::Request request(unsigned source = 2) { return {{source, 2200, 9000, true}, {0x11223344, 3}}; }
static m::SessionHandle admit(p::Adapter& a, unsigned base, unsigned source = 2) {
    auto id = a.try_admit({base, 1}, source); CHECK(bool(id.session));
    CHECK(a.associate_record(id.session, {base + 0x100, 1}));
    CHECK(a.publish_record_id(id.session, {base + 0x100, 1}, source));
    return id.session;
}
static void drain(p::Adapter& a) { m::Command c; while (a.pop_command(c)) {} }
static void layout_and_decode() {
    static_assert(std::is_trivially_copyable<m::Command>::value);
    static_assert(!std::is_copy_constructible<m::PreparedPlay>::value);
    static_assert(!std::is_copy_constructible<p::Transition>::value);
    CHECK(!p::production_admission_enabled);
    unsigned char bytes[0xb6]; std::memset(bytes, 0xa5, sizeof bytes);
    CHECK(!p::initialize_shell(bytes + 1, 0xb3, {{0, 1}, 2}, 8));
    CHECK(bytes[1] == 0xa5);
    CHECK(p::initialize_shell(bytes + 1, 0xb4, {{0, 1}, 2}, 8));
    unsigned char expected[0xb4]{};
    expected[0] = 2; expected[0x8c] = 8;
    std::memset(expected + 0x94, 0xff, 4); expected[0xb0] = 1;
    CHECK(std::memcmp(bytes + 1, expected, sizeof expected) == 0);
    CHECK(bytes[0] == 0xa5 && bytes[0xb5] == 0xa5);
    p::Shell32 shell{};
    CHECK(p::initialize_shell(&shell, sizeof shell, {{1, 2}, 800}, 0x10));
    CHECK(shell.flags == 0x50 && shell.pump_state == 0 && shell.start_ms == 0 && shell.end_ms == -1);
    auto r = p::decode_play({3, 0x123, 2, 1, 2, 3, 2, 3, 4, 1});
    CHECK(r.playback.start_ms == 62003 && r.playback.end_ms == 123004 && r.playback.loop);
    CHECK(r.callback.context == 0x123 && r.callback.index == 3);
    r = p::decode_play({3, 0x123, 2, UINT32_MAX, 60, 0, 0, 0, 0, 0});
    CHECK(r.playback.start_ms == 0 && r.playback.end_ms == -1 && !r.playback.loop);
    p::SeekCaller caller; m::SeekIntent intent;
    CHECK(p::decode_seek_caller(0x49840f, caller, intent) && intent == m::SeekIntent::loop_restart);
    CHECK(p::decode_seek_caller(0x498d59, caller, intent) && caller == p::SeekCaller::explicit_play && intent == m::SeekIntent::preserve);
    CHECK(p::decode_seek_caller(0x498f5a, caller, intent) && caller == p::SeekCaller::speech_play);
    CHECK(!p::decode_seek_caller(0, caller, intent));
    CHECK(p::manager_abort == 0x4984be && p::stop_all_abort == 0x498362);
    CHECK(p::rate_result_tail == 0x498697 && p::stop_all_owned_tail == 0x498322);
}
static void capacity_and_reservation() {
    m::Runtime r(2, 2);
    auto a = r.try_admit(2), b = r.try_admit(3);
    CHECK(a && b && a.slot != b.slot);
    CHECK(!r.try_admit(4));
    m::Snapshot before{}, after{}; CHECK(r.snapshot(a, before));
    CHECK(!r.prepare_play(a, request().playback));
    CHECK(r.snapshot(a, after) && after.publication.epoch == before.publication.epoch && after.intent == before.intent);
    m::Command c; CHECK(r.pop_command(c) && c.session == a && c.kind == m::CommandKind::construct);
    {
        auto prep = r.prepare_play(a, request().playback); CHECK(bool(prep));
        CHECK(r.occupied_commands() == 2);
        CHECK(!r.prepare_play(a, request().playback));
        auto moved = std::move(prep); CHECK(!prep && moved);
    }
    CHECK(r.occupied_commands() == 1);
    auto prep = r.prepare_play(a, request().playback); CHECK(bool(prep));
    CHECK(r.stop(a).accepted); // Reserved cell released despite full queue.
    CHECK(!r.commit_play(std::move(prep)).accepted);
    CHECK(r.occupied_commands() == 1);
    auto pb = r.prepare_play(b, request(3).playback); CHECK(bool(pb));
    auto started = r.commit_play(std::move(pb)); CHECK(started.accepted);
    CHECK(r.accepts_publication(b, started.operation, started.epoch));
    CHECK(r.retire(a).accepted);
    m::Publication publication{}; CHECK(r.publication(a.slot, publication) && !publication.live);
    CHECK(!r.try_admit(4)); // B's construct+play occupy both cells.
    CHECK(r.pop_command(c));
    auto recycled = r.try_admit(4); CHECK(recycled && recycled.slot == a.slot && recycled.generation != a.generation);
    CHECK(!r.stop(a).accepted && !r.snapshot(a, after));
    CHECK(r.accepts_publication(b, started.operation, started.epoch));
    m::Runtime invalid(m::Runtime::max_sessions + 1, 2); CHECK(!invalid.try_admit(2));
    // An abandoned token must not free a different reservation after slot reuse.
    m::Runtime reused(1, 1); auto old_handle = reused.try_admit(2); CHECK(reused.pop_command(c));
    auto abandoned = reused.prepare_play(old_handle, request().playback); CHECK(bool(abandoned));
    CHECK(reused.retire(old_handle).accepted);
    auto fresh = reused.try_admit(2); CHECK(fresh && reused.pop_command(c));
    auto reserved = reused.prepare_play(fresh, request().playback); CHECK(bool(reserved));
    abandoned.reset(); CHECK(reused.occupied_commands() == 1);
    CHECK(reused.commit_play(std::move(reserved)).accepted);
}
static void same_key_and_failure() {
    p::Adapter a; auto h = admit(a, 0x1000); drain(a);
    auto first = a.prepare_play(h, request()); CHECK(bool(first));
    auto t1 = a.commit_play(std::move(first)); CHECK(t1.accepted && t1.operation != 0);
    p::Notification n{}; CHECK(!a.take_notification(t1, n));
    auto second = a.prepare_play(h, request()); CHECK(bool(second));
    auto moved = std::move(second); CHECK(!second && moved);
    CHECK(!a.commit_play(std::move(second)).accepted);
    auto t2 = a.commit_play(std::move(moved)); CHECK(t2.accepted && t2.operation != t1.operation);
    CHECK(a.take_notification(t2, n) && n.status == 1 && n.operation == t1.operation && n.key.context == request().callback.context);
    CHECK(!a.take_notification(t2, n));
    CHECK(!a.commit_play(std::move(moved)).accepted);
    auto stale = a.consume_event(h, t1.operation, t1.epoch, m::Event::failed);
    CHECK(!stale.accepted && !a.take_notification(stale, n));
    auto failure = a.consume_event(h, t2.operation, t2.epoch, m::Event::failed);
    CHECK(failure.accepted && a.take_notification(failure, n) && n.status == 1 && n.operation == t2.operation);
    auto twice = a.consume_event(h, t2.operation, t2.epoch, m::Event::failed);
    CHECK(!twice.accepted && !a.take_notification(twice, n));
    // Rejection uses incoming key/status0 and leaves the current operation intact.
    p::Adapter full(2, 1); auto f = admit(full, 0x3000); drain(full);
    auto prepared = full.prepare_play(f, request()); auto old = full.commit_play(std::move(prepared));
    auto rejected = full.prepare_play(f, request()); CHECK(!rejected);
    CHECK(full.take_notification(rejected.rejection, n) && n.status == 0);
    CHECK(!full.take_notification(rejected.rejection, n));
    CHECK(full.accepts_publication(f, old.operation, old.epoch));
    // A stop between prepare and commit releases its reservation and rejects only incoming play.
    drain(full); auto pending = full.prepare_play(f, request()); CHECK(bool(pending));
    auto stopped = full.stop(f); CHECK(full.take_notification(stopped, n) && n.operation == old.operation);
    auto denied = full.commit_play(std::move(pending));
    CHECK(!denied.accepted && full.take_notification(denied, n) && n.status == 0);
}
static void independent_sessions_and_seek() {
    p::Adapter a(2, 2); auto h = admit(a, 0x1000), other = admit(a, 0x3000, 3); drain(a);
    auto pa = a.prepare_play(h, request()), pb = a.prepare_play(other, request(3));
    auto ta = a.commit_play(std::move(pa)), tb = a.commit_play(std::move(pb));
    CHECK(ta.accepted && tb.accepted && a.occupied_commands() == 2);
    m::Snapshot s{}; CHECK(a.snapshot(h, s) && s.intent == m::Intent::preparing && s.publication.playing);
    auto stop = a.stop(h); p::Notification n{};
    CHECK(stop.accepted && a.take_notification(stop, n) && n.status == 1);
    CHECK(!a.accepts_publication(h, ta.operation, ta.epoch));
    CHECK(a.accepts_publication(other, tb.operation, tb.epoch));
    CHECK(a.snapshot(h, s) && s.intent == m::Intent::stopped);
    // A stopped seek cannot become playing until a distinct accepted play/Run path.
    auto seek = a.seek(h, 777, m::SeekIntent::preserve); CHECK(seek.accepted);
    CHECK(a.snapshot(h, s) && s.intent == m::Intent::stopped && !s.publication.playing && s.request.start_ms == 777);
    CHECK(!a.run(h).accepted); // Stop consumed operation ownership; no resurrection.
    drain(a);
    auto loop = a.seek(other, 2200, m::SeekIntent::loop_restart);
    CHECK(loop.accepted && loop.operation == tb.operation && loop.epoch != tb.epoch);
    CHECK(!a.accepts_publication(other, tb.operation, tb.epoch));
    CHECK(a.accepts_publication(other, loop.operation, loop.epoch));
    CHECK(a.set_end(other, loop.epoch, 9000)); CHECK(!a.set_end(other, tb.epoch, 123));
    CHECK(a.snapshot(other, s) && s.request.end_ms == 9000 && s.intent == m::Intent::preparing);
    auto ready = a.consume_event(other, loop.operation, loop.epoch, m::Event::ready); CHECK(ready.accepted);
    CHECK(a.snapshot(other, s) && s.intent == m::Intent::playing);
    // Cancelling A's lifetime must not discard B's current callback ownership.
    auto retired = a.retire(h); CHECK(retired.accepted);
    auto stop_all = a.stop(other); CHECK(stop_all.accepted && a.take_notification(stop_all, n) && n.operation == tb.operation);
    CHECK(!a.consume_event(h, ta.operation, ta.epoch, m::Event::ready).accepted);
    CHECK(!a.consume_event(other, loop.operation, loop.epoch, m::Event::failed).accepted);
}
struct ClockSpy { unsigned calls = 0; bool fail = false; double slope = 1.0, position = 12.5; std::int32_t input = 0; };
static bool apply_clock(void* v, std::int32_t input, double rate) noexcept {
    auto& c = *static_cast<ClockSpy*>(v); ++c.calls;
    if (c.fail) return false;
    c.slope = rate; c.input = input; return true;
}
static void rates() {
    p::Adapter a; auto h = admit(a, 0x1000); ClockSpy clock;
    const p::ClockTransaction transaction{&clock, apply_clock};
    double decoded = 42;
    CHECK(!p::decode_rate(0, decoded) && decoded == 42);
    CHECK(a.set_rate(h, 0, transaction) < 0 && a.set_rate(h, -1, transaction) < 0 && clock.calls == 0);
    std::uint32_t bits = 0x3727c5ac; float constant; std::memcpy(&constant, &bits, sizeof bits);
    for (auto input : {1, 100000, INT_MAX}) {
        CHECK(a.set_rate(h, input, transaction) == 0);
        CHECK(clock.slope == double(input) * double(constant) && clock.input == input && clock.position == 12.5);
    }
    CHECK(p::decode_rate(100000, decoded) && decoded < 1.0);
    const auto before = clock.slope; clock.fail = true;
    CHECK(a.set_rate(h, 50000, transaction) < 0 && clock.slope == before && clock.position == 12.5);
    CHECK(p::rate_tail_boolean(p::rate_ok) == 1 && p::rate_tail_boolean(p::rate_failed) == 0);
    CHECK(p::rate_tail_boolean(1) == 1); // Why Boolean1 is NOT a valid failure code at the tail.
    a.retire(h); const auto calls = clock.calls;
    CHECK(a.set_rate(h, 100000, transaction) < 0 && clock.calls == calls);
}
static void reentry_and_reuse() {
    p::Adapter a; auto h = admit(a, 0x1000); drain(a);
    CHECK(a.find_owned(0x1000, 1).ownership == p::Ownership::owned_live);
    CHECK(a.find_owned(0x1000, 2).ownership == p::Ownership::owned_stale);
    CHECK(a.find_owned(0x8000, 1).ownership == p::Ownership::unknown);
    auto prep = a.prepare_play(h, request()); auto first = a.commit_play(std::move(prep));
    auto next = a.prepare_play(h, request()); auto replaced = a.commit_play(std::move(next));
    a.invalidate_dispatch_scope(); p::Notification n{};
    CHECK(!a.take_notification(replaced, n));
    auto fail = a.consume_event(h, replaced.operation, replaced.epoch, m::Event::failed);
    CHECK(fail.accepted && !a.take_notification(fail, n)); // Context never resurrected.
    auto pending = a.prepare_play(h, request()); CHECK(bool(pending));
    a.invalidate_dispatch_scope(); CHECK(!a.commit_play(std::move(pending)).accepted);
    CHECK(a.occupied_commands() == 0);
    // Tickets have no pointers. Simulate current/cached-next retirement at each
    // external copy boundary, with an engine buffer poisoned immediately afterward.
    enum Seam { acquire, lock, unlock, release };
    for (auto seam : {acquire, lock, unlock, release}) {
        (void)seam;
        for (bool owned_current : {false, true}) {
            p::Adapter b; auto bh = admit(b, 0x5000); drain(b);
            auto bp = b.prepare_play(bh, request()); auto bt = b.commit_play(std::move(bp));
            p::Record32 poisoned{};
            auto manager = b.begin_traversal(p::Traversal::manager), all = b.begin_traversal(p::Traversal::stop_all);
            b.observe_record_retirement(owned_current ? p::EngineKey{0x5100, 1} : p::EngineKey{0x7000, 9});
            std::memset(&poisoned, 0xdd, sizeof poisoned);
            CHECK(b.classify_continuation(manager) == p::Continuation::abort_manager);
            CHECK(b.classify_continuation(all) == p::Continuation::abort_stop_all);
            unsigned old_record_reads = 0;
            if (b.classify_continuation(manager) == p::Continuation::live) old_record_reads += poisoned.source;
            CHECK(old_record_reads == 0);
            if (owned_current) CHECK(!b.consume_event(bh, bt.operation, bt.epoch, m::Event::ready).accepted);
        }
    }
    auto ticket = a.begin_traversal(p::Traversal::manager); CHECK(a.replace_binding(h, 7));
    CHECK(a.classify_continuation(ticket) == p::Continuation::abort_manager);
    auto alive = a.begin_traversal(p::Traversal::manager); CHECK(a.classify_continuation(alive) == p::Continuation::live);
    a.clear(); CHECK(a.classify_continuation(alive) == p::Continuation::abort_manager);
    CHECK(a.find_owned(0x1000, 1).ownership == p::Ownership::owned_stale);
    auto recycled = a.try_admit({0x1000, 2}, 2); CHECK(recycled.session && recycled.session.generation != h.generation);
    CHECK(a.associate_record(recycled.session, {0x1100, 2}));
    CHECK(!a.publish_record_id(recycled.session, {0x1100, 1}, 2));
    CHECK(!a.publish_record_id(recycled.session, {0x1100, 2}, 3));
    CHECK(a.publish_record_id(recycled.session, {0x1100, 2}, 2));
    CHECK(!a.consume_event(h, first.operation, first.epoch, m::Event::ready).accepted);
    auto newprep = a.prepare_play(recycled.session, request()); auto newplay = a.commit_play(std::move(newprep));
    auto term = a.stop(recycled.session); a.observe_record_retirement({0x1100, 2});
    CHECK(!a.take_notification(term, n));
    auto replaced_shell = a.try_admit({0x9000, 3}, 2); CHECK(bool(replaced_shell.session));
    CHECK(a.find_owned(0x1000, 2).ownership == p::Ownership::unknown); // Never a legacy-COM proof.
    CHECK(!a.consume_event(recycled.session, newplay.operation, newplay.epoch, m::Event::failed).accepted);
}
// Models only transport acceptance/backpressure; it does not mirror state or
// retain another frame queue. The actual Runtime owns every command under test.
struct SubmitSpy {
    bool busy = false;
    unsigned accepted = 0, refused = 0;
    m::Command last{};
    bool try_submit(const m::Command& c) noexcept {
        if (busy) { ++refused; return false; }
        ++accepted; last = c; return true;
    }
};
static bool transfer(m::Runtime& r, m::SessionHandle h, SubmitSpy& worker) {
    m::CommandOffer offer;
    if (!r.peek_command(h, offer)) return false;
    CHECK(r.offer_current(offer));
    if (!worker.try_submit(offer.command())) return false;
    CHECK(r.acknowledge_command(offer));
    return true;
}
static void per_session_transfer() {
    m::Runtime r(3, 2);
    auto a = r.try_admit(2), b = r.try_admit(3);
    CHECK(a && b && r.occupied_commands() == 2);
    CHECK(!r.try_admit(4));
    SubmitSpy worker_a, worker_b; worker_a.busy = true;
    m::CommandOffer retained_a, retained_b;
    CHECK(r.peek_command(a, retained_a) && r.peek_command(b, retained_b));
    CHECK(!transfer(r, a, worker_a) && r.occupied_commands() == 2);
    CHECK(worker_a.refused == 1 && worker_a.accepted == 0 && r.offer_current(retained_a));
    CHECK(transfer(r, b, worker_b) && worker_b.accepted == 1 && worker_b.last.session == b);
    CHECK(!r.offer_current(retained_b) && !r.acknowledge_command(retained_b));
    CHECK(!transfer(r, b, worker_b) && worker_b.accepted == 1);
    CHECK(r.occupied_commands() == 1 && r.offer_current(retained_a));
    auto c = r.try_admit(4); CHECK(c && r.occupied_commands() == 2);
    CHECK(!r.acknowledge_command(retained_b)); // B's freed cell now belongs to C.
    worker_a.busy = false;
    CHECK(transfer(r, a, worker_a) && worker_a.accepted == 1);
    CHECK(!r.acknowledge_command(retained_a));
    CHECK(!transfer(r, a, worker_a) && worker_a.accepted == 1);
    CHECK(transfer(r, c, worker_b) && worker_b.accepted == 2 && worker_b.last.session == c);
    CHECK(r.occupied_commands() == 0);

    // Multiple same-epoch commands preserve source order. A copied, consumed
    // token cannot acknowledge the next command, even when that cell is reused.
    auto prepared = r.prepare_play(a, request().playback);
    auto play = r.commit_play(std::move(prepared)); CHECK(play.accepted);
    CHECK(r.run(a).accepted && r.occupied_commands() == 2);
    m::CommandOffer play_offer;
    CHECK(r.peek_command(a, play_offer) && play_offer.command().kind == m::CommandKind::play);
    auto copied_offer = play_offer;
    CHECK(transfer(r, a, worker_a) && worker_a.last.kind == m::CommandKind::play);
    CHECK(!r.acknowledge_command(copied_offer));
    CHECK(r.peek_command(a, play_offer) && play_offer.command().kind == m::CommandKind::run);
    CHECK(transfer(r, a, worker_a) && worker_a.last.kind == m::CommandKind::run);
    CHECK(!r.acknowledge_command(play_offer));

    // Supersession before acceptance makes the old offer ineligible. Peek removes
    // only its obsolete command, never the replacement or an independent session.
    prepared = r.prepare_play(a, request().playback); play = r.commit_play(std::move(prepared));
    CHECK(r.peek_command(a, play_offer));
    auto next = r.prepare_play(a, request().playback); auto newer = r.commit_play(std::move(next));
    CHECK(newer.accepted && newer.operation != play.operation && r.occupied_commands() == 2);
    CHECK(!r.offer_current(play_offer) && !r.acknowledge_command(play_offer));
    CHECK(r.occupied_commands() == 2);
    CHECK(r.peek_command(a, play_offer) && play_offer.command().operation == newer.operation);
    CHECK(r.occupied_commands() == 1);
    CHECK(transfer(r, a, worker_a) && worker_a.last.operation == newer.operation);

    // Pending reservations are invisible to the transport, and a busy normal
    // queue never prevents stop/cancellation or stale acknowledgement refusal.
    prepared = r.prepare_play(a, request().playback); CHECK(bool(prepared));
    m::CommandOffer empty;
    CHECK(!r.peek_command(a, empty) && !empty && r.occupied_commands() == 1);
    auto prepare_b = r.prepare_play(b, request(3).playback);
    auto play_b = r.commit_play(std::move(prepare_b)); CHECK(play_b.accepted);
    CHECK(!r.prepare_play(c, request(4).playback));
    CHECK(transfer(r, b, worker_b) && worker_b.last.operation == play_b.operation);
    play = r.commit_play(std::move(prepared)); CHECK(play.accepted);
    CHECK(r.peek_command(a, play_offer));
    CHECK(r.stop(a).accepted && !r.offer_current(play_offer));
    m::Publication cancelled;
    CHECK(r.publication(a.slot, cancelled) && !cancelled.playing && cancelled.epoch != play.epoch);
    prepared = r.prepare_play(a, request().playback); newer = r.commit_play(std::move(prepared));
    CHECK(newer.accepted && !r.acknowledge_command(play_offer) && r.occupied_commands() == 1);
    CHECK(transfer(r, a, worker_a) && worker_a.last.operation == newer.operation);

    // Construct is optional eager work. Latest seek and fresh-identity Run must
    // be self-contained in the actual worker when older epochs were skipped.
    m::Runtime superseded(1, 4); auto h = superseded.try_admit(2);
    CHECK(superseded.peek_command(h, play_offer) && play_offer.command().kind == m::CommandKind::construct);
    auto pending = superseded.prepare_play(h, request().playback);
    auto accepted = superseded.commit_play(std::move(pending)); CHECK(accepted.accepted);
    auto sought = superseded.seek(h, 7777, m::SeekIntent::preserve); CHECK(sought.accepted);
    CHECK(superseded.run(h).accepted && superseded.occupied_commands() == 4);
    CHECK(!superseded.acknowledge_command(play_offer));
    CHECK(superseded.peek_command(h, play_offer) && play_offer.command().kind == m::CommandKind::seek);
    CHECK(play_offer.command().request.start_ms == 7777 && play_offer.command().epoch == sought.epoch);
    CHECK(superseded.occupied_commands() == 2);
    CHECK(transfer(superseded, h, worker_a) && worker_a.last.kind == m::CommandKind::seek);
    CHECK(transfer(superseded, h, worker_a) && worker_a.last.kind == m::CommandKind::run);

    // Allocation generation, monotonic cell serial and Runtime identity prevent
    // ABA, including identical initial handles and serials in a second service.
    CHECK(superseded.run(h).accepted && superseded.peek_command(h, play_offer));
    auto stale_copy = play_offer;
    CHECK(superseded.retire(h).accepted);
    auto recycled = superseded.try_admit(2); CHECK(recycled && recycled.generation != h.generation);
    CHECK(!superseded.acknowledge_command(play_offer));
    CHECK(!superseded.peek_command(h, empty));
    CHECK(superseded.peek_command(recycled, play_offer));
    CHECK(!superseded.acknowledge_command(stale_copy) && superseded.offer_current(play_offer));
    m::Runtime foreign(1, 4); auto foreign_h = foreign.try_admit(2);
    m::CommandOffer foreign_offer; CHECK(foreign.peek_command(foreign_h, foreign_offer));
    auto foreign_copy = foreign_offer;
    CHECK(!superseded.offer_current(foreign_offer) && !superseded.acknowledge_command(foreign_copy));
    CHECK(foreign.offer_current(foreign_offer) && foreign.occupied_commands() == 1);
    CHECK(superseded.acknowledge_command(play_offer));
    CHECK(!superseded.acknowledge_command(play_offer));
}
int main() {
    const auto before = allocations;
    layout_and_decode(); capacity_and_reservation(); same_key_and_failure();
    independent_sessions_and_seek(); rates(); reentry_and_reuse(); per_session_transfer();
    p::Adapter a; auto h = admit(a, 0x1000); drain(a);
    auto prep = a.prepare_play(h, request()); auto current = a.commit_play(std::move(prep));
    constexpr unsigned iterations = 200000;
    unsigned accepted = 0;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        accepted += a.accepts_publication(h, current.operation, current.epoch);
        auto t = a.consume_event(h, current.operation, current.epoch, m::Event::ready);
        accepted += t.accepted;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(accepted == iterations * 2);
    // Worst configured scan size with every command still current. Busy offers
    // remain in place, so both scans execute against all 32 occupied cells.
    m::Runtime full(m::Runtime::max_sessions, m::Runtime::max_commands);
    m::SessionHandle handles[m::Runtime::max_sessions];
    for (unsigned i = 0; i < m::Runtime::max_sessions; ++i) handles[i] = full.try_admit(i + 2);
    m::Command discarded; while (full.pop_command(discarded)) {}
    for (unsigned i = 0; i < m::Runtime::max_sessions; ++i) {
        auto pending = full.prepare_play(handles[i], request(i + 2).playback);
        CHECK(full.commit_play(std::move(pending)).accepted);
        for (unsigned j = 0; j < 3; ++j) CHECK(full.run(handles[i]).accepted);
    }
    CHECK(full.occupied_commands() == m::Runtime::max_commands);
    unsigned offered = 0; m::CommandOffer offer;
    start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        offered += full.peek_command(handles[0], offer);
        offered += full.offer_current(offer);
    }
    const auto offer_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(offered == iterations * 2 && full.occupied_commands() == m::Runtime::max_commands);
    CHECK(allocations == before);
    std::printf("media_owned_adapter checks=%u failures=%u allocations=%llu iterations=%u pair_ns=%.2f runtime_bytes=%zu adapter_bytes=%zu offer_pair_ns=%.2f\n",
                checks, failures, allocations - before, iterations, double(elapsed) / iterations, sizeof(m::Runtime), sizeof(p::Adapter), double(offer_elapsed) / iterations);
    return failures ? 1 : 0;
}
