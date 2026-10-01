// Engine effects phase 1a fixture (docs/architecture/engine-effects-modern.md sections 1, 2, 5; run_engine_effects.py).
// Drives the production draw path of the motion seam DLL (capture.cpp + motion_output.cpp under
// X3M_MOTION_OUTPUT_FIXTURE, copied beside this executable as d3d9.dll): synthetic object scopes through the seam's
// configure export (the node block lives in this process and is read through engine_memory like a game node), the
// effects pair's program bytes (local, untracked: /tmp/x3-shader-sweep/programs), a synthetic engine body manager for
// the name -> id resolution, and the session log for the census rows. Modes:
//   main       X3M_ENGINE_EFFECTS=off, X3M_DEBUG=1: the scenario frames, ring overflow, a Reset, the records
//   native     X3M_ENGINE_EFFECTS=native, X3M_DEBUG=1: the census counts, nothing suppressed or recorded
//   unverified X3M_ENGINE_EFFECTS=off without the identity seam: refused, everything forwarded
//   unpatched  X3M_ENGINE_EFFECTS=off, identity verified, the call redirects not live: forwarded_patch_missing
//   timing     X3M_ENGINE_EFFECTS=off, no census: per-draw cost of the suppressed, not_jet and non-candidate paths
//   plumes     X3M_ENGINE_EFFECTS=plumes without --hdr --taa: suppressed as off, the stage refuses to arm
//   armed      X3M_ENGINE_EFFECTS=plumes with X3M_HDR=1 X3M_TAA=1 X3M_MOTION_JITTER=1 and the fixture camera: frames in the
//              scene-boundary pattern (initial Clear, background draw, depth-only Clear, a depth writer, SetDepth null,
//              the bloom StretchRect where the resolve runs); the production stage arms, attaches inside the resolve and
//              draws the scene view's records (two of four: one of another camera, one from the background phase); a
//              forced draw fault disarms 64 frames, three consecutive ones refuse until Reset; Reset releases and the
//              next armed frame recreates the pass; taa_references unchanged across the cycle
// Output: CHECK <label> PASS|FAIL lines, FRAME / RECORD / TIMING lines, RESULT PASS|FAIL. Original synthetic content
// only; no game bytes are written or redistributed.
#include <windows.h>
#include <d3d9.h>
#define X3M_MOTION_OUTPUT_FIXTURE
#include "../../src/proxy/motion_output.h"
#include "../../src/proxy/engine_effects_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace ee = x3m::engine_effects::core;
unsigned failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL");
}
void api(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::printf("API_FAIL %s %08lx\n", what, static_cast<unsigned long>(hr));
        std::fflush(stdout);
        ExitProcess(3);
    }
}
std::vector<char> slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool close_to(float a, float b, float tol = 1e-5f) {
    const float scale = std::fabs(b) > 1.f ? std::fabs(b) : 1.f;
    return std::fabs(a - b) <= tol * scale;
}

using Configure = void (*)(const x3m::MotionOutputFixtureConfig*);
using Identity = void (*)(int);
using BodyGlobal = void (*)(std::uintptr_t);
using Status = unsigned (*)(IDirect3DDevice9*, unsigned);
using RecordFn = int (*)(IDirect3DDevice9*, unsigned, void*, unsigned);
using EmissionStatus = unsigned (*)(IDirect3DDevice9*, unsigned);
using PlumesFault = int (*)(IDirect3DDevice9*, unsigned);
using CameraInstall = void (*)(const float* const*, const float* const*);
using Create9 = IDirect3D9*(WINAPI*)(UINT);
// The armed mode's engine camera globals (camera_state::fixture_install reads them like *0x00608a38 / *0x00608a40):
// identity view (world = view), the game's projection terms (m00 0.8, m11 4/3, near 6).
float camera_projection[16] = {.8f, 0, 0, 0, 0, 4.f / 3.f, 0, 0, 0, 0, 1.000003f, 1.f, 0, 0, -6.0000184f, 0};
float camera_view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
const float* camera_projection_slot = camera_projection;
const float* camera_view_slot = camera_view;

