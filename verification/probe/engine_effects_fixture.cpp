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
//              forced draw fault disarms 64 frames, three consecutive ones refuse until Reset (the game's glow forwarded
//              meanwhile: forwarded_stage_off); Reset releases and the next armed frame recreates the pass;
//              taa_references unchanged across the cycle; far copies (the small-parts cull's handler) drawn by their view
//              handle whatever the selector's phase, another view's skipped, duplicates dropped, a draw-free frame's
//              scene view the most frequent far handle (512x512: one nozzle over the distance law's 12 px)
//   armed_refused  as armed with the FP16 refusal staged before the first attach: the glow jets forwarded natively
//              (forwarded_stage_off 4 per frame) from the frame after the refusal until Reset; then attached and drawn
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
using FarCall = void (*)(std::uint32_t, std::int32_t, std::uint32_t);
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
    FarCall far_call = nullptr; // the small-parts cull stub's far-jet call (x3m_engine_far_jet) on a synthetic node
    IDirect3DTexture9* bloom = nullptr;          // armed: the application's bloom source (the resolve's copy target)
    IDirect3DSurface9* bloom_surface = nullptr;
    IDirect3DSurface9* back = nullptr;           // armed: the back buffer and the auto depth surface
    IDirect3DSurface9* depth = nullptr;
    bool armed = false;
    x3m::MotionOutputFixtureConfig config{};
    Node node_d, node_e; // not_jet (flags 0x200) and the fixed-function JET node
    Jet a, b, c;
    // The ships' root nodes (the plume floor's radius, +0xa4): a and b hang under root_a (radius 5 x their +0x70), c under
    // root_c whose subtree radius is dirty (-1: no floor); root_b is the timing mode's second parent.
    Node root_a, root_b, root_c;
    static constexpr std::uint32_t jet_scale70 = 1000u << 16; // Node::set's +0x70

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
    // The target's side: 64, armed 512 (a nozzle over the distance law's 12 px stays under the near-camera cap's fade
    // band, 0.8 x 0.12 of the height, only from about 300 px up).
    UINT target_size() const { return armed ? 512u : 64u; }
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
        pp.BackBufferWidth = target_size();
        pp.BackBufferHeight = target_size();
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
        api(device->CreateOffscreenPlainSurface(target_size(), target_size(), pp.BackBufferFormat, D3DPOOL_SYSTEMMEM, &readback, nullptr),
            "readback surface");
        if (armed) swapchain_surfaces();
    }
    DWORD pixel(int x = 32, int y = 32) {
        DWORD v = 0;
        const int at[2] = {x, y};
        pixels(at, 1, &v);
        return v;
    }
    // One readback, `n` pixels at (at[2i], at[2i + 1]).
    void pixels(const int* at, unsigned n, DWORD* out) {
        IDirect3DSurface9* target = nullptr;
        api(device->GetRenderTarget(0, &target), "GetRenderTarget");
        api(device->GetRenderTargetData(target, readback), "GetRenderTargetData");
        target->Release();
        D3DLOCKED_RECT lr{};
        api(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY), "readback lock");
        for (unsigned i = 0; i < n; ++i)
            out[i] = reinterpret_cast<const DWORD*>(static_cast<const char*>(lr.pBits) + at[2 * i + 1] * lr.Pitch)[at[2 * i]] & 0xffffffu;
        readback->UnlockRect();
    }
    // armed: the back buffer, the auto depth surface and the bloom source (DEFAULT: released before Reset).
    void swapchain_surfaces() {
        api(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), "GetBackBuffer");
        api(device->GetDepthStencilSurface(&depth), "GetDepthStencilSurface");
        api(device->CreateTexture(target_size(), target_size(), 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &bloom,
                                  nullptr),
            "bloom texture");
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
        root_a.words[0xa4 / 4] = 5u * jet_scale70;
        root_b.words[0xa4 / 4] = 7u * jet_scale70;
        root_c.words[0xa4 / 4] = 0xffffffffu;
        a.node.words[0x18 / 4] = b.node.words[0x18 / 4] = std::uint32_t(reinterpret_cast<std::uintptr_t>(root_a.words));
        c.node.words[0x18 / 4] = std::uint32_t(reinterpret_cast<std::uintptr_t>(root_c.words));
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
            // The ship radius beside the records (the plume floor), in record units: node E has no parent (0); a and b
            // root_a's 5 x +0x70 -> 5 x their size (5,000 and 1,300); c's root is dirty (-1 -> 0). Read only with the
            // floor on (plumes) or for a census row: off past the census's eight frames (main 9..11) reads none (all 0).
            const bool read = std::strcmp(mode, "plumes") == 0 || frame <= 8;
            float radii[4]{};
            for (unsigned i = 0; i < 4; ++i) {
                const std::uint32_t bits = status(device, 40 + i);
                std::memcpy(&radii[i], &bits, 4);
            }
            std::printf("PARENT_RADIUS frame=%u radii=%.3f,%.3f,%.3f,%.3f\n", frame, double(radii[0]), double(radii[1]), double(radii[2]),
                        double(radii[3]));
            check(read ? radii[0] == 0.f && close_to(radii[1], 5.f * a.k) && close_to(radii[2], 5.f * b.k) && radii[3] == 0.f
                       : radii[0] == 0.f && radii[1] == 0.f && radii[2] == 0.f && radii[3] == 0.f,
                  named(read ? "parent_radius" : "parent_radius_not_read"));
        }
        api(device->EndScene(), "EndScene");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
    }
    // armed: four jets 100 units ahead (z 2.0, axis -x: the plume runs left of the nozzle; m00 0.8, m11 4/3 at
    // 512x512): p1, p3 and p4 value 1.25 (a 2.13 px nozzle: the distance law's 0.15), p2 value 8 (a 13.65 px nozzle,
    // over the law's 12 px: whole radiance, under the near-camera cap's fade band).
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
            j.k = i == 1 ? 8.f : 1.25f;
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
            suppressed, resolved, stage_off, attach_refused, jets_submitted;
        unsigned far_records, far_nozzles, far_copies, last_flags; // keys 44..47 (far engine jets, the distance law)
        unsigned view_rule, far_duplicates;                        // keys 48, 49
        DWORD px[6];   // p1..p4, far jets 1 and 2
        DWORD prev[6]; // the same pixels in the previous armed frame
    };
    // One frame in the scene-boundary pattern: p4 in the background phase, p1 / p2 (the scene camera) and p3 (another
    // camera) in the scene phase, the resolve at the bloom copy; the statuses and the nozzle pixels after the copy.
    // armed: two far jets the small-parts cull culled (never drawn by the game): synthetic nodes 100 units ahead at
    // (-20, -20) and (20, -20) with the four jets' orientation (basis row 0 = -z, row 2 = +x: the axis -x), value 1.25
    // (+0x70 125 x context 0.01: a 2.13 px nozzle), z 2.0, handed over with a view of the scene camera's handle or of
    // the other camera's.
    alignas(16) std::uint32_t far_node[2][0x260 / 4]{};
    alignas(16) std::uint32_t far_view[0x80 / 4]{};       // the scene camera's view
    alignas(16) std::uint32_t far_view_other[0x80 / 4]{}; // another camera's view (a target monitor)
    alignas(16) std::uint32_t far_context[0x40 / 4]{};
    void make_far_jets() {
        for (unsigned k = 0; k < 2; ++k) {
            std::uint32_t* n = far_node[k];
            n[0x28 / 4] = 0xf9 + k;
            n[0x70 / 4] = 125;
            n[0x80 / 4] = n[0x84 / 4] = 0x10000;
            n[0x88 / 4] = 0x20000;
            n[0xb0 / 4] = std::uint32_t(k ? 2000 : -2000);
            n[0xb4 / 4] = std::uint32_t(-2000);
            n[0xb8 / 4] = 10000;
            n[0xc8 / 4] = std::uint32_t(-0x10000); // basis row 0 (model x) = -z
            n[0xd4 / 4] = 0x10000;                 // row 1 = y
            n[0xe0 / 4] = 0x10000;                 // row 2 (model z) = +x
            n[0x12c / 4] = 0x1002;
            n[0x130 / 4] = 0x4000001;
            n[0x140 / 4] = 20000;
        }
        const float scale = 0.01f;
        std::memcpy(&far_context[0x2c / 4], &scale, 4);
        far_view[0x28 / 4] = scene_camera;
        far_view[0x1c / 4] = std::uint32_t(reinterpret_cast<std::uintptr_t>(far_context));
        far_view_other[0x28 / 4] = other_camera;
        far_view_other[0x1c / 4] = far_view[0x1c / 4];
    }
    // One far copy of a frame: far jet `jet` (0, 1) through the scene camera's view or the other one's, handed over in
    // the scene phase (after the depth Clear, as the scene view's cull/LOD pass runs after its activation) or before it
    // (the background phase: the main view's pass may run before the selector's latching depth Clear).
    struct Far {
        unsigned jet;
        bool other_view, before_scene;
    };
    void far_copy(const Far& f) {
        far_call(std::uint32_t(reinterpret_cast<std::uintptr_t>(far_node[f.jet])), 6,
                 std::uint32_t(reinterpret_cast<std::uintptr_t>(f.other_view ? far_view_other : far_view)));
    }
    DWORD last_px_[6]{};
    bool profiled_ = false;
    Armed armed_frame(const Far* copies = nullptr, unsigned copy_count = 0, bool draw_jets = true) {
        // p1 probed 2 px into the plume (the axis runs to -x): after flight C the mouth is no longer the brightest point
        // (the shock cells ramp in, the ring halved, the mouth terms a soft maximum), and on this 2 px nozzle half the
        // jittered frames sample behind it; p2 (13.65 px) 4 px into it; the far jets 2 px into theirs. p3 / p4 at their
        // nozzles (never drawn). Nozzle centres: p1 (256, 256), p2 (153.6, 153.6), p3 (358.4, 358.4), p4 (358.4, 153.6),
        // far jets (215.0, 324.3) and (297.0, 324.3).
        static const int probe[12] = {254, 256, 150, 154, 358, 358, 358, 154, 213, 324, 295, 324};
        api(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0), "Clear initial");
        api(device->BeginScene(), "BeginScene");
        effect_state(false); // background: an unscoped effect-pair draw (WVP zero: no pixel); the sentinel fill runs here
        scope(nullptr, 0);
        api(draw(1), "background draw");
        unsigned jets_before = primitive_calls();
        if (draw_jets) jet_draw(p4, false, true, scene_camera);
        unsigned jets_submitted = primitive_calls() - jets_before;
        for (unsigned i = 0; i < copy_count; ++i)
            if (copies[i].before_scene) far_copy(copies[i]);
        api(device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0), "Clear depth"); // the scene phase; the camera latch
        effect_state(true); // the scene's depth writer (unscoped: forwarded)
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        scope(nullptr, 0);
        api(draw(1), "depth writer");
        for (unsigned i = 0; i < copy_count; ++i)
            if (!copies[i].before_scene) far_copy(copies[i]);
        jets_before = primitive_calls();
        if (draw_jets) {
            jet_draw(p1, false, true, scene_camera);
            jet_draw(p2, false, true, scene_camera);
            jet_draw(p3, false, true, other_camera);
        }
        jets_submitted += primitive_calls() - jets_before;
        api(device->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
        api(device->StretchRect(back, nullptr, bloom_surface, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
        Armed a{};
        const unsigned keys[20] = {25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 2, 3, 37, 39, 44, 45, 46, 47, 48, 49};
        unsigned* out[20] = {&a.armed, &a.ran, &a.result, &a.nozzles, &a.skipped, &a.drew, &a.references, &a.taa_references,
                             &a.failures, &a.refused, &a.records, &a.suppressed, &a.stage_off, &a.attach_refused,
                             &a.far_records, &a.far_nozzles, &a.far_copies, &a.last_flags, &a.view_rule, &a.far_duplicates};
        for (unsigned i = 0; i < 20; ++i) *out[i] = status(device, keys[i]);
        a.jets_submitted = jets_submitted;
        a.resolved = emission(device, 97); // this frame's resolve ran and its copy-back succeeded
        pixels(probe, 6, a.px);
        std::memcpy(a.prev, last_px_, sizeof a.prev);
        std::memcpy(last_px_, a.px, sizeof last_px_);
        if (!profiled_ && a.drew && a.nozzles == 2) { // once: p2's axis profile, the probe's choice (PROFILE row)
            profiled_ = true;
            static const int along[16] = {158, 154, 154, 154, 150, 154, 146, 154, 140, 154, 130, 154, 115, 154, 100, 154};
            DWORD v[8];
            pixels(along, 8, v);
            std::printf("PROFILE p2 y=154 x=158,154,150,146,140,130,115,100 sums=%u,%u,%u,%u,%u,%u,%u,%u\n", sum(v[0]), sum(v[1]),
                        sum(v[2]), sum(v[3]), sum(v[4]), sum(v[5]), sum(v[6]), sum(v[7]));
        }
        api(device->EndScene(), "EndScene");
        api(device->SetDepthStencilSurface(depth), "SetDepthStencilSurface rebind");
        api(device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        return a;
    }
    static unsigned sum(DWORD v) { return ((v >> 16) & 0xff) + ((v >> 8) & 0xff) + (v & 0xff); }
    // `drew` (the stage's own report) is the authority on whether a frame drew; the pixels confirm actual writes (the
    // forced fault reports failure after submitting the draw, so a fault frame shows the plume: 765 at p2, measured).
    // p2's 13.65 px nozzle takes the whole radiance: a drawn frame reads 96 and above there (765 measured, the probe
    // 4 px into the plume where the axis profile reads 765). The small nozzles are under the distance law (0.15 of the
    // radiance), where TAA history alone can read as much as a drawn frame, so a far nozzle is checked as a rise against
    // the previous frame where it was absent (rose: at least `rise` above it; the far jets rose 23..48 from 0, p1 66..376,
    // measured 2026-10-03); dark stays at most 6.
    static bool near_lit(DWORD v) { return sum(v) >= 96; }
    static bool lit(DWORD v) { return sum(v) >= 16; }
    static bool dark(DWORD v) { return sum(v) <= 6; }
    static constexpr unsigned rise = 12;
    static bool rose(const Armed& a, unsigned i) { return sum(a.px[i]) >= sum(a.prev[i]) + rise; }
    static void print_armed(const char* phase, unsigned frame, const Armed& a) {
        std::printf("ARMED phase=%s frame=%u armed=%u ran=%u result=%08x nozzles=%u skipped_other_view=%u drew=%u references=%u taa_references=%u failures=%u refused=%u records=%u suppressed=%u resolved=%u forwarded_stage_off=%u attach_refused=%u jets_submitted=%u px=%06lx,%06lx,%06lx,%06lx sums=%u,%u,%u,%u prev=%u,%u\n",
                    phase, frame, a.armed, a.ran, a.result, a.nozzles, a.skipped, a.drew, a.references, a.taa_references,
                    a.failures, a.refused, a.records, a.suppressed, a.resolved, a.stage_off, a.attach_refused, a.jets_submitted,
                    static_cast<unsigned long>(a.px[0]),
                    static_cast<unsigned long>(a.px[1]), static_cast<unsigned long>(a.px[2]), static_cast<unsigned long>(a.px[3]),
                    sum(a.px[0]), sum(a.px[1]), sum(a.px[2]), sum(a.px[3]), sum(a.prev[0]), sum(a.prev[1]));
    }
    static void print_far(const char* phase, unsigned frame, const Armed& a) {
        std::printf("FAR phase=%s frame=%u ran=%u drew=%u nozzles=%u skipped_other_view=%u records=%u suppressed=%u far_records=%u far_nozzles=%u far_copies=%u far_duplicates=%u view_rule=%u last_flags=%04x jets_submitted=%u far_sums=%u,%u prev=%u,%u p1=%u p2=%u\n",
                    phase, frame, a.ran, a.drew, a.nozzles, a.skipped, a.records, a.suppressed, a.far_records, a.far_nozzles,
                    a.far_copies, a.far_duplicates, a.view_rule, a.last_flags, a.jets_submitted, sum(a.px[4]), sum(a.px[5]),
                    sum(a.prev[4]), sum(a.prev[5]), sum(a.px[0]), sum(a.px[1]));
    }
    // A frame that drew the scene view's records: p2 near-lit, p1 lit, p3 (another camera) and p4 (background phase)
    // dark.
    static bool drawn_frame(const Armed& a) {
        return a.armed && a.ran && a.result == 0 && a.nozzles == 2 && a.skipped == 2 && a.drew && a.references == 5 &&
               a.records == 4 && a.suppressed == 4 && a.stage_off == 0 && a.jets_submitted == 0 && a.resolved && lit(a.px[0]) &&
               near_lit(a.px[1]) && dark(a.px[2]) && dark(a.px[3]);
    }
    // A frame whose stage is not attached (refused, failed until Reset or disarmed): nothing drawn by the stage, the
    // four glow-jet draws forwarded to the device (forwarded_stage_off 4, none recorded); the effect pair's WVP is zero,
    // so the forwarded draws leave no pixel.
    static bool glow_native(const Armed& a) {
        return !a.ran && a.stage_off == 4 && a.suppressed == 0 && a.records == 0 && a.jets_submitted == 4 && dark(a.px[0]) &&
               dark(a.px[1]);
    }
    // Frames until the stage is armed (at most `limit`): the unarmed frames before it; the armed frame in *last;
    // *forwarded counts the unarmed frames that forwarded the glow natively (glow_native).
    unsigned until_armed(unsigned limit, unsigned* frame, Armed* last, unsigned* forwarded) {
        unsigned unarmed = 0;
        *forwarded = 0;
        for (unsigned i = 0; i < limit; ++i) {
            *last = armed_frame();
            ++*frame;
            if (last->armed) return unarmed;
            ++unarmed;
            *forwarded += glow_native(*last);
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
        check(rose(a, 0), "armed_first_frame_far_nozzle_rises_from_dark_history");
        unsigned drawn = 0;
        for (unsigned i = 0; i < 3; ++i) {
            a = armed_frame();
            ++frame;
            drawn += drawn_frame(a);
        }
        print_armed("steady", frame, a);
        check(drawn == 3, "armed_three_frames_draw");
        // Far jets (after flight E; the review fixes): the cull stub's copies become records of the view whose handle they
        // carry, whatever the selector's phase when the pass met them; nothing is submitted to the device. Each far frame
        // follows two plain frames (the far probes' TAA history settles), and its far nozzle must rise against the
        // previous frame's pixel (`rose`).
        make_far_jets();
        unsigned settled = 0, settles = 0;
        auto settle = [&] {
            for (unsigned i = 0; i < 2; ++i) {
                a = armed_frame();
                ++frame;
                ++settles;
                settled += drawn_frame(a) && a.far_records == 0 && a.far_copies == 0;
            }
        };
        // The scene view's copy in the scene phase: a fifth record of the scene camera, drawn with p1 / p2.
        const Far scene_in_scene[1] = {{0, false, false}};
        a = armed_frame(scene_in_scene, 1);
        ++frame;
        print_far("scene_copy", frame, a);
        check(a.armed && a.ran && a.result == 0 && a.drew && a.nozzles == 3 && a.skipped == 2 && a.records == 5 && a.suppressed == 4 &&
                  a.far_records == 1 && a.far_copies == 1 && (a.last_flags & 0x200u) && a.jets_submitted == 0 && a.view_rule == 2 &&
                  near_lit(a.px[1]) && rose(a, 4),
              "armed_far_jet_drawn_as_a_scene_record");
        check(a.far_nozzles == 2, "armed_far_jet_distance_law_counts_the_far_nozzles");
        settle();
        // Finding 1: the main view's cull pass before the selector's scene phase (the latching depth Clear): the copy is
        // still drawn (the handle decides, not the phase).
        const Far scene_before[1] = {{0, false, true}};
        a = armed_frame(scene_before, 1);
        ++frame;
        print_far("before_scene", frame, a);
        check(a.drew && a.nozzles == 3 && a.skipped == 2 && a.far_records == 1 && a.view_rule == 2 && rose(a, 4),
              "armed_far_copy_before_the_scene_phase_drawn");
        settle();
        // The cull pass twice for one view: the second copy of (node handle, view handle) is dropped (far_duplicates).
        const Far twice[2] = {{0, false, false}, {0, false, false}};
        a = armed_frame(twice, 2);
        ++frame;
        print_far("duplicate", frame, a);
        check(a.drew && a.nozzles == 3 && a.far_copies == 2 && a.far_records == 1 && a.far_duplicates == 1 && rose(a, 4),
              "armed_far_duplicate_dropped");
        settle();
        // Another camera's copy (a target monitor's cull pass) beside the scene view's draws: skipped, its pixel dark.
        const Far other[1] = {{0, true, false}};
        a = armed_frame(other, 1);
        ++frame;
        print_far("other_view", frame, a);
        check(a.drew && a.nozzles == 2 && a.skipped == 3 && a.far_records == 1 && a.view_rule == 2 && dark(a.px[4]),
              "armed_far_copy_of_another_view_skipped");
        // A frame whose only jet is a far one (no glow-jet draw at all), culled before the scene phase: the stage still
        // runs, the scene view is the far handle (view_rule far) and draws it alone.
        const Far alone[1] = {{0, false, true}};
        a = armed_frame(alone, 1, false);
        ++frame;
        print_far("far_only", frame, a);
        check(a.armed && a.ran && a.result == 0 && a.drew && a.nozzles == 1 && a.skipped == 0 && a.records == 1 &&
                  a.far_records == 1 && a.suppressed == 0 && a.view_rule == 3 && rose(a, 4),
              "armed_far_only_frame_runs_the_stage");
        a = armed_frame();
        ++frame;
        check(drawn_frame(a), "armed_after_far_only_draws");
        settle();
        // Far copies only, of two views: the most frequent far handle is the scene view (two copies of the scene camera,
        // one before the scene phase, against one of the other camera): both scene copies drawn, the other skipped.
        const Far mixed[3] = {{0, false, true}, {1, false, false}, {0, true, false}};
        a = armed_frame(mixed, 3, false);
        ++frame;
        print_far("far_only_mixed", frame, a);
        check(a.drew && a.nozzles == 2 && a.skipped == 1 && a.records == 3 && a.far_records == 3 && a.view_rule == 3 &&
                  rose(a, 4) && rose(a, 5),
              "armed_far_only_majority_far_handle_drawn_other_skipped");
        settle();
        check(settled == settles, "armed_far_settle_frames_draw_without_far_records");
        const unsigned taa_before = a.taa_references;
        // One forced draw fault (the pass's fixture fault reports the draw failed after submitting it, so the frame may
        // still show the plume): the resolve goes on and resolves, the stage disarms for 64 frames (63 after the failed
        // one: no plume, the game's glow forwarded), then re-arms and draws again (failures back to 0).
        check(plumes_fault(device, 2) == 1, "armed_fault_once_set");
        a = armed_frame();
        ++frame;
        print_armed("fault_once", frame, a);
        check(a.ran && a.result == 0x80004005u && !a.drew && a.failures == 1 && !a.refused && a.resolved,
              "armed_fault_once_fails_stage_only");
        unsigned disarmed = until_armed(80, &frame, &a, &glow_dark);
        print_armed("rearmed", frame, a);
        std::printf("DISARMED cycle=once frames=%u glow_native=%u\n", disarmed, glow_dark);
        check(disarmed == 63 && glow_dark == 63, "armed_disarmed_63_frames_glow_forwarded");
        check(drawn_frame(a) && a.failures == 0, "armed_rearmed_draws_failures_cleared");
        check(rose(a, 0), "armed_rearmed_far_nozzle_rises_from_dark_history");
        // Persistent fault: three consecutive failed stage frames (no drawn frame between) refuse until Reset.
        check(plumes_fault(device, 4) == 1, "armed_fault_persistent_set");
        unsigned cycles = 0, gaps_ok = 0;
        for (unsigned c = 0; c < 3; ++c) {
            if (c) {
                disarmed = until_armed(80, &frame, &a, &glow_dark);
                std::printf("DISARMED cycle=persistent_%u frames=%u glow_native=%u\n", c, disarmed, glow_dark);
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
            refused_frames += !a.armed && a.refused && glow_native(a);
        }
        print_armed("refused", frame, a);
        check(refused_frames == 70, "armed_refused_until_reset_70_frames_glow_forwarded");
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
        check(rose(a, 0), "armed_after_reset_far_nozzle_rises_from_dark_history");
        check(a.taa_references == taa_before, "armed_taa_references_delta_0");
    }
    // armed_refused: the pass refuses at its first attach (the FP16 blending fault staged before the first arming): the
    // refusing frame's records stay suppressed (decided before its resolve), every later frame forwards the four glow
    // jets natively (forwarded_stage_off 4, nothing recorded or drawn by the stage) until Reset; after a Reset without
    // the fault the stage attaches, arms and draws.
    void refused_script() {
        make_armed_jets();
        unsigned frame = 1, forwarded = 0;
        check(plumes_fault(device, 1) == 1, "refused_fault_staged_before_attach");
        Armed a{};
        unsigned warm = 0;
        for (; warm < 8; ++warm) {
            a = armed_frame();
            ++frame;
            if (a.attach_refused) break;
        }
        print_armed("refusing", frame, a);
        check(a.attach_refused == 1 && !a.armed && !a.ran && a.suppressed == 4 && a.stage_off == 0 && a.references == 0,
              "refused_at_first_attach_suppressed_that_frame");
        unsigned native = 0;
        for (unsigned i = 0; i < 40; ++i) {
            a = armed_frame();
            ++frame;
            native += !a.armed && a.attach_refused && glow_native(a);
        }
        print_armed("refused", frame, a);
        std::printf("REFUSED frames=40 glow_native=%u forwarded_stage_off=%u\n", native, a.stage_off);
        check(native == 40, "refused_40_frames_glow_forwarded_4_each");
        check(plumes_fault(device, 0) == 1, "refused_fault_cleared");
        release_swapchain_surfaces();
        api(device->Reset(&pp), "Reset");
        std::printf("RESET PASS\n");
        const unsigned refused_after = status(device, 39);
        swapchain_surfaces();
        const unsigned warm_after = until_armed(8, &frame, &a, &forwarded);
        print_armed("after_reset", frame, a);
        std::printf("REFUSED_RESET refused_after=%u warm=%u forwarded_during_warm=%u\n", refused_after, warm_after, forwarded);
        check(refused_after == 0 && forwarded == 0, "refused_reset_clears_the_refusal");
        check(drawn_frame(a), "refused_after_reset_attaches_and_draws");
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
        } else if (kind == 2) { // not a candidate: the same fixed-function quad with Z-write on, no blending
            fixed_function(false, true);
            scope(&node_d, 0);
        } else { // 3: suppressed, the parent cycling through five roots: every draw misses the radius memo (one read);
                 // 4: suppressed without a parent (no radius read: the path before the plume floor); 5: suppressed, the
                 // parent alternating between two roots (interleaved ships: a one-entry memo misses, a recent list hits)
            effect_state(false);
            device->SetVertexShaderConstantF(4, a.rows, 3);
            scope(&a.node, a.serial);
        }
        const std::uint32_t parents[5] = {std::uint32_t(reinterpret_cast<std::uintptr_t>(root_a.words)),
                                          std::uint32_t(reinterpret_cast<std::uintptr_t>(root_b.words)),
                                          std::uint32_t(reinterpret_cast<std::uintptr_t>(root_c.words)),
                                          std::uint32_t(reinterpret_cast<std::uintptr_t>(node_d.words)),
                                          std::uint32_t(reinterpret_cast<std::uintptr_t>(node_e.words))};
        QueryPerformanceCounter(&t0);
        const unsigned primitives = kind == 0 || kind >= 3 ? 1u : 2u;
        if (kind == 4) a.node.words[0x18 / 4] = 0;
        if (kind == 3)
            for (unsigned i = 0; i < n; ++i) {
                a.node.words[0x18 / 4] = parents[i % 5];
                draw(primitives);
            }
        else if (kind == 5)
            for (unsigned i = 0; i < n; ++i) {
                a.node.words[0x18 / 4] = parents[i & 1];
                draw(primitives);
            }
        else
            for (unsigned i = 0; i < n; ++i) draw(primitives);
        QueryPerformanceCounter(&t1);
        a.node.words[0x18 / 4] = parents[0];
        return double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart) / double(n);
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::printf("usage: engine_effects_fixture <vs_effect.bin> <ps_effect.bin> main|native|unverified|unpatched|timing|plumes|armed|armed_refused\n");
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
    f.far_call = reinterpret_cast<FarCall>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_engine_far_jets_fixture_call")));
    const auto camera_install = reinterpret_cast<CameraInstall>(reinterpret_cast<void*>(GetProcAddress(f.runtime, "x3m_camera_state_fixture_install")));
    if (!f.configure || !identity || !redirects || !body_global || !f.status || !f.record || !f.emission || !f.plumes_fault ||
        !f.far_call || !camera_install) {
        std::printf("RESULT FAIL seam_exports\n");
        return 2;
    }
    static BodyManager manager;
    manager.build();
    body_global(reinterpret_cast<std::uintptr_t>(&manager.global));
    if (mode != "unverified") identity(1); // before Direct3DCreate9, which runs initialize
    if (mode != "unpatched") redirects(1);  // the fixture EXE has no engine sites: the patch module stays native
    f.armed = mode == "armed" || mode == "armed_refused";
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
    } else if (mode == "armed_refused") {
        check(f.status(f.device, 19) == 1 && f.status(f.device, 20) == 1, "refused_hook_and_suppress_armed");
        f.refused_script();
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
        // Warm the four paths, then five frames of 1000 draws each per class; the median per-draw microseconds. The
        // five-parent, parentless and two-parent classes run in frames of their own (the ring holds 1,024 records).
        const unsigned n = 1000;
        std::vector<double> t[6];
        for (unsigned frame = 0; frame < 6; ++frame) {
            api(f.device->BeginScene(), "BeginScene");
            for (unsigned kind = 0; kind < 3; ++kind) {
                const double us = f.batch(n, kind);
                if (frame) t[kind].push_back(us);
            }
            unsigned suppressed = f.status(f.device, 3);
            api(f.device->EndScene(), "EndScene");
            api(f.device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
            if (frame) check(suppressed == n, "timing_suppressed_count");
            for (unsigned kind = 3; kind < 6; ++kind) {
                api(f.device->BeginScene(), "BeginScene");
                const double us = f.batch(n, kind);
                if (frame) t[kind].push_back(us);
                suppressed = f.status(f.device, 3);
                api(f.device->EndScene(), "EndScene");
                api(f.device->Present(nullptr, nullptr, nullptr, nullptr), "Present");
                if (frame)
                    check(suppressed == n, kind == 3   ? "timing_new_parent_suppressed_count"
                                           : kind == 4 ? "timing_no_parent_suppressed_count"
                                                       : "timing_two_parents_suppressed_count");
            }
        }
        static const char* const names[6] = {"suppressed", "not_jet", "not_candidate", "suppressed_new_parent", "suppressed_no_parent",
                                             "suppressed_two_parents"};
        for (unsigned kind = 0; kind < 6; ++kind) {
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
