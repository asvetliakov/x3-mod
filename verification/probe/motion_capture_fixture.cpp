// Original geometry/shaders, real ownership uploads/leases and diagnostic replay.
// Lifetime evidence below is a link-only synthetic registry, never a game hook.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/proxy/motion_capture.h"
#include "../../src/proxy/capture_state.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace x3m {
void log(const char*, ...) {}
}
namespace {
using namespace x3m;
constexpr UINT W = 32, H = 32;
unsigned checks = 0, samples = 0, states = 0;
void require(bool b, const char* s) {
    ++checks;
    std::printf("CHECK %s %s\n", s, b ? "PASS" : "FAIL");
    if (!b) throw std::runtime_error(s);
}
void api(HRESULT h, const char* s) {
    if (FAILED(h)) {
        std::printf("API FAIL %s %08lx\n", s, h);
        throw std::runtime_error(s);
    }
}
template <class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
};
template <class T> T symbol(HMODULE m, const char* n) {
    auto p = GetProcAddress(m, n);
    T f;
    static_assert(sizeof f == sizeof p);
    std::memcpy(&f, &p, sizeof f);
    if (!f) throw std::runtime_error(n);
    return f;
}
using Assembler = decltype(&D3DXAssembleShader);
std::vector<std::uint32_t> vertex_words, pixel_words;
renderer::RigidPositionProfile vertex_profile{};
renderer::PixelCoverageProfile pixel_profile{};
const renderer::RigidPositionProfile* lookup_vertex(const std::uint32_t* p, std::size_t n) {
    return n == vertex_words.size() && !std::memcmp(p, vertex_words.data(), n * 4) ? &vertex_profile : nullptr;
}
const renderer::PixelCoverageProfile* lookup_pixel(const std::uint32_t* p, std::size_t n) {
    return n == pixel_words.size() && !std::memcmp(p, pixel_words.data(), n * 4) ? &pixel_profile : nullptr;
}
std::uint64_t hash(const std::vector<std::uint32_t>& words) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto w : words)
        for (unsigned i = 0; i < 4; ++i) {
            h ^= (w >> (8 * i)) & 255;
            h *= 1099511628211ull;
        }
    return h;
}
std::vector<std::uint32_t> assemble(Assembler a, const char* s) {
    Com<ID3DXBuffer> code, error;
    auto hr = a(s, UINT(std::strlen(s)), nullptr, nullptr, 0, &code.p, &error.p);
    if (error.p) std::printf("ASM %s\n", static_cast<char*>(error->GetBufferPointer()));
    api(hr, "original assembly");
    auto p = static_cast<std::uint32_t*>(code->GetBufferPointer());
    return {p, p + code->GetBufferSize() / 4};
}
object_lifetime::Snapshot live{true, object_lifetime::Reason::Known, 1, 1, 1, 11, 12, 1};
bool live_enabled = true;
constexpr std::uintptr_t registry = 0x1000, node = 0x2000, camera = 0x3000;
const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
struct Snapshot {
    std::vector<unsigned char> bytes;
    template <class T> void add(const T& x) {
        auto p = reinterpret_cast<const unsigned char*>(&x);
        bytes.insert(bytes.end(), p, p + sizeof x);
    }
    template <class T> void object(T* p) {
        add(p);
        if (p) p->Release();
    }
    explicit Snapshot(IDirect3DDevice9* d) {
        D3DCAPS9 caps{};
        api(d->GetDeviceCaps(&caps), "snapshot caps");
        for (UINT i = 0; i < caps.NumSimultaneousRTs; ++i) {
            IDirect3DSurface9* p = nullptr;
            add(d->GetRenderTarget(i, &p));
            object(p);
        }
        IDirect3DSurface9* ds = nullptr;
        add(d->GetDepthStencilSurface(&ds));
        object(ds);
        D3DVIEWPORT9 vp{};
        RECT r{};
        api(d->GetViewport(&vp), "snapshot viewport");
        api(d->GetScissorRect(&r), "snapshot scissor");
        add(vp);
        add(r);
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        api(d->GetVertexShader(&vs), "snapshot VS");
        api(d->GetPixelShader(&ps), "snapshot PS");
        api(d->GetVertexDeclaration(&decl), "snapshot declaration");
        object(vs);
        object(ps);
        object(decl);
        IDirect3DIndexBuffer9* ib = nullptr;
        api(d->GetIndices(&ib), "snapshot indices");
        object(ib);
        for (UINT i = 0; i < caps.MaxStreams; ++i) {
            IDirect3DVertexBuffer9* p = nullptr;
            UINT offset = 0, stride = 0, freq = 0;
            api(d->GetStreamSource(i, &p, &offset, &stride), "snapshot stream");
            api(d->GetStreamSourceFreq(i, &freq), "snapshot frequency");
            object(p);
            add(offset);
            add(stride);
            add(freq);
        }
        for (UINT i = 0; i < 20; ++i) {
            UINT slot = i < 16 ? i : D3DVERTEXTEXTURESAMPLER0 + i - 16;
            IDirect3DBaseTexture9* p = nullptr;
            api(d->GetTexture(slot, &p), "snapshot texture");
            object(p);
            for (UINT j = 1; j <= 13; ++j) {
                DWORD v = 0;
                add(d->GetSamplerState(slot, D3DSAMPLERSTATETYPE(j), &v));
                add(v);
            }
        }
        for (auto rs : {D3DRS_ZENABLE,
                        D3DRS_ZWRITEENABLE,
                        D3DRS_ZFUNC,
                        D3DRS_STENCILENABLE,
                        D3DRS_ALPHATESTENABLE,
                        D3DRS_ALPHABLENDENABLE,
                        D3DRS_SEPARATEALPHABLENDENABLE,
                        D3DRS_FOGENABLE,
                        D3DRS_SRGBWRITEENABLE,
                        D3DRS_SCISSORTESTENABLE,
                        D3DRS_CLIPPLANEENABLE,
                        D3DRS_CLIPPING,
                        D3DRS_LIGHTING,
                        D3DRS_INDEXEDVERTEXBLENDENABLE,
                        D3DRS_POINTSPRITEENABLE,
                        D3DRS_DITHERENABLE,
                        D3DRS_ANTIALIASEDLINEENABLE,
                        D3DRS_VERTEXBLEND,
                        D3DRS_WRAP0,
                        D3DRS_FILLMODE,
                        D3DRS_CULLMODE,
                        D3DRS_COLORWRITEENABLE,
                        D3DRS_MULTISAMPLEMASK,
                        D3DRS_DEPTHBIAS,
                        D3DRS_SLOPESCALEDEPTHBIAS}) {
            DWORD v = 0;
            api(d->GetRenderState(rs, &v), "snapshot render state");
            add(v);
        }
        float f[256 * 4]{};
        api(d->GetVertexShaderConstantF(0, f, std::min<DWORD>(caps.MaxVertexShaderConst, 256)), "snapshot VS rows");
        add(f);
        std::memset(f, 0, sizeof f);
        api(d->GetPixelShaderConstantF(0, f, 224), "snapshot PS rows");
        add(f);
    }
    void equals(IDirect3DDevice9* d) {
        ++states;
        require(bytes == Snapshot(d).bytes, "pre-Clear replay preserves captured application state");
    }
};
// Independent machine-state oracle. No production CpuState helper is used.
struct CpuImage {
    alignas(16) unsigned char x87[108]{};
    DWORD mxcsr = 0, error = 0;
    void save() {
        error = GetLastError();
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr)::"memory");
    }
    void restore() {
        asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(x87), "m"(mxcsr) : "memory");
        SetLastError(error);
    }
    bool same(const CpuImage& other) const {
        return !std::memcmp(x87, other.x87, sizeof x87) && mxcsr == other.mxcsr && error == other.error;
    }
};
void hostile_cpu() {
    const unsigned short control = 0x0b7f;
    const DWORD csr = 0x5fa1;
    asm volatile("fninit\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(control), "m"(csr) : "memory");
    SetLastError(0x31415926);
}
void perturb_cpu() {
    const DWORD csr = 0x1f80;
    asm volatile("fninit\n\tfldz\n\tldmxcsr %0" ::"m"(csr) : "memory");
    SetLastError(0x27182818);
}
struct PerturbOnExit {
    ~PerturbOnExit() { perturb_cpu(); }
};
struct NativeDrawFault {
    IDirect3DDevice9* device;
    void** previous;
    void* table[119];
    static inline unsigned calls = 0;
    static HRESULT WINAPI fail(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT) {
        ++calls;
        perturb_cpu();
        return E_FAIL;
    }
    explicit NativeDrawFault(IDirect3DDevice9* d)
        : device(d)
        , previous(*reinterpret_cast<void***>(d)) {
        std::copy(previous, previous + 119, table);
        auto replacement = &fail;
        std::memcpy(&table[83], &replacement, sizeof replacement);
        calls = 0;
        *reinterpret_cast<void***>(d) = table;
    }
    ~NativeDrawFault() { *reinterpret_cast<void***>(device) = previous; }
};
struct Consumer {
    IDirect3DDevice9* native = nullptr;
    bool read = true, called = false, okay = false;
    float rgba[4]{};
    static void consume(void* context, const renderer::RigidMotionOutput& out) noexcept {
        PerturbOnExit perturb;
        auto& c = *static_cast<Consumer*>(context);
        c.called = true;
        if (!out.motion || !out.generation) return;
        if (!c.read) {
            c.okay = true;
            return;
        }
        Com<IDirect3DSurface9> surface, copy;
        if (FAILED(out.motion->GetSurfaceLevel(0, &surface.p)) ||
            FAILED(c.native->CreateOffscreenPlainSurface(W, H, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &copy.p,
                                                         nullptr)) ||
            FAILED(c.native->GetRenderTargetData(surface.p, copy.p)))
            return;
        D3DLOCKED_RECT r{};
        if (FAILED(copy->LockRect(&r, nullptr, D3DLOCK_READONLY))) return;
        std::memcpy(c.rgba, static_cast<char*>(r.pBits) + 16 * r.Pitch + 16 * 16, 16);
        c.okay = SUCCEEDED(copy->UnlockRect());
    }
    void expect(float x, float y, float z, float alpha) {
        require(called && okay, "synchronous diagnostic consumer copied pixels without retaining output");
        const float expected[] = {x, y, z, alpha};
        for (UINT i = 0; i < 4; ++i) {
            ++samples;
            bool pass = std::isfinite(rgba[i]) && std::fabs(rgba[i] - expected[i]) < .0001f;
            std::printf("SAMPLE component=%u actual=%.9f expected=%.9f %s\n", i, rgba[i], expected[i],
                        pass ? "PASS" : "FAIL");
            if (!pass) throw std::runtime_error("motion numeric");
        }
    }
};
struct Fixture {
    Com<IDirect3D9> factory;
    Com<IDirect3DDevice9> d;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DVertexBuffer9> vb;
    IDirect3DDevice9* native = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    DrawInputReader reader;
    object_trace::Snapshot scope{};
    MotionCapture capture;
    std::uint64_t frame = 0;
    bool open = false, last_produced = false;
    Fixture(IDirect3D9* raw, HWND window, bool pure) {
        ownership::Options options{};
        options.track_buffer_writes = true;
        options.capture_finite_positions = true;
        options.track_execution_state = true;
        api(ownership::wrap_factory(raw, &factory.p, options), "wrap native factory");
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = W;
        pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_D24X8;
        api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING | (pure ? D3DCREATE_PUREDEVICE : 0), &pp, &d.p),
            "wrapped fixture device");
        native = ownership::borrowed_native_device(d.p);
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(vertex_words.data()), &vs.p), "original VS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(pixel_words.data()), &ps.p), "original PS");
        D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                        D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements, &declaration.p), "original declaration");
        api(d->CreateVertexBuffer(36, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb.p, nullptr), "managed upload VB");
        upload();
        reader.fixture_profiles(lookup_vertex, lookup_pixel);
        scope.valid = object_trace::Node | object_trace::Camera | object_trace::Registry;
        scope.scope_depth = 1;
        scope.node = node;
        scope.camera = camera;
        scope.mesh = 0x4000;
        scope.registry = registry;
        scope.node_handle = 1;
        scope.camera_handle = 2;
        scope.model = 1;
        scope.session = 1;
    }
    ~Fixture() {
        capture.invalidate();
        if (open) d->EndScene();
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetVertexDeclaration(nullptr);
        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
    }
    void upload() {
        void* p = nullptr;
        api(vb->Lock(0, 0, &p, 0), "application full managed Lock");
        const float triangle[] = {-1, 1, .5f, 3, 1, .5f, -1, -3, .5f};
        std::memcpy(p, triangle, sizeof triangle);
        api(vb->Unlock(), "application managed Unlock");
    }
    void state(float translation) {
        api(d->SetVertexShader(vs.p), "scene VS");
        api(d->SetPixelShader(ps.p), "scene PS");
        api(d->SetVertexDeclaration(declaration.p), "scene declaration");
        api(d->SetStreamSource(0, vb.p, 0, 12), "scene stream");
        api(d->SetIndices(nullptr), "scene indices");
        float rows[16];
        std::copy(identity, identity + 16, rows);
        rows[3] = translation;
        api(d->SetVertexShaderConstantF(0, rows, 4), "actual submitted rows");
        for (auto pair : {std::pair<D3DRENDERSTATETYPE, DWORD>{D3DRS_ZENABLE, TRUE},
                          {D3DRS_ZWRITEENABLE, TRUE},
                          {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
                          {D3DRS_CULLMODE, D3DCULL_NONE},
                          {D3DRS_CLIPPING, TRUE},
                          {D3DRS_FILLMODE, D3DFILL_SOLID},
                          {D3DRS_COLORWRITEENABLE, 15},
                          {D3DRS_ALPHABLENDENABLE, FALSE},
                          {D3DRS_ALPHATESTENABLE, FALSE},
                          {D3DRS_STENCILENABLE, FALSE},
                          {D3DRS_SCISSORTESTENABLE, FALSE},
                          {D3DRS_CLIPPLANEENABLE, 0},
                          {D3DRS_DEPTHBIAS, 0},
                          {D3DRS_SLOPESCALEDEPTHBIAS, 0}})
            api(d->SetRenderState(pair.first, pair.second), "original raster state");
    }
    DrawInput start(float translation = 0) {
        capture.begin_frame(d.p, ++frame, true);
        require(capture.geometry_frame().value != 0, "diagnostic geometry frame opened");
        state(translation);
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0), "initial scene clear");
        api(d->BeginScene(), "application BeginScene");
        open = true;
        auto input = reader.read(d.p, {DrawMethod::Primitive, D3DPT_TRIANGLELIST, 1, 0, 0, 0, 0}, &scope);
        require(!input.blockers && input.vertex_finite_verified && input.index_range_verified &&
                    !input.replay_source.qualified(),
                "actual finite input remains distinct from synthetic shader admission");
        input.replay_source = renderer::original_synthetic_sm3_contract();
        input.observation.key.object_lifetime = live.node_serial;
        input.observation.key.camera_lifetime = live.camera_serial;
        input.observation.proofs |= renderer::LifetimeVerified;
        ownership::GeometryLeaseRequest request{};
        request.expected_generation = input.finite_positions.generation;
        request.positions.expected_revision = input.observation.key.vertex_revision;
        request.positions.stride = 12;
        request.positions.position_type = D3DDECLTYPE_FLOAT3;
        request.positions.vertex_count = 3;
        api(ownership::acquire_geometry_lease(capture.geometry_frame(), vb.p, nullptr, request, &input.geometry_lease),
            "exact finite geometry lease");
        require(input.geometry_lease.value != 0, "lease admitted");
        input.lease_status = S_OK;
        auto hr = d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
        api(hr, "original scene draw");
        DrawInputReader::complete(input, hr);
        capture.observe(input, registry, live);
        api(d->EndScene(), "application EndScene");
        open = false;
        return input;
    }
    renderer::Selection selection() {
        renderer::Selection s{};
        s.valid = true;
        s.device = 1;
        s.generation = 1;
        s.frame = frame;
        s.sequence = 10;
        Com<IDirect3DSurface9> color, depth;
        api(d->GetRenderTarget(0, &color.p), "selection color");
        api(d->GetDepthStencilSurface(&depth.p), "selection depth");
        s.color = {true, resource_id(color.p), 0, W, H, D3DFMT_A8R8G8B8, 0};
        s.depth = {true, resource_id(depth.p), 0, W, H, D3DFMT_D24X8, 0};
        return s;
    }
    MotionCaptureDiagnostics boundary(Consumer& consumer) {
        consumer.native = native;
        Snapshot before(native);
        const auto selected = selection();
        CpuImage original, expected, actual;
        original.save();
        hostile_cpu();
        expected.save();
        auto stats = capture.before_clear(d.p, selected, Consumer::consume, &consumer);
        actual.save();
        original.restore();
        require(expected.same(actual), "boundary preserves full x87 MXCSR and LastError after injected work");
        before.equals(native);
        last_produced = stats.produced;
        require(!stats.continuity_known && !stats.color_coverage_known,
                "private motion is not temporal-color eligible");
        return stats;
    }
    void finish(bool clear = true, HRESULT present = S_OK, bool selected = true) {
        api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "destructive application Clear after consumer");
        capture.after_clear(clear);
        const bool committed = capture.end_frame(selected, present);
        require(!capture.geometry_frame().value &&
                    committed == (last_produced && clear && selected && SUCCEEDED(present)),
                "frame geometry released and commit agrees with confirmation");
    }
};
void cases(Fixture& f) {
    auto in = f.start();
    const auto old_frame = f.capture.geometry_frame();
    Consumer first;
    auto stat = f.boundary(first);
    require(stat.produced && stat.matched == 0, "first committed diagnostic has no correspondence");
    first.expect(0, 0, 0, -1);
    ownership::GeometryLeaseView expired{};
    require(FAILED(ownership::inspect_geometry_lease(old_frame, in.geometry_lease, &expired)),
            "boundary retires native geometry leases");
    f.finish();
    f.start(.25f);
    Consumer moved;
    stat = f.boundary(moved);
    require(stat.produced && stat.matched == 1 && stat.completed == 1, "adjacent original draw yields paired motion");
    moved.expect(.390625f, .515625f, .5f, 1);
    f.finish();
    in = f.start(.25f);
    auto conflict = in;
    conflict.observation.submitted_wvp[3] = .5f;
    f.capture.observe(conflict, registry, live);
    Consumer ambiguous;
    stat = f.boundary(ambiguous);
    require(stat.produced && stat.matched == 0, "late conflicting duplicate cannot publish earlier motion");
    ambiguous.expect(0, 0, 0, -1);
    f.finish();
    f.start();
    f.upload();
    Consumer changed;
    stat = f.boundary(changed);
    require(!stat.produced && !changed.called && stat.rejected == 1, "changed VB before boundary revokes replay lease");
    f.finish();
    f.start();
    Consumer recovered;
    stat = f.boundary(recovered);
    require(stat.produced && stat.matched == 0, "content failure invalidates correspondence");
    recovered.expect(0, 0, 0, -1);
    f.finish(false);
    f.start();
    Consumer failed_clear;
    stat = f.boundary(failed_clear);
    require(stat.produced && stat.matched == 0, "failed selection Clear prevents previous-frame commit");
    f.finish(true, E_FAIL);
    f.start();
    Consumer failed_present;
    stat = f.boundary(failed_present);
    require(stat.produced && stat.matched == 0, "failed Present prevents previous-frame commit");
    f.finish();
    f.start();
    Com<IDirect3DQuery9> query;
    api(f.d->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query.p), "query scope");
    api(query->Issue(D3DISSUE_BEGIN), "query begin");
    Consumer active;
    stat = f.boundary(active);
    require(!stat.produced && !active.called, "active application query refuses diagnostic");
    api(query->Issue(D3DISSUE_END), "query end");
    f.finish();
    query.reset();
    f.start();
    api(f.d->BeginStateBlock(), "recording begin");
    Consumer recording;
    stat = f.boundary(recording);
    require(!stat.produced && !recording.called, "stateblock recording refuses diagnostic");
    Com<IDirect3DStateBlock9> block;
    api(f.d->EndStateBlock(&block.p), "recording end");
    block.reset();
    f.finish();
    f.start();
    ++live.mutation_revision;
    Consumer lifetime;
    stat = f.boundary(lifetime);
    require(!stat.produced && !lifetime.called, "registry mutation before boundary refuses diagnostic");
    f.finish();
    f.start();
    api(f.d->BeginScene(), "open boundary scene");
    f.open = true;
    Consumer open;
    open.read = false;
    stat = f.boundary(open);
    require(stat.produced && open.called && open.okay, "diagnostic preserves already-open caller scene");
    api(f.d->EndScene(), "close boundary scene");
    f.open = false;
    f.finish();
    f.start();
    f.capture.invalidate();
    api(f.d->SetStreamSource(0, nullptr, 0, 0), "unbind Reset stream");
    api(f.d->Reset(&f.pp), "actual native Reset after collection cancellation");
    f.upload();
    f.start();
    Consumer reset;
    stat = f.boundary(reset);
    require(stat.produced && stat.matched == 0, "Reset invalidates private correspondence and uploads renewed");
    reset.expect(0, 0, 0, -1);
    f.finish();
    f.start();
    f.capture.invalidate();
    require(!f.capture.geometry_frame().value, "teardown cancels pending collection");
    f.start();
    Consumer faulted;
    {
        NativeDrawFault fault(f.native);
        stat = f.boundary(faulted);
        require(stat.operation == E_FAIL && !stat.produced && !faulted.called && NativeDrawFault::calls == 1,
                "native replay failure publishes no diagnostic");
    }
    ownership::ExecutionView execution{};
    api(ownership::get_execution_view(f.d.p, &execution), "execution after injected replay failure");
    require(!execution.known && !execution.queries_idle && execution.reason == ownership::ExecutionReason::NativeBypass,
            "uncertain injected native pass permanently revokes execution proof");
    f.finish();
    f.start();
    Consumer after_fault;
    stat = f.boundary(after_fault);
    require(!stat.produced && !after_fault.called, "next frame refuses after uncertain native execution");
    f.finish();
}
}
namespace x3m::object_lifetime {
bool current(std::uintptr_t r, std::uintptr_t n, std::uint32_t nh, std::uintptr_t c, std::uint32_t ch, Snapshot* out) {
    if (!out) return false;
    *out = {};
    if (!live_enabled || r != registry || n != node || nh != 1 || c != camera || ch != 2) return false;
    *out = live;
    return live.known;
}
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int result = 1;
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "X3MotionCaptureFixture";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "Private motion capture fixture", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, wc.hInstance, nullptr);
    try {
        if (argc != 2 || !window) throw std::runtime_error("expected native D3DX path");
        HMODULE runtime = LoadLibraryA("d3d9.dll"), compiler = LoadLibraryA(argv[1]);
        require(runtime && compiler, "native libraries");
        auto assembler = symbol<Assembler>(compiler, "D3DXAssembleShader");
        vertex_words = assemble(
            assembler,
            "vs_3_0\ndef c20, 1, 0, 0, 0\ndcl_position v0\ndcl_position o0\nmad r3, v0.xyzx, c20.xxxy, c20.yyyx\ndp4 o0.x, r3, c0\ndp4 o0.y, r3, c1\ndp4 o0.z, r3, c2\ndp4 o0.w, r3, c3\n");
        pixel_words = assemble(assembler, "ps_3_0\ndef c0, 1, 0.5, 0.25, 1\nmov oC0, c0\n");
        vertex_profile = {hash(vertex_words), std::uint32_t(vertex_words.size()), 0, true};
        pixel_profile = {hash(pixel_words), std::uint32_t(pixel_words.size())};
        auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime, "Direct3DCreate9");
        for (bool pure : {false, true}) {
            std::printf("DEVICE pure=%u\n", pure);
            Fixture fixture(create(D3D_SDK_VERSION), window, pure);
            cases(fixture);
        }
        FreeLibrary(compiler);
        FreeLibrary(runtime);
        std::printf("RESULT PASS checks=%u samples=%u state_comparisons=%u devices=2\n", checks, samples, states);
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s\n", e.what());
    }
    if (window) DestroyWindow(window);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return result;
}
