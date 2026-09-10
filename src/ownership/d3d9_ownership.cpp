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
struct CopyDepth {
    IDirect3DSurface9* original = nullptr; // Native ref only; never substituted.
    CopyDepthView view;
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
HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil);
void initialize_copy_depth(Device* node, const D3DPRESENT_PARAMETERS& requested);
void retire_copy_depth(Device* node, HRESULT status);
bool copy_source_bound(Device* node, HRESULT* query_status = nullptr);
HRESULT begin_state_block(Device* node);
HRESULT end_state_block(Device* node, IDirect3DStateBlock9** out);
Device* device_of(Node* node);
template<class T> T* unwrap(Device* owner, T* value);
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
        retire_copy_depth(device, S_FALSE);
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
                static_cast<Factory*>(existing)->options.capture_auto_depth != options->capture_auto_depth)
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
    else {
        auto device = static_cast<Device*>(*out);
        initialize_copy_depth(device, requested);
    }
    return FAILED(wrapped) ? wrapped : hr;
}

HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp) {
    const D3DPRESENT_PARAMETERS requested = pp ? *pp : D3DPRESENT_PARAMETERS{};
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->resetting = true; }
    retire_copy_depth(node, S_FALSE);
    discard_renderer_resources(node);
    const HRESULT hr = node->native_->Reset(pp);
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        node->resetting = false;
        node->lost = FAILED(hr);
    }
    if (SUCCEEDED(hr)) {
        node->recording_state_block = false;
        initialize_copy_depth(node, requested);
    } else {
        node->copy_depth.view.status = hr;
    }
    return hr;
}
HRESULT observe_result(Device* node, HRESULT hr) {
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->lost = true; }
        retire_copy_depth(node, hr);
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

HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil) {
    HRESULT source_status = S_OK;
    const bool original_bound = node->copy_depth.view.available &&
        (flags & D3DCLEAR_ZBUFFER) && copy_source_bound(node, &source_status);
    // Snapshot bookkeeping must not change an application's Clear result.
    if (FAILED(source_status)) node->copy_depth.view.status = source_status;
    const HRESULT hr = node->native_->Clear(count, rects, flags, color, depth, stencil);
    if (SUCCEEDED(hr) && original_bound) ++node->copy_depth.view.source_epoch;
    // Preserve the app's own HRESULT, but a loss observed by our bookkeeping
    // query must still invalidate native snapshots and borrowed resource views.
    if (FAILED(source_status)) observe_result(node, source_status);
    observe_result(node, hr);
    return hr;
}

HRESULT begin_state_block(Device* node) {
    const HRESULT hr = node->native_->BeginStateBlock();
    if (SUCCEEDED(hr)) node->recording_state_block = true;
    return observe_result(node, hr);
}

HRESULT end_state_block(Device* node, IDirect3DStateBlock9** out) {
    IDirect3DStateBlock9* owned = untouched_output<IDirect3DStateBlock9>();
    const HRESULT hr = node->native_->EndStateBlock(out ? &owned : nullptr);
    if (SUCCEEDED(hr)) node->recording_state_block = false;
    const HRESULT result = output(node, hr, owned, out);
    // Clean up/adopt the backend output before retiring resources on loss.
    observe_result(node, hr);
    return result;
}

bool copy_source_bound(Device* node, HRESULT* query_status) {
    if (query_status) *query_status = S_OK;
    if (!node->copy_depth.original || node->lost) return false;
    IDirect3DSurface9* actual = nullptr;
    const HRESULT hr = node->native_->GetDepthStencilSurface(&actual);
    if (query_status) *query_status = hr == D3DERR_NOTFOUND ? S_OK : hr;
    const bool bound = SUCCEEDED(hr) && same_surface(actual, node->copy_depth.original);
    if (actual) actual->Release();
    return bound;
}

void retire_copy_depth(Device* node, HRESULT status) {
    auto& copy = node->copy_depth;
    auto original = copy.original;
    auto texture = copy.view.texture;
    copy.original = nullptr;
    copy.view.texture = nullptr;
    copy.view.available = false;
    copy.view.copy_valid = false;
    copy.view.source_bound = false;
    copy.view.source_epoch = 0;
    copy.view.copy_epoch = 0;
    copy.view.status = status;
    if (texture) ++copy.view.generation;
    if (texture) texture->Release();
    if (original) original->Release();
}

