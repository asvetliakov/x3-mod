// Proxy-loaded fixture for the step B locked-prefix bound
// (docs/architecture/screen-emission-region.md). Runs against the built
// d3d9.dll placed beside it (WINEDLLOVERRIDES d3d9=n,b) with
// X3M_OWNERSHIP=1 X3M_MOTION_OUTPUT=1 X3M_SCREEN_EMISSION_BOUND=1: the
// game's bullet vertex shader (vs_5e484a06672e28fb) and pixel shader draw
// non-indexed quads from a DISCARD-locked dynamic buffer with the writer's
// layout, one draw per frame, through a scripted sequence (first draw,
// steady state, near-plane straddling / exact / behind / long beam, NaN
// tail, nested lock, instanced stream, Reset). The
// proxy's capture log carries the locked_prefix / locked_prefix_frame lines
// the runner checks. No X3, no game launch.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned W = 96, H = 96, stride = 24, max_vertices = 6144, bytes = max_vertices * stride;
template <class T> T symbol(HMODULE m, const char* name) {
    auto raw = GetProcAddress(m, name); T fn = nullptr; std::memcpy(&fn, &raw, sizeof fn);
    if (!fn) throw std::runtime_error(name);
    return fn;
}
std::vector<DWORD> load(const char* name) {
    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("shader missing");
    const auto size = f.tellg();
    if (size <= 0 || size % 4 || size > 65536) throw std::runtime_error("shader size");
    std::vector<DWORD> words(std::size_t(size) / 4);
    f.seekg(0); f.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}
