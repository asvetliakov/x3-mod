// Proxy-loaded fixture for the locked-prefix bullet bound (step B of
// docs/architecture/screen-emission-region.md, step D of
// docs/architecture/screen-emission-bullet-bound.md). Runs against the built
// d3d9.dll placed beside it (WINEDLLOVERRIDES d3d9=n,b) with
// X3M_OWNERSHIP=1 X3M_MOTION_OUTPUT=1 X3M_SCREEN_EMISSION_BOUND=1: the
// game's bullet vertex shader (vs_5e484a06672e28fb) and pixel shader draw
// non-indexed quads from a DISCARD-locked dynamic buffer with the writer's
// layout, one draw per frame, through a scripted sequence (first draw,
// steady state, near-plane straddling / exact / behind / long beam, NaN
// tail, NaN inside the prefix, nested lock, instanced stream, the run-15/17
// geometries of the bullet-bound note through a diagonal world frame with
// a 1e5 offset, Reset). The proxy's capture log carries the locked_prefix /
// locked_prefix_frame lines the runner checks; this program prints, per
// frame, the footprint the GPU actually rasterised (one documented
// GetRenderTargetData of the back buffer), which the runner holds against
// the logged rectangle. No X3, no game launch.
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
constexpr unsigned W = 320, H = 192, stride = 24, max_vertices = 6144, bytes = max_vertices * stride;
constexpr DWORD clear_colour = 0xff202020u;
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

// The bullet-bound note's camera: a D3D left-handed perspective (60 degree
// vertical field, zn = 6, zf = 1e5) behind a world frame rotated 50/-28/35
// degrees about y/x/z and offset by (-112000, 3000, 45000), so a beam along
// the view axis is diagonal to every world axis and its coordinates carry
// the fp32 cancellation of section 4. View-space geometry is generated,
// then taken to world space; the rows are P * [R | -R W].
struct Camera {
    double R[3][3]{};
    double W[3] = {-112000.0, 3000.0, 45000.0};
    Camera() {
        const double ax = -28 * 3.14159265358979323846 / 180, ay = 50 * 3.14159265358979323846 / 180, az = 35 * 3.14159265358979323846 / 180;
        const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az), sz = std::sin(az);
        const double Rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}, Ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}, Rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
        double RxRy[3][3]{};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) RxRy[i][j] += Rx[i][k] * Ry[k][j];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) R[i][j] += Rz[i][k] * RxRy[k][j];
    }
    // p_world = R^T p_view + W
    void world(const double view[3], float out[3]) const {
        for (int a = 0; a < 3; ++a) { double s = W[a]; for (int k = 0; k < 3; ++k) s += R[k][a] * view[k]; out[a] = float(s); }
    }
    void rows(unsigned width, unsigned height, float out[16]) const {
        const double zn = 6, zf = 100000, fovy = 60 * 3.14159265358979323846 / 180;
        const double fy = 1 / std::tan(fovy / 2), fx = fy * double(height) / double(width), Q = zf / (zf - zn);
        double t[3]{};
        for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) t[i] -= R[i][k] * W[k];
        const double M[16] = {R[0][0] * fx, R[0][1] * fx, R[0][2] * fx, t[0] * fx,
                              R[1][0] * fy, R[1][1] * fy, R[1][2] * fy, t[1] * fy,
                              R[2][0] * Q, R[2][1] * Q, R[2][2] * Q, t[2] * Q - Q * zn,
                              R[2][0], R[2][1], R[2][2], t[2]};
        for (int i = 0; i < 16; ++i) out[i] = float(M[i]);
    }
};
struct Lcg {
    unsigned seed;
    explicit Lcg(unsigned s) : seed(s) {}
    double next() { seed = seed * 1664525u + 1013904223u; return double(seed >> 8) / double(1u << 24); }
};
// View-space bolt centres of the note's rows (6 vertices per bolt: a quad
// 3 units wide, 40 long along +z, as two triangles).
enum class Batch { Fan176, Straddle72, Big605, Small27 };
void bolt_centre(Batch batch, double u, Lcg& rng, double out[3]) {
    switch (batch) {
    case Batch::Fan176: { // four guns, bolts fanning out ahead, view depth 148..1842
        const double guns[4][2] = {{-22, -8}, {22, -8}, {-9, 4}, {9, 4}};
        const auto& g = guns[unsigned(rng.next() * 4) % 4];
        const double z = 148 + u * (1842 - 148 - 40);
        out[0] = g[0] + z * (0.010 + 0.015 * (rng.next() - .5)); out[1] = g[1] + z * (0.006 + 0.012 * (rng.next() - .5)); out[2] = z; return; }
    case Batch::Straddle72: { // one beam from behind the camera (-320) to 7726, passing it 12 units aside, 1.5 above
        const double z = -320 + u * (7726 + 320 - 40);
        out[0] = 12 + z * 0.003 + 2 * (rng.next() - .5); out[1] = 1.5 + z * 0.0015 + 2 * (rng.next() - .5); out[2] = z; return; }
    case Batch::Big605: { // a broad batch 500..6498 deep, +-250 x +-80 across
        const double z = 500 + u * (6498 - 500 - 40);
        out[0] = (rng.next() * 2 - 1) * 250; out[1] = (rng.next() * 2 - 1) * 80; out[2] = z; return; }
    case Batch::Small27: { // a short volley 186..1288 deep
        const double z = 186 + u * (1288 - 186 - 40);
        out[0] = -10 + z * 0.02 + 4 * (rng.next() - .5); out[1] = 8 - z * 0.01 + 4 * (rng.next() - .5); out[2] = z; return; }
    }
}

