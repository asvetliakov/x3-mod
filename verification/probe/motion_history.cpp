// Original correspondence scenarios; no engine data or graphics device.
#include "../../src/renderer/motion_history.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
using namespace x3m::renderer;
static bool fail_allocation = false;
void* operator new(std::size_t bytes) {
    if (fail_allocation) throw std::bad_alloc();
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
static unsigned checks = 0;
static void expect(bool success, const char* label) {
    ++checks;
    if (!success) {
        std::printf("FAIL %s\n", label);
        std::exit(1);
    }
}
static RigidObservation draw(unsigned object = 1) {
    RigidObservation o{};
    auto& k = o.key;
    k.object_lifetime = object;
    k.camera_lifetime = 2;
    k.draw_domain = 3;
    k.node = 4 + object;
    k.camera = 5;
    k.mesh = 6;
    k.node_handle = 7;
    k.camera_handle = 8;
    k.model = 9;
    k.lod = 1;
    k.vertex_buffer = 10;
    k.vertex_revision = 1;
    k.index_buffer = 11;
    k.index_revision = 2;
    k.declaration = 12;
    k.position_program = 13;
    k.stride = 32;
    k.position_type = 16;
    k.topology = 4;
    k.primitives = 1;
    k.indexed = true;
    k.vertex_count = 3;
    k.index_format = 101;
    o.submitted_wvp[0] = o.submitted_wvp[5] = o.submitted_wvp[10] = o.submitted_wvp[15] = 1;
    o.proofs = AllRigidProofs;
    return o;
}
static void begin(MotionHistory& h, std::uint64_t frame, std::uint64_t epoch = 1, unsigned width = 64) {
    expect(h.begin_frame({frame, epoch, width, 64, MotionContinuity::Continuous}), "begin authored continuous frame");
}
static void observe(MotionHistory& h, const RigidObservation& o) {
    expect(h.observe(o), "collect observation");
}
static void seal(MotionHistory& h) {
    expect(h.seal(), "seal observations");
}
static void commit(MotionHistory& h) {
    expect(h.commit(true), "commit complete frame");
}
static void seed(MotionHistory& h, const RigidObservation& o) {
    begin(h, 1);
    observe(h, o);
    seal(h);
    commit(h);
}
static void status(MotionHistory& h, const RigidDrawKey& k, Correspondence expected) {
    const auto p = h.lookup(k);
    expect(p.status == expected, "expected correspondence status");
    if (expected != Correspondence::Matched) {
        for (float v : p.current) expect(v == 0, "rejection exposes no current matrix");
        for (float v : p.previous) expect(v == 0, "rejection exposes no prior matrix");
    }
}
int main() {
    const auto original = draw();
    {
        MotionHistory h;
        begin(h, 1);
        observe(h, original);
        status(h, original.key, Correspondence::NotSealed);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
        commit(h);
        auto moved = original;
        moved.submitted_wvp[3] = 0.125f;
        begin(h, 2);
        observe(h, moved);
        seal(h);
        const auto pair = h.lookup(moved.key);
        expect(pair.status == Correspondence::Matched, "moving object matches");
        expect(pair.temporal_continuity_attested(), "authored continuous temporal pair carries continuity evidence");
        expect(!std::memcmp(pair.previous.data(), original.submitted_wvp.data(), sizeof(SubmittedMatrix)),
               "prior submitted rows preserved");
        expect(!std::memcmp(pair.current.data(), moved.submitted_wvp.data(), sizeof(SubmittedMatrix)),
               "current submitted rows preserved");
        commit(h);
    }
    // Every key field affects identity. Draw order does not.
    for (unsigned field = 0; field < 28; ++field) {
        MotionHistory h;
        seed(h, original);
        auto changed = original;
        switch (field) {
        case 0: ++changed.key.object_lifetime; break;
        case 1: ++changed.key.camera_lifetime; break;
        case 2: ++changed.key.draw_domain; break;
        case 3: ++changed.key.node; break;
        case 4: ++changed.key.camera; break;
        case 5: ++changed.key.mesh; break;
        case 6: ++changed.key.node_handle; break;
        case 7: ++changed.key.camera_handle; break;
        case 8: ++changed.key.model; break;
        case 9: ++changed.key.lod; break;
        case 10: ++changed.key.vertex_buffer; break;
        case 11: ++changed.key.vertex_revision; break;
        case 12: ++changed.key.index_buffer; break;
        case 13: ++changed.key.index_revision; break;
        case 14: ++changed.key.declaration; break;
        case 15: ++changed.key.position_program; break;
        case 16: ++changed.key.stream_offset; break;
        case 17: ++changed.key.stride; break;
        case 18: ++changed.key.position_offset; break;
        case 19: changed.key.position_type = 2; break;
        case 20: changed.key.topology = 5; break;
        case 21: ++changed.key.first; break;
        case 22: ++changed.key.primitives; break;
        case 23: --changed.key.base_vertex; break;
        case 24: ++changed.key.min_vertex; break;
        case 25: ++changed.key.vertex_count; break;
        case 26: changed.key.index_format = 102; break;
        case 27:
            changed.key.indexed = false;
            changed.key.index_buffer = changed.key.index_revision = 0;
            changed.key.index_format = changed.key.vertex_count = 0;
            break;
        }
        begin(h, 2);
        observe(h, changed);
        seal(h);
        status(h, changed.key, Correspondence::MissingPrevious);
    }
    {
        MotionHistory h;
        auto other = draw(2);
        begin(h, 1);
        observe(h, original);
        observe(h, other);
        seal(h);
        commit(h);
        begin(h, 2);
        observe(h, other);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::Matched);
        status(h, other.key, Correspondence::Matched);
    }
    for (unsigned defect = 0; defect < 6; ++defect) {
        MotionHistory h;
        seed(h, original);
        auto bad = original;
        if (defect < 5)
            bad.proofs &= ~(1u << defect);
        else
            bad.proofs |= 32;
        begin(h, 2);
        observe(h, bad);
        seal(h);
        status(h, bad.key, Correspondence::MissingProof);
        commit(h);
        begin(h, 3);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::MissingProof);
    }
    for (float value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 2e15f}) {
        MotionHistory h;
        seed(h, original);
        auto bad = original;
        bad.submitted_wvp[6] = value;
        begin(h, 2);
        observe(h, bad);
        seal(h);
        status(h, bad.key, Correspondence::InvalidMatrix);
    }
    for (unsigned order = 0; order < 2; ++order)
        for (unsigned duplicate = 0; duplicate < 4; ++duplicate) {
            MotionHistory h;
            seed(h, original);
            auto second = original;
            if (duplicate == 1) second.submitted_wvp[3] = .25f;
            if (duplicate == 2) second.submitted_wvp[3] = -0.0f;
            if (duplicate == 3) second.proofs = 0;
            begin(h, 2);
            observe(h, order ? second : original);
            observe(h, order ? original : second);
            seal(h);
            expect(h.current_size() == 1, "duplicate folded into one key");
            const auto expected = duplicate ? Correspondence::Ambiguous : Correspondence::Matched;
            status(h, original.key, expected);
            commit(h);
            begin(h, 3);
            observe(h, original);
            seal(h);
            status(h, original.key, expected);
        }
    for (unsigned transition = 0; transition < 8; ++transition) {
        MotionHistory h;
        seed(h, original);
        if (transition == 0) begin(h, 3);         // Gap.
        if (transition == 1) begin(h, 1);         // Repeated frame.
        if (transition == 2) begin(h, 2, 2);      // Epoch.
        if (transition == 3) begin(h, 2, 1, 128); // Resize.
        if (transition == 4) {
            h.invalidate();
            begin(h, 2);
        }
        if (transition == 5) {
            begin(h, 2);
            observe(h, original);
            seal(h);
            expect(!h.commit(false), "failed frame rejected");
            begin(h, 3);
        }
        if (transition == 6) {
            begin(h, 2);
            observe(h, original);
            begin(h, 3);
        } // Uncommitted collecting frame.
        if (transition == 7) {
            begin(h, 2);
            observe(h, original);
            seal(h);
            begin(h, 3);
        } // Uncommitted sealed frame.
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
    }
    {
        MotionHistory h;
        begin(h, std::numeric_limits<std::uint64_t>::max());
        observe(h, original);
        seal(h);
        commit(h);
        expect(!h.begin_frame({0, 1, 64, 64}), "frame overflow cannot wrap");
        begin(h, 1);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
    }
    {
        MotionHistory h(1);
        seed(h, original);
        begin(h, 2);
        observe(h, original);
        expect(!h.observe(original), "capacity applies to duplicates too");
        expect(!h.seal(), "overflow invalidated frame");
        begin(h, 3);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
        MotionHistory disabled(0);
        expect(!disabled.begin_frame({1, 1, 64, 64}), "zero capacity disabled");
        MotionHistory capped(1000000);
        expect(capped.capacity() == 65536, "capacity bounded");
    }
    {
        MotionHistory h;
        seed(h, original);
        begin(h, 2);
        fail_allocation = true;
        const bool success = h.observe(original);
        fail_allocation = false;
        expect(!success, "allocation failure is nonthrowing and invalidates");
        begin(h, 3);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
    }
    {
        MotionHistory h;
        seed(h, original);
        begin(h, 2);
        seal(h);
        status(h, original.key, Correspondence::MissingCurrent);
        commit(h);
        begin(h, 3);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::MissingPrevious);
        expect(!h.observe(original), "observations after seal invalidate");
        expect(!h.commit(true), "invalidated frame cannot publish");
    }
    for (unsigned field = 0; field < 4; ++field) {
        MotionHistory h;
        seed(h, original);
        MotionFrame bad{2, 1, 64, 64};
        if (field == 0) bad.frame = 0;
        if (field == 1) bad.epoch = 0;
        if (field == 2) bad.width = 0;
        if (field == 3) bad.height = 0;
        expect(!h.begin_frame(bad), "invalid frame rejected");
        begin(h, 3);
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
    }
    for (unsigned defect = 0; defect < 6; ++defect) {
        MotionHistory h;
        auto bad = original;
        if (defect == 0) bad.key.object_lifetime = 0;
        if (defect == 1) bad.key.camera_lifetime = 0;
        if (defect == 2) bad.key.position_type = 0;
        if (defect == 3) bad.key.stride = 7;
        if (defect == 4) bad.key.position_offset = std::numeric_limits<std::uint32_t>::max();
        if (defect == 5) bad.key.indexed = false; // Contradictory index fields.
        begin(h, 1);
        observe(h, bad);
        seal(h);
        status(h, bad.key, Correspondence::InvalidKey);
    }
    {
        MotionHistory h;
        seed(h, original);
        expect(h.begin_frame({2, 1, 64, 64}), "default unknown frame can collect a fresh seed");
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
        expect(!h.lookup(original.key).temporal_continuity_attested(),
               "unknown continuity never attests temporal pair");
        commit(h);
        begin(h, 3);
        observe(h, original);
        seal(h);
        expect(h.lookup(original.key).temporal_continuity_attested(),
               "known next transition may use previously unknown fresh seed");
    }
    for (auto purpose :
         {MotionHistoryPurpose::TemporalAccumulation, MotionHistoryPurpose::DiagnosticStorageCorrespondence}) {
        MotionHistory h(8192, purpose);
        seed(h, original);
        expect(h.begin_frame({2, 1, 64, 64, MotionContinuity::Discontinuity}), "explicit cut collects fresh seed");
        observe(h, original);
        seal(h);
        status(h, original.key, Correspondence::NoPreviousFrame);
        expect(!h.lookup(original.key).temporal_continuity_attested(), "explicit cut cannot carry temporal continuity");
        commit(h);
        begin(h, 3);
        observe(h, original);
        seal(h);
        const auto pair = h.lookup(original.key);
        expect(pair.status == Correspondence::Matched && pair.purpose == purpose,
               "both purposes restart after explicit cut");
        expect(pair.temporal_continuity_attested() == (purpose == MotionHistoryPurpose::TemporalAccumulation),
               "diagnostic purpose cannot attest temporal use even with continuous input");
    }
    {
        MotionHistory h(8192, MotionHistoryPurpose::DiagnosticStorageCorrespondence);
        expect(h.begin_frame({1, 1, 64, 64}), "diagnostic unknown seed");
        observe(h, original);
        seal(h);
        commit(h);
        auto moved = original;
        moved.submitted_wvp[3] = 0.75f;
        expect(h.begin_frame({2, 1, 64, 64}), "diagnostic unknown adjacent frame");
        observe(h, moved);
        seal(h);
        const auto pair = h.lookup(moved.key);
        expect(pair.status == Correspondence::Matched && pair.continuity == MotionContinuity::Unknown,
               "diagnostic storage matches without inventing camera continuity");
        expect(!pair.temporal_continuity_attested(), "diagnostic match cannot authorize temporal continuity");
        expect(!std::memcmp(pair.previous.data(), original.submitted_wvp.data(), sizeof(SubmittedMatrix)) &&
                   !std::memcmp(pair.current.data(), moved.submitted_wvp.data(), sizeof(SubmittedMatrix)),
               "diagnostic pairing retains exact submitted matrices");
        commit(h);
        expect(h.begin_frame({4, 1, 64, 64}), "diagnostic gap begins");
        observe(h, moved);
        seal(h);
        status(h, moved.key, Correspondence::NoPreviousFrame);
    }
    {
        MotionHistory h;
        seed(h, original);
        expect(!h.begin_frame({2, 1, 64, 64, static_cast<MotionContinuity>(99)}), "unknown enum value fails closed");
        MotionHistory bad(8192, static_cast<MotionHistoryPurpose>(99));
        expect(!bad.begin_frame({1, 1, 64, 64, MotionContinuity::Continuous}), "invalid purpose fails closed");
    }
    std::printf("RESULT PASS checks=%u\n", checks);
}
