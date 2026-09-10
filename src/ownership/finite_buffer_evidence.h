#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace x3m::ownership {
enum class EvidenceBufferKind : std::uint8_t { Unknown, Vertex, Index16, Index32 };
enum class PositionStorage : std::uint8_t { Unknown, Float3, Half4 };
enum class FiniteStatus : std::uint8_t { Unknown, Finite, NonFinite };
enum class EvidenceWriteMode : std::uint8_t { Preserving, Discard, Unsupported };
struct PositionEvidenceRange {
    std::uint64_t stream_offset = 0;
    std::uint32_t stride = 0, position_offset = 0;
    std::int64_t first_vertex = 0;
    std::uint64_t vertex_count = 0;
    PositionStorage storage = PositionStorage::Unknown;
};
struct IndexEvidenceBounds {
    bool known = false;
    // Whole-allocation extrema even when the query describes a subdraw. Never
    // claim these are the exact extrema of a proper subrange.
    std::uint32_t minimum = 0, maximum = 0;
    bool exact_range = false;
};
struct EvidenceCounters {
    std::uint64_t classified_bytes = 0, position_components = 0, cache_hits = 0;
};

// Allocation-owned, serialized CPU metadata only: no COM, payload pointer,
// floating arithmetic or hidden reads. The owner supplies exact current revision
// and enforces native mapping readability/coherence, flags, thread, MFENCE,
// allocation/contract identity, and absence of foreign writes/pending mappings.
class FiniteBufferEvidence {
public:
    FiniteBufferEvidence() = default;
    FiniteBufferEvidence(const FiniteBufferEvidence&) = delete;
    FiniteBufferEvidence& operator=(const FiniteBufferEvidence&) = delete;
    // Releases prior storage before allocating. Vertex payload is one nibble per
    // aligned 4B cell; index evidence is inline metadata. Budget includes this
    // retained allocation; there is no second atlas or temporary payload copy.
    // Owner must separately reserve global/per-allocation budget plus sidecar
    // metadata. A false result retains no atlas and all queries remain unknown.
    bool initialize(EvidenceBufferKind kind, std::uint64_t byte_size,
                    std::size_t payload_budget) noexcept;
    void reset() noexcept;      // Release storage; no allocation remains.
    void invalidate(std::uint64_t revision = 0) noexcept; // Retain storage; advance revision floor, forget evidence.
    static bool required_payload(EvidenceBufferKind kind, std::uint64_t byte_size,
                                 std::size_t* bytes) noexcept;
    std::size_t payload_bytes() const noexcept { return payload_bytes_; }
    std::uint64_t byte_size() const noexcept { return byte_size_; }
    bool pending() const noexcept { return pending_; }
    EvidenceCounters counters() const noexcept { return counters_; }

    // Exactly one successful ordinary writable mapping. Revision must be new,
    // nonzero and increasing; a revision gap discards prior untouched evidence.
    // (0,0) means whole allocation; nonzero offset with
    // zero length rejects. Begin invalidates intersecting cells and all cached
    // queries before returning. Nested/discard/unsupported/invalid begin resets
    // all evidence and fails; owner must independently account native lock state.
    bool begin_write(std::uint64_t revision, std::uint64_t offset,
                     std::uint64_t length, EvidenceWriteMode mode) noexcept;
    // Pointer is borrowed only for this call and must cover exactly the normalized
    // mapping span. Only complete aligned 4B cells are read. No fringe repair.
    // Stage in-place while queries are blocked; duplicate/wrong stage resets all.
    // For indices, only a complete allocation upload can stage a certificate.
    // Accepted partial/no-complete-cell stages return true but may prove nothing.
    bool stage_mapped(std::uint64_t revision, const void* bytes,
                      std::uint64_t mapped_length) noexcept;
    // Call after native Unlock. Success also means owner's identity/revision/
    // reservation/native state remained trusted and no mapping remains pending.
    // Failure, missing stage or mismatch resets the entire allocation, so staged
    // cells can never leak into a later preserving write.
    bool finish_write(std::uint64_t revision, bool success) noexcept;

    FiniteStatus query_positions(std::uint64_t revision,
                                 const PositionEvidenceRange& range) noexcept;
    IndexEvidenceBounds query_indices(std::uint64_t revision,
                                      std::uint64_t first_index,
                                      std::uint64_t index_count) const noexcept;
    // Validate conservative whole-IB extrema against the declared relative index
    // interval, then signed base addition. Returns the full declared effective
    // vertex interval for a conservative finite-position scan. No VB limit is
    // assumed here: query_positions validates it against its actual allocation.
    static bool indexed_vertex_range(const IndexEvidenceBounds& bounds,
                                     std::int64_t base_vertex,
                                     std::uint64_t minimum_vertex,
                                     std::uint64_t vertex_count,
                                     std::int64_t* first_vertex) noexcept;
private:
    std::unique_ptr<std::uint8_t[]> atlas_;
    std::size_t payload_bytes_ = 0;
    std::uint64_t byte_size_ = 0, revision_ = 0, high_revision_ = 0;
    EvidenceBufferKind kind_ = EvidenceBufferKind::Unknown;
    bool pending_ = false, staged_ = false;
    std::uint64_t pending_revision_ = 0, offset_ = 0, length_ = 0;
    bool indices_known_ = false, staged_indices_known_ = false;
    std::uint32_t index_minimum_ = 0, index_maximum_ = 0;
    bool cache_valid_ = false;
    std::uint64_t cache_revision_ = 0;
    PositionEvidenceRange cache_range_{};
    FiniteStatus cache_result_ = FiniteStatus::Unknown;
    EvidenceCounters counters_{};
    std::uint8_t cell(std::uint64_t index) const noexcept;
    void set_cell(std::uint64_t index, std::uint8_t bits) noexcept;
};
} // namespace x3m::ownership
