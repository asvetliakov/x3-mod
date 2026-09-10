#include "d3d9_ownership.h"
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace x3m::ownership {
namespace {

enum class Kind {
    Factory, Device, Texture, CubeTexture, VolumeTexture, Surface, Volume,
    VertexBuffer, IndexBuffer, VertexDeclaration, VertexShader, PixelShader,
    StateBlock, Query, SwapChain
};
struct Node;
struct Factory;
struct Device;

// These are backend-only references. They do not own application wrappers or
// logical device references, and are retired before the native device root.
struct AutoDepth {
    IDirect3DSurface9* original = nullptr;
    IDirect3DSurface9* replacement = nullptr;
    DepthView view;
    bool lost_mapping = false; // Do not expose a retained physical target while lost.
    HRESULT stencil_clear_result = D3DERR_INVALIDCALL;
};

// Weak registries contain only wrappers with positive application refcounts.
// Backend binding/state-block retention never adds an application reference.
// Never hold this lock across backend Release: destruction can reenter COM.
std::recursive_mutex registry_mutex;
std::unordered_map<IUnknown*, Node*> application_nodes;
std::unordered_map<IUnknown*, Node*> native_nodes;

struct Node {
    Kind kind;
    ULONG refs = 1;
    IUnknown* backend;
    IUnknown* application = nullptr;
    IUnknown* identity = nullptr; // Borrowed, stable while backend is owned.
    Node* parent;
    Node(Kind type, IUnknown* native, Node* owner) : kind(type), backend(native), parent(owner) {}
    virtual ~Node() = default;
};

HRESULT query(Node* node, REFIID iid, void** out);
ULONG add_ref(Node* node);
ULONG release(Node* node);
HRESULT get_device(Node* node, IDirect3DDevice9** out);
HRESULT get_factory(Device* node, IDirect3D9** out);
HRESULT get_container(Node* node, REFIID iid, void** out);
HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,
                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out);
HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp);
HRESULT observe_result(Device* node, HRESULT hr);
HRESULT get_depth(Device* node, IDirect3DSurface9** out);
HRESULT set_depth(Device* node, IDirect3DSurface9* surface);
HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil);
void initialize_auto_depth(Device* node, const D3DPRESENT_PARAMETERS& requested);
void retire_auto_depth(Device* node, HRESULT status);
Device* device_of(Node* node);
template<class T> T* unwrap(Device* owner, T* value);
IDirect3DSurface9* unwrap_physical_surface(Device* owner, IDirect3DSurface9* value);
template<class T> HRESULT output(Device* owner, HRESULT hr, T* owned, T** out);

// Some D3D9 failure paths leave an output slot untouched, while others clear it.
// Never initialize from caller storage: it may be uninitialized. This private
// marker is only passed in an output-only slot and is never dereferenced.
unsigned char untouched_output_marker;
template<class T> T* untouched_output() {
    return reinterpret_cast<T*>(&untouched_output_marker);
}

// Generated ABI-complete normal-D3D9 declarations. Handwritten methods above
// own every interface-return, identity, parent, reset and lifetime boundary.
#include "d3d9_classes_inc.h"

bool supports(Kind kind, REFIID iid) {
    if (iid == IID_IUnknown) return true;
    switch (kind) {
    case Kind::Factory: return iid == IID_IDirect3D9;
    case Kind::Device: return iid == IID_IDirect3DDevice9;
    case Kind::Texture:
        return iid == IID_IDirect3DTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::CubeTexture:
        return iid == IID_IDirect3DCubeTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::VolumeTexture:
        return iid == IID_IDirect3DVolumeTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::Surface: return iid == IID_IDirect3DSurface9 || iid == IID_IDirect3DResource9;
    case Kind::Volume: return iid == IID_IDirect3DVolume9;
    case Kind::VertexBuffer: return iid == IID_IDirect3DVertexBuffer9 || iid == IID_IDirect3DResource9;
    case Kind::IndexBuffer: return iid == IID_IDirect3DIndexBuffer9 || iid == IID_IDirect3DResource9;
    case Kind::VertexDeclaration: return iid == IID_IDirect3DVertexDeclaration9;
    case Kind::VertexShader: return iid == IID_IDirect3DVertexShader9;
    case Kind::PixelShader: return iid == IID_IDirect3DPixelShader9;
    case Kind::StateBlock: return iid == IID_IDirect3DStateBlock9;
    case Kind::Query: return iid == IID_IDirect3DQuery9;
    case Kind::SwapChain: return iid == IID_IDirect3DSwapChain9;
    }
    return false;
}

