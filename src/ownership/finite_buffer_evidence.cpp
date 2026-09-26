#include "finite_buffer_evidence.h"
#include <algorithm>
#include <limits>
#include <new>

namespace x3m::ownership {
namespace {
constexpr std::uint8_t known = 1, half_low = 2, half_high = 4, float_finite = 8;
void add(std::uint64_t& counter, std::uint64_t amount) noexcept {
    counter = amount > std::numeric_limits<std::uint64_t>::max() - counter ? std::numeric_limits<std::uint64_t>::max()
                                                                           : counter + amount;
}
std::uint32_t load16(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8);
}
std::uint32_t load32(const std::uint8_t* p) noexcept {
    return load16(p) | (load16(p + 2) << 16);
}
bool same(const PositionEvidenceRange& a, const PositionEvidenceRange& b) noexcept {
    return a.stream_offset == b.stream_offset && a.stride == b.stride && a.position_offset == b.position_offset &&
           a.first_vertex == b.first_vertex && a.vertex_count == b.vertex_count && a.storage == b.storage;
}
}
bool FiniteBufferEvidence::required_payload(EvidenceBufferKind kind, std::uint64_t bytes,
                                            std::size_t* result) noexcept {
    if (result) *result = 0;
    if (!result || !bytes || bytes > std::numeric_limits<std::size_t>::max()) return false;
    if (kind == EvidenceBufferKind::Index16 || kind == EvidenceBufferKind::Index32)
        return bytes % (kind == EvidenceBufferKind::Index16 ? 2 : 4) == 0;
    if (kind != EvidenceBufferKind::Vertex) return false;
    const auto cells = bytes / 4 + (bytes % 4 != 0);
    const auto payload = cells / 2 + (cells % 2 != 0);
    if (payload > std::numeric_limits<std::size_t>::max()) return false;
    *result = static_cast<std::size_t>(payload);
    return true;
}
bool FiniteBufferEvidence::initialize(EvidenceBufferKind kind, std::uint64_t bytes, std::size_t budget) noexcept {
    reset();
    std::size_t payload = 0;
    if (!required_payload(kind, bytes, &payload) || payload > budget) return false;
    if (payload) {
        atlas_.reset(new (std::nothrow) std::uint8_t[payload]);
        if (!atlas_) return false;
        std::fill_n(atlas_.get(), payload, std::uint8_t{0});
    }
    payload_bytes_ = payload;
    byte_size_ = bytes;
    kind_ = kind;
    return true;
}
void FiniteBufferEvidence::reset() noexcept {
    atlas_.reset();
    payload_bytes_ = 0;
    byte_size_ = 0;
    kind_ = EvidenceBufferKind::Unknown;
    revision_ = high_revision_ = 0;
    pending_ = staged_ = false;
    pending_revision_ = offset_ = length_ = 0;
    indices_known_ = staged_indices_known_ = false;
    index_minimum_ = index_maximum_ = 0;
    cache_valid_ = false;
    cache_revision_ = 0;
    cache_range_ = {};
    cache_result_ = FiniteStatus::Unknown;
    counters_ = {};
}
void FiniteBufferEvidence::invalidate(std::uint64_t revision) noexcept {
    high_revision_ = std::max(high_revision_, revision);
    if (atlas_) std::fill_n(atlas_.get(), payload_bytes_, std::uint8_t{0});
    revision_ = 0;
    pending_ = staged_ = false;
    pending_revision_ = offset_ = length_ = 0;
    indices_known_ = staged_indices_known_ = false;
    index_minimum_ = index_maximum_ = 0;
    cache_valid_ = false;
}
std::uint8_t FiniteBufferEvidence::cell(std::uint64_t index) const noexcept {
    return (atlas_[static_cast<std::size_t>(index / 2)] >> (4 * (index % 2))) & 15;
}
void FiniteBufferEvidence::set_cell(std::uint64_t index, std::uint8_t bits) noexcept {
    auto& value = atlas_[static_cast<std::size_t>(index / 2)];
    const unsigned shift = 4 * unsigned(index % 2);
    value = std::uint8_t((value & ~(15u << shift)) | (unsigned(bits) << shift));
}
bool FiniteBufferEvidence::begin_write(std::uint64_t revision, std::uint64_t offset, std::uint64_t length,
                                       EvidenceWriteMode mode) noexcept {
    if (pending_ || kind_ == EvidenceBufferKind::Unknown || mode != EvidenceWriteMode::Preserving || !revision ||
        revision <= high_revision_ || offset > byte_size_ || (!length && offset) || length > byte_size_ - offset) {
        invalidate(revision);
        return false;
    }
    if (!length) length = byte_size_; // Only (0,0) reaches this normalization.
    if (!length) {
        invalidate(revision);
        return false;
    }
    // A skipped write notification cannot preserve untouched cells. Still permit
    // this mapping to establish fresh evidence for its own covered span.
    if (revision != high_revision_ + 1) invalidate();
    high_revision_ = revision;
    pending_ = true;
    staged_ = false;
    pending_revision_ = revision;
    offset_ = offset;
    length_ = length;
    cache_valid_ = false;
    indices_known_ = staged_indices_known_ = false;
    if (kind_ == EvidenceBufferKind::Vertex) {
        const auto end = offset + length;
        const auto last = end / 4 + (end % 4 != 0);
        for (auto i = offset / 4; i < last; ++i) set_cell(i, 0);
    }
    return true;
}
bool FiniteBufferEvidence::stage_mapped(std::uint64_t revision, const void* bytes,
                                        std::uint64_t mapped_length) noexcept {
    if (!pending_ || staged_ || revision != pending_revision_ || !bytes || mapped_length != length_ ||
        mapped_length > std::numeric_limits<std::size_t>::max()) {
        invalidate(revision);
        return false;
    }
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    if (kind_ == EvidenceBufferKind::Vertex) {
        const auto first = offset_ / 4 + (offset_ % 4 != 0), last = (offset_ + length_) / 4;
        for (auto i = first; i < last; ++i) {
            const auto bits = load32(p + static_cast<std::size_t>(i * 4 - offset_));
            std::uint8_t flags = known;
            if ((bits & 0x7c00u) != 0x7c00u) flags |= half_low;
            if ((bits & 0x7c000000u) != 0x7c000000u) flags |= half_high;
            if ((bits & 0x7f800000u) != 0x7f800000u) flags |= float_finite;
            set_cell(i, flags);
        }
        if (last > first) add(counters_.classified_bytes, (last - first) * 4);
    } else if (offset_ == 0 && length_ == byte_size_) {
        const std::size_t width = kind_ == EvidenceBufferKind::Index16 ? 2 : 4;
        std::uint32_t minimum = std::numeric_limits<std::uint32_t>::max(), maximum = 0;
        for (std::size_t at = 0; at < static_cast<std::size_t>(length_); at += width) {
            const auto value = width == 2 ? load16(p + at) : load32(p + at);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        index_minimum_ = minimum;
        index_maximum_ = maximum;
        staged_indices_known_ = true;
        add(counters_.classified_bytes, length_);
    }
    staged_ = true;
    return true;
}
bool FiniteBufferEvidence::finish_write(std::uint64_t revision, bool success) noexcept {
    if (!success || !pending_ || !staged_ || revision != pending_revision_) {
        invalidate(revision);
        return false;
    }
    revision_ = revision;
    indices_known_ = staged_indices_known_;
    pending_ = staged_ = false;
    pending_revision_ = offset_ = length_ = 0;
    staged_indices_known_ = false;
    return true;
}
FiniteStatus FiniteBufferEvidence::query_positions(std::uint64_t revision,
                                                   const PositionEvidenceRange& range) noexcept {
    if (kind_ != EvidenceBufferKind::Vertex || pending_ || !revision || revision != revision_)
        return FiniteStatus::Unknown;
    if (cache_valid_ && cache_revision_ == revision && same(cache_range_, range)) {
        add(counters_.cache_hits, 1);
        return cache_result_;
    }
    const std::uint32_t width = range.storage == PositionStorage::Float3  ? 12
                                : range.storage == PositionStorage::Half4 ? 8
                                                                          : 0;
    const std::uint32_t alignment = range.storage == PositionStorage::Float3 ? 4 : 2;
    if (!width || !range.vertex_count || range.first_vertex < 0 || range.stride < width ||
        range.position_offset > range.stride - width || range.stream_offset > byte_size_ ||
        range.position_offset > byte_size_ - range.stream_offset)
        return FiniteStatus::Unknown;
    const auto base = range.stream_offset + range.position_offset;
    if (base % alignment || range.stride % alignment || width > byte_size_ - base) return FiniteStatus::Unknown;
    const auto capacity = (byte_size_ - base - width) / range.stride;
    const auto first = static_cast<std::uint64_t>(range.first_vertex);
    // Bound before adding/multiplying attacker-sized first/count/stride fields.
    if (first > capacity || range.vertex_count - 1 > capacity - first) return FiniteStatus::Unknown;
    bool unknown = false;
    FiniteStatus result = FiniteStatus::Finite;
    for (std::uint64_t vertex = 0; vertex < range.vertex_count; ++vertex) {
        const auto start = base + (first + vertex) * range.stride;
        for (unsigned component = 0; component < 3; ++component) {
            const auto at = start + component * alignment;
            const auto bits = cell(at / 4);
            add(counters_.position_components, 1);
            if (!(bits & known))
                unknown = true;
            else if (!(bits & (range.storage == PositionStorage::Float3 ? float_finite
                               : at % 4 == 0                            ? half_low
                                                                        : half_high))) {
                result = FiniteStatus::NonFinite;
                break;
            }
        }
        if (result == FiniteStatus::NonFinite) break;
    }
    if (result != FiniteStatus::NonFinite && unknown) result = FiniteStatus::Unknown;
    cache_valid_ = true;
    cache_revision_ = revision;
    cache_range_ = range;
    cache_result_ = result;
    return result;
}
IndexEvidenceBounds FiniteBufferEvidence::query_indices(std::uint64_t revision, std::uint64_t first,
                                                        std::uint64_t count) const noexcept {
    if ((kind_ != EvidenceBufferKind::Index16 && kind_ != EvidenceBufferKind::Index32) || pending_ || !revision ||
        revision != revision_ || !indices_known_ || !count)
        return {};
    const auto total = byte_size_ / (kind_ == EvidenceBufferKind::Index16 ? 2 : 4);
    if (first >= total || count > total - first) return {};
    return {true, index_minimum_, index_maximum_, first == 0 && count == total};
}
bool FiniteBufferEvidence::indexed_vertex_range(const IndexEvidenceBounds& bounds, std::int64_t base,
                                                std::uint64_t minimum, std::uint64_t count,
                                                std::int64_t* first) noexcept {
    if (first) *first = 0;
    if (!first || !bounds.known || bounds.minimum > bounds.maximum || !count || minimum > bounds.minimum ||
        count - 1 > std::numeric_limits<std::uint64_t>::max() - minimum || bounds.maximum > minimum + count - 1)
        return false;
    // minimum <= a uint32 index here, so its signed conversion is exact.
    const auto relative = static_cast<std::int64_t>(minimum);
    if (base > std::numeric_limits<std::int64_t>::max() - relative) return false;
    const auto effective = base + relative;
    if (effective < 0 || count - 1 > std::uint64_t(std::numeric_limits<std::int64_t>::max() - effective)) return false;
    *first = effective;
    return true;
}
} // namespace x3m::ownership