// Synthetic engine body manager: fixed 11000 slots, two dynamic ones (the global at manager_global -> manager).
struct BodyManager {
    unsigned char header[0xc0]{};
    std::vector<unsigned char> slots;
    std::uint32_t global = 0;
    char named[2][64] = {"effects\\engines\\fx_engine_test_red", "effects\\engines\\other"};
    void build() {
        slots.assign((11000 + 2) * 0x1c, 0);
        const std::uint32_t fixed = 11000, dynamic = 2, at = std::uint32_t(reinterpret_cast<std::uintptr_t>(slots.data()));
        std::memcpy(header + 0xb4, &fixed, 4);
        std::memcpy(header + 0xb8, &dynamic, 4);
        std::memcpy(header + 0xbc, &at, 4);
        for (unsigned i = 0; i < 2; ++i) {
            const std::uint32_t p = std::uint32_t(reinterpret_cast<std::uintptr_t>(named[i]));
            std::memcpy(slots.data() + (fixed + i) * 0x1c + 0x0c, &p, 4);
        }
        global = std::uint32_t(reinterpret_cast<std::uintptr_t>(header));
    }
};
// A synthetic render node: the words object_trace reads (+0x28 handle, +0x70/+0x80..+0x88 scale, +0xb0 position,
// +0xc0/+0xd0/+0xe0 basis rows 16.16, +0x130 flags, +0x140 model).
struct Node {
    alignas(16) std::uint32_t words[0x150 / 4]{};
    void set(std::uint32_t handle, std::uint32_t model, std::uint32_t flags130, float z, const float r[9], const int position[3]) {
        std::memset(words, 0, sizeof words);
        words[0x28 / 4] = handle;
        words[0x70 / 4] = 1000u << 16;
        words[0x80 / 4] = words[0x84 / 4] = 0x10000;
        words[0x88 / 4] = std::uint32_t(std::int32_t(std::lround(z * 65536.f)));
        for (unsigned i = 0; i < 3; ++i) words[0xb0 / 4 + i] = std::uint32_t(position[i]);
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned c = 0; c < 3; ++c) words[0xc0 / 4 + row * 4 + c] = std::uint32_t(std::int32_t(std::lround(r[row * 3 + c] * 65536.f)));
        words[0x130 / 4] = flags130;
        words[0x140 / 4] = model;
    }
};
void rotation(float yaw, float pitch, float roll, float r[9]) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    const float m[9] = {cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr, -cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr,
                        sy * cp, -sp, cy * cp};
    std::memcpy(r, m, sizeof m);
}
// c4-6 of a world (unit rows r = model x, y, z; scale k, k, k z; translation t) in register order a or b.
void rows_of(const float r[9], float k, float z, const float t[3], bool order_b, float out[12]) {
    const float X[3] = {r[0] * k, r[1] * k, r[2] * k}, Y[3] = {r[3] * k, r[4] * k, r[5] * k}, Z[3] = {r[6] * k * z, r[7] * k * z, r[8] * k * z};
    for (unsigned i = 0; i < 3; ++i) {
        if (order_b) {
            const float* axis = i == 0 ? X : i == 1 ? Y : Z;
            out[i * 4 + 0] = axis[0];
            out[i * 4 + 1] = axis[1];
            out[i * 4 + 2] = axis[2];
        } else {
            out[i * 4 + 0] = X[i];
            out[i * 4 + 1] = Y[i];
            out[i * 4 + 2] = Z[i];
        }
        out[i * 4 + 3] = t[i];
    }
}
constexpr std::uint32_t jet = 0x4000001u, other_flags = 0x200u;
struct Jet {
    Node node;
    float r[9], k, z, t[3], rows[12];
    bool order_b;
    std::uint64_t serial;
    std::uint32_t model;
    int body; // expected table entry (-1 unknown)
    std::uint8_t cluster;
    bool steering;
};

struct Fixture {
    HMODULE runtime = nullptr;
    HWND window = nullptr;
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* device = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* effect_vb = nullptr;
    IDirect3DVertexBuffer9* quad_vb = nullptr;
    IDirect3DSurface9* readback = nullptr;
    Configure configure = nullptr;
    Status status = nullptr;
    RecordFn record = nullptr;
    EmissionStatus emission = nullptr;
    PlumesFault plumes_fault = nullptr;
    IDirect3DTexture9* bloom = nullptr;          // armed: the application's bloom source (the resolve's copy target)
    IDirect3DSurface9* bloom_surface = nullptr;
    IDirect3DSurface9* back = nullptr;           // armed: the back buffer and the auto depth surface
    IDirect3DSurface9* depth = nullptr;
    bool armed = false;
    x3m::MotionOutputFixtureConfig config{};
    Node node_d, node_e; // not_jet (flags 0x200) and the fixed-function JET node
    Jet a, b, c;

