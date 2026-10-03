#include "engine_far_jets.h"

// Inside the engine's cull/LOD pass (cull_small_parts_core.h stub, the far block): integer loads from the node and the
// view the pass itself dereferences, integer stores into a static buffer, no call, no floating point, no Win32.
static_assert(sizeof(void*) == 4, "x86 engine fields");
namespace {
namespace core = x3m::engine_far_jets::core;
core::Raw buffer_[core::capacity];
unsigned count_ = 0;
core::Stats stats_{};
core::Requests requests_{};
inline std::uint32_t word(std::uint32_t base, unsigned offset) noexcept {
    return *reinterpret_cast<const volatile std::uint32_t*>(base + offset);
}
} // namespace

volatile std::uint32_t x3m_engine_far_armed = 0;

extern "C" void x3m_engine_far_jet(std::uint32_t node, std::int32_t measure, std::uint32_t view) {
    if (!x3m_engine_far_armed || !node || !view) {
        ++stats_.disarmed;
        return;
    }
    const std::uint32_t model = word(node, core::model_offset);
    if (model == core::steering_model) {
        ++stats_.steering;
        return;
    }
    const std::uint32_t parent = word(node, core::parent_offset);
    const std::int32_t parent_1d8 = parent ? std::int32_t(word(parent, core::threshold_1d8_offset)) : 0;
    if (core::engine_culls(measure, std::int32_t(word(node, core::threshold_1d8_offset)), parent, parent_1d8,
                           word(node, core::flags12c_offset))) {
        ++stats_.engine;
        return;
    }
    if (count_ >= core::capacity) {
        ++stats_.overflow;
        return;
    }
    core::Raw& r = buffer_[count_++];
    r.node = node;
    r.view_handle = word(view, core::view_handle_offset);
    r.context = word(view, core::view_context_offset);
    r.parent = parent;
    r.handle = word(node, core::handle_offset);
    r.model = model;
    r.scale70 = word(node, core::scale70_offset);
    r.scale80 = word(node, core::scale80_offset);
    r.scale88 = word(node, core::scale88_offset);
    for (unsigned i = 0; i < 3; ++i) {
        r.position[i] = std::int32_t(word(node, core::position_offset + 4 * i));
        r.basis_x[i] = std::int32_t(word(node, core::basis_x_offset + 4 * i));
        r.basis_z[i] = std::int32_t(word(node, core::basis_z_offset + 4 * i));
    }
    ++stats_.written;
}

namespace x3m::engine_far_jets {
void request(bool* counted, bool requested, const void* device) noexcept {
    requests_.request(counted, requested, reinterpret_cast<std::uintptr_t>(device));
    x3m_engine_far_armed = requests_.armed() ? 1u : 0u;
}
void claim(const void* device) noexcept {
    requests_.claim(reinterpret_cast<std::uintptr_t>(device));
}
bool clears(const void* device) noexcept {
    return requests_.clears(reinterpret_cast<std::uintptr_t>(device));
}
void begin_frame() noexcept {
    count_ = 0;
    stats_ = core::Stats{};
}
unsigned count() noexcept {
    return count_;
}
const core::Raw* entries() noexcept {
    return buffer_;
}
core::Stats stats() noexcept {
    return stats_;
}
} // namespace x3m::engine_far_jets
