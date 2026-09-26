// Original native-resource lifetime witnesses; no game or synthetic source contract.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/rigid_motion.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
using namespace x3m::renderer;
static unsigned checks = 0, cases = 0, callbacks = 0;
void require(bool okay, const char* name) {
    ++checks;
    std::printf("CHECK %s %s\n", name, okay ? "PASS" : "FAIL");
    if (!okay) throw std::runtime_error(name);
}
void api(HRESULT h) {
    if (FAILED(h)) {
        std::printf("API %08lx\n", h);
        throw std::runtime_error("native API");
    }
}
template <class T> void drop(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
const GUID marker_guid = {0x17aa929f, 0x3718, 0x44d1, {0x99, 0x35, 0x41, 0x20, 0x98, 0x07, 0x63, 0x12}};
struct Context {
    RigidMotionPass* pass = nullptr;
    RigidMotionInputs input{};
    RigidMotionRetirement* batch = nullptr;
    unsigned destroyed[7]{};
    bool callback_phase = false, callback_ok = true;
    void callback(unsigned index) noexcept {
        ++destroyed[index];
        ++callbacks;
        if (!callback_phase) return;
        RigidMotionOutput out;
        out.motion = reinterpret_cast<IDirect3DTexture9*>(1);
        callback_ok &= !batch->empty();
        batch->release(); // Must not recursively release any detached reference.
        callback_ok &= pass->run(input, &out, batch) == E_INVALIDARG && !out.motion && !batch->empty();
    }
    unsigned total() const {
        unsigned n = 0;
        for (auto v : destroyed) n += v;
        return n;
    }
};
struct Marker final : IUnknown {
    ULONG refs = 1;
    Context* context;
    unsigned index;
    Marker(Context* c, unsigned i)
        : context(c)
        , index(i) {}
    HRESULT WINAPI QueryInterface(REFIID, void** p) override {
        if (!p) return E_POINTER;
        *p = nullptr;
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() override { return ++refs; }
    ULONG WINAPI Release() override {
        auto n = --refs;
        if (!n) {
            context->callback(index);
            delete this;
        }
        return n;
    }
};
void mark(IDirect3DResource9* resource, Context& c, unsigned i) {
    auto* m = new Marker(&c, i);
    HRESULT h = resource->SetPrivateData(marker_guid, m, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    m->Release();
    api(h);
}
struct Cohort {
    IDirect3DDevice9* device;
    IDirect3DSurface9 *targets[4]{}, *depth = nullptr, *back = nullptr;
    IDirect3DTexture9 *motion = nullptr, *sampler = nullptr;
    Context context;
    Cohort(IDirect3DDevice9* d, RigidMotionPass& pass, RigidMotionRetirement* batch)
        : device(d) {
        context.pass = &pass;
        context.batch = batch;
        api(d->GetRenderTarget(0, &back));
        for (unsigned i = 0; i < 4; ++i) {
            api(d->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &targets[i], nullptr));
            mark(targets[i], context, i);
            api(d->SetRenderTarget(i, targets[i]));
        }
        api(d->CreateDepthStencilSurface(16, 16, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth, nullptr));
        mark(depth, context, 4);
        api(d->SetDepthStencilSurface(depth));
        api(d->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &motion,
                             nullptr));
        IDirect3DSurface9* surface = nullptr;
        api(motion->GetSurfaceLevel(0, &surface));
        mark(surface, context, 5);
        drop(surface);
        api(d->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &sampler, nullptr));
        mark(sampler, context, 6);
        api(d->SetTexture(0, sampler));
        auto& in = context.input;
        in.motion = motion;
        in.scene_depth = depth;
        in.width = in.height = 16;
        in.scene_depth_current = true;
        in.caller_scene_open = false;
        in.caller_queries_idle = true;
    }
    void unbind_release() {
        api(device->SetTexture(0, nullptr));
        api(device->SetDepthStencilSurface(nullptr));
        for (unsigned i = 1; i < 4; ++i) api(device->SetRenderTarget(i, nullptr));
        api(device->SetRenderTarget(0, back));
        for (auto& t : targets) {
            drop(t);
        }
        drop(depth);
        drop(motion);
        drop(sampler);
    }
    ~Cohort() {
        for (auto& t : targets) {
            drop(t);
        }
        drop(depth);
        drop(motion);
        drop(sampler);
        drop(back);
    }
};
struct Spy {
    enum Mode { Reenter, Partial, Restore, Loss };
    using Depth = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DSurface9**);
    using Texture = HRESULT(WINAPI*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
    using Draw = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    static inline Spy* active = nullptr;
    IDirect3DDevice9* d;
    void** previous;
    void* table[119];
    Depth getDepth;
    Texture texture;
    Draw draw;
    Mode mode;
    Context* context;
    bool fired = false, drawn = false, reentry_ok = true;
    static HRESULT WINAPI depth_call(IDirect3DDevice9* d, IDirect3DSurface9** p) {
        auto& s = *active;
        if (!s.fired) {
            s.fired = true;
            if (s.mode == Partial) {
                *p = nullptr;
                return E_FAIL;
            }
            if (s.mode == Reenter) {
                RigidMotionOutput out;
                s.reentry_ok = !s.context->batch->empty();
                s.context->batch->release();
                s.reentry_ok &= s.context->pass->run(s.context->input, &out, s.context->batch) == E_INVALIDARG &&
                                !out.motion && !s.context->batch->empty();
            }
        }
        return s.getDepth(d, p);
    }
    static HRESULT WINAPI texture_call(IDirect3DDevice9* d, DWORD stage, IDirect3DBaseTexture9* p) {
        auto& s = *active;
        if (s.mode == Restore && s.drawn && !s.fired) {
            s.fired = true;
            return E_FAIL;
        }
        return s.texture(d, stage, p);
    }
    static HRESULT WINAPI draw_call(IDirect3DDevice9* d, D3DPRIMITIVETYPE p, UINT n, const void* data, UINT stride) {
        auto& s = *active;
        s.drawn = true;
        if (s.mode == Loss) {
            s.fired = true;
            return D3DERR_DEVICELOST;
        }
        return s.draw(d, p, n, data, stride);
    }
    Spy(IDirect3DDevice9* device, Mode m, Context& c)
        : d(device)
        , previous(*reinterpret_cast<void***>(d))
        , mode(m)
        , context(&c) {
        std::copy(previous, previous + 119, table);
        std::memcpy(&getDepth, &table[40], sizeof getDepth);
        std::memcpy(&texture, &table[65], sizeof texture);
        std::memcpy(&draw, &table[83], sizeof draw);
        auto a = &depth_call;
        auto b = &texture_call;
        auto e = &draw_call;
        // Restore injection arms only after the initializer; depth spy only in capture modes.
        if (m == Reenter || m == Partial) std::memcpy(&table[40], &a, sizeof a);
        std::memcpy(&table[65], &b, sizeof b);
        std::memcpy(&table[83], &e, sizeof e);
        active = this;
        *reinterpret_cast<void***>(d) = table;
    }
    ~Spy() {
        *reinterpret_cast<void***>(d) = previous;
        active = nullptr;
    }
};
void run_case(IDirect3DDevice9* d, RigidMotionPass& pass, RigidMotionRetirement& batch, Spy::Mode mode) {
    ++cases;
    require(batch.empty(), "batch initially reusable");
    Cohort c(d, pass, &batch);
    RigidMotionOutput out;
    HRESULT hr;
    bool fired, reentry;
    {
        Spy spy(d, mode, c.context);
        hr = pass.run(c.context.input, &out, &batch);
        fired = spy.fired;
        reentry = spy.reentry_ok;
    }
    require(fired, "native injection reached");
    require(reentry, "same batch reentry refused during native capture");
    require(mode == Spy::Reenter ? hr == S_OK && out.motion == c.motion : FAILED(hr) && !out.motion,
            "operation result and output contract");
    require(!batch.empty() && c.context.total() == 0, "all acquired witnesses retained at run return");
    RigidMotionOutput refused;
    require(pass.run(c.context.input, &refused, &batch) == E_INVALIDARG && !refused.motion,
            "nonempty batch rejected without clearing");
    if (mode == Spy::Loss) api(d->EndScene()); // Synthetic failure left real BeginScene open; repair outside scope.
    c.unbind_release();
    const unsigned early = mode == Spy::Partial ? 1 : 0;
    require(c.context.total() == early, "only uncaptured depth may retire before batch release");
    if (early) require(c.context.destroyed[4] == 1, "partial capture did not acquire failed depth output");
    c.context.callback_phase = true;
    batch.release();
    c.context.callback_phase = false;
    require(c.context.total() == 7 && c.context.callback_ok && batch.empty(),
            "explicit release retires seven witnesses and refuses callback reentry");
    for (auto n : c.context.destroyed) require(n == 1, "each private IUnknown witness destroyed exactly once");
    batch.release();
    require(c.context.total() == 7 && batch.empty(), "release on empty batch is idempotent");
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int result = 1;
    static_assert(!std::is_copy_constructible_v<RigidMotionRetirement> &&
                  !std::is_move_constructible_v<RigidMotionRetirement>);
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3RigidRetirement";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "Original retirement fixture", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, cls.hInstance, nullptr);
    HMODULE module = LoadLibraryA("d3d9.dll");
    IDirect3D9* factory = nullptr;
    try {
        if (!window || !module) throw std::runtime_error("initialization");
        using Create = IDirect3D9*(WINAPI*)(UINT);
        Create create;
        auto proc = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &proc, sizeof create);
        factory = create(D3D_SDK_VERSION);
        if (!factory) throw std::runtime_error("factory");
        for (unsigned pure = 0; pure < 2; ++pure) {
            std::printf("DEVICE pure=%u\n", pure);
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE;
            pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow = window;
            pp.BackBufferWidth = pp.BackBufferHeight = 16;
            pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            IDirect3DDevice9* d = nullptr;
            api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING | (pure ? D3DCREATE_PUREDEVICE : 0), &pp,
                                      &d));
            {
                RigidMotionPass pass;
                api(pass.initialize(d));
                RigidMotionRetirement batch;
                for (auto mode : {Spy::Reenter, Spy::Partial, Spy::Restore, Spy::Loss, Spy::Reenter})
                    run_case(d, pass, batch, mode);
                // Destructor path: cohort outlives batch so callback context stays valid.
                {
                    Cohort c(d, pass, nullptr);
                    {
                        RigidMotionRetirement scoped;
                        c.context.batch = &scoped;
                        RigidMotionOutput out;
                        api(pass.run(c.context.input, &out, &scoped));
                        c.unbind_release();
                        require(c.context.total() == 0, "destructor batch retains resources");
                        c.context.callback_phase = true;
                    }
                    c.context.callback_phase = false;
                    require(c.context.total() == 7 && c.context.callback_ok, "destructor retires outside caller scope");
                }
                pass.before_reset();
                api(d->Reset(&pp));
                std::puts("RESET PASS");
            }
            require(d->Release() == 0, "native device final release");
        }
        std::printf("RESULT PASS checks=%u cases=%u callbacks=%u devices=2\n", checks, cases, callbacks);
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s\n", e.what());
    }
    drop(factory);
    if (module) FreeLibrary(module);
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return result;
}
