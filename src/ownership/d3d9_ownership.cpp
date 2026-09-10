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
    if (node->kind == Kind::Device) discard_renderer_resources(static_cast<Device*>(node));
    node->backend->Release();
    delete node;
    if (parent) release(parent);
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
HRESULT adopt(Node* parent, Kind kind, IUnknown* owned, REFIID iid, void** out) noexcept {
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
            ++existing->refs;
            *out = existing->application;
        } else {
            if (!supports(kind, iid)) return E_NOINTERFACE;
            fresh = allocate_node(kind, owned, parent);
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
    IDirect3DDevice9* owned = untouched_output<IDirect3DDevice9>();
    HRESULT hr = node->native_->CreateDevice(adapter, type, window, flags, pp, out ? &owned : nullptr);
    if (owned == untouched_output<IDirect3DDevice9>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    if (has_ex(owned, IID_IDirect3DDevice9Ex)) { owned->Release(); return E_NOINTERFACE; }
    const HRESULT wrapped = adopt(node, Kind::Device, owned, IID_IDirect3DDevice9, reinterpret_cast<void**>(out));
    if (FAILED(wrapped)) owned->Release();
    return FAILED(wrapped) ? wrapped : hr;
}

HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp) {
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->resetting = true; }
    discard_renderer_resources(node);
    const HRESULT hr = node->native_->Reset(pp);
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        node->resetting = false;
        node->lost = FAILED(hr);
    }
    return hr;
}
HRESULT observe_result(Device* node, HRESULT hr) {
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->lost = true; }
        discard_renderer_resources(node);
    }
    return hr;
}

#include "d3d9_forwarders_inc.h"
} // namespace

HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!owned_native) return E_INVALIDARG;
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      if (application_nodes.count(owned_native)) return E_INVALIDARG; }
    if (has_ex(owned_native, IID_IDirect3D9Ex)) return E_NOINTERFACE;
    return adopt(nullptr, Kind::Factory, owned_native, IID_IDirect3D9, reinterpret_cast<void**>(out));
}

IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9* wrapped) noexcept {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(wrapped);
    return found != application_nodes.end() && found->second->kind == Kind::Device
        ? static_cast<IDirect3DDevice9*>(found->second->backend) : nullptr;
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