void api(HRESULT hr, const char* what) { if (FAILED(hr)) { std::printf("FAIL %s hr=%08lx\n", what, hr); throw std::runtime_error(what); } }
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct Fixture {
    IDirect3DDevice9* d = nullptr;
    IDirect3DVertexBuffer9* bullets = nullptr;
    IDirect3DVertexBuffer9* instance = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    unsigned frame = 0;
    void create_buffers() {
        api(d->CreateVertexBuffer(bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &bullets, nullptr), "bullet buffer");
        api(d->CreateVertexBuffer(64, 0, 0, D3DPOOL_MANAGED, &instance, nullptr), "instance buffer");
        void* data = nullptr;
        api(instance->Lock(0, 0, &data, 0), "instance lock"); std::memset(data, 0, 64); api(instance->Unlock(), "instance unlock");
    }
    // The writer: whole-buffer DISCARD lock, stale tail first, then N quads.
    void write(unsigned quads, float tail, bool nested = false) {
        void* data = nullptr;
        api(bullets->Lock(0, bytes, &data, D3DLOCK_DISCARD), "discard lock");
        void* inner = nullptr;
        if (nested) api(bullets->Lock(0, bytes, &inner, D3DLOCK_DISCARD), "nested lock");
        auto* words = static_cast<float*>(data);
        for (unsigned n = 0; n < bytes / 4; ++n) words[n] = tail;
        unsigned seed = 0x2545f491u + quads;
        auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
        for (unsigned q = 0; q < quads; ++q) {
            const float c[3] = {(2 * next() - 1) * .4f, (2 * next() - 1) * .4f, (2 * next() - 1) * .2f}, s = .08f;
            const float corners[6][2] = {{c[0] - s, c[1] - s}, {c[0] + s, c[1] - s}, {c[0] - s, c[1] + s}, {c[0] + s, c[1] - s}, {c[0] + s, c[1] + s}, {c[0] - s, c[1] + s}};
            for (unsigned k = 0; k < 6; ++k) {
                auto* v = reinterpret_cast<unsigned char*>(data) + (q * 6 + k) * stride;
                const float position[3] = {corners[k][0], corners[k][1], c[2]}, uv[2] = {float(k & 1), float(k >> 1)};
                const DWORD colour = 0xff000000u;
                std::memcpy(v, position, 12); std::memcpy(v + 12, uv, 8); std::memcpy(v + 20, &colour, 4);
            }
        }
        if (nested) api(bullets->Unlock(), "nested unlock");
        api(bullets->Unlock(), "discard unlock");
    }
    // Near-plane cases: six explicit clip-space vertices (x, y, w; the rows
    // below map object (x, y, z) to clip (x, y, .1 (z - 1), z), so the D3D
    // near plane z' = 0 sits at w = 1) repeated 16 times: exactly 96
    // vertices, one whole checkpoint, no stale tail in the box.
    void write_near(const float (*vertices)[3]) {
        void* data = nullptr;
        api(bullets->Lock(0, bytes, &data, D3DLOCK_DISCARD), "near discard lock");
        auto* words = static_cast<float*>(data);
        for (unsigned n = 0; n < bytes / 4; ++n) words[n] = 0.f;
        for (unsigned q = 0; q < 16; ++q) for (unsigned k = 0; k < 6; ++k) {
            auto* v = reinterpret_cast<unsigned char*>(data) + (q * 6 + k) * stride;
            const float uv[2] = {float(k & 1), float(k >> 1)};
            const DWORD colour = 0xff000000u;
            std::memcpy(v, vertices[k], 12); std::memcpy(v + 12, uv, 8); std::memcpy(v + 20, &colour, 4);
        }
        api(bullets->Unlock(), "near discard unlock");
    }
    static constexpr float near_rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .1f, -.1f, 0, 0, 1, 0};
    void draw(const char* label, unsigned quads, bool instanced = false, const float* rows_override = nullptr) {
        ++frame;
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff202020, 1.f, 0), "clear");
        api(d->BeginScene(), "BeginScene");
        // g_mViewProjection at c0-3: x' = x, y' = y, z' = z/2 + 1, w = z + 2.
        const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .5f, 1, 0, 0, 1, 2};
        api(d->SetVertexShaderConstantF(0, rows_override ? rows_override : rows, 4), "rows");
        api(d->SetVertexDeclaration(declaration), "declaration");
        api(d->SetStreamSource(0, bullets, 0, stride), "stream 0");
        api(d->SetStreamSource(1, instance, 0, 4), "stream 1");
        api(d->SetStreamSourceFreq(0, instanced ? (D3DSTREAMSOURCE_INDEXEDDATA | 1u) : 1u), "stream 0 frequency");
        api(d->SetStreamSourceFreq(1, instanced ? (D3DSTREAMSOURCE_INSTANCEDATA | 1u) : 1u), "stream 1 frequency");
        api(d->SetIndices(nullptr), "no indices");
        const HRESULT hr = d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, quads * 2);
        api(d->EndScene(), "EndScene");
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        std::printf("FRAME %u label=%s quads=%u vertices=%u instanced=%u draw=%08lx\n", frame, label, quads, quads * 6, instanced, hr);
    }
};
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) { std::puts("usage: fixture <vs.bin> <ps.bin>"); return 2; }
    WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3LockedPrefix";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "Locked-prefix bound fixture", WS_OVERLAPPEDWINDOW, 0, 0, W, H, nullptr, nullptr, cls.hInstance, nullptr);
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    if (!window || !runtime) { std::puts("FAIL window or d3d9"); return 2; }
    try {
        const auto vs_words = load(argv[1]), ps_words = load(argv[2]);
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime, "Direct3DCreate9");
        IDirect3D9* factory = create(D3D_SDK_VERSION);
        if (!factory) throw std::runtime_error("factory");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = W; pp.BackBufferHeight = H; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24X8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Fixture f;
        api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &f.d), "CreateDevice");
        IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
        api(f.d->CreateVertexShader(vs_words.data(), &vs), "bullet VS");
        api(f.d->CreatePixelShader(ps_words.data(), &ps), "bullet PS");
        // The writer's declaration (0x00608d58) plus an unused stream-1 element
        // for the instanced-frequency frame.
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
            {1, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7},
            D3DDECL_END()};
        api(f.d->CreateVertexDeclaration(elements, &f.declaration), "declaration");
        f.create_buffers();
        const D3DVIEWPORT9 vp{0, 0, W, H, 0, 1};
        api(f.d->SetViewport(&vp), "viewport");
        api(f.d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill mode");
        api(f.d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
        api(f.d->SetRenderState(D3DRS_ZENABLE, FALSE), "z");
        api(f.d->SetVertexShader(vs), "set VS");
        api(f.d->SetPixelShader(ps), "set PS");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        f.write(17, 0.f); f.draw("first_draw_unknown", 17);            // marks; refused (unknown)
        f.write(17, 0.f); f.draw("bound", 17);
        // Near-plane cases (screen-emission-region.md, step B): a triangle
        // with one vertex behind the camera (the second triangle degenerate),
        // a quad whose near edge lies exactly on the near plane, a quad
        // entirely behind it, and a beam from the near plane to w = 1000.
        const float straddle[6][3] = {{0, 0, -1}, {0, .75f, 3}, {1.5f, .75f, 3}, {0, .75f, 3}, {0, .75f, 3}, {0, .75f, 3}};
        const float exact[6][3] = {{0, -.25f, 1}, {.5f, -.25f, 1}, {0, .75f, 3}, {.5f, -.25f, 1}, {1.5f, .75f, 3}, {0, .75f, 3}};
        const float behind[6][3] = {{0, -.25f, -1}, {.5f, -.25f, -1}, {0, .75f, -3}, {.5f, -.25f, -1}, {1.5f, .75f, -3}, {0, .75f, -3}};
        const float beam[6][3] = {{0, -.25f, 1}, {.5f, -.25f, 1}, {0, 250, 1000}, {.5f, -.25f, 1}, {500, 250, 1000}, {0, 250, 1000}};
        f.write_near(straddle); f.draw("near_straddle", 16, false, Fixture::near_rows);
        f.write_near(exact); f.draw("near_exact", 16, false, Fixture::near_rows);
        f.write_near(behind); f.draw("near_behind", 16, false, Fixture::near_rows);
        f.write_near(beam); f.draw("near_beam", 16, false, Fixture::near_rows);
        f.write(16, nan); f.draw("bound_exact_checkpoint", 16);       // 96 vertices: the NaN tail is beyond
        f.write(17, nan); f.draw("nan_tail_refused", 17);             // 102: checkpoint 1 holds NaN
        f.write(17, 0.f, true); f.draw("nested_lock_invalid", 17);
        f.write(17, 0.f); f.draw("instanced_refused", 17, true);
        f.write(17, 0.f); f.draw("bound_after_instanced", 17);
        f.write(1024, 0.f); f.draw("full_buffer", 1024);
        f.draw("no_relock_same_revision", 1024);                      // the published scan serves again
        release(f.bullets);
        api(f.d->Reset(&pp), "Reset");
        api(f.d->SetViewport(&vp), "viewport after reset");
        api(f.d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill mode after reset");
        api(f.d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull after reset");
        api(f.d->SetVertexShader(vs), "set VS after reset");
        api(f.d->SetPixelShader(ps), "set PS after reset");
        release(f.instance); f.create_buffers();
        f.write(17, 0.f); f.draw("after_reset_unknown", 17);
        f.write(17, 0.f); f.draw("after_reset_bound", 17);
        release(f.bullets); release(f.instance); release(f.declaration); release(vs); release(ps);
        const ULONG remaining = f.d->Release();
        release(factory);
        std::printf("RESULT PASS frames=%u device_refs=%lu\n", f.frame, remaining);
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL %s\n", error.what());
        return 1;
    }
    DestroyWindow(window);
    return 0;
}