HRESULT query(Node* node, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!supports(node->kind, iid)) return E_NOINTERFACE;
    add_ref(node);
    *out = node->application;
    return S_OK;
}
ULONG add_ref(Node* node) {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    return ++node->refs;
}

void discard_renderer_resources(Device* device) {
    std::vector<IUnknown*> retired;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        retired.swap(device->renderer_resources);
    }
    for (auto it = retired.rbegin(); it != retired.rend(); ++it) (*it)->Release();
}

ULONG release(Node* node) {
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const ULONG remaining = --node->refs;
        if (remaining) return remaining;
        application_nodes.erase(node->application);
        native_nodes.erase(node->identity);
        if (node->kind == Kind::Device) static_cast<Device*>(node)->retiring = true;
    }
    // Parent remains logically alive until backend destruction has completed.
    // A child native Release may internally release its native device.
    Node* parent = node->parent;
    if (node->kind == Kind::Device) {
        auto device = static_cast<Device*>(node);
        retire_auto_depth(device, S_FALSE);
        discard_renderer_resources(device);
    }
    node->backend->Release();
    delete node;
    // Dispatch through the application vtable: capture/observation hooks must
    // see a parent's last release even when its last owner was a child wrapper.
    if (parent) parent->application->Release();
    return 0;
}

Device* device_of(Node* node) {
    return node->kind == Kind::Device ? static_cast<Device*>(node) : static_cast<Device*>(node->parent);
}
HRESULT get_device(Node* node, IDirect3DDevice9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    Device* device = device_of(node);
    add_ref(device);
    *out = static_cast<IDirect3DDevice9*>(device);
    return S_OK;
}
HRESULT get_factory(Device* node, IDirect3D9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    auto factory = static_cast<Factory*>(node->parent);
    add_ref(factory);
    *out = static_cast<IDirect3D9*>(factory);
    return S_OK;
}

template<class T> T* unwrap(Device* owner, T* value) {
    if (!value) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto found = application_nodes.find(static_cast<IUnknown*>(value));
    // Foreign native objects are passed unchanged for the backend to validate;
    // never invoke RTTI or read a guessed wrapper layout from an unknown pointer.
    if (found == application_nodes.end()) return value;
    Node* node = found->second;
    // All supported child interfaces use their canonical primary COM address.
    // Device mismatch remains visible to the backend using the real native input.
    (void)owner;
    return static_cast<T*>(node->backend);
}

Node* allocate_node(Kind kind, IUnknown* native, Node* parent) {
    switch (kind) {
    case Kind::Factory: return new Factory(static_cast<IDirect3D9*>(native), parent);
    case Kind::Device: return new Device(static_cast<IDirect3DDevice9*>(native), parent);
    case Kind::Texture: return new Texture(static_cast<IDirect3DTexture9*>(native), parent);
    case Kind::CubeTexture: return new CubeTexture(static_cast<IDirect3DCubeTexture9*>(native), parent);
    case Kind::VolumeTexture: return new VolumeTexture(static_cast<IDirect3DVolumeTexture9*>(native), parent);
    case Kind::Surface: return new Surface(static_cast<IDirect3DSurface9*>(native), parent);
    case Kind::Volume: return new Volume(static_cast<IDirect3DVolume9*>(native), parent);
    case Kind::VertexBuffer: return new VertexBuffer(static_cast<IDirect3DVertexBuffer9*>(native), parent);
    case Kind::IndexBuffer: return new IndexBuffer(static_cast<IDirect3DIndexBuffer9*>(native), parent);
    case Kind::VertexDeclaration: return new VertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(native), parent);
    case Kind::VertexShader: return new VertexShader(static_cast<IDirect3DVertexShader9*>(native), parent);
    case Kind::PixelShader: return new PixelShader(static_cast<IDirect3DPixelShader9*>(native), parent);
    case Kind::StateBlock: return new StateBlock(static_cast<IDirect3DStateBlock9*>(native), parent);
    case Kind::Query: return new Query(static_cast<IDirect3DQuery9*>(native), parent);
    case Kind::SwapChain: return new SwapChain(static_cast<IDirect3DSwapChain9*>(native), parent);
    }
    return nullptr;
}

