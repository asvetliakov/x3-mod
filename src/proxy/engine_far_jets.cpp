#include "engine_far_jets.h"
#include "engine_node_layout.h"

// Inside the engine's cull/LOD pass (cull_small_parts_core.h stub, the far block): integer loads from the node and the
// view the pass itself dereferences, integer stores into a static buffer, no call, no floating point, no Win32.
static_assert(sizeof(void*) == 4, "x86 engine fields");
namespace {
namespace core = x3m::engine_far_jets::core;
core::Raw buffer_[core::capacity];
unsigned count_ = 0;
core::CulledPair culled_[core::capacity]; // the engine-culled (node handle, view handle) pairs of the frame
unsigned culled_count_ = 0;
core::Stats stats_{};
core::Requests requests_{};
// The node's model id: the edition layout's (engine_node_layout.h: +0x140 on X3AP.exe, +0x144 on X3FL.exe). Every other
// field the handler reads is at the same offset in both editions (docs/reverse-engineering/fl/cull-chain.md, "Verified
// 2026-11-03": 18 fields, 47 anchors, verification/results/fl-far-jets/read_set.py).
namespace layout = x3m::engine_node_layout;
constexpr unsigned model_at = layout::edition.model;
static_assert(layout::ap.model == core::model_offset && layout::edition.parent == core::parent_offset &&
                  layout::edition.handle == core::handle_offset,
              "the shared node fields are the edition layout's");
#if X3M_EDITION_FL
static_assert(core::scale70_offset == 0x70 && core::scale80_offset == 0x80 && core::scale88_offset == 0x88 &&
                  core::position_offset == 0xb0 && core::basis_x_offset == 0xc0 && core::basis_z_offset == 0xe0 &&
                  core::flags12c_offset == 0x12c && core::threshold_1d8_offset == 0x1d8 &&
                  core::view_handle_offset == 0x28 && core::view_context_offset == 0x1c &&
                  core::context_scale_offset == 0x2c,
              "every other far-copy field sits at the AP offset on X3FL.exe (read_set.json)");
// FL owns the context scale at the site (engine_node_layout.h fl_context): one dword per entry, the float's bits
// (an integer copy, no floating point here).
std::uint32_t scale_bits_[core::capacity];
#endif
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
    const std::uint32_t model = word(node, model_at);
    if (model == core::steering_model) {
        ++stats_.steering;
        return;
    }
    const std::uint32_t parent = word(node, core::parent_offset);
    const std::int32_t parent_1d8 = parent ? std::int32_t(word(parent, core::threshold_1d8_offset)) : 0;
    if (core::engine_culls(measure, std::int32_t(word(node, core::threshold_1d8_offset)), parent, parent_1d8,
                           word(node, core::flags12c_offset))) {
        ++stats_.engine;
        // The node-sourced walk's exclusion list: the game's own size verdict for this jet under this view.
        if (culled_count_ < core::capacity) {
            culled_[culled_count_].handle = word(node, core::handle_offset);
            culled_[culled_count_].view_handle = word(view, core::view_handle_offset);
            ++culled_count_;
        } else
            ++stats_.culled_overflow;
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
#if X3M_EDITION_FL
    // The node's context and its scale, the fields the selector loaded on this node at 0x095fc7/0x095fce, read again
    // here (calls run in between, so the values are this read's, not provably the selector's); kept only when that
    // context is the view's (the world matrix's scale), otherwise 0, which far_record refuses.
    const std::uint32_t node_context = word(node, layout::fl_context);
    scale_bits_[count_ - 1] =
        node_context && node_context == r.context ? word(node_context, core::context_scale_offset) : 0u;
#endif
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
    culled_count_ = 0;
    stats_ = core::Stats{};
}
unsigned culled_count() noexcept {
    return culled_count_;
}
const core::CulledPair* culled() noexcept {
    return culled_;
}
unsigned count() noexcept {
    return count_;
}
const core::Raw* entries() noexcept {
    return buffer_;
}
#if X3M_EDITION_FL
const std::uint32_t* context_scale_bits() noexcept {
    return scale_bits_;
}
#endif
core::Stats stats() noexcept {
    return stats_;
}
} // namespace x3m::engine_far_jets