    void scope(const Node* node, std::uint64_t serial, std::uint32_t camera_handle = 0) {
        config.scope = {};
        if (node) {
            config.scope.known = 1;
            config.scope.node = reinterpret_cast<std::uintptr_t>(node->words);
            config.scope.node_serial = serial;
            config.scope.camera_handle = camera_handle;
        }
        configure(&config);
    }
    unsigned primitive_calls() { return emission(device, 49); } // DrawPrimitive submissions that reached the device
    void create(bool timing) {
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "x3m_engine_effects_fixture";
        RegisterClassA(&cls);
        window = CreateWindowA(cls.lpszClassName, "engine effects fixture", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, nullptr, nullptr, cls.hInstance, nullptr);
        d3d = reinterpret_cast<Create9>(reinterpret_cast<void*>(GetProcAddress(runtime, "Direct3DCreate9")))(D3D_SDK_VERSION);
        if (!d3d) api(E_FAIL, "Direct3DCreate9");
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = armed ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8; // armed: the selector's main target format
        pp.EnableAutoDepthStencil = armed;
        pp.AutoDepthStencilFormat = D3DFMT_D24X8;
        pp.hDeviceWindow = window;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        api(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device), "CreateDevice");
        (void)timing;
    }
    void resources(const std::vector<char>& vs_bytes, const std::vector<char>& ps_bytes) {
        api(device->CreateVertexShader(reinterpret_cast<const DWORD*>(vs_bytes.data()), &vs), "CreateVertexShader effect");
        api(device->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_bytes.data()), &ps), "CreatePixelShader effect");
        const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                              {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                              {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                              D3DDECL_END()};
        api(device->CreateVertexDeclaration(elements, &decl), "CreateVertexDeclaration");
        api(device->CreateVertexBuffer(3 * 24, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &effect_vb, nullptr), "effect VB");
        void* p = nullptr;
        api(effect_vb->Lock(0, 0, &p, 0), "effect VB lock");
        const float tri[3][6] = {{0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 1, 0}, {0, 1, 0, 0, 0, 1}};
        for (unsigned i = 0; i < 3; ++i) {
            float v[6];
            std::memcpy(v, tri[i], sizeof v);
            const DWORD white = 0xffffffffu;
            std::memcpy(&v[3], &white, 4);
            std::memcpy(static_cast<char*>(p) + i * 24, v, 24);
        }
        effect_vb->Unlock();
        struct Q { float x, y, z, rhw; DWORD c; };
        const Q quad[6] = {{-.5f, -.5f, 0, 1, 0xffffffffu}, {63.5f, -.5f, 0, 1, 0xffffffffu}, {-.5f, 63.5f, 0, 1, 0xffffffffu},
                           {63.5f, -.5f, 0, 1, 0xffffffffu}, {63.5f, 63.5f, 0, 1, 0xffffffffu}, {-.5f, 63.5f, 0, 1, 0xffffffffu}};
        api(device->CreateVertexBuffer(sizeof quad, D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &quad_vb, nullptr), "quad VB");
        api(quad_vb->Lock(0, 0, &p, 0), "quad VB lock");
        std::memcpy(p, quad, sizeof quad);
        quad_vb->Unlock();
        api(device->CreateOffscreenPlainSurface(64, 64, pp.BackBufferFormat, D3DPOOL_SYSTEMMEM, &readback, nullptr), "readback surface");
        if (armed) swapchain_surfaces();
    }
    DWORD pixel(int x = 32, int y = 32) {
        IDirect3DSurface9* target = nullptr;
        api(device->GetRenderTarget(0, &target), "GetRenderTarget");
        api(device->GetRenderTargetData(target, readback), "GetRenderTargetData");
        target->Release();
        D3DLOCKED_RECT lr{};
        api(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY), "readback lock");
        const DWORD v = reinterpret_cast<const DWORD*>(static_cast<const char*>(lr.pBits) + y * lr.Pitch)[x] & 0xffffffu;
        readback->UnlockRect();
        return v;
    }
    // armed: the back buffer, the auto depth surface and the bloom source (DEFAULT: released before Reset).
    void swapchain_surfaces() {
        api(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), "GetBackBuffer");
        api(device->GetDepthStencilSurface(&depth), "GetDepthStencilSurface");
        api(device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &bloom, nullptr), "bloom texture");
        api(bloom->GetSurfaceLevel(0, &bloom_surface), "bloom level");
    }
    void release_swapchain_surfaces() {
        for (IUnknown** p : {reinterpret_cast<IUnknown**>(&bloom_surface), reinterpret_cast<IUnknown**>(&bloom),
                             reinterpret_cast<IUnknown**>(&back), reinterpret_cast<IUnknown**>(&depth)})
            if (*p) {
                (*p)->Release();
                *p = nullptr;
            }
    }
    void fixed_function(bool blend, bool zwrite) {
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device->SetStreamSource(0, quad_vb, 0, 20);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, blend);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, zwrite);
    }
    void effect_state(bool zwrite) {
        device->SetVertexShader(vs);
        device->SetPixelShader(ps);
        device->SetVertexDeclaration(decl);
        device->SetStreamSource(0, effect_vb, 0, 24);
        const float zero[16] = {};
        device->SetVertexShaderConstantF(0, zero, 4); // c0-3 WVP zero: every vertex at the clip origin, nothing drawn
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
        device->SetRenderState(D3DRS_ZWRITEENABLE, zwrite);
    }
    HRESULT draw(unsigned primitives) {
        return device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitives);
    }
    void make_jets() {
        const int pa[3] = {1000, -2000, 3000}, pb[3] = {-50, 60, -70}, pc[3] = {7, 8, 9};
        rotation(.7f, -.4f, 1.1f, a.r);
        rotation(-1.3f, .2f, .5f, b.r);
        rotation(2.1f, .9f, -.6f, c.r);
        a.k = 1000.f; a.z = .25f; a.t[0] = 100.f; a.t[1] = 200.f; a.t[2] = 300.f; a.order_b = false; a.serial = 501; a.model = 20000; a.body = 0; a.cluster = ee::red; a.steering = false;
        b.k = 260.f; b.z = 1.125f; b.t[0] = -5.f; b.t[1] = 6.f; b.t[2] = -7.f; b.order_b = false; b.serial = 502; b.model = 566; b.body = 2; b.cluster = ee::grey; b.steering = true;
        c.k = 9366.f; c.z = 2.f; c.t[0] = 1e4f; c.t[1] = -2e4f; c.t[2] = 3e4f; c.order_b = true; c.serial = 0; c.model = 777; c.body = -1; c.cluster = ee::default_cluster; c.steering = false;
        a.node.set(0xa1, a.model, jet, a.z, a.r, pa);
        b.node.set(0xb2, b.model, jet, b.z, b.r, pb);
        c.node.set(0xc3, c.model, jet | 0x10u, c.z, c.r, pc);
        for (Jet* j : {&a, &b, &c}) rows_of(j->r, j->k, j->z, j->t, j->order_b, j->rows);
        const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        const int origin[3] = {0, 0, 0};
        node_d.set(0xd4, 20001, other_flags, 1.f, ident, origin);
        node_e.set(0xe5, 20000, jet, 1.5f, ident, origin);
    }
    void jet_draw(const Jet& j, bool zwrite, bool scoped, std::uint32_t camera_handle = 0) {
        effect_state(zwrite);
        device->SetVertexShaderConstantF(4, j.rows, 3);
        scope(scoped ? &j.node : nullptr, j.serial, camera_handle);
        api(draw(1), "effect draw");
    }
    struct Counts { unsigned v[25]; };
    Counts counts() {
        Counts c{};
        for (unsigned k = 0; k < 25; ++k) c.v[k] = status(device, k);
        return c;
    }
    void print_frame(unsigned frame, const Counts& c, unsigned submitted, const char* tag) {
        std::printf("FRAME %s frame=%u candidates=%u not_jet=%u records=%u suppressed=%u forwarded_unscoped=%u forwarded_snapshot=%u forwarded_opaque=%u forwarded_state=%u forwarded_overflow=%u forwarded_native=%u forwarded_patch_missing=%u unknown_body=%u steering=%u rows_unknown=%u order_a=%u order_b=%u order_ambiguous=%u order_mismatch=%u order_invalid=%u hook=%u suppress=%u world46=%u pinned_b=%u rows=%u redirects=%u submitted=%u\n",
                    tag, frame, c.v[0], c.v[1], c.v[2], c.v[3], c.v[4], c.v[5], c.v[6], c.v[7], c.v[8], c.v[9], c.v[10], c.v[11], c.v[12], c.v[13], c.v[14], c.v[15],
                    c.v[16], c.v[17], c.v[18], c.v[19], c.v[20], c.v[21], c.v[22], c.v[23], c.v[24], submitted);
    }
    // One scenario frame: the pixel pair (a suppressed fixed-function JET draw leaves the target black, a not_jet one
    // writes white), the three effect-pair JET draws at z 0.25 / 1.125 / 2.0, an opaque JET draw (pair, Z-write on), an
    // unscoped pair draw, an opaque fixed-function draw on a JET node and an unscoped blended one (neither a candidate).
    void scenario(unsigned frame, const char* mode, bool suppressing, bool expect_records) {
        api(device->BeginScene(), "BeginScene");
        api(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0), "Clear");
        const unsigned before = primitive_calls();
        fixed_function(true, false);
        scope(&node_e, 505);
        api(draw(2), "fixed-function JET draw");
        const DWORD after_jet = pixel();
        scope(&node_d, 0);
        api(draw(2), "fixed-function not_jet draw");
        const DWORD after_other = pixel();
        jet_draw(a, false, true);
        jet_draw(b, false, true);
        jet_draw(c, false, true);
        jet_draw(a, true, true);  // opaque: Z-write on
        jet_draw(b, false, false); // unscoped
        fixed_function(false, true);
        scope(&node_e, 505);
        api(draw(2), "opaque fixed-function draw on a JET node");
        fixed_function(true, false);
        scope(nullptr, 0);
        api(draw(2), "unscoped blended fixed-function draw"); // not a candidate: no state read, forwarded
        const unsigned submitted = primitive_calls() - before;
        const Counts n = counts();
        print_frame(frame, n, submitted, mode);
        char label[160];
        auto named = [&](const char* what) { std::snprintf(label, sizeof label, "%s_f%u_%s", mode, frame, what); return label; };
        check(after_jet == (suppressing ? 0u : 0xffffffu), named("pixel_jet"));
        check(after_other == 0xffffffu, named("pixel_not_jet"));
        if (suppressing) {
            check(submitted == 5, named("submitted_5_of_9"));
            check(n.v[0] == 7 && n.v[1] == 1 && n.v[2] == 4 && n.v[3] == 4, named("candidates7_notjet1_records4_suppressed4"));
            check(n.v[4] == 1 && n.v[5] == 0 && n.v[6] == 1 && n.v[7] == 0 && n.v[8] == 0 && n.v[9] == 0 && n.v[10] == 0, named("forwarded_unscoped1_opaque1"));
            check(n.v[11] == 1 && n.v[12] == 1 && n.v[13] == 1, named("unknown_body1_steering1_rows_unknown1"));
            check(n.v[14] == 2 && n.v[15] == 1 && n.v[16] == 0 && n.v[17] == 0 && n.v[18] == 1, named("order_a2_b1_invalid1"));
            check(n.v[21] == 7 && n.v[24] == 1, named("c46_known_redirects_live"));
        } else {
            check(submitted == 9, named("submitted_9_of_9"));
            check(n.v[2] == 0 && n.v[3] == 0, named("nothing_recorded"));
            if (n.v[19] && n.v[20]) // off without the redirects: every JET draw forwarded, nothing hidden
                check(n.v[0] == 7 && n.v[1] == 1 && n.v[10] == 4 && n.v[9] == 0 && n.v[4] == 1 && n.v[6] == 1 && n.v[24] == 0,
                      named("forwarded_patch_missing4_unscoped1_opaque1"));
            else if (n.v[19]) // native under --debug: the census counts what off would suppress
                check(n.v[0] == 7 && n.v[1] == 1 && n.v[9] == 4 && n.v[10] == 0 && n.v[4] == 1 && n.v[6] == 1, named("forwarded_native4_unscoped1_opaque1"));
            else // refused: the hook never runs
                check(n.v[0] == 0 && n.v[9] == 0 && n.v[10] == 0, named("hook_off_counts_nothing"));
        }
        if (suppressing && expect_records) {
            ee::Record r[4]{};
            bool got = true;
            for (unsigned i = 0; i < 4; ++i) got = record(device, i, &r[i], sizeof r[i]) && got;
            check(got, named("records_read"));
            const Jet* jets[3] = {&a, &b, &c};
            // Record 0 is the fixed-function JET draw (node E): no geometry, z 1.5.
            check((r[0].flags & ee::flag_rows_unknown) && !(r[0].flags & ee::flag_effect_pair) && r[0].size == 0.f && close_to(r[0].z, 1.5f) &&
                      close_to(r[0].s, (1.5f - .25f) / 1.75f) && r[0].serial == 505 && r[0].body == 0 && r[0].model == 20000, named("record_ff"));
            for (unsigned i = 0; i < 3; ++i) {
                const Jet& j = *jets[i];
                const ee::Record& x = r[i + 1];
                char what[48];
                std::snprintf(what, sizeof what, "record_%c", char('a' + i));
                const float s = std::min(1.f, std::max(0.f, (j.z - .25f) / 1.75f));
                const bool geometry = close_to(x.origin[0], j.t[0]) && close_to(x.origin[1], j.t[1]) && close_to(x.origin[2], j.t[2]) && close_to(x.axis[0], -j.r[6]) &&
                                      close_to(x.axis[1], -j.r[7]) && close_to(x.axis[2], -j.r[8]) && close_to(x.size, j.k) && std::fabs(x.s - s) <= 1e-5f &&
                                      close_to(x.z, j.z) && close_to(x.ratio, j.z);
                const bool identity = x.model == j.model && x.serial == j.serial && ((x.flags & ee::flag_serial) != 0) == (j.serial != 0) &&
                                      x.node_handle == j.node.words[0x28 / 4] && x.body == j.body && (x.flags >> ee::cluster_shift) == j.cluster &&
                                      ((x.flags & ee::flag_steering) != 0) == j.steering && ((x.flags & ee::flag_unknown_body) != 0) == (j.body < 0) &&
                                      ((x.flags & ee::flag_order_b) != 0) == j.order_b && (x.flags & ee::flag_effect_pair) && !(x.flags & ee::flag_order_mismatch) &&
                                      x.frame == frame;
                std::printf("RECORD %s frame=%u origin=%.6g,%.6g,%.6g axis=%.7f,%.7f,%.7f size=%.7g s=%.7f z=%.7f ratio=%.7f body=%d flags=%04x serial=%llu\n", what, frame,
                            double(x.origin[0]), double(x.origin[1]), double(x.origin[2]), double(x.axis[0]), double(x.axis[1]), double(x.axis[2]), double(x.size),
                            double(x.s), double(x.z), double(x.ratio), int(x.body), unsigned(x.flags), static_cast<unsigned long long>(x.serial));
                check(geometry, named((std::string(what) + "_geometry").c_str()));
                check(identity, named((std::string(what) + "_identity").c_str()));
            }
        }
        api(device->EndScene(), "EndScene");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
    }
    // armed: four jets 100 units ahead (value 10, z 2.0, axis -x: the plume runs left of the nozzle); the window pixels
    // of the nozzles (m00 0.8, m11 4/3 at 64x64).
    Jet p1, p2, p3, p4;
    static constexpr std::uint32_t scene_camera = 0x77, other_camera = 0x99;
    struct Px { int x, y; };
    void make_armed_jets() {
        const float r[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0}; // model x = -z, y = y, z = x: the axis -(model z) = -x
        const float t[4][3] = {{0, 0, 100}, {-50, 30, 100}, {50, -30, 100}, {50, 30, 100}};
        Jet* jets[4] = {&p1, &p2, &p3, &p4};
        const int origin[3] = {0, 0, 0};
        for (unsigned i = 0; i < 4; ++i) {
            Jet& j = *jets[i];
            std::memcpy(j.r, r, sizeof r);
            j.k = 10.f;
            j.z = 2.f;
            std::memcpy(j.t, t[i], sizeof j.t);
            j.order_b = false;
            j.serial = 600 + i;
            j.model = 20000;
            j.node.set(0xf0 + i, j.model, jet, j.z, j.r, origin);
            rows_of(j.r, j.k, j.z, j.t, false, j.rows);
        }
    }
    struct Armed {
        unsigned armed, ran, result, nozzles, skipped, drew, references, taa_references, failures, refused, records,
            suppressed, resolved;
        DWORD px[4];
    };
    // One frame in the scene-boundary pattern: p4 in the background phase, p1 / p2 (the scene camera) and p3 (another
    // camera) in the scene phase, the resolve at the bloom copy; the statuses and the nozzle pixels after the copy.
    Armed armed_frame() {
        static const Px nozzle_px[4] = {{32, 32}, {19, 19}, {45, 45}, {45, 19}};
        api(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0), "Clear initial");
        api(device->BeginScene(), "BeginScene");
        effect_state(false); // background: an unscoped effect-pair draw (WVP zero: no pixel); the sentinel fill runs here
        scope(nullptr, 0);
        api(draw(1), "background draw");
        jet_draw(p4, false, true, scene_camera);
        api(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0), "Clear depth"); // the scene phase; the camera latch
        effect_state(true); // the scene's depth writer (unscoped: forwarded)
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        scope(nullptr, 0);
        api(draw(1), "depth writer");
        jet_draw(p1, false, true, scene_camera);
        jet_draw(p2, false, true, scene_camera);
        jet_draw(p3, false, true, other_camera);
        api(device->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
        api(device->StretchRect(back, nullptr, bloom_surface, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
        Armed a{};
        const unsigned keys[12] = {25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 2, 3};
        unsigned* out[12] = {&a.armed, &a.ran, &a.result, &a.nozzles, &a.skipped, &a.drew, &a.references, &a.taa_references,
                             &a.failures, &a.refused, &a.records, &a.suppressed};
        for (unsigned i = 0; i < 12; ++i) *out[i] = status(device, keys[i]);
        a.resolved = emission(device, 97); // this frame's resolve ran and its copy-back succeeded
        for (unsigned i = 0; i < 4; ++i) a.px[i] = pixel(nozzle_px[i].x, nozzle_px[i].y);
        api(device->EndScene(), "EndScene");
        api(device->SetDepthStencilSurface(depth), "SetDepthStencilSurface rebind");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        return a;
    }
    static unsigned sum(DWORD v) { return ((v >> 16) & 0xff) + ((v >> 8) & 0xff) + (v & 0xff); }
    static bool lit(DWORD v) { return sum(v) >= 96; }
    static bool dark(DWORD v) { return sum(v) <= 6; }
    static void print_armed(const char* phase, unsigned frame, const Armed& a) {
        std::printf("ARMED phase=%s frame=%u armed=%u ran=%u result=%08x nozzles=%u skipped_other_view=%u drew=%u references=%u taa_references=%u failures=%u refused=%u records=%u suppressed=%u resolved=%u px=%06lx,%06lx,%06lx,%06lx\n",
                    phase, frame, a.armed, a.ran, a.result, a.nozzles, a.skipped, a.drew, a.references, a.taa_references,
                    a.failures, a.refused, a.records, a.suppressed, a.resolved, static_cast<unsigned long>(a.px[0]),
                    static_cast<unsigned long>(a.px[1]), static_cast<unsigned long>(a.px[2]), static_cast<unsigned long>(a.px[3]));
    }
    // A frame that drew the scene view's records: p1 / p2 lit, p3 (another camera) and p4 (background phase) dark.
    static bool drawn_frame(const Armed& a) {
        return a.armed && a.ran && a.result == 0 && a.nozzles == 2 && a.skipped == 2 && a.drew && a.references == 5 &&
               a.records == 4 && a.suppressed == 4 && a.resolved && lit(a.px[0]) && lit(a.px[1]) && dark(a.px[2]) && dark(a.px[3]);
    }
    // Frames until the stage is armed (at most `limit`): the unarmed frames before it; the armed frame in *last;
    // *glow_dark counts the unarmed frames that drew nothing while the four records stayed suppressed.
    unsigned until_armed(unsigned limit, unsigned* frame, Armed* last, unsigned* glow_dark) {
        unsigned unarmed = 0;
        *glow_dark = 0;
        for (unsigned i = 0; i < limit; ++i) {
            *last = armed_frame();
            ++*frame;
            if (last->armed) return unarmed;
            ++unarmed;
            *glow_dark += dark(last->px[0]) && dark(last->px[1]) && !last->ran && last->suppressed == 4;
        }
        return unarmed;
    }
    void armed_script() {
        make_armed_jets();
        unsigned frame = 1, glow_dark = 0;
        Armed a{};
        // Warm-up: the first frames latch the main target, the jitter and the camera.
        const unsigned warm = until_armed(8, &frame, &a, &glow_dark);
        print_armed("first", frame, a);
        check(a.armed && warm < 8, "armed_arms_within_8_frames");
        check(drawn_frame(a), "armed_first_frame_draws_scene_view");
        unsigned drawn = 0;
        for (unsigned i = 0; i < 3; ++i) {
            a = armed_frame();
            ++frame;
            drawn += drawn_frame(a);
        }
        print_armed("steady", frame, a);
        check(drawn == 3, "armed_three_frames_draw");
        const unsigned taa_before = a.taa_references;
        // One forced draw fault (the pass's fixture fault reports the draw failed after submitting it, so the frame may
        // still show the plume): the resolve goes on and resolves, the stage disarms for 64 frames (63 after the failed
        // one, the plumes gone), then re-arms and draws again (failures back to 0).
        check(plumes_fault(device, 2) == 1, "armed_fault_once_set");
        a = armed_frame();
        ++frame;
        print_armed("fault_once", frame, a);
        check(a.ran && a.result == 0x80004005u && !a.drew && a.failures == 1 && !a.refused && a.resolved,
              "armed_fault_once_fails_stage_only");
        unsigned disarmed = until_armed(80, &frame, &a, &glow_dark);
        print_armed("rearmed", frame, a);
        std::printf("DISARMED cycle=once frames=%u glow_dark=%u\n", disarmed, glow_dark);
        check(disarmed == 63 && glow_dark == 63, "armed_disarmed_63_frames_after_the_failed_one");
        check(drawn_frame(a) && a.failures == 0, "armed_rearmed_draws_failures_cleared");
        // Persistent fault: three consecutive failed stage frames (no drawn frame between) refuse until Reset.
        check(plumes_fault(device, 4) == 1, "armed_fault_persistent_set");
        unsigned cycles = 0, gaps_ok = 0;
        for (unsigned c = 0; c < 3; ++c) {
            if (c) {
                disarmed = until_armed(80, &frame, &a, &glow_dark);
                std::printf("DISARMED cycle=persistent_%u frames=%u glow_dark=%u\n", c, disarmed, glow_dark);
                gaps_ok += disarmed == 63 && glow_dark == 63;
            } else {
                a = armed_frame();
                ++frame;
            }
            print_armed("fault_persistent", frame, a);
            cycles += a.ran && a.result == 0x80004005u && !a.drew && a.failures == c + 1 && a.refused == (c == 2 ? 1u : 0u);
        }
        check(cycles == 3 && gaps_ok == 2, "armed_three_consecutive_failures_63_frame_gaps");
        unsigned refused_frames = 0;
        for (unsigned i = 0; i < 70; ++i) {
            a = armed_frame();
            ++frame;
            refused_frames += !a.armed && !a.ran && a.refused && dark(a.px[0]) && a.suppressed == 4;
        }
        print_armed("refused", frame, a);
        check(refused_frames == 70, "armed_refused_until_reset_70_frames");
        // Reset: every pass object released, the refusal and the failure count cleared; the next armed frame recreates
        // the pass (5 objects) and draws; the TAA reference count is back where it was.
        check(plumes_fault(device, 0) == 1, "armed_faults_cleared");
        const unsigned held = status(device, 31);
        release_swapchain_surfaces();
        api(device->Reset(&pp), "Reset");
        std::printf("RESET PASS\n");
        const unsigned released = status(device, 31), refused_after = status(device, 34), failures_after = status(device, 33);
        swapchain_surfaces();
        const unsigned warm_after = until_armed(8, &frame, &a, &glow_dark);
        print_armed("after_reset", frame, a);
        std::printf("RESET_CYCLE held=%u released=%u refused_after=%u failures_after=%u warm=%u references=%u taa_before=%u taa_after=%u\n",
                    held, released, refused_after, failures_after, warm_after, a.references, taa_before, a.taa_references);
        check(held == 5 && released == 0 && !refused_after && !failures_after, "armed_reset_releases_and_clears");
        check(drawn_frame(a), "armed_after_reset_recreated_and_draws");
        check(a.taa_references == taa_before, "armed_taa_references_delta_0");
    }
    void overflow_frame(unsigned frame) {
        api(device->BeginScene(), "BeginScene");
        const unsigned before = primitive_calls();
        effect_state(false);
        device->SetVertexShaderConstantF(4, a.rows, 3);
        scope(&a.node, a.serial);
        unsigned failed = 0;
        for (unsigned i = 0; i < ee::ring_capacity + 6; ++i) failed += FAILED(draw(1));
        const unsigned submitted = primitive_calls() - before;
        const Counts n = counts();
        print_frame(frame, n, submitted, "overflow");
        check(failed == 0, "overflow_draws_ok");
        check(n.v[2] == ee::ring_capacity && n.v[3] == ee::ring_capacity && n.v[8] == 6 && submitted == 6, "overflow_1024_recorded_6_forwarded");
        api(device->EndScene(), "EndScene");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
    }
    void reset() {
        api(device->Reset(&pp), "Reset");
        std::printf("RESET PASS\n");
    }
    void warmup() {
        api(device->BeginScene(), "BeginScene");
        api(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0), "Clear");
        api(device->EndScene(), "EndScene");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
    }
    // Per-draw time of a class over n draws (QPC), the scope and state set once.
    double batch(unsigned n, unsigned kind) {
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        if (kind == 0) { // suppressed: the effect pair on a JET node
            effect_state(false);
            device->SetVertexShaderConstantF(4, a.rows, 3);
            scope(&a.node, a.serial);
        } else if (kind == 1) { // not_jet: blended with Z-write off, scoped, a node without the flags (node read included)
            fixed_function(true, false);
            scope(&node_d, 0);
        } else { // not a candidate: the same fixed-function quad with Z-write on, no blending
            fixed_function(false, true);
            scope(&node_d, 0);
        }
        QueryPerformanceCounter(&t0);
        const unsigned primitives = kind == 0 ? 1u : 2u;
        for (unsigned i = 0; i < n; ++i) draw(primitives);
        QueryPerformanceCounter(&t1);
        return double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) / double(n);
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::printf("usage: engine_effects_fixture <vs_effect.bin> <ps_effect.bin> main|native|unverified|unpatched|timing|plumes|armed\n");
        return 2;
    }
    const std::string mode = argv[3];
    const std::vector<char> vs_bytes = slurp(argv[1]), ps_bytes = slurp(argv[2]);
    if (vs_bytes.size() < 8 || ps_bytes.size() < 8) {
        std::printf("RESULT FAIL shader_bytes\n");
        return 2;
    }
    Fixture f;
    f.runtime = LoadLibraryA("d3d9.dll");
    if (!f.runtime) {
        std::printf("RESULT FAIL load\n");
        return 2;
    }
    f.configure = reinterpret_cast<Configure>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_motion_output_fixture_configure")));
    const auto identity = reinterpret_cast<Identity>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_effects_fixture_identity")));
    const auto body_global = reinterpret_cast<BodyGlobal>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_effects_fixture_body_global")));
    const auto redirects = reinterpret_cast<Identity>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_effects_fixture_redirects")));
    f.status = reinterpret_cast<Status>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_effects_fixture_status")));
    f.record = reinterpret_cast<RecordFn>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_effects_fixture_record")));
    f.emission = reinterpret_cast<EmissionStatus>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_linear_emission_fixture_status")));
    f.plumes_fault = reinterpret_cast<PlumesFault>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_plumes_fixture_fault")));
    const auto camera_install = reinterpret_cast<CameraInstall>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_camera_state_fixture_install")));
    if (!f.configure || !identity || !redirects || !body_global || !f.status || !f.record || !f.emission || !f.plumes_fault ||
        !camera_install) {
        std::printf("RESULT FAIL seam_exports\n");
        return 2;
    }
    static BodyManager manager;
    manager.build();
    body_global(reinterpret_cast<std::uintptr_t>(&manager.global));
    if (mode != "unverified") identity(1); // before Direct3DCreate9, which runs initialize
    if (mode != "unpatched") redirects(1);  // the fixture EXE has no engine sites: the patch module stays native
    f.armed = mode == "armed";
    if (f.armed) camera_install(&camera_projection_slot, &camera_view_slot);
    f.create(mode == "timing");
    f.resources(vs_bytes, ps_bytes);
    f.make_jets();
    f.configure(&f.config);
    f.warmup(); // frame 0: the first Present loads engine_bodies.json and resolves the names
    if (mode == "main") {
        const unsigned h = f.status(f.device, 19), s = f.status(f.device, 20);
        check(h == 1 && s == 1, "main_hook_and_suppress_armed");
        for (unsigned frame = 1; frame <= 3; ++frame) f.scenario(frame, "main", true, true);
        f.overflow_frame(4);
        f.scenario(5, "main", true, true);
        f.reset();
        // After the Reset: the shadow resynchronised from the device (c4-6 read back), the hook still armed.
        check(f.status(f.device, 19) == 1, "reset_hook_kept");
        for (unsigned frame = 6; frame <= 8; ++frame) f.scenario(frame, "main", true, true);
        for (unsigned frame = 9; frame <= 11; ++frame) f.scenario(frame, "main", true, true); // past the census's eight frames
        // Frames 12..316 without a candidate: engine_frame rows at most once per 300 such frames (frames 0 and 300).
        for (unsigned frame = 12; frame <= 316; ++frame) f.warmup();
    } else if (mode == "plumes") {
        // plumes on a device without --hdr --taa: suppressed exactly as off (records, pixels), the stage refuses to arm
        // (the runner checks the engine_plumes_state / engine_stage rows).
        check(f.status(f.device, 19) == 1 && f.status(f.device, 20) == 1, "plumes_hook_and_suppress_armed");
        for (unsigned frame = 1; frame <= 2; ++frame) f.scenario(frame, "plumes", true, true);
    } else if (mode == "armed") {
        check(f.status(f.device, 19) == 1 && f.status(f.device, 20) == 1, "armed_hook_and_suppress_armed");
        f.armed_script();
    } else if (mode == "native") {
        check(f.status(f.device, 19) == 1 && f.status(f.device, 20) == 0, "native_census_hook_without_suppression");
        for (unsigned frame = 1; frame <= 2; ++frame) f.scenario(frame, "native", false, false);
    } else if (mode == "unverified") {
        check(f.status(f.device, 19) == 0, "unverified_hook_off");
        f.scenario(1, "unverified", false, false);
    } else if (mode == "unpatched") {
        check(f.status(f.device, 19) == 1 && f.status(f.device, 20) == 1 && f.status(f.device, 24) == 0, "unpatched_armed_without_redirects");
        for (unsigned frame = 1; frame <= 2; ++frame) f.scenario(frame, "unpatched", false, false);
    } else if (mode == "timing") {
        // Warm the three paths, then five frames of 1000 draws each per class; the median per-draw microseconds.
        const unsigned n = 1000;
        std::vector<double> t[3];
        for (unsigned frame = 0; frame < 6; ++frame) {
            api(f.device->BeginScene(), "BeginScene");
            for (unsigned kind = 0; kind < 3; ++kind) {
                const double us = f.batch(n, kind);
                if (frame) t[kind].push_back(us);
            }
            const unsigned suppressed = f.status(f.device, 3);
            api(f.device->EndScene(), "EndScene");
            api(f.device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
            if (frame) check(suppressed == n, "timing_suppressed_count");
        }
        static const char* const names[3] = {"suppressed", "not_jet", "not_candidate"};
        for (unsigned kind = 0; kind < 3; ++kind) {
            std::sort(t[kind].begin(), t[kind].end());
            std::printf("TIMING %s draws=%u frames=%u median_us_per_draw=%.3f min_us=%.3f max_us=%.3f\n", names[kind], n, unsigned(t[kind].size()),
                        t[kind][t[kind].size() / 2], t[kind].front(), t[kind].back());
        }
    } else {
        std::printf("RESULT FAIL mode\n");
        return 2;
    }
    f.release_swapchain_surfaces();
    if (f.readback) f.readback->Release();
    if (f.quad_vb) f.quad_vb->Release();
    if (f.effect_vb) f.effect_vb->Release();
    if (f.decl) f.decl->Release();
    if (f.ps) f.ps->Release();
    if (f.vs) f.vs->Release();
    f.device->Release();
    f.d3d->Release();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