// Consumes owned only on success, including when another live wrapper exists.
HRESULT adopt(Node* parent, Kind kind, IUnknown* owned, REFIID iid, void** out,
              const Options* options = nullptr) noexcept {
    if (!out || !owned) return D3DERR_INVALIDCALL;
    *out = nullptr;
    IUnknown* identity = nullptr;
    HRESULT hr = owned->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
    if (FAILED(hr)) return hr;
    identity->Release(); // borrowed key remains alive through owned
    Node* fresh = nullptr;
    Node* existing = nullptr;
    try {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        auto found = native_nodes.find(identity);
        if (found != native_nodes.end()) {
            existing = found->second;
            if (!supports(existing->kind, iid) || existing->parent != parent) return E_NOINTERFACE;
            if (kind == Kind::Factory && options &&
                static_cast<Factory*>(existing)->options.sampleable_auto_depth != options->sampleable_auto_depth)
                return E_INVALIDARG; // Never silently reconfigure a live factory.
            ++existing->refs;
            *out = existing->application;
        } else {
            if (!supports(kind, iid)) return E_NOINTERFACE;
            fresh = allocate_node(kind, owned, parent);
            if (options && kind == Kind::Factory) static_cast<Factory*>(fresh)->options = *options;
            if (options && kind == Kind::Device) static_cast<Device*>(fresh)->options = *options;
            fresh->identity = identity;
            native_nodes.emplace(identity, fresh);
            try { application_nodes.emplace(fresh->application, fresh); }
            catch (...) { native_nodes.erase(identity); throw; }
            if (parent) ++parent->refs;
            *out = fresh->application;
        }
    } catch (const std::bad_alloc&) {
        delete fresh; // constructors/destructors do not own until adoption succeeds
        return E_OUTOFMEMORY;
    } catch (...) {
        delete fresh;
        return E_FAIL;
    }
    if (existing) owned->Release(); // redundant getter reference is consumed
    return S_OK;
}

// Factory/device wrappers deliberately do not advertise Ex or backend-private
// interfaces. Reject Ex creation before exposing the normal wrapper boundary.
bool has_ex(IUnknown* native, REFIID iid) {
    IUnknown* extended = nullptr;
    const HRESULT hr = native->QueryInterface(iid, reinterpret_cast<void**>(&extended));
    if (extended) extended->Release();
    return SUCCEEDED(hr);
}

template<class T> struct Traits;
#define X3M_TRAIT(type, tag) template<> struct Traits<type> { \
    static constexpr Kind kind = Kind::tag; \
    static const GUID& iid() { return IID_##type; } \
};
X3M_TRAIT(IDirect3DTexture9, Texture)
X3M_TRAIT(IDirect3DCubeTexture9, CubeTexture)
X3M_TRAIT(IDirect3DVolumeTexture9, VolumeTexture)
X3M_TRAIT(IDirect3DSurface9, Surface)
X3M_TRAIT(IDirect3DVolume9, Volume)
X3M_TRAIT(IDirect3DVertexBuffer9, VertexBuffer)
X3M_TRAIT(IDirect3DIndexBuffer9, IndexBuffer)
X3M_TRAIT(IDirect3DVertexDeclaration9, VertexDeclaration)
X3M_TRAIT(IDirect3DVertexShader9, VertexShader)
X3M_TRAIT(IDirect3DPixelShader9, PixelShader)
X3M_TRAIT(IDirect3DStateBlock9, StateBlock)
X3M_TRAIT(IDirect3DQuery9, Query)
X3M_TRAIT(IDirect3DSwapChain9, SwapChain)
#undef X3M_TRAIT

