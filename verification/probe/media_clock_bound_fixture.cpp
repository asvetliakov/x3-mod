#include "../../src/media/owned_clock/clock.h"
#include <cstdio>
#include <cstring>

using media_owned::Admission;
using media_owned::Clock;
using media_owned::End;
static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "line%d: %s\n", __LINE__, #x); } } while (false)
static void arm(Clock& c, std::int32_t bound, bool loop = true) {
    CHECK(c.begin(77, 0, bound, loop, 0));
    CHECK(c.submit({c.generation(), 1, 0, 500000}) == Admission::accepted);
    CHECK(c.update(0).selected);
}
static void extension_preserves_queue() {
    Clock c(1000); arm(c, 10);
    CHECK(c.submit({c.generation(), 2, 500000, 1000000}) == Admission::accepted);
    const auto generation = c.generation(), operation = c.operation(), rate = c.rate_numerator();
    CHECK(c.set_end(100, 15)); // Advance beyond old bound; no old-bound settlement here.
    CHECK(c.compare(150000) == 0 && c.end() == End::none);
    CHECK(c.queued() == 1 && c.armed() && c.intent() && !c.paused());
    CHECK(c.generation() == generation && c.operation() == operation && c.rate_numerator() == rate);
    const auto selected = c.update(50);
    CHECK(selected.accepted && selected.selected && selected.frame.token == 2 && selected.consumed == 1);
    CHECK(selected.end == End::none && c.generation() == generation);
    CHECK(c.update(100).end == End::none); // Equality is still eligible on an empty queue.
    CHECK(c.update(101).end == End::positive);
    CHECK(c.restart_loop(101)); // Bound-only change preserved loop/start/operation/rate.
    CHECK(c.operation() == operation && c.rate_numerator() == rate && c.intent() && !c.armed());
    CHECK(c.compare(0) == 0);
}
static void shortening_and_disable() {
    for (auto bound : {20, 10}) {
        Clock c(1000); arm(c, 1000);
        CHECK(c.submit({c.generation(), 2, 500000, 1000000}) == Admission::accepted);
        CHECK(c.set_end(bound, 20));
        CHECK(c.end() == End::none && c.queued() == 1 && c.compare(200000) == 0);
        const auto at_bound = c.update(20);
        CHECK(at_bound.accepted && !at_bound.selected && at_bound.consumed == 0);
        CHECK(at_bound.end == (bound == 20 ? End::none : End::positive));
        CHECK(c.update(21).end == End::positive && c.queued() == 1);
    }
    for (auto disabled : {0, -1, INT32_MIN}) {
        Clock c(1000); arm(c, 10);
        CHECK(c.submit({c.generation(), 2, 500000, 1000000}) == Admission::accepted);
        CHECK(c.set_end(disabled, 20) && c.queued() == 1);
        const auto selected = c.update(50);
        CHECK(selected.selected && selected.frame.token == 2 && selected.end == End::none);
        CHECK(c.update(1000).end == End::none); // Underrun is not EOF or the old finite end.
    }
}
static void paused_and_preparing() {
    Clock c(1000); arm(c, 100);
    CHECK(c.set_rate(200000, 0));
    CHECK(c.submit({c.generation(), 2, 500000, 1000000}) == Admission::accepted);
    CHECK(c.pause(5));
    const auto position = c.numerator(); const auto rate_position = c.milliseconds();
    const auto generation = c.generation(), rate = c.rate_numerator();
    CHECK(c.set_end(rate_position.truncated, 30));
    CHECK(c.numerator().compare(position) == 0 && c.paused() && c.intent() && c.queued() == 1);
    CHECK(c.rate_numerator() == rate && c.generation() == generation);
    CHECK(c.update(40).end == End::none && c.numerator().compare(position) == 0);
    CHECK(c.resume(50));
    CHECK(c.update(50).end == End::none);
    CHECK(c.update(51).end == End::positive);
    Clock pending(1000); CHECK(pending.begin(88, 10000000, 2000, false, 0));
    CHECK(pending.set_end(3000, 10000));
    CHECK(pending.compare(10000000) == 0 && !pending.armed() && pending.intent());
    CHECK(pending.submit({pending.generation(), 9, 10000000, 11000000}) == Admission::accepted);
    CHECK(pending.update(20000).selected && pending.compare(10000000) == 0);
}
static void settled_state_is_not_rearmed(Clock& c, End settled, std::uint64_t now) {
    CHECK(c.end() == settled);
    const auto generation = c.generation(), operation = c.operation(), rate = c.rate_numerator();
    const auto position = c.numerator(); const auto queued = c.queued();
    const auto armed = c.armed(), paused = c.paused(), intent = c.intent();
    CHECK(c.set_end(0, now));
    CHECK(c.end() == settled && c.numerator().compare(position) == 0 && c.queued() == queued);
    CHECK(c.generation() == generation && c.operation() == operation && c.rate_numerator() == rate);
    CHECK(c.armed() == armed && c.paused() == paused && c.intent() == intent);
    CHECK(c.set_end(INT32_MAX, now + 1));
    const auto update = c.update(now + 2);
    CHECK(update.accepted && update.end == settled && !update.selected && update.consumed == 0);
    CHECK(c.numerator().compare(position) == 0 && c.generation() == generation);
    CHECK(c.submit({generation, 99, 2000000, 3000000}) == Admission::inactive);
}
static void settled_ends() {
    Clock positive(1000); arm(positive, 1);
    CHECK(positive.submit({positive.generation(), 2, 500000, 1000000}) == Admission::accepted);
    CHECK(positive.update(2).end == End::positive);
    settled_state_is_not_rearmed(positive, End::positive, 100);
    Clock eof(1000); arm(eof, 100);
    CHECK(eof.provider_eof(eof.generation())); CHECK(eof.update(50).end == End::eof);
    settled_state_is_not_rearmed(eof, End::eof, 100);
    Clock malformed(1000); arm(malformed, 100);
    CHECK(malformed.submit({malformed.generation(), 2, 5, 5}) == Admission::malformed);
    settled_state_is_not_rearmed(malformed, End::malformed, 100);
    Clock range(1000); constexpr std::int64_t start = std::int64_t(INT32_MAX) * 10000 + 20000;
    CHECK(range.begin(99, start, 0, true, 0));
    CHECK(range.submit({range.generation(), 1, start, start + 100000}) == Admission::accepted);
    CHECK(range.update(0).end == End::position_range);
    settled_state_is_not_rearmed(range, End::position_range, 100);
}
static void rejects_without_any_mutation(Clock& c, std::uint64_t now) {
    unsigned char before[sizeof c]; std::memcpy(before, &c, sizeof c);
    CHECK(!c.set_end(INT32_MAX, now));
    CHECK(std::memcmp(before, &c, sizeof c) == 0);
}
static void rejection_and_old_slope() {
    Clock inactive(1000); rejects_without_any_mutation(inactive, 100);
    Clock zero(0); rejects_without_any_mutation(zero, 100);
    Clock c(1000); arm(c, 100);
    CHECK(c.submit({c.generation(), 2, 500000, 1000000}) == Admission::accepted);
    CHECK(c.set_end(50, 30)); rejects_without_any_mutation(c, 29);
    CHECK(c.update(50).end == End::none && c.update(51).end == End::positive);
    rejects_without_any_mutation(c, 50); // Settled end still enforces monotonic input.
    Clock paused(1000); arm(paused, 100); CHECK(paused.pause(10));
    rejects_without_any_mutation(paused, 9);
    CHECK(paused.stop(20)); rejects_without_any_mutation(paused, 100);
    // Compare continuous position using another instance of the SAME canonical
    // clock's established advance path, not a second timing implementation.
    Clock rated(1000003); arm(rated, 1000); CHECK(rated.set_rate(123456, 0));
    Clock control = rated;
    CHECK(control.pause(123)); CHECK(rated.set_end(2000, 123));
    CHECK(rated.numerator().compare(control.numerator()) == 0);
    CHECK(rated.rate_numerator() == control.rate_numerator() && !rated.paused());
    CHECK(rated.set_end(-5, 123)); // Equal QPC is a valid transaction, with no time jump.
    CHECK(rated.numerator().compare(control.numerator()) == 0);
}
int main() {
    extension_preserves_queue(); shortening_and_disable(); paused_and_preparing();
    settled_ends(); rejection_and_old_slope();
    std::printf("media_clock_bound checks=%u failures=%u clock_bytes=%zu\n", checks, failures, sizeof(Clock));
    return failures ? 1 : 0;
}
