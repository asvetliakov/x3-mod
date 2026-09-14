#pragma once
#include "fade_region_core.h"

// Production binding of the bound table (fade_region_core.h) to the proxy:
// engine_memory::read, object_trace::scope_descriptor and
// ownership::get_buffer_content_view. resolve preserves LastError.
namespace x3m::fade_region {
const Environment& production_environment() noexcept;
Result resolve(BoundTable& table, const Query& query) noexcept;
}