template<class T> HRESULT output(Device* owner, HRESULT hr, T* owned, T** out) {
    if (owned == untouched_output<T>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    const HRESULT wrapped = adopt(owner, Traits<T>::kind, owned, Traits<T>::iid(), reinterpret_cast<void**>(out));
    if (FAILED(wrapped)) owned->Release();
    return FAILED(wrapped) ? wrapped : hr;
}
template<> HRESULT output(Device* owner, HRESULT hr, IDirect3DBaseTexture9* owned,
                          IDirect3DBaseTexture9** out) {
    if (owned == untouched_output<IDirect3DBaseTexture9>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    Kind kind;
    switch (owned->GetType()) {
    case D3DRTYPE_TEXTURE: kind = Kind::Texture; break;
    case D3DRTYPE_CUBETEXTURE: kind = Kind::CubeTexture; break;
    case D3DRTYPE_VOLUMETEXTURE: kind = Kind::VolumeTexture; break;
    default: owned->Release(); return E_NOINTERFACE;
    }
    const HRESULT wrapped = adopt(owner, kind, owned, IID_IDirect3DBaseTexture9, reinterpret_cast<void**>(out));
    if (FAILED(wrapped)) owned->Release();
    return FAILED(wrapped) ? wrapped : hr;
}

HRESULT get_container(Node* node, REFIID iid, void** out) {
    IUnknown* owned = untouched_output<IUnknown>();
    HRESULT hr;
    if (node->kind == Kind::Surface)
        hr = static_cast<IDirect3DSurface9*>(node->backend)->GetContainer(iid, out ? reinterpret_cast<void**>(&owned) : nullptr);
    else
        hr = static_cast<IDirect3DVolume9*>(node->backend)->GetContainer(iid, out ? reinterpret_cast<void**>(&owned) : nullptr);
    if (owned == untouched_output<IUnknown>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    Device* device = device_of(node);
    // Classify only known COM interfaces, and always return a canonical wrapper.
    // Unknown requested IIDs never escape as backend interfaces.
    struct Candidate { Kind kind; const IID* iid; };
    const Candidate candidates[] = {
        {Kind::Device, &IID_IDirect3DDevice9}, {Kind::Factory, &IID_IDirect3D9},
        {Kind::Texture, &IID_IDirect3DTexture9}, {Kind::CubeTexture, &IID_IDirect3DCubeTexture9},
        {Kind::VolumeTexture, &IID_IDirect3DVolumeTexture9}, {Kind::SwapChain, &IID_IDirect3DSwapChain9}
    };
    for (const auto& candidate : candidates) {
        if (!supports(candidate.kind, iid)) continue;
        IUnknown* typed = nullptr;
        if (FAILED(owned->QueryInterface(*candidate.iid, reinterpret_cast<void**>(&typed)))) continue;
        Node* parent = candidate.kind == Kind::Factory ? nullptr :
                       candidate.kind == Kind::Device ? device->parent : static_cast<Node*>(device);
        const HRESULT wrapped = adopt(parent, candidate.kind, typed, iid, out);
        if (FAILED(wrapped)) typed->Release();
        owned->Release();
        return FAILED(wrapped) ? wrapped : hr;
    }
    owned->Release();
    return E_NOINTERFACE;
}

HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,
                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    const D3DPRESENT_PARAMETERS requested = pp ? *pp : D3DPRESENT_PARAMETERS{};
    IDirect3DDevice9* owned = untouched_output<IDirect3DDevice9>();
    HRESULT hr = node->native_->CreateDevice(adapter, type, window, flags, pp, out ? &owned : nullptr);
    if (owned == untouched_output<IDirect3DDevice9>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    if (has_ex(owned, IID_IDirect3DDevice9Ex)) { owned->Release(); return E_NOINTERFACE; }
    const HRESULT wrapped = adopt(node, Kind::Device, owned, IID_IDirect3DDevice9, reinterpret_cast<void**>(out), &node->options);
    if (FAILED(wrapped)) owned->Release();
    else initialize_auto_depth(static_cast<Device*>(*out), requested);
    return FAILED(wrapped) ? wrapped : hr;
}

HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp) {
    const D3DPRESENT_PARAMETERS requested = pp ? *pp : D3DPRESENT_PARAMETERS{};
    const bool had_mapping = node->auto_depth.view.available || node->auto_depth.lost_mapping;
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->resetting = true; }
    retire_auto_depth(node, S_FALSE);
    discard_renderer_resources(node);
    const HRESULT hr = node->native_->Reset(pp);
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        node->resetting = false;
        node->lost = FAILED(hr);
        node->auto_depth.lost_mapping = FAILED(hr) && had_mapping;
    }
    if (SUCCEEDED(hr)) initialize_auto_depth(node, requested);
    else node->auto_depth.view.status = hr;
    return hr;
}
HRESULT observe_result(Device* node, HRESULT hr) {
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->lost = true; }
        retire_auto_depth(node, hr);
        discard_renderer_resources(node);
    }
    return hr;
}