struct Fixture {
    IDirect3DDevice9* d = nullptr;
    IDirect3DVertexBuffer9* bullets = nullptr;
    IDirect3DVertexBuffer9* instance = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DSurface9* readback = nullptr;
    Camera camera;
    unsigned frame = 0;
    void create_buffers() {
        api(d->CreateVertexBuffer(bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &bullets, nullptr), "bullet buffer");
        api(d->CreateVertexBuffer(64, 0, 0, D3DPOOL_MANAGED, &instance, nullptr), "instance buffer");
        void* data = nullptr;
        api(instance->Lock(0, 0, &data, 0), "instance lock"); std::memset(data, 0, 64); api(instance->Unlock(), "instance unlock");
        api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr), "readback surface");
    }
    static void put_vertex(void* data, unsigned index, const float position[3], unsigned k) {
        auto* v = reinterpret_cast<unsigned char*>(data) + index * stride;
        const float uv[2] = {float(k & 1), float(k >> 1)};
        const DWORD colour = 0xffffffffu;
        std::memcpy(v, position, 12); std::memcpy(v + 12, uv, 8); std::memcpy(v + 20, &colour, 4);
    }
    // The writer: whole-buffer DISCARD lock, stale tail first (overwriting
    // the proxy's sentinel, as recycled DISCARD memory would), then N quads.
    void write(unsigned quads, float tail, bool nested = false, int nan_vertex = -1) {
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
                float position[3] = {corners[k][0], corners[k][1], c[2]};
                if (int(q * 6 + k) == nan_vertex) position[0] = std::numeric_limits<float>::quiet_NaN();
                put_vertex(data, q * 6 + k, position, k);
            }
        }
        if (nested) api(bullets->Unlock(), "nested unlock");
        api(bullets->Unlock(), "discard unlock");
    }
    // Near-plane cases: six explicit clip-space vertices (x, y, w; the rows
    // below map object (x, y, z) to clip (x, y, .1 (z - 1), z), so the D3D
    // near plane z' = 0 sits at w = 1) repeated 16 times: 96 vertices over a
    // zero (non-sentinel) tail.
    void write_near(const float (*vertices)[3]) {
        void* data = nullptr;
        api(bullets->Lock(0, bytes, &data, D3DLOCK_DISCARD), "near discard lock");
        auto* words = static_cast<float*>(data);
        for (unsigned n = 0; n < bytes / 4; ++n) words[n] = 0.f;
        for (unsigned q = 0; q < 16; ++q) for (unsigned k = 0; k < 6; ++k) put_vertex(data, q * 6 + k, vertices[k], k);
        api(bullets->Unlock(), "near discard unlock");
    }
    // The note's batches as the game writes them: only the prefix (the
    // sentinel tail survives), or over a zero-filled window first (the
    // run-15 stale origin tail: vertex (0, 0, 0) past the prefix).
    void write_batch(Batch batch, unsigned bolts, unsigned seed, bool zero_tail) {
        void* data = nullptr;
        api(bullets->Lock(0, bytes, &data, D3DLOCK_DISCARD), "batch discard lock");
        if (zero_tail) std::memset(data, 0, bytes);
        Lcg rng(seed);
        for (unsigned i = 0; i < bolts; ++i) {
            const double u = rng.next();
            double c[3]; bolt_centre(batch, u, rng, c);
            const double hw = 1.5, L = 40;
            const double corners[6][3] = {{c[0] - hw, c[1], c[2]}, {c[0] + hw, c[1], c[2]}, {c[0] - hw, c[1], c[2] + L},
                                          {c[0] + hw, c[1], c[2]}, {c[0] + hw, c[1], c[2] + L}, {c[0] - hw, c[1], c[2] + L}};
            for (unsigned k = 0; k < 6; ++k) { float p[3]; camera.world(corners[k], p); put_vertex(data, i * 6 + k, p, k); }
        }
        api(bullets->Unlock(), "batch discard unlock");
    }
    static constexpr float near_rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .1f, -.1f, 0, 0, 1, 0};
    // One frame: the draw, then the rasterised footprint inside the viewport
    // (pixels that differ from the clear colour; bbox in D3D RECT convention).
    void draw(const char* label, unsigned quads, bool instanced = false, const float* rows_override = nullptr, unsigned vw = 96, unsigned vh = 96) {
        ++frame;
        const D3DVIEWPORT9 vp{0, 0, vw, vh, 0, 1};
        api(d->SetViewport(&vp), "viewport");
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_colour, 1.f, 0), "clear");
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
        unsigned covered = 0; long l = 0, t = 0, r = 0, b = 0;
        {
            IDirect3DSurface9* back = nullptr;
            api(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), "back buffer");
            api(d->GetRenderTargetData(back, readback), "readback");
            release(back);
            D3DLOCKED_RECT locked{};
            api(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "readback lock");
            for (unsigned y = 0; y < vh; ++y) {
                const auto* row = reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(locked.pBits) + y * locked.Pitch);
                for (unsigned x = 0; x < vw; ++x) {
                    if ((row[x] & 0xffffffu) == (clear_colour & 0xffffffu)) continue;
                    if (!covered) { l = r = long(x); t = b = long(y); }
                    l = long(x) < l ? long(x) : l; r = long(x) > r ? long(x) : r; t = long(y) < t ? long(y) : t; b = long(y) > b ? long(y) : b;
                    ++covered;
                }
            }
            api(readback->UnlockRect(), "readback unlock");
        }
        api(d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        std::printf("FRAME %u label=%s quads=%u vertices=%u instanced=%u draw=%08lx viewport=%u,%u covered=%u footprint=%ld,%ld,%ld,%ld\n",
                    frame, label, quads, quads * 6, instanced, hr, vw, vh, covered, l, t, covered ? r + 1 : 0, covered ? b + 1 : 0);
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
        auto states = [&] {
            api(f.d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID), "fill mode");
            api(f.d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
            api(f.d->SetRenderState(D3DRS_ZENABLE, FALSE), "z");
            api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "blend");
            api(f.d->SetVertexShader(vs), "set VS");
            api(f.d->SetPixelShader(ps), "set PS");
        };
        states();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        f.write(17, 0.f); f.draw("first_draw_unknown", 17);            // marks; refused (unknown)
        // Step D: the bullet-bound note's batches (section 1) through the
        // diagonal world frame, 320x192 viewport, inside the proxy's capture
        // window (frames 1-8) so the per-draw line carries the AABB
        // comparison: hull rectangle against the AABB of the same vertices;
        // the writer leaves the sentinel tail (exact scan) except the
        // 605-bolt batch over a zero window (the run-15 origin tail, scanned
        // to the window end, never in the bound).
        float rows[16]; f.camera.rows(W, H, rows);
        auto batches = [&](const char* suffix) {
            std::string fan = std::string("fan_176") + suffix, straddle = std::string("straddle_72") + suffix, big = std::string("big_605_origin_tail") + suffix, small = std::string("small_27") + suffix;
            f.write_batch(Batch::Fan176, 176, 1001, false); f.draw(fan.c_str(), 176, false, rows, W, H);
            f.write_batch(Batch::Straddle72, 72, 1002, false); f.draw(straddle.c_str(), 72, false, rows, W, H);
            f.write_batch(Batch::Big605, 605, 1003, true); f.draw(big.c_str(), 605, false, rows, W, H);
            f.write_batch(Batch::Small27, 27, 1004, false); f.draw(small.c_str(), 27, false, rows, W, H);
        };
        batches("");
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
        f.write(16, nan); f.draw("nan_tail_96", 16);                  // 96 written, NaN from 96: the exact prefix is clean
        f.write(17, nan); f.draw("nan_tail_102", 17);                 // 102 written over a NaN window: still clean (step B refused this)
        f.write(17, 0.f, false, 3); f.draw("prefix_nan_refused", 17); // NaN inside the drawn prefix: refused
        f.write(17, 0.f, true); f.draw("nested_lock_invalid", 17);
        f.write(17, 0.f); f.draw("instanced_refused", 17, true);
        f.write(17, 0.f); f.draw("bound_after_instanced", 17);
        f.write(1024, 0.f); f.draw("full_buffer", 1024);
        f.draw("no_relock_same_revision", 1024);                      // the published scan serves again
        // The same batches outside the capture window: the steady-state
        // draw path (no AABB comparison), whose derive_us is the production
        // per-draw cost.
        batches("_plain");
        release(f.bullets); release(f.readback);
        api(f.d->Reset(&pp), "Reset");
        states();
        release(f.instance); f.create_buffers();
        f.write(17, 0.f); f.draw("after_reset_unknown", 17);
        f.write(17, 0.f); f.draw("after_reset_bound", 17);
        release(f.bullets); release(f.instance); release(f.readback); release(f.declaration); release(vs); release(ps);
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
