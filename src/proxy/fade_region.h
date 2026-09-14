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
}