bool same_surface(IDirect3DSurface9* left, IDirect3DSurface9* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    IUnknown *a = nullptr, *b = nullptr;
    const HRESULT ha = left->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a));
    const HRESULT hb = right->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b));
    const bool equal = SUCCEEDED(ha) && SUCCEEDED(hb) && a == b;
    if (a) a->Release();
    if (b) b->Release();
    return equal;
}

bool replacement_bound(Device* node, HRESULT* query_status = nullptr) {
    if (query_status) *query_status = S_OK;
    if (!node->auto_depth.replacement || node->lost) return false;
    IDirect3DSurface9* bound = nullptr;
    const HRESULT hr = node->native_->GetDepthStencilSurface(&bound);
    if (query_status) *query_status = hr == D3DERR_NOTFOUND ? S_OK : hr;
    const bool result = SUCCEEDED(hr) && same_surface(bound, node->auto_depth.replacement);
    if (bound) bound->Release();
    return result;
}

IDirect3DSurface9* unwrap_physical_surface(Device* owner, IDirect3DSurface9* value) {
    auto native = unwrap(owner, value);
    if (owner->auto_depth.view.available && same_surface(native, owner->auto_depth.original))
        return owner->auto_depth.replacement;
    return native;
}

void retire_auto_depth(Device* node, HRESULT status) {
    auto& depth = node->auto_depth;
    if (node->lost && depth.view.available) depth.lost_mapping = true;
    // This is only an operational teardown/reset boundary, never a mid-frame
    // fallback. Once loss is known, ordinary D3D9 Get/Set calls are not legal.
    if (!node->lost && replacement_bound(node))
        node->native_->SetDepthStencilSurface(depth.original);
    auto original = depth.original;
    auto replacement = depth.replacement;
    auto texture = depth.view.texture;
    depth.original = nullptr;
    depth.replacement = nullptr;
    depth.view.texture = nullptr;
    depth.view.available = false;
    depth.view.bound = false;
    depth.view.clear_epoch = 0;
    depth.view.status = status;
    if (texture || replacement) ++depth.view.generation;
    // Clear published state before resource destructors can reenter observers.
    if (replacement) replacement->Release();
    if (texture) texture->Release();
    if (original) original->Release();
}

