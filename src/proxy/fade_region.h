#pragma once
#include "fade_region_core.h"

// Production binding of the bound table (fade_region_core.h) to the proxy:
// engine_memory::read, object_trace::scope_descriptor and
// ownership::get_buffer_content_view. resolve preserves LastError.
namespace x3m::fade_region {
const Environment& production_environment() noexcept;
Result resolve(BoundTable& table, const Query& query) noexcept;
Result peek(const BoundTable& table, const Query& query) noexcept; // read-only twin (diagnostics)
// Step B locked-prefix bound (ownership::get_locked_prefix_view); preserves LastError.
Result resolve_locked_prefix(const Query& query, std::uint32_t vertex_count) noexcept;
// Step D: true while the record is still published at `revision` (called after the projection); preserves LastError.
bool recheck_locked_prefix(const Query& query, std::uint32_t vertex_count, std::uint64_t revision) noexcept;
// Bolt footprint (bolt_footprint_core.h): the whole scanned vertices of the
// drawn prefix, positions (3 floats) and the UV/colour words (3 words) per
// vertex, marking the buffer like resolve_locked_prefix. false with the
// prefix::Lookup reason in *refusal (1 = unknown, also for an unrecognised
// wrapper or an ownership layer without the scan). Valid while the revision
// holds: recheck_locked_prefix afterwards. LastError preserved.
bool locked_prefix_vertices(const Query& query, std::uint32_t vertex_count, const float** positions,
                            const std::uint32_t** extras, std::uint64_t* revision, unsigned* refusal) noexcept;
}