void initialize_copy_depth(Device* node, const D3DPRESENT_PARAMETERS& requested) {
    auto& copy = node->copy_depth;
    copy.view.requested = node->options.capture_auto_depth;
    copy.view.status = S_FALSE;
    copy.view.source_desc = {};
    if (!copy.view.requested || !requested.EnableAutoDepthStencil ||
        requested.AutoDepthStencilFormat != D3DFMT_D24X8 ||
        requested.MultiSampleType != D3DMULTISAMPLE_NONE) return;
    IDirect3DSurface9* original = nullptr;
    IDirect3DTexture9* texture = nullptr;
    IDirect3DBaseTexture9* sampled = nullptr;
    auto attempt = [&]() -> HRESULT {
        HRESULT hr = node->native_->GetDepthStencilSurface(&original);
        if (FAILED(hr)) return hr;
        if (!original) return E_FAIL;
        D3DSURFACE_DESC desc{};
        hr = original->GetDesc(&desc);
        if (FAILED(hr)) return hr;
        copy.view.source_desc = desc;
        if (desc.Format != D3DFMT_D24X8 || desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
            desc.Pool != D3DPOOL_DEFAULT || !(desc.Usage & D3DUSAGE_DEPTHSTENCIL)) return S_FALSE;
        // The exact save/restore calls are required on the real pure device;
        // unsupported getters decline capture before application rendering.
        hr = node->native_->GetTexture(0, &sampled);
        if (FAILED(hr)) return hr;
        DWORD point_size = 0;
        hr = node->native_->GetRenderState(D3DRS_POINTSIZE, &point_size);
        if (FAILED(hr)) return hr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        hr = node->native_->GetCreationParameters(&creation);
        if (FAILED(hr)) return hr;
        auto factory = static_cast<Factory*>(node->parent);
        hr = factory->native_->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode);
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
            D3DFORMAT(MAKEFOURCC('R', 'E', 'S', 'Z')));
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24X8);
        if (FAILED(hr)) return hr;
        return node->native_->CreateTexture(desc.Width, desc.Height, 1,
            D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8, D3DPOOL_DEFAULT, &texture, nullptr);
    };
    const HRESULT hr = attempt();
    copy.view.status = hr;
    if (hr == S_OK) {
        copy.original = original;
        copy.view.texture = texture;
        copy.view.available = true;
        copy.view.copy_valid = false;
        copy.view.source_bound = true;
        copy.view.source_epoch = 0;
        copy.view.copy_epoch = 0;
        ++copy.view.generation;
    } else {
        if (texture) texture->Release();
        if (original) original->Release();
    }
    if (sampled) sampled->Release();
    // Optional setup can observe device loss too. Retire only after temporary
    // native references are released, and reject later renderer adoption.
    observe_result(node, hr);
}

HRESULT copy_depth(Device* node) {
    auto& copy = node->copy_depth;
    auto reject = [&](HRESULT hr) {
        copy.view.status = hr;
        // Call only after any saved native references have been released.
        // A pre-mutation loss invalidates an earlier successful snapshot too.
        return observe_result(node, hr);
    };
    if (node->lost) return reject(copy.view.status == D3DERR_DEVICENOTRESET
        ? D3DERR_DEVICENOTRESET : D3DERR_DEVICELOST);
    if (node->resetting || node->retiring || node->recording_state_block ||
        !copy.view.available) return reject(D3DERR_INVALIDCALL);
    HRESULT bound_status = S_OK;
    const bool bound = copy_source_bound(node, &bound_status);
    if (FAILED(bound_status)) return reject(bound_status);
    if (!bound) return reject(D3DERR_INVALIDCALL);
    IDirect3DBaseTexture9* texture0 = nullptr;
    DWORD point_size = 0;
    HRESULT hr = node->native_->GetTexture(0, &texture0);
    if (FAILED(hr)) { if (texture0) texture0->Release(); return reject(hr); }
    hr = node->native_->GetRenderState(D3DRS_POINTSIZE, &point_size);
    if (FAILED(hr)) { if (texture0) texture0->Release(); return reject(hr); }

    // Verified on Preview with native D24X8 source/destination and no dummy
    // draw. Do not open/close a scene or change the original depth binding.
    copy.view.copy_valid = false;
    hr = node->native_->SetTexture(0, copy.view.texture);
    auto loss = [](HRESULT value) {
        return value == D3DERR_DEVICELOST || value == D3DERR_DEVICENOTRESET;
    };
    if (loss(hr)) {
        if (texture0) texture0->Release();
        return reject(hr); // No ordinary Set calls after observed loss.
    }
    HRESULT point_restored = S_OK;
    if (SUCCEEDED(hr)) {
        hr = node->native_->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000u);
        if (loss(hr)) {
            if (texture0) texture0->Release();
            return reject(hr);
        }
        point_restored = node->native_->SetRenderState(D3DRS_POINTSIZE, point_size);
        if (loss(point_restored)) {
            if (texture0) texture0->Release();
            return reject(point_restored);
        }
    }
    const HRESULT texture_restored = node->native_->SetTexture(0, texture0);
    if (texture0) texture0->Release();
    // A restoration loss takes precedence over an earlier ordinary failure:
    // otherwise its dead-device state could leave a borrowed snapshot exposed.
    if (loss(texture_restored)) return reject(texture_restored);
    if (FAILED(hr)) return reject(hr);
    if (FAILED(point_restored)) return reject(point_restored);
    if (FAILED(texture_restored)) return reject(texture_restored);
    copy.view.copy_valid = true;
    copy.view.copy_epoch = copy.view.source_epoch;
    copy.view.status = S_OK;
    return S_OK;
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

HRESULT get_copy_depth_view(IDirect3DDevice9* wrapped, CopyDepthView* out) noexcept {
    if (!out) return E_POINTER;
    *out = {};
    Device* device;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
        device = static_cast<Device*>(found->second);
        *out = device->copy_depth.view;
    }
    HRESULT bound_status = S_OK;
    out->source_bound = out->available && copy_source_bound(device, &bound_status);
    if (FAILED(bound_status)) {
        if (bound_status == D3DERR_DEVICELOST || bound_status == D3DERR_DEVICENOTRESET) {
            observe_result(device, bound_status);
            *out = device->copy_depth.view;
        } else out->status = bound_status;
    }
    return S_OK;
}

HRESULT copy_auto_depth(IDirect3DDevice9* wrapped) noexcept {
    Device* device;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
        device = static_cast<Device*>(found->second);
    }
    return copy_depth(device);
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