void initialize_auto_depth(Device* node, const D3DPRESENT_PARAMETERS& requested) {
    auto& depth = node->auto_depth;
    depth.lost_mapping = false;
    depth.view.requested = node->options.sampleable_auto_depth;
    depth.view.status = S_FALSE;
    depth.view.logical_desc = {};
    if (!depth.view.requested || !requested.EnableAutoDepthStencil ||
        requested.AutoDepthStencilFormat != D3DFMT_D24X8 ||
        requested.MultiSampleType != D3DMULTISAMPLE_NONE) return;

    IDirect3DSurface9 *original = nullptr, *replacement = nullptr, *color = nullptr;
    IDirect3DTexture9* texture = nullptr;
    D3DSURFACE_DESC description{};
    const D3DFORMAT intz = D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'));
    auto attempt = [&]() -> HRESULT {
        HRESULT hr = node->native_->GetDepthStencilSurface(&original);
        if (FAILED(hr)) return hr;
        if (!original) return E_FAIL;
        hr = original->GetDesc(&description);
        if (FAILED(hr)) return hr;
        depth.view.logical_desc = description;
        if (description.Format != D3DFMT_D24X8 || description.MultiSampleType != D3DMULTISAMPLE_NONE ||
            description.Pool != D3DPOOL_DEFAULT || !(description.Usage & D3DUSAGE_DEPTHSTENCIL)) return S_FALSE;
        // The physical INTZ allocation has stencil bits, unlike logical D24X8.
        // Scoped draw masking requires this getter even on a pure device. If
        // the backend forbids it, decline now rather than changing draw behavior.
        DWORD stencil_enabled = FALSE;
        hr = node->native_->GetRenderState(D3DRS_STENCILENABLE, &stencil_enabled);
        if (FAILED(hr)) return hr;
        // D24X8 has no logical stencil plane. Backends differ on whether a
        // stencil-only clear is accepted as a no-op or rejected. Calibrate on
        // the still-bound original before any application clear/draw; never
        // clear color or depth here. No application depth epoch has begun.
        depth.stencil_clear_result = node->native_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1, 0);
        hr = node->native_->GetRenderTarget(0, &color);
        if (FAILED(hr)) return hr;
        D3DSURFACE_DESC color_desc{};
        hr = color->GetDesc(&color_desc);
        if (FAILED(hr)) return hr;
        if (color_desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
            color_desc.Width != description.Width || color_desc.Height != description.Height) return S_FALSE;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        hr = node->native_->GetCreationParameters(&creation);
        if (FAILED(hr)) return hr;
        auto factory = static_cast<Factory*>(node->parent);
        hr = factory->native_->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode);
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, intz);
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDepthStencilMatch(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, color_desc.Format, intz);
        if (FAILED(hr)) return hr;
        hr = node->native_->CreateTexture(description.Width, description.Height, 1,
            D3DUSAGE_DEPTHSTENCIL, intz, D3DPOOL_DEFAULT, &texture, nullptr);
        if (FAILED(hr)) return hr;
        hr = texture->GetSurfaceLevel(0, &replacement);
        if (FAILED(hr)) return hr;
        return node->native_->SetDepthStencilSurface(replacement);
    };
    const HRESULT hr = attempt();
    depth.view.status = hr;
    if (hr == S_OK) {
        depth.original = original;
        depth.replacement = replacement;
        depth.view.texture = texture;
        depth.view.available = true;
        depth.view.bound = true;
        depth.view.clear_epoch = 0;
        ++depth.view.generation;
    } else {
        // Selection occurs before CreateDevice/Reset returns. The untouched
        // original allocation remains authoritative for this whole generation.
        if (replacement) replacement->Release();
        if (texture) texture->Release();
        if (original) original->Release();
    }
    if (color) color->Release();
}

HRESULT get_depth(Device* node, IDirect3DSurface9** out) {
    // The backend can retain the physical target after loss while our external
    // refs are retired. Ordinary calls after failed Reset are not valid D3D9;
    // never expose that target as a new logical INTZ resource in this interval.
    if (node->lost && node->auto_depth.lost_mapping) {
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        return D3DERR_DEVICELOST;
    }
    IDirect3DSurface9* owned = untouched_output<IDirect3DSurface9>();
    const HRESULT hr = node->native_->GetDepthStencilSurface(out ? &owned : nullptr);
    if (owned == untouched_output<IDirect3DSurface9>()) return hr;
    if (SUCCEEDED(hr) && owned && node->auto_depth.view.available &&
        same_surface(owned, node->auto_depth.replacement)) {
        owned->Release();
        owned = node->auto_depth.original;
        owned->AddRef();
    }
    return output(node, hr, owned, out);
}

