#include "scene_capture.h"
#include "capture.h"
#include "capture_state.h"
#include "../ownership/d3d9_ownership.h"
#include "../renderer/scene_boundary.h"
#include <vector>

namespace x3m {
namespace {
template <class T> struct Ref {
    T* p = nullptr;
    ~Ref() {
        if (p) p->Release();
    }
    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
};
// Container cache (describe_surface). Key: the resource_id allocation identity.
// Identities come from one process-wide 64-bit counter (capture_state.cpp,
// `id = next_resource_id++`) and live as private data on the backend surface
// (the ownership wrapper's Surface::SetPrivateData forwards to native_,
// d3d9_forwarders_inc.h), which the runtime frees with the surface: a later
// allocation, even at a recycled address or behind a re-created wrapper, starts
// untagged and receives a fresh value, so an identity never names a second
// object within the process. A D3D9 surface's container is fixed at creation
// (texture level, swap chain or device), so identity alone is a safe key; the
// Reset/destroy/cap clears only bound memory. GetDesc stays on the hit path (it
// was there before and keeps the failure semantics) and must match the stored
// fields, else the entry is re-queried. Called under the capture mutex, like
// resource_id. Fixed storage: no allocation on any path.
struct ContainerEntry {
    std::uint64_t id, container;
    std::uint32_t width, height, format, msaa;
};
constexpr unsigned container_slots = 512; // power of two, open addressing; load factor <= 1/2
constexpr unsigned container_cap = 256;
ContainerEntry container_table[container_slots];
unsigned container_count = 0;
SurfaceContainerCacheStats container_stats{};
unsigned container_slot(std::uint64_t id) noexcept {
    return unsigned((id * 0x9E3779B97F4A7C15ull) >> 55) & (container_slots - 1);
}
void container_clear() noexcept {
    if (!container_count) return;
    for (auto& entry : container_table) entry.id = 0;
    container_count = 0;
}
} // namespace
renderer::Surface describe_surface(IDirect3DSurface9* surface) noexcept {
    renderer::Surface result{};
    if (!surface) {
        result.known = true;
        return result;
    }
    D3DSURFACE_DESC desc{};
    if (FAILED(surface->GetDesc(&desc))) return result;
    const auto id = resource_id(surface);
    if (!id) return result;
    const auto format = static_cast<std::uint32_t>(desc.Format);
    const auto msaa = static_cast<std::uint32_t>(desc.MultiSampleType);
    unsigned slot = container_slot(id);
    while (container_table[slot].id && container_table[slot].id != id) slot = (slot + 1) & (container_slots - 1);
    if (container_table[slot].id == id) {
        const ContainerEntry& entry = container_table[slot];
        if (entry.width == desc.Width && entry.height == desc.Height && entry.format == format && entry.msaa == msaa) {
            ++container_stats.hits;
            return {true, id, entry.container, desc.Width, desc.Height, format, msaa};
        }
    }
    ++container_stats.misses;
    Ref<IDirect3DBaseTexture9> container;
    const HRESULT hr = surface->GetContainer(IID_IDirect3DBaseTexture9, reinterpret_cast<void**>(&container.p));
    std::uint64_t container_id = 0;
    if (SUCCEEDED(hr)) {
        if (!container.p || container.p->GetType() != D3DRTYPE_TEXTURE || !(container_id = resource_id(container.p)))
            return result;
    } else if (hr != E_NOINTERFACE)
        return result;
    // Only a known answer is stored; a mismatching entry for the same identity is rewritten in place.
    if (container_table[slot].id != id) {
        if (container_count >= container_cap) {
            container_clear();
            ++container_stats.resets;
            slot = container_slot(id);
        }
        ++container_count;
    }
    container_table[slot] = {id, container_id, desc.Width, desc.Height, format, msaa};
    return {true, id, container_id, desc.Width, desc.Height, format, msaa};
}
SurfaceContainerCacheStats surface_container_cache_stats() noexcept {
    return container_stats;
}
void surface_container_cache_clear(const char* reason) noexcept {
    container_clear();
    log("surface_container_cache reason=%s hits=%llu misses=%llu resets=%llu", reason ? reason : "unknown",
        static_cast<unsigned long long>(container_stats.hits), static_cast<unsigned long long>(container_stats.misses),
        static_cast<unsigned long long>(container_stats.resets));
}
namespace {
renderer::Surface describe(IDirect3DSurface9* surface) {
    return describe_surface(surface);
}
template <class T> std::uint64_t shader_hash(T* shader) {
    if (!shader) return 0;
    UINT size = 0;
    if (FAILED(shader->GetFunction(nullptr, &size)) || !size || size > 4 * 1024 * 1024) return 0;
    std::vector<unsigned char> bytes(size);
    const UINT capacity = size;
    if (FAILED(shader->GetFunction(bytes.data(), &size)) || size != capacity) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
void bindings(IDirect3DDevice9* device, renderer::Event& event, DWORD target_count) {
    Ref<IDirect3DSurface9> rt, depth;
    if (SUCCEEDED(device->GetRenderTarget(0, &rt.p)) && rt.p) event.rt = describe(rt.p);
    const HRESULT depth_hr = device->GetDepthStencilSurface(&depth.p);
    if (SUCCEEDED(depth_hr))
        event.depth = describe(depth.p);
    else if (depth_hr == D3DERR_NOTFOUND && !depth.p)
        event.depth.known = true;
    event.only_rt0 = true;
    for (DWORD index = 1; index < target_count; ++index) {
        Ref<IDirect3DSurface9> extra;
        const HRESULT hr = device->GetRenderTarget(index, &extra.p);
        if (extra.p || (FAILED(hr) && hr != D3DERR_NOTFOUND)) event.only_rt0 = false;
    }
    D3DVIEWPORT9 vp{};
    if (SUCCEEDED(device->GetViewport(&vp))) event.viewport = {true, vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ};
}
const char* event_name(renderer::EventKind kind) {
    using K = renderer::EventKind;
    switch (kind) {
    case K::Clear: return "Clear";
    case K::Draw: return "Draw";
    case K::SetRenderTarget: return "SetRenderTarget";
    case K::SetDepth: return "SetDepth";
    case K::Copy: return "StretchRect";
    case K::ColorFill: return "ColorFill";
    default: return "Unsupported";
    }
}
const char* rejection_name(renderer::BoundaryRejection reason) {
    using R = renderer::BoundaryRejection;
    switch (reason) {
    case R::InvalidFrame: return "InvalidFrame";
    case R::Invalidated: return "Invalidated";
    case R::Sequence: return "Sequence";
    case R::FailedCall: return "FailedCall";
    case R::Pattern: return "Pattern";
    default: return "None";
    }
}
} // namespace

struct SceneCapture::Impl {
    renderer::SceneBoundarySelector selector;
    explicit Impl(const renderer::SceneSignatures& signatures)
        : selector(signatures) {}
    renderer::Event pending;
    std::uint64_t device_id = 0, frame = 0, generation = 0, sequence = 0;
    std::uint64_t copy_epoch = 0;
    DWORD target_count = 0;
    bool active = false, pending_draw = false, pending_clear = false;
    bool attempted = false, copied = false, confirmed = false;
    void stop() noexcept {
        active = false;
        selector.invalidate();
    }
    renderer::Event event(renderer::EventKind kind) {
        renderer::Event result{};
        result.kind = kind;
        result.sequence = ++sequence;
        return result;
    }
    renderer::Selection complete(renderer::Event event, HRESULT hr, const char* operation = nullptr) {
        event.result_known = true;
        event.result = static_cast<std::uint32_t>(hr);
        const auto prior = selector.state();
        const auto selection = selector.observe(event);
        if (prior != renderer::BoundaryState::Rejected && selector.state() == renderer::BoundaryState::Rejected)
            log("scene_depth_reject device=%llu frame=%llu event=%llu operation=%s prior_state=%u rejection=%u reason=%s result=%08lx rt=%llu depth=%llu destination_known=%u destination=%llu destination_container=%llu destination_format=%u destination_width=%u destination_height=%u destination_msaa=%u",
                device_id, frame, selector.rejection_sequence(), operation ? operation : event_name(event.kind),
                unsigned(prior), unsigned(selector.rejection()), rejection_name(selector.rejection()), hr,
                event.rt.identity, event.depth.identity, event.destination.known, event.destination.identity,
                event.destination.container, event.destination.format, event.destination.width,
                event.destination.height, event.destination.msaa);
        return selection;
    }
};

SceneCapture::SceneCapture() noexcept = default;
SceneCapture::SceneCapture(const renderer::SceneSignatures& signatures) noexcept
    : signatures_(signatures) {}
SceneCapture::~SceneCapture() = default;
void SceneCapture::configure(bool requested) noexcept {
    requested_ = requested;
    invalidate();
}
void SceneCapture::invalidate() noexcept {
    if (impl_) impl_->stop();
}
void SceneCapture::unsupported(const char* operation, HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    impl_->complete(impl_->event(renderer::EventKind::Unsupported), result, operation);
    impl_->confirmed = false;
    log("scene_depth_unsupported device=%llu frame=%llu operation=%s result=%08lx", impl_->device_id, impl_->frame,
        operation, result);
}
void SceneCapture::begin_frame(IDirect3DDevice9* device, std::uint64_t device_id, std::uint64_t frame,
                               bool capturing) noexcept {
    invalidate();
    if (!requested_ || !capturing) return;
    try {
        ownership::CopyDepthView view{};
        if (FAILED(ownership::get_copy_depth_view(device, &view)) || !view.requested || !view.available ||
            !view.generation)
            return;
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps)) || caps.NumSimultaneousRTs < 1 || caps.NumSimultaneousRTs > 4) return;
        impl_ = std::make_unique<Impl>(signatures_);
        auto& state = *impl_;
        state.device_id = device_id;
        state.frame = frame;
        state.generation = view.generation;
        state.target_count = caps.NumSimultaneousRTs;
        state.active = true;
        state.selector.begin_frame(device_id, view.generation, frame);
        log("scene_depth_frame phase=begin device=%llu frame=%llu generation=%llu", device_id, frame, view.generation);
    } catch (...) {
        invalidate();
    }
}
bool SceneCapture::collecting_scene() const noexcept {
    return impl_ && impl_->active && impl_->selector.state() == renderer::BoundaryState::Scene;
}
bool SceneCapture::end_frame(HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return false;
    auto& state = *impl_;
    if (FAILED(result)) state.selector.invalidate();
    state.confirmed = state.confirmed && state.selector.state() == renderer::BoundaryState::Selected;
    log("scene_depth_frame phase=end device=%llu frame=%llu generation=%llu events=%llu attempted=%u copied=%u confirmed=%u state=%u rejection=%u rejection_event=%llu present=%08lx",
        state.device_id, state.frame, state.generation, state.sequence, state.attempted, state.copied, state.confirmed,
        unsigned(state.selector.state()), unsigned(state.selector.rejection()), state.selector.rejection_sequence(),
        result);
    state.active = false;
    return state.confirmed;
}
void SceneCapture::before_draw(IDirect3DDevice9* device, D3DPRIMITIVETYPE topology, UINT primitives) noexcept {
    if (!impl_ || !impl_->active) return;
    try {
        auto& state = *impl_;
        auto event = state.event(renderer::EventKind::Draw);
        bindings(device, event, state.target_count);
        event.topology = topology;
        event.primitives = primitives;
        Ref<IDirect3DVertexShader9> vs;
        Ref<IDirect3DPixelShader9> ps;
        Ref<IDirect3DBaseTexture9> texture;
        DWORD z = 0, write = 0;
        const HRESULT vs_hr = device->GetVertexShader(&vs.p), ps_hr = device->GetPixelShader(&ps.p);
        const HRESULT texture_hr = device->GetTexture(0, &texture.p);
        const HRESULT z_hr = device->GetRenderState(D3DRS_ZENABLE, &z);
        const HRESULT write_hr = device->GetRenderState(D3DRS_ZWRITEENABLE, &write);
        event.vs = shader_hash(vs.p);
        event.ps = shader_hash(ps.p);
        event.texture0 = resource_id(texture.p);
        event.z_enable = z;
        event.z_write = write;
        event.draw_state_known = SUCCEEDED(vs_hr) && SUCCEEDED(ps_hr) && SUCCEEDED(texture_hr) && SUCCEEDED(z_hr) &&
                                 SUCCEEDED(write_hr) && event.vs && event.ps && (!texture.p || event.texture0);
        state.pending = event;
        state.pending_draw = true;
    } catch (...) {
        invalidate();
    }
}
void SceneCapture::after_draw(HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    if (!impl_->pending_draw) {
        invalidate();
        return;
    }
    impl_->pending_draw = false;
    impl_->complete(impl_->pending, result);
}
renderer::Selection SceneCapture::before_clear(IDirect3DDevice9* device, DWORD count, const D3DRECT*, DWORD flags,
                                               float depth) noexcept {
    if (!impl_ || !impl_->active) return {};
    try {
        auto& state = *impl_;
        auto event = state.event(renderer::EventKind::Clear);
        bindings(device, event, state.target_count);
        event.clear_flags = flags;
        event.rect_count = count;
        event.clear_z = depth;
        state.pending = event;
        state.pending_clear = true;
        const auto selection = state.selector.before_clear(event);
        if (!selection.valid) return {};
        state.attempted = true;
        ownership::CopyDepthView before{}, after{};
        const HRESULT before_hr = ownership::get_copy_depth_view(device, &before);
        HRESULT result = D3DERR_INVALIDCALL;
        if (SUCCEEDED(before_hr) && before.available && before.source_bound && before.generation == state.generation)
            result = ownership::copy_auto_depth(device);
        const HRESULT after_hr = ownership::get_copy_depth_view(device, &after);
        state.copied = SUCCEEDED(result) && SUCCEEDED(after_hr) && after.available && after.copy_valid &&
                       after.source_bound && after.generation == state.generation &&
                       after.copy_epoch == after.source_epoch;
        state.copy_epoch = after.copy_epoch;
        log("scene_depth_copy device=%llu frame=%llu event=%llu result=%08lx valid=%u generation=%llu source_epoch=%llu copy_epoch=%llu color=%llu depth=%llu",
            state.device_id, state.frame, event.sequence, result, state.copied, after.generation, after.source_epoch,
            after.copy_epoch, selection.color.identity, selection.depth.identity);
        return state.copied ? selection : renderer::Selection{};
    } catch (...) {
        invalidate();
        return {};
    }
}
bool SceneCapture::after_clear(IDirect3DDevice9* device, HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return false;
    if (!impl_->pending_clear) {
        invalidate();
        return false;
    }
    auto& state = *impl_;
    state.pending_clear = false;
    state.pending.result_known = true;
    state.pending.result = static_cast<std::uint32_t>(result);
    const auto selection = state.complete(state.pending, result);
    if (!selection.valid) return false;
    ownership::CopyDepthView view{};
    const HRESULT hr = ownership::get_copy_depth_view(device, &view);
    state.confirmed = state.copied && SUCCEEDED(hr) && SUCCEEDED(view.status) && view.available && view.copy_valid &&
                      view.source_bound && view.generation == state.generation && view.copy_epoch == state.copy_epoch &&
                      view.source_epoch == state.copy_epoch + 1;
    log("scene_depth_boundary device=%llu frame=%llu event=%llu confirmed=%u generation=%llu copy_epoch=%llu source_epoch=%llu",
        state.device_id, state.frame, state.pending.sequence, state.confirmed, view.generation, view.copy_epoch,
        view.source_epoch);
    return state.confirmed;
}
void SceneCapture::after_set_rt(IDirect3DDevice9* device, DWORD index, HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    try {
        auto event = impl_->event(renderer::EventKind::SetRenderTarget);
        event.rt_index = index;
        Ref<IDirect3DSurface9> surface;
        if (SUCCEEDED(device->GetRenderTarget(index, &surface.p))) event.rt = describe(surface.p);
        impl_->complete(event, result);
    } catch (...) {
        invalidate();
    }
}
void SceneCapture::after_set_depth(IDirect3DDevice9* device, HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    try {
        auto event = impl_->event(renderer::EventKind::SetDepth);
        Ref<IDirect3DSurface9> surface;
        const HRESULT hr = device->GetDepthStencilSurface(&surface.p);
        if (SUCCEEDED(hr))
            event.depth = describe(surface.p);
        else if (hr == D3DERR_NOTFOUND && !surface.p)
            event.depth.known = true;
        impl_->complete(event, result);
    } catch (...) {
        invalidate();
    }
}
void SceneCapture::after_stretch(IDirect3DDevice9*, IDirect3DSurface9* source, const RECT* source_rect,
                                 IDirect3DSurface9* destination, const RECT* destination_rect,
                                 HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    try {
        auto event = impl_->event(renderer::EventKind::Copy);
        // A failed call may not have validated either surface pointer.
        if (SUCCEEDED(result)) {
            event.source = describe(source);
            event.destination = describe(destination);
        }
        event.source_rect_null = source_rect == nullptr;
        event.destination_rect_null = destination_rect == nullptr;
        impl_->complete(event, result);
    } catch (...) {
        invalidate();
    }
}
void SceneCapture::after_color_fill(IDirect3DDevice9*, IDirect3DSurface9* destination, const RECT* rect,
                                    HRESULT result) noexcept {
    if (!impl_ || !impl_->active) return;
    try {
        auto event = impl_->event(renderer::EventKind::ColorFill);
        // Native failure may precede pointer validation. Preserve that boundary:
        // describe/read arguments only after success, while still rejecting the event.
        if (SUCCEEDED(result)) event.destination = describe(destination);
        event.destination_rect_null = rect == nullptr;
        const RECT* verified_rect = SUCCEEDED(result) ? rect : nullptr;
        log("scene_depth_color_fill device=%llu frame=%llu event=%llu result=%08lx target_ptr=%p rect_ptr=%p target_known=%u target=%llu container=%llu width=%u height=%u format=%u msaa=%u rect_null=%u left=%ld top=%ld right=%ld bottom=%ld",
            impl_->device_id, impl_->frame, event.sequence, result, destination, rect, event.destination.known,
            event.destination.identity, event.destination.container, event.destination.width, event.destination.height,
            event.destination.format, event.destination.msaa, rect == nullptr, verified_rect ? verified_rect->left : 0L,
            verified_rect ? verified_rect->top : 0L, verified_rect ? verified_rect->right : 0L,
            verified_rect ? verified_rect->bottom : 0L);
        impl_->complete(event, result);
    } catch (...) {
        invalidate();
    }
}
} // namespace x3m
