// Host-only unit fixture for the live route's in-frame previous-row table.
#include "../../src/renderer/motion_row_history.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace x3m::renderer;
namespace {
unsigned checks = 0;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void passed(const char* name) { ++checks; std::printf("CHECK %s\n", name); }
RigidDrawKey key(std::uint64_t node_serial, std::uint32_t first = 0) {
    RigidDrawKey k{};
    k.object_lifetime = node_serial; k.camera_lifetime = 21; k.draw_domain = 5; k.node = 0x1000 + node_serial; k.camera = 0x2000;
    k.vertex_buffer = 7; k.declaration = 9; k.position_program = 0x53a0a641107ed76cull; k.stride = 24; k.primitives = 1;
    k.first = first; k.topology = 4;
    return k;
}
SubmittedMatrix rows(float t) { SubmittedMatrix m{}; m[0] = m[5] = m[10] = m[15] = 1; m[3] = t; return m; }
const MotionRowFrame frame{1, 64, 64};
} // namespace

int main() {
    try {
        MotionRowHistory h(4);
        SubmittedMatrix previous{};
        require(h.ready() && h.stats().capacity == 4, "ready");
        require(!h.lookup_and_record(key(1), rows(0), previous), "not collecting");
        h.begin_frame(frame);
        require(!h.lookup_and_record(key(1), rows(.1f), previous), "no previous frame");
        require(h.stats().current == 1 && !h.stats().previous_valid, "recorded without previous");
        require(h.commit(true), "first commit");
        passed("first_frame_records_without_history");

        h.begin_frame(frame);
        require(h.lookup_and_record(key(1), rows(.2f), previous) && previous[3] == .1f, "matched previous rows");
        require(!h.lookup_and_record(key(1), rows(.2f), previous), "second draw with the same key gets no history");
        require(!h.lookup_and_record(key(2), rows(0), previous), "unknown key");
        require(h.commit(true), "commit with duplicate");
        passed("match_consumes_previous_once");

        h.begin_frame(frame);
        require(!h.lookup_and_record(key(1), rows(.3f), previous), "duplicate key in previous frame is poisoned");
        require(h.lookup_and_record(key(2), rows(1), previous) && previous[3] == 0, "unique key still matches");
        require(h.commit(true), "commit");
        passed("previous_duplicate_poisons_key");

        h.begin_frame(frame);
        RigidDrawKey invalid = key(1); invalid.object_lifetime = 0;
        require(!h.lookup_and_record(invalid, rows(0), previous) && h.stats().current == 0, "invalid key not recorded");
        require(!MotionRowHistory::key_valid(invalid), "key_valid rejects zero lifetime");
        invalid = key(1); invalid.indexed = true; invalid.index_buffer = 0;
        require(!MotionRowHistory::key_valid(invalid), "indexed key needs index buffer identity");
        require(h.lookup_and_record(key(1), rows(.4f), previous) && previous[3] == .3f, "valid key after invalid attempt");
        require(h.commit(true), "commit");
        passed("invalid_keys_rejected");

        h.begin_frame({2, 64, 64});
        require(!h.lookup_and_record(key(1), rows(.5f), previous), "generation change drops previous");
        require(h.commit(true), "commit");
        h.begin_frame({2, 32, 64});
        require(!h.lookup_and_record(key(1), rows(.6f), previous), "dimension change drops previous");
        require(h.commit(true), "commit");
        h.begin_frame({2, 32, 64});
        require(h.lookup_and_record(key(1), rows(.7f), previous) && previous[3] == .6f, "stable regime matches");
        require(!h.commit(false), "failed present drops everything");
        h.begin_frame({2, 32, 64});
        require(!h.lookup_and_record(key(1), rows(.8f), previous), "no history after failed present");
        require(h.commit(true), "commit");
        passed("regime_change_and_failed_present_invalidate");

        h.begin_frame({2, 32, 64});
        require(h.lookup_and_record(key(1), rows(.9f), previous), "match before overflow");
        for (unsigned i = 2; i <= 4; ++i) require(!h.lookup_and_record(key(i), rows(0), previous), "fill to capacity");
        require(!h.lookup_and_record(key(5), rows(0), previous) && h.stats().overflow, "overflow refuses");
        require(!h.commit(true), "overflow poisons the frame at commit");
        require(!h.stats().previous_valid && h.stats().previous == 0, "overflow invalidated tables");
        passed("capacity_overflow_fails_closed");

        h.begin_frame({2, 32, 64});
        SubmittedMatrix bad = rows(0); bad[0] = std::numeric_limits<float>::infinity();
        require(!h.lookup_and_record(key(1), bad, previous), "stored regardless");
        require(h.commit(true), "commit");
        h.begin_frame({2, 32, 64});
        require(!h.lookup_and_record(key(1), rows(0), previous), "non-finite previous rows never match");
        h.begin_frame({2, 32, 64});
        require(!h.stats().previous_valid, "begin without commit drops previous");
        require(h.commit(true), "commit");
        passed("nonfinite_rows_and_uncommitted_frames_reject");

        MotionRowHistory none(0);
        require(!none.ready(), "zero capacity is not ready");
        none.begin_frame(frame);
        require(!none.lookup_and_record(key(1), rows(0), previous) && !none.commit(true), "not-ready table reports nothing");
        h.invalidate();
        require(!h.stats().previous_valid && !h.stats().collecting, "invalidate clears state");
        passed("not_ready_and_invalidate");
        std::printf("RESULT PASS checks=%u\n", checks);
        return 0;
    } catch (const std::exception& e) { std::printf("RESULT FAIL %s\n", e.what()); return 1; }
}