HRESULT set_depth(Device* node, IDirect3DSurface9* surface) {
    return node->native_->SetDepthStencilSurface(unwrap_physical_surface(node, surface));
}

template<class Draw> HRESULT draw_device(Device* node, Draw draw) {
    auto& depth = node->auto_depth;
    if (!depth.view.available) return draw();
    HRESULT bound_status;
    const bool bound = replacement_bound(node, &bound_status);
    if (FAILED(bound_status)) { depth.view.status = bound_status; return bound_status; }
    if (!bound) return draw();
    if (FAILED(depth.view.status)) return depth.view.status;
    DWORD stencil = FALSE;
    HRESULT hr = node->native_->GetRenderState(D3DRS_STENCILENABLE, &stencil);
    if (FAILED(hr)) { depth.view.status = hr; return hr; }
    if (!stencil) return draw();
    hr = node->native_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    if (FAILED(hr)) { depth.view.status = hr; return hr; }
    const HRESULT drawn = draw();
    const HRESULT restored = node->native_->SetRenderState(D3DRS_STENCILENABLE, stencil);
    if (FAILED(restored)) depth.view.status = restored;
    // Always restore even if the original draw failed. Never move to the stale
    // original surface if an unexpected guard failure occurs mid-generation.
    return FAILED(drawn) ? drawn : restored;
}

HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil) {
    HRESULT bound_status = S_OK;
    const bool active = node->auto_depth.view.available && replacement_bound(node, &bound_status);
    if (FAILED(bound_status)) { node->auto_depth.view.status = bound_status; return bound_status; }
    // Preserve this backend's measured no-stencil behavior, rather than using
    // INTZ's extra physical stencil plane or assuming D3DERR_INVALIDCALL.
    if (active && (flags & D3DCLEAR_STENCIL)) {
        if (FAILED(node->auto_depth.stencil_clear_result)) return node->auto_depth.stencil_clear_result;
        flags &= ~D3DCLEAR_STENCIL;
        if (!flags) return node->auto_depth.stencil_clear_result;
    }
    const HRESULT hr = node->native_->Clear(count, rects, flags, color, depth, stencil);
    if (SUCCEEDED(hr) && active && (flags & D3DCLEAR_ZBUFFER)) ++node->auto_depth.view.clear_epoch;
    return hr;
}

#include "d3d9_forwarders_inc.h"
} // namespace

HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out, const Options& options) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!owned_native) return E_INVALIDARG;
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      if (application_nodes.count(owned_native)) return E_INVALIDARG; }
    if (has_ex(owned_native, IID_IDirect3D9Ex)) return E_NOINTERFACE;
    return adopt(nullptr, Kind::Factory, owned_native, IID_IDirect3D9, reinterpret_cast<void**>(out), &options);
}

IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9* wrapped) noexcept {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(wrapped);
    return found != application_nodes.end() && found->second->kind == Kind::Device
        ? static_cast<IDirect3DDevice9*>(found->second->backend) : nullptr;
}

HRESULT get_depth_view(IDirect3DDevice9* wrapped, DepthView* out) noexcept {
    if (!out) return E_POINTER;
    *out = {};
    Device* device;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
        device = static_cast<Device*>(found->second);
        *out = device->auto_depth.view;
    }
    // As with the borrowed-device seam, the caller owns a live wrapper reference
    // and serializes rendering/reset. No backend Release runs under registry lock.
    HRESULT bound_status = S_OK;
    out->bound = out->available && replacement_bound(device, &bound_status);
    if (FAILED(bound_status)) out->status = bound_status;
    return S_OK;
}

HRESULT retain_renderer_resource(IDirect3DDevice9* wrapped, IUnknown* owned_resource) noexcept {
    if (!owned_resource) return E_INVALIDARG;
    try {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device ||
            application_nodes.count(owned_resource)) return E_INVALIDARG;
        auto device = static_cast<Device*>(found->second);
        if (device->retiring || device->resetting || device->lost) return D3DERR_INVALIDCALL;
        device->renderer_resources.push_back(owned_resource);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}

} // namespace x3m::ownership
