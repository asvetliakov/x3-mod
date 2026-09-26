// Original portable metadata tests. Never reads game payload or uses COM/GPU.
#include "../../src/ownership/finite_buffer_evidence.h"
#include <algorithm>
#include <array>
#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
using namespace x3m::ownership;
namespace allocation_test {
struct alignas(std::max_align_t) Header {
    std::size_t bytes;
};
std::size_t live = 0, peak = 0, calls = 0;
bool fail_next = false;
}
// Test-only array allocator instrumentation proves retained/peak atlas accounting
// and deterministic allocation failure. Production has no allocation test hook.
void* operator new[](std::size_t n) {
    using namespace allocation_test;
    ++calls;
    if (fail_next) {
        fail_next = false;
        throw std::bad_alloc();
    }
    if (n > std::numeric_limits<std::size_t>::max() - sizeof(Header)) throw std::bad_alloc();
    auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + n));
    if (!h) throw std::bad_alloc();
    h->bytes = n;
    live += n;
    peak = std::max(peak, live);
    return h + 1;
}
void operator delete[](void* p) noexcept {
    if (p) {
        auto* h = static_cast<allocation_test::Header*>(p) - 1;
        allocation_test::live -= h->bytes;
        std::free(h);
    }
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete[](p);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](n);
    } catch (...) {
        return nullptr;
    }
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    ::operator delete[](p);
}
unsigned long long checks = 0;
void require(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t bits) {
    bytes.at(at) = std::uint8_t(bits);
    bytes.at(at + 1) = std::uint8_t(bits >> 8);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t bits) {
    put16(bytes, at, std::uint16_t(bits));
    put16(bytes, at + 2, std::uint16_t(bits >> 16));
}
PositionEvidenceRange floats(std::uint64_t count = 1) {
    PositionEvidenceRange r;
    r.storage = PositionStorage::Float3;
    r.stride = 12;
    r.vertex_count = count;
    return r;
}
PositionEvidenceRange halves(std::uint64_t count = 1) {
    PositionEvidenceRange r;
    r.storage = PositionStorage::Half4;
    r.stride = 8;
    r.vertex_count = count;
    return r;
}
void upload(FiniteBufferEvidence& e, std::uint64_t revision, const std::vector<std::uint8_t>& data,
            std::uint64_t offset = 0, std::uint64_t length = 0) {
    require(e.begin_write(revision, offset, length, EvidenceWriteMode::Preserving), "begin original upload");
    const auto n = length ? length : e.byte_size();
    require(offset + n <= data.size(), "test upload bounds");
    require(e.stage_mapped(revision, data.data() + offset, n), "stage original mapped bytes");
    require(e.finish_write(revision, true), "publish matching successful original upload");
}
void exhaustive_half() {
    unsigned long long finite = 0, nonfinite = 0;
    for (unsigned lane = 0; lane < 3; ++lane) {
        // Stride10 and offset2 alternate cell halves across vertices. Stored W
        // visits every encoding independently and must never affect XYZ status.
        std::vector<std::uint8_t> bytes(65536 * 10);
        for (unsigned h = 0; h < 65536; ++h) {
            const auto at = 2 + h * 10;
            put16(bytes, at, 0x3555);
            put16(bytes, at + 2, 0x0001);
            put16(bytes, at + 4, 0x8000);
            put16(bytes, at + lane * 2, std::uint16_t(h));
            put16(bytes, at + 6, std::uint16_t(h ^ 0xa55a));
        }
        FiniteBufferEvidence e;
        require(e.initialize(EvidenceBufferKind::Vertex, bytes.size(), bytes.size() / 8), "half atlas budget");
        upload(e, 1, bytes);
        auto r = halves();
        r.stream_offset = 2;
        r.stride = 10;
        for (unsigned h = 0; h < 65536; ++h) {
            r.first_vertex = h;
            const bool expected = (h % 32768) < 31744;
            require(e.query_positions(1, r) == (expected ? FiniteStatus::Finite : FiniteStatus::NonFinite),
                    "all half encodings independent IEEE classification");
            expected ? ++finite : ++nonfinite;
        }
        require(e.counters().classified_bytes == bytes.size(), "half upload scan exact byte count");
    }
    std::printf("HALF encodings_per_lane=65536 lanes=3 finite=%llu nonfinite=%llu\n", finite, nonfinite);
}
void float_classes() {
    const std::uint32_t mantissas[] = {0, 1, 0x3fffff, 0x400000, 0x7fffff};
    std::vector<std::uint32_t> bits;
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned exponent = 0; exponent < 256; ++exponent)
            for (auto m : mantissas) bits.push_back((sign << 31) | (exponent << 23) | m);
    unsigned long long finite = 0, nonfinite = 0;
    for (unsigned lane = 0; lane < 3; ++lane) {
        std::vector<std::uint8_t> bytes(bits.size() * 20);
        for (std::size_t i = 0; i < bits.size(); ++i) {
            const auto at = 8 + i * 20;
            put32(bytes, at, 0);
            put32(bytes, at + 4, 1);
            put32(bytes, at + 8, 0x80000000);
            put32(bytes, at + 4 * lane, bits[i]);
        }
        FiniteBufferEvidence e;
        require(e.initialize(EvidenceBufferKind::Vertex, bytes.size(), bytes.size() / 8), "float atlas");
        upload(e, 7, bytes);
        auto r = floats();
        r.stream_offset = 4;
        r.position_offset = 4;
        r.stride = 20;
        for (std::size_t i = 0; i < bits.size(); ++i) {
            r.first_vertex = static_cast<std::int64_t>(i);
            const bool expected = (bits[i] & 0x7fffffff) < 0x7f800000;
            require(e.query_positions(7, r) == (expected ? FiniteStatus::Finite : FiniteStatus::NonFinite),
                    "all float sign/exponent classes and mantissa edges");
            expected ? ++finite : ++nonfinite;
        }
    }
    std::printf("FLOAT payloads_per_lane=%zu lanes=3 finite=%llu nonfinite=%llu\n", bits.size(), finite, nonfinite);
}
void budget_and_layout() {
    std::size_t size = 99;
    require(!FiniteBufferEvidence::required_payload(EvidenceBufferKind::Unknown, 8, &size) && !size,
            "unknown kind no storage");
    require(!FiniteBufferEvidence::required_payload(EvidenceBufferKind::Vertex, 0, &size), "empty allocation refused");
    require(!FiniteBufferEvidence::required_payload(EvidenceBufferKind::Vertex, 8, nullptr),
            "null budget result refused");
    for (std::uint64_t n = 1; n <= 65; ++n) {
        require(FiniteBufferEvidence::required_payload(EvidenceBufferKind::Vertex, n, &size) && size == (n + 7) / 8,
                "nibble packed rounding");
    }
    require(!FiniteBufferEvidence::required_payload(EvidenceBufferKind::Index16, 3, &size),
            "odd index16 allocation refused");
    require(!FiniteBufferEvidence::required_payload(EvidenceBufferKind::Index32, 6, &size),
            "unaligned index32 allocation refused");
    FiniteBufferEvidence e;
    const auto before = allocation_test::calls;
    require(!e.initialize(EvidenceBufferKind::Vertex, 64, 7) && !e.payload_bytes() && allocation_test::calls == before,
            "budget rejection precedes allocation");
    allocation_test::fail_next = true;
    require(!e.initialize(EvidenceBufferKind::Vertex, 64, 8) && !e.payload_bytes() && !e.byte_size(),
            "allocation failure leaves unknown empty state");
    require(e.initialize(EvidenceBufferKind::Vertex, 64, 8) && e.payload_bytes() == 8, "exact allocation cap accepted");
    allocation_test::peak = allocation_test::live;
    require(e.initialize(EvidenceBufferKind::Vertex, 80, 10) && allocation_test::peak == 10 &&
                allocation_test::live == 10,
            "reinitialize releases old storage before allocating new atlas");
    std::vector<std::uint8_t> bytes(80, 0);
    require(e.query_positions(1, floats()) == FiniteStatus::Unknown, "creation does not invent zeros");
    upload(e, 1, bytes);
    auto r = floats();
    require(e.query_positions(1, r) == FiniteStatus::Finite, "ordinary float layout finite");
    const auto cached = e.counters();
    require(e.query_positions(1, r) == FiniteStatus::Finite &&
                e.counters().position_components == cached.position_components &&
                e.counters().cache_hits == cached.cache_hits + 1,
            "exact layout revision cache avoids rescanning");
    r.first_vertex = 1;
    require(e.query_positions(1, r) == FiniteStatus::Finite &&
                e.counters().position_components == cached.position_components + 3,
            "changed range rescans");
    const auto unknown = [&](const PositionEvidenceRange& x, const char* label) {
        require(e.query_positions(1, x) == FiniteStatus::Unknown, label);
    };
    r = floats();
    r.vertex_count = 0;
    unknown(r, "zero count never certifies");
    r.storage = PositionStorage::Unknown;
    unknown(r, "zero count does not bypass unknown layout");
    r = floats();
    r.first_vertex = -1;
    unknown(r, "negative first vertex refused");
    r = floats();
    r.first_vertex = std::numeric_limits<std::int64_t>::max();
    unknown(r, "large first bound before multiplication");
    r = floats();
    r.vertex_count = std::numeric_limits<std::uint64_t>::max();
    unknown(r, "count overflow refused");
    r = floats();
    r.stride = 0;
    unknown(r, "zero stride refused");
    r = floats();
    r.stream_offset = std::numeric_limits<std::uint64_t>::max();
    r.position_offset = 1;
    unknown(r, "stream addition overflow refused");
    r = floats();
    r.position_offset = 1;
    r.stride = 16;
    unknown(r, "float offset alignment required");
    r = floats();
    r.stride = 13;
    unknown(r, "float stride alignment required");
    r = halves();
    r.stream_offset = 1;
    unknown(r, "half offset alignment required");
    r = halves();
    r.stride = 9;
    unknown(r, "half stride alignment required");
    r = halves();
    r.stream_offset = 74;
    unknown(r, "ignored W still requires whole storage element in bounds");
    require(e.query_positions(0, floats()) == FiniteStatus::Unknown &&
                e.query_positions(2, floats()) == FiniteStatus::Unknown,
            "zero/wrong revision cannot adopt old evidence");
    put32(bytes, 0, 0x7fc00000);
    require(e.query_positions(1, floats()) == FiniteStatus::Finite,
            "query never dereferences retained payload pointer");
    upload(e, 2, bytes);
    require(e.query_positions(2, floats()) == FiniteStatus::NonFinite, "new notified revision sees nonfinite payload");
    const auto bad = e.counters();
    require(e.query_positions(2, floats()) == FiniteStatus::NonFinite &&
                e.counters().position_components == bad.position_components,
            "nonfinite result also cached");
    e.reset();
    require(!e.payload_bytes() && !e.byte_size() && !allocation_test::live &&
                e.query_positions(2, floats()) == FiniteStatus::Unknown,
            "reset releases all payload and certificates");
}
void transactions() {
    std::vector<std::uint8_t> bytes(24, 0);
    FiniteBufferEvidence e;
    require(e.initialize(EvidenceBufferKind::Vertex, 24, 3), "transaction atlas");
    require(e.begin_write(1, 0, 0, EvidenceWriteMode::Preserving), "whole normalized mapping begins");
    require(e.query_positions(1, floats()) == FiniteStatus::Unknown && e.pending(),
            "pending before stage blocks query");
    require(e.stage_mapped(1, bytes.data(), 24), "stage full mapping");
    require(e.query_positions(1, floats()) == FiniteStatus::Unknown, "staged cells not published before finish");
    require(e.finish_write(1, true) && !e.pending() && e.query_positions(1, floats(2)) == FiniteStatus::Finite,
            "successful finish atomically publishes");
    std::uint64_t revision = 1;
    for (const auto span : {std::pair<unsigned, unsigned>{1, 1}, {1, 2}, {2, 1}}) {
        const auto scanned = e.counters().classified_bytes;
        require(e.begin_write(++revision, span.first, span.second, EvidenceWriteMode::Preserving),
                "tiny partial begin");
        std::vector<std::uint8_t> mapped(span.second, 0); // Exact size gives ASan red zones on both mapping edges.
        require(e.stage_mapped(revision, mapped.data(), mapped.size()) && e.finish_write(revision, true),
                "tiny stage and finish");
        require(e.counters().classified_bytes == scanned,
                "tiny unaligned span classifies zero cells without counter underflow");
        require(e.query_positions(revision, floats()) == FiniteStatus::Unknown, "partial cell remains unknown");
        auto unaffected = floats();
        unaffected.first_vertex = 1;
        require(e.query_positions(revision, unaffected) == FiniteStatus::Finite,
                "consecutive preserving write retains disjoint cells");
        upload(e, ++revision, bytes);
    }
    // offset1 length10 classifies only cells4..7; fringes at0..3 and8..11 unknown.
    require(e.begin_write(++revision, 1, 10, EvidenceWriteMode::Preserving), "fringe begin");
    const auto scanned = e.counters().classified_bytes;
    std::vector<std::uint8_t> fringe(10, 0);
    require(e.stage_mapped(revision, fringe.data(), fringe.size()) && e.finish_write(revision, true),
            "fringe stage finish");
    require(e.counters().classified_bytes == scanned + 4, "only complete mapped cell read");
    require(e.query_positions(revision, floats()) == FiniteStatus::Unknown, "both fringe cells remain unknown");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving) &&
                e.stage_mapped(revision, bytes.data(), 12),
            "failure staged in place");
    require(!e.finish_write(revision, false), "failed native finish resets");
    auto second = floats();
    second.first_vertex = 1;
    require(e.query_positions(revision, second) == FiniteStatus::Unknown,
            "failed finish also clears previously untouched cells");
    upload(e, ++revision, bytes, 12, 12);
    require(e.query_positions(revision, floats()) == FiniteStatus::Unknown &&
                e.query_positions(revision, second) == FiniteStatus::Finite,
            "failed staged cells cannot leak through later preserving write");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "abandoned begin");
    require(!e.finish_write(revision, true) && e.query_positions(revision, second) == FiniteStatus::Unknown,
            "missing stage resets all");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "duplicate stage begin");
    require(e.stage_mapped(revision, bytes.data(), 12), "first stage succeeds");
    require(!e.stage_mapped(revision, bytes.data(), 12) && !e.finish_write(revision, true),
            "duplicate stage resets reservation");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "nested first begin");
    require(!e.begin_write(++revision, 12, 12, EvidenceWriteMode::Preserving) && !e.finish_write(revision, true),
            "nested mapping resets all");
    upload(e, ++revision, bytes);
    require(!e.begin_write(++revision, 0, 0, EvidenceWriteMode::Discard) &&
                e.query_positions(revision, second) == FiniteStatus::Unknown,
            "discard invalidates untouched evidence");
    upload(e, ++revision, bytes);
    require(!e.begin_write(++revision, 0, 0, EvidenceWriteMode::Unsupported), "unsupported flags invalidate");
    upload(e, ++revision, bytes);
    require(!e.begin_write(++revision, 1, 0, EvidenceWriteMode::Preserving),
            "nonzero offset zero native size unsupported");
    upload(e, ++revision, bytes);
    require(!e.begin_write(++revision, 23, 2, EvidenceWriteMode::Preserving), "out of bounds map rejected");
    upload(e, ++revision, bytes);
    require(!e.begin_write(++revision, std::numeric_limits<std::uint64_t>::max(), 1, EvidenceWriteMode::Preserving),
            "overflow map rejected before read");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "null mapped pointer begin");
    require(!e.stage_mapped(revision, nullptr, 12), "null mapped pointer resets without dereference");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "wrong length begin");
    require(!e.stage_mapped(revision, bytes.data(), 11), "wrong mapped length resets all");
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving), "wrong revision begin");
    require(!e.stage_mapped(revision + 1, bytes.data(), 12), "wrong staged revision resets all");
    ++revision;
    upload(e, ++revision, bytes);
    require(e.begin_write(++revision, 0, 12, EvidenceWriteMode::Preserving) &&
                e.stage_mapped(revision, bytes.data(), 12),
            "wrong finish begin stage");
    require(!e.finish_write(revision + 1, true), "wrong finish revision resets all");
    ++revision;
    upload(e, ++revision, bytes);
    require(!e.begin_write(revision, 0, 12, EvidenceWriteMode::Preserving), "same revision cannot refresh evidence");
    upload(e, ++revision, bytes);
    revision += 2;
    upload(e, revision, bytes, 12, 12);
    require(e.query_positions(revision, floats()) == FiniteStatus::Unknown &&
                e.query_positions(revision, second) == FiniteStatus::Finite,
            "revision gap cannot preserve unobserved write region");
    e.invalidate(revision + 7);
    revision += 7;
    require(!e.begin_write(revision - 1, 0, 0, EvidenceWriteMode::Preserving),
            "explicit invalidation keeps revision floor");
    upload(e, ++revision, bytes);
    require(!e.begin_write(0, 0, 0, EvidenceWriteMode::Preserving), "zero revision refused");
    upload(e, std::numeric_limits<std::uint64_t>::max(), bytes);
    require(!e.begin_write(1, 0, 0, EvidenceWriteMode::Preserving), "revision rollover cannot reuse evidence");
    e.reset();
    require(e.initialize(EvidenceBufferKind::Vertex, 24, 3), "new allocation resets revision generation");
    upload(e, 1, bytes);
    // Cell1 unknown, cell2 known nonfinite: status can still report a witness.
    put32(bytes, 8, 0x7f800000);
    upload(e, 2, bytes);
    upload(e, 3, bytes, 4, 1);
    require(e.query_positions(3, floats()) == FiniteStatus::NonFinite,
            "known nonfinite witness survives other unknown cells");
}
void indices() {
    for (auto kind : {EvidenceBufferKind::Index16, EvidenceBufferKind::Index32}) {
        const unsigned width = kind == EvidenceBufferKind::Index16 ? 2 : 4;
        std::vector<std::uint8_t> bytes(width * 6);
        const unsigned values[] = {5, 3, 7, 4, 6, 3};
        for (unsigned i = 0; i < 6; ++i)
            width == 2 ? put16(bytes, i * width, std::uint16_t(values[i])) : put32(bytes, i * width, values[i]);
        FiniteBufferEvidence e;
        require(e.initialize(kind, bytes.size(), 0) && !e.payload_bytes(),
                "index metadata retains no payload allocation");
        require(!e.query_indices(1, 0, 6).known, "creation has no actual index values");
        upload(e, 1, bytes);
        auto b = e.query_indices(1, 0, 6);
        require(b.known && b.exact_range && b.minimum == 3 && b.maximum == 7, "whole actual index extrema");
        auto sub = e.query_indices(1, 1, 2);
        require(sub.known && !sub.exact_range && sub.minimum == 3 && sub.maximum == 7,
                "subdraw gets conservative whole extrema not invented exact bounds");
        require(!e.query_indices(0, 0, 6).known && !e.query_indices(2, 0, 6).known && !e.query_indices(1, 6, 1).known &&
                    !e.query_indices(1, 0, 0).known,
                "index stale/zero/outside query refused");
        require(!e.query_indices(1, std::numeric_limits<std::uint64_t>::max(), 1).known &&
                    !e.query_indices(1, 1, std::numeric_limits<std::uint64_t>::max()).known,
                "index query additions cannot overflow");
        std::int64_t first = -99;
        require(FiniteBufferEvidence::indexed_vertex_range(b, -3, 3, 5, &first) && first == 0,
                "negative base valid when effective declared range nonnegative");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, -4, 3, 5, &first),
                "negative effective declared range refused");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, 0, 4, 4, &first),
                "actual minimum outside declared interval refused");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, 0, 3, 4, &first),
                "actual maximum outside declared interval refused");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, std::numeric_limits<std::int64_t>::max(), 3, 5, &first),
                "signed base addition overflow refused");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, 0, 3, std::numeric_limits<std::uint64_t>::max(), &first),
                "declared end overflow refused");
        require(!FiniteBufferEvidence::indexed_vertex_range(b, std::numeric_limits<std::int64_t>::min(), 3, 5, &first),
                "negative extreme base refused without overflow");
        require(!FiniteBufferEvidence::indexed_vertex_range({}, 0, 0, 8, &first) &&
                    !FiniteBufferEvidence::indexed_vertex_range(b, 0, 3, 5, nullptr),
                "unknown extrema/null result refused");
        require(e.query_positions(1, floats()) == FiniteStatus::Unknown,
                "index certificate cannot act as position atlas");
        require(e.begin_write(2, 0, width, EvidenceWriteMode::Preserving) && !e.query_indices(1, 0, 6).known,
                "pending blocks old index certificate");
        require(e.stage_mapped(2, bytes.data(), width) && e.finish_write(2, true) && !e.query_indices(2, 0, 6).known,
                "partial IB upload cannot reuse extrema");
        upload(e, 3, bytes);
        require(e.query_indices(3, 0, 6).known, "complete upload restores actual index certificate");
        require(e.begin_write(4, 0, 0, EvidenceWriteMode::Preserving) &&
                    e.stage_mapped(4, bytes.data(), bytes.size()) && !e.finish_write(4, false) &&
                    !e.query_indices(4, 0, 6).known,
                "failed IB finish cannot publish staged extrema");
        if (width == 4) {
            put32(bytes, 0, 0xffffffff);
            upload(e, 5, bytes);
            b = e.query_indices(5, 0, 6);
            require(b.maximum == 0xffffffff && b.minimum == 3, "index32 maximum payload is preserved as integer");
        }
    }
}
void fp_and_cache() {
    std::vector<std::uint8_t> bytes(12 * 4096, 0);
    FiniteBufferEvidence e;
    require(e.initialize(EvidenceBufferKind::Vertex, bytes.size(), bytes.size() / 8), "cache benchmark atlas");
    fenv_t saved{};
    require(std::fegetenv(&saved) == 0, "save original FP environment");
    require(std::fesetround(FE_DOWNWARD) == 0 && std::feraiseexcept(FE_INVALID | FE_INEXACT) == 0,
            "hostile FP environment");
    const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
    upload(e, 1, bytes);
    auto range = floats(4096);
    auto begin = std::chrono::steady_clock::now();
    require(e.query_positions(1, range) == FiniteStatus::Finite, "cold full range query");
    auto cold = std::chrono::steady_clock::now();
    const auto components = e.counters().position_components;
    for (unsigned i = 0; i < 10000; ++i) require(e.query_positions(1, range) == FiniteStatus::Finite, "cached query");
    auto warm = std::chrono::steady_clock::now();
    require(e.counters().position_components == components && e.counters().cache_hits == 10000,
            "ten thousand cache hits scan zero additional components");
    require(std::fegetround() == FE_DOWNWARD && std::fetestexcept(FE_ALL_EXCEPT) == flags,
            "classification and queries preserve FP rounding/exception flags");
    require(std::fesetenv(&saved) == 0, "restore original FP environment");
    std::printf(
        "CACHE vertices=4096 cold_components=%llu warm_hits=10000 cold_ns=%lld warm_total_ns=%lld classified_bytes=%llu\n",
        static_cast<unsigned long long>(components),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(cold - begin).count()),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(warm - cold).count()),
        static_cast<unsigned long long>(e.counters().classified_bytes));
}
int main() {
    try {
        exhaustive_half();
        float_classes();
        budget_and_layout();
        transactions();
        indices();
        fp_and_cache();
        require(allocation_test::live == 0, "all atlas allocations released");
        std::printf("RESULT PASS checks=%llu all_atlases_released=1\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL checks=%llu %s\n", checks, e.what());
        return 1;
    }
}
