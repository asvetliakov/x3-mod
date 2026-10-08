// Occlusion cull fixture (docs/architecture/occlusion-cull.md): the production pass (src/renderer/occlusion_cull_pass.cpp)
// and batcher (src/proxy/occlusion_cull_core.h) on a real device.
//
//   occlusion_cull_fixture.exe <d3d9 path | builtin> [cost repeats, default 1]
//
// Part 1, the functional scene (256 x 256, run per target shape: the HDR scene target A16B16G16R16F with the motion
// route's lazy RT1 (A32B32G32R32F, a -0.0 lane included) and RT2 (R32F) bound through the scene, with each rectangle
// geometry (dynamic, managed, discard, constants), and A8R8G8B8 alone with the production one), once with the cull and once without, frame by frame, every
// target read back. Clip space, w = 1, CULLMODE cycling NONE/CW/CCW per frame for the parts; one ship: the hull covers
// the left half at z 0.3 until frame K, then only its upper half (the "mover" part below is revealed with its rectangle
// unchanged, so the pass skips it once more: the one late frame); three parts sit behind the hull, two right of it, one
// in front of it, one right of it with alpha test on, and a "teleport" part jumps from behind the hull to the right half
// at frame T (the stability guard draws it at once). After frame R the device is Reset (the pass releases and recreates
// its queries and a dynamic buffer; the batcher's results are reset; the first frame after draws everything). Frame NR
// is presented without the GPU wait the other frames get, so the next frame may read results not ready (drawn,
// counted). Asserts: hidden parts skipped from frame 2 on (frame 0 lists them, frame 1's block tests them; except after
// the Reset and after a not-ready read), visible parts always drawn, the teleport drawn at T, the mover skipped through
// K and drawn from K + 1 (drawn_late 1), the frames identical with and without the cull except frame K (the difference
// inside the mover's rectangle), the application state restored after every part (shaders, declaration, stream, the
// changed render states, cull mode, c252/c253 untouched), the query pool back after the Reset, and the device's
// reference count unchanged by attach/detach. Six frames back to back without readback or GPU wait follow: a skip only
// ever follows a ready hidden result.
//
// Part 1b, the engine-side skip's verdict table (occlusion_cull = engine; src/proxy/occlusion_engine_core.h) driven by
// the pass's verdicts on the same scene: the duty cycle, the guards and the fall-back to drawn (see engine_duty_cycle).
//
// Part 2, the realistic state (occlusion_cull_scene_inc.h: 1280 x 720 HDR MRT binding, a real vs/ps pair bound before
// every block, three ships of four hull pieces and 50 parts, 111 hidden and 39 visible, one ship drifting, a hull piece
// removed at frame X = 3K + 2): the cull on and off interleaved frame by frame with every target read back. Asserts:
// hidden parts skipped from frame 2, visible parts never skipped, the revealed parts drawn from X + 1 (X + 2 when frame
// X + 1 read results not ready), every visible part re-tested exactly once per K frames over 3K frames on its own
// phase and the per-frame visible re-test count within +-50 % of N/K, images and RT1/RT2 byte-identical except frame X
// (and X + 1 when not ready) inside the revealed parts. Then the cost, paced at 16 ms without readback: the cull as it
// runs (K = 8), every part tested every frame (K = 1, verdict ignored) and one part per ship (K = 1, verdict ignored),
// per rectangle geometry (dynamic, managed, discard, constants), and the scene without the pass, interleaved and repeated: COST rows per repeat (test_us = QPC
// inside the blocks, frame_us = CPU from BeginScene to EndScene) and a DERIVED row per buffer from the medians over the
// repeats (per test, per block, frame overhead; cpu = BeginScene to the return of Present, which also catches a
// command-stream back-pressure the pass's work moved into or out of the frame). Documented D3D9 only.
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../../src/renderer/occlusion_cull_pass.h"
#include "../../src/proxy/occlusion_engine_core.h"
#include "occlusion_cull_scene_inc.h"

namespace {
namespace oc = x3m::occlusion_cull::core;
using x3m::renderer::OcclusionCullBuffer;
using x3m::renderer::OcclusionCullPart;
using x3m::renderer::OcclusionCullPass;
using x3m::renderer::OcclusionCullState;
using x3m::renderer::OcclusionCullVerdict;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", what, ok ? "PASS" : "FAIL");
}
// The batcher's slot of a draw key (as note_part probes), or -1.
int entry_of(const oc::Batcher& b, std::uint64_t key) {
    unsigned at = oc::slot_of(std::uint32_t(key) ^ std::uint32_t(key >> 32), oc::part_slots);
    for (unsigned probe = 0; probe < oc::part_probe; ++probe, at = (at + 1) & (oc::part_slots - 1))
        if (b.parts[at].seen && b.parts[at].key == key) return int(at);
    return -1;
}
bool tested_now(const oc::Batcher& b, std::uint64_t key) {
    const int e = entry_of(b, key);
    return e >= 0 && b.parts[e].tested == b.frame;
}
constexpr int kSize = 256, kFrames = 14, kK = 7, kT = 5, kR = 10, kNR = 8;
constexpr std::uint32_t kShip = 0x1000, kHullNode = 0x2000;
constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
// The "game" programs: vs_3_0 position pass-through, ps_3_0 oC0 = c0.
constexpr DWORD game_vs[] = {0xfffe0300u, 0x0200001fu, 0x80000000u, 0x900f0000u, 0x0200001fu, 0x80000000u,
                             0xe00f0000u, 0x02000001u, 0xe00f0000u, 0x90e40000u, 0x0000ffffu};
constexpr DWORD game_ps[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x0000ffffu};
// The routed shape: oC0 = c0, oC1 = c1 (the motion lane), oC2 = c2 (the depth lane).
constexpr DWORD game_mrt_ps[] = {0xffff0300u, 0x02000001u, 0x800f0800u, 0xa0e40000u, 0x02000001u, 0x800f0801u,
                                 0xa0e40001u, 0x02000001u, 0x800f0802u, 0xa0e40002u, 0x0000ffffu};
enum Kind { hidden, visible, front, mover, teleport, alpha };
struct Part {
    const char* name;
    Kind kind;
    float x0, y0, x1, y1, z0, z1;
    float color[4];
};
const Part parts[] = {
    {"hidden1", hidden, -0.9f, 0.2f, -0.6f, 0.5f, 0.5f, 0.7f, {1, 0, 0, 1}},
    {"hidden2", hidden, -0.5f, 0.6f, -0.2f, 0.9f, 0.5f, 0.7f, {0, 1, 0, 1}},
    {"hidden3", hidden, -0.9f, 0.6f, -0.6f, 0.9f, 0.5f, 0.7f, {0, 0, 1, 1}},
    {"visible1", visible, 0.2f, 0.2f, 0.5f, 0.5f, 0.5f, 0.7f, {1, 1, 0, 1}},
    {"visible2", visible, 0.6f, 0.6f, 0.9f, 0.9f, 0.5f, 0.7f, {0, 1, 1, 1}},
    {"front", front, -0.8f, -0.2f, -0.5f, 0.1f, 0.1f, 0.2f, {1, 0, 1, 1}},
    {"mover", mover, -0.6f, -0.7f, -0.3f, -0.4f, 0.5f, 0.7f, {1, .5f, 0, 1}},
    {"teleport", teleport, -0.5f, 0.2f, -0.2f, 0.5f, 0.5f, 0.7f, {.5f, 0, 1, 1}},
    {"alpha", alpha, 0.2f, -0.9f, 0.5f, -0.6f, 0.5f, 0.7f, {.5f, 1, .5f, 1}},
};
constexpr int kParts = int(sizeof parts / sizeof parts[0]);
constexpr float teleport_after[4] = {0.3f, -0.5f, 0.6f, -0.2f}; // x0 y0 x1 y1 from frame T
// Box corners (k bit 0 x, bit 1 y, bit 2 z) and the twelve triangles.
constexpr int box_index[36] = {0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7, 0, 4, 1, 1, 4, 5,
                               2, 3, 6, 6, 3, 7, 0, 2, 4, 4, 2, 6, 1, 5, 3, 3, 5, 7};
struct V {
    float x, y, z, w;
};
void box(V* out, float x0, float y0, float x1, float y1, float z0, float z1) {
    for (int t = 0; t < 36; ++t) {
        const int k = box_index[t];
        out[t] = {(k & 1) ? x1 : x0, (k & 2) ? y1 : y0, (k & 4) ? z1 : z0, 1};
    }
}
void quad(V* out, float x0, float y0, float x1, float y1, float z) { // two triangles, both windings drawn (cull NONE)
    const V a{x0, y0, z, 1}, b{x1, y0, z, 1}, c{x0, y1, z, 1}, d{x1, y1, z, 1};
    out[0] = a, out[1] = b, out[2] = c, out[3] = c, out[4] = b, out[5] = d;
}
// Vertex buffer layout: [0] hull full (6), [6] hull upper (6), then per part a box (36), then the teleport's second box.
constexpr int hull_full = 0, hull_upper = 6, parts_base = 12, teleport_second = parts_base + kParts * 36,
              vertex_count = teleport_second + 36;
oc::Box part_box(int i, int frame) {
    const Part& p = parts[i];
    if (p.kind == teleport && frame >= kT)
        return {{teleport_after[0], teleport_after[1], p.z0}, {teleport_after[2], teleport_after[3], p.z1}};
    return {{p.x0, p.y0, p.z0}, {p.x1, p.y1, p.z1}};
}
std::uint64_t part_key(int i) {
    return oc::draw_key(std::uint32_t(0x3000 + i * 0x40), 0, 0, 36, 7);
}

struct Device {
    IDirect3DDevice9* d = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    IDirect3DSurface9 *rt = nullptr, *sys = nullptr, *depth = nullptr;
    // The route's lazy RT1 (A32B32G32R32F) and RT2 (R32F), bound through the scene when mrt (the fp16 run).
    IDirect3DSurface9 *rt1 = nullptr, *rt2 = nullptr, *sys1 = nullptr, *sys2 = nullptr;
    IDirect3DPixelShader9* mrt_ps = nullptr;
    bool mrt = false;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    D3DFORMAT format = D3DFMT_A8R8G8B8;
    bool targets(bool create) {
        if (rt) rt->Release(), rt = nullptr;
        if (depth) depth->Release(), depth = nullptr;
        if (rt1) rt1->Release(), rt1 = nullptr;
        if (rt2) rt2->Release(), rt2 = nullptr;
        if (!create) return true;
        return SUCCEEDED(d->CreateRenderTarget(kSize, kSize, format, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
               SUCCEEDED(d->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                      &depth, nullptr)) &&
               (!mrt || (SUCCEEDED(d->CreateRenderTarget(kSize, kSize, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0,
                                                         FALSE, &rt1, nullptr)) &&
                         SUCCEEDED(d->CreateRenderTarget(kSize, kSize, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                         &rt2, nullptr))));
    }
};
ULONG references(IDirect3DDevice9* d) {
    d->AddRef();
    return d->Release();
}
void wait_gpu(IDirect3DDevice9* d) {
    IDirect3DQuery9* q = nullptr;
    if (FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT, &q)) || !q) return;
    q->Issue(D3DISSUE_END);
    const DWORD start = GetTickCount();
    BOOL done = FALSE;
    while (q->GetData(&done, sizeof done, D3DGETDATA_FLUSH) == S_FALSE && GetTickCount() - start < 5000) Sleep(0);
    q->Release();
}

struct FrameLog {
    bool drawn[kParts], tested[kParts];
    x3m::renderer::OcclusionCullFrameStats stats;
    std::vector<unsigned char> image, image1, image2; // RT0, and RT1 / RT2 when bound
};
bool read_target(IDirect3DDevice9* d, IDirect3DSurface9* rt, IDirect3DSurface9* sys, unsigned bytes_per_pixel,
                 std::vector<unsigned char>& out) {
    if (FAILED(d->GetRenderTargetData(rt, sys))) return false;
    D3DLOCKED_RECT lr{};
    if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    const unsigned row = kSize * bytes_per_pixel;
    out.resize(std::size_t(row) * kSize);
    for (int y = 0; y < kSize; ++y)
        std::memcpy(out.data() + std::size_t(y) * row, static_cast<const char*>(lr.pBits) + y * lr.Pitch, row);
    sys->UnlockRect();
    return true;
}
// The application's state after a part's call into the pass: what the block changes must be back.
bool state_back(Device& dev, DWORD cull, bool alpha_test, const float reserved[8]) {
    IDirect3DDevice9* d = dev.d;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT offset = 1, stride = 0;
    DWORD zw = 0, ab = 1, sb = 0, db = 1, at = 2, cm = 0;
    float c[8]{};
    d->GetVertexShader(&vs);
    d->GetPixelShader(&ps);
    d->GetVertexDeclaration(&decl);
    d->GetStreamSource(0, &vb, &offset, &stride);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    d->GetRenderState(D3DRS_SRCBLEND, &sb);
    d->GetRenderState(D3DRS_DESTBLEND, &db);
    d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
    d->GetRenderState(D3DRS_CULLMODE, &cm);
    d->GetVertexShaderConstantF(252, c, 2);
    const bool same = vs == dev.vs && ps == (dev.mrt ? dev.mrt_ps : dev.ps) && decl == dev.decl && vb == dev.vb &&
                      offset == 0 && stride == sizeof(V) && zw == TRUE && ab == FALSE && sb == D3DBLEND_ONE &&
                      db == D3DBLEND_ZERO && at == DWORD(alpha_test) && cm == cull && !std::memcmp(c, reserved, sizeof c);
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (decl) decl->Release();
    if (vb) vb->Release();
    return same;
}

// One run of kFrames frames; pass = the cull decides (with its batcher). Returns false on a setup failure.
bool run(Device& dev, OcclusionCullPass* pass, oc::Batcher* batcher, FrameLog (&log)[kFrames], unsigned* state_failures,
         unsigned* resets_ok) {
    IDirect3DDevice9* d = dev.d;
    const DWORD culls[3] = {D3DCULL_NONE, D3DCULL_CW, D3DCULL_CCW};
    const float reserved[8] = {7, 7, 7, 7, 9, 9, 9, 9}; // the "application's" c252/c253: never touched by the pass
    for (int frame = 0; frame < kFrames; ++frame) {
        FrameLog& f = log[frame];
        const std::uint32_t stamp = std::uint32_t(frame + 1);
        if (pass) {
            pass->begin_frame(stamp);
            batcher->begin(stamp);
        }
        d->SetRenderTarget(0, dev.rt);
        d->SetRenderTarget(1, dev.mrt ? dev.rt1 : nullptr);
        d->SetRenderTarget(2, dev.mrt ? dev.rt2 : nullptr);
        d->SetDepthStencilSurface(dev.depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        d->BeginScene();
        d->SetVertexDeclaration(dev.decl);
        d->SetStreamSource(0, dev.vb, 0, sizeof(V));
        d->SetVertexShader(dev.vs);
        d->SetPixelShader(dev.mrt ? dev.mrt_ps : dev.ps);
        {
            const float minus_zero = -0.0f;
            const float lane1[4] = {minus_zero, 2.5f, -1.0f, 1e-30f}, lane2[4] = {0.75f, 0, 0, 1};
            d->SetPixelShaderConstantF(1, lane1, 1);
            d->SetPixelShaderConstantF(2, lane2, 1);
        }
        d->SetVertexShaderConstantF(252, reserved, 2);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        const float grey[4] = {.4f, .4f, .4f, 1};
        d->SetPixelShaderConstantF(0, grey, 1);
        d->DrawPrimitive(D3DPT_TRIANGLELIST, frame < kK ? hull_full : hull_upper, 2);
        // The hull piece's first draw: its ship's signature (the hull's geometry changes at K) and reference rows.
        if (pass) batcher->note_hull(kShip, kHullNode, oc::draw_key(kHullNode, 0, frame < kK ? hull_full : hull_upper, 6, 5), identity);
        for (int i = 0; i < kParts; ++i) {
            const Part& p = parts[i];
            const DWORD cull = culls[frame % 3];
            d->SetRenderState(D3DRS_CULLMODE, cull);
            if (p.kind == alpha) {
                d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                d->SetRenderState(D3DRS_ALPHAREF, 0x80);
                d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            }
            d->SetPixelShaderConstantF(0, p.color, 1);
            bool skip = false;
            f.tested[i] = false;
            if (pass) {
                OcclusionCullPart part{};
                part.box = part_box(i, frame);
                if (oc::test_rect(identity, part.box, kSize, kSize, oc::cmp_lessequal, &part.rect) == oc::RectStatus::ok) {
                    part.key = part_key(i);
                    part.ship = kShip;
                    part.model = 7;
                    part.rows = identity;
                    part.hull_drawn = true;
                    part.zfunc = oc::cmp_lessequal;
                    part.vp_width = part.vp_height = float(kSize);
                    part.targets_key = dev.mrt ? 3u : 0u;
                    OcclusionCullState state{};
                    state.z_write = TRUE;
                    state.alpha_test = p.kind == alpha;
                    state.cull = cull;
                    state.reserved = reserved; // the application's c252/c253 (put back in the constants mode)
                    HRESULT restore = S_OK;
                    const auto verdict = pass->part(*batcher, part, state, &restore);
                    skip = verdict == OcclusionCullVerdict::skip;
                    if (FAILED(restore) || !state_back(dev, cull, p.kind == alpha, reserved)) ++*state_failures;
                }
            }
            f.drawn[i] = !skip;
            if (!skip) {
                const int start = p.kind == teleport && frame >= kT ? teleport_second : parts_base + i * 36;
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(start), 12);
            }
            if (p.kind == alpha) d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        d->EndScene();
        if (pass) {
            f.stats = pass->frame_stats();
            pass->frame_stats() = {};
            for (int i = 0; i < kParts; ++i) f.tested[i] = tested_now(*batcher, part_key(i));
        }
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        if (!read_target(d, dev.rt, dev.sys, dev.format == D3DFMT_A16B16G16R16F ? 8 : 4, f.image)) return false;
        if (dev.mrt && (!read_target(d, dev.rt1, dev.sys1, 16, f.image1) || !read_target(d, dev.rt2, dev.sys2, 4, f.image2)))
            return false;
        d->Present(nullptr, nullptr, nullptr, nullptr);
        if (frame != kNR) wait_gpu(d);
        if (frame == kR) { // Reset: the DEFAULT targets and the pass's queries (and dynamic buffer) go first
            if (pass) {
                pass->before_reset();
                batcher->reset_results();
            }
            dev.targets(false);
            const HRESULT hr = d->Reset(&dev.pp);
            if (pass) {
                pass->after_reset(hr);
                if (!pass->recreate_pending() || FAILED(pass->recreate())) return false; // the owner's lazy recreation
            }
            if (SUCCEEDED(hr) && dev.targets(true) && (!pass || pass->queries() == oc::pool_size)) ++*resets_ok;
            else
                return false;
        }
    }
    return true;
}

// Back-to-back frames without readback or GPU wait (the readback above synchronises): the next frame's reads may find
// results not ready. Every part is drawn unless its previous result was ready and hidden.
void unready_frames(Device& dev, OcclusionCullPass& pass, oc::Batcher& batcher, unsigned* not_ready, unsigned* skipped,
                    unsigned* ready_decisions, unsigned* tested) {
    IDirect3DDevice9* d = dev.d;
    for (int frame = 0; frame < 6; ++frame) {
        const std::uint32_t stamp = std::uint32_t(kFrames + 1 + frame);
        pass.begin_frame(stamp);
        batcher.begin(stamp);
        d->SetRenderTarget(0, dev.rt);
        d->SetDepthStencilSurface(dev.depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        d->BeginScene();
        d->SetVertexDeclaration(dev.decl);
        d->SetStreamSource(0, dev.vb, 0, sizeof(V));
        d->SetVertexShader(dev.vs);
        d->SetPixelShader(dev.ps);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->DrawPrimitive(D3DPT_TRIANGLELIST, hull_full, 2);
        batcher.note_hull(kShip, kHullNode, oc::draw_key(kHullNode, 0, hull_full, 6, 5), identity);
        for (int i = 0; i < kParts; ++i) {
            OcclusionCullPart part{};
            part.box = part_box(i, 0);
            if (oc::test_rect(identity, part.box, kSize, kSize, oc::cmp_lessequal, &part.rect) != oc::RectStatus::ok)
                continue;
            part.key = part_key(i);
            part.ship = kShip;
            part.model = 7;
            part.rows = identity;
            part.hull_drawn = true;
            part.vp_width = part.vp_height = float(kSize);
            OcclusionCullState state{};
            state.cull = D3DCULL_NONE;
            HRESULT restore = S_OK;
            if (pass.part(batcher, part, state, &restore) != OcclusionCullVerdict::skip)
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(parts_base + i * 36), 12);
        }
        d->EndScene();
        d->Present(nullptr, nullptr, nullptr, nullptr);
        const auto& st = pass.frame_stats();
        *not_ready += st.not_ready;
        *skipped += st.skipped;
        *ready_decisions += st.age1 + st.age2;
        *tested += st.tested;
        if (st.skipped > st.age1 + st.age2) *ready_decisions = 0, *skipped = ~0u; // a skip without a ready result: fail
        pass.frame_stats() = {};
    }
    wait_gpu(d);
}

// ---- part 1b: the engine-side skip's verdict table on the production pass (occlusion_cull = engine) ----
// The duty-cycle probe's table (occlusion_engine_core.h) driven by the pass's verdicts on the part-1 scene, A8R8G8B8,
// the production buffer, kEngineFrames frames with a GPU wait each (results ready). Per frame the table is published
// from the previous frame's ledger (as the sector view's Clear does); a part the table lists at its position, model and
// the frame's stamp is engine-skipped: not visited, not drawn, not listed (mark E). Every other part goes through the
// pass as in part 1 (mark s = proxy-skipped, D = drawn). The parts' "view-space positions" are synthetic integers:
// hidden3 drifts 1/256 of its magnitude per frame (inside the 1/64 window), hidden1 drifts 1/32 per frame from frame
// W on (outside: the position guard draws it from W + 1 on); hidden2 has a second, visible draw under its node (never
// fully skipped: partial); hidden3's model id changes at frame M (the model guard; the new draw key is a new part);
// at frame S every lookup uses the wrong stamp (the stamp guard); the hull shrinks at H (the mover revealed). Asserts:
// an engine skip only ever follows a frame where the proxy skipped every draw of the node; visible parts never E;
// hidden1 before W alternates proxy-skip and engine-skip except on its phase frame (stamp mod K, the engine skip withheld) and at S;
// after W it is never E and the position guard counts every frame; the partial node is never E; the guards' counts;
// the mover drawn from H + 2 on (the duty cycle reveals one frame later than the draw-level cull when the reveal
// lands on a proxy frame: its last test was issued before the hull changed).
namespace oe = x3m::occlusion_cull::engine;
constexpr int kEngineFrames = 30, kEngineK = 4, kEngineW = 19, kEngineM = 11, kEngineS = 14, kEngineH = 23;
constexpr std::uint32_t kEngineCamera = 0x41c68200u;
std::uint32_t engine_node(int i) {
    return std::uint32_t(0x3000 + i * 0x40); // part_key's node
}
void engine_pos(int i, int frame, std::int32_t out[3]) {
    const std::int32_t base = 200000 + i * 7000;
    out[0] = base;
    out[1] = -40000 + i * 300;
    out[2] = 600000;
    if (i == 2) out[0] = base + frame * (600000 / 256); // hidden3: inside the window every frame
    if (i == 0 && frame >= kEngineW) out[1] = -40000 + (frame - kEngineW + 1) * (600000 / 32); // hidden1: outside
}
void engine_duty_cycle(Device& dev, void* const* native) {
    IDirect3DDevice9* d = dev.d;
    dev.format = D3DFMT_A8R8G8B8;
    dev.mrt = false;
    if (!dev.targets(true)) {
        check(false, "engine: targets");
        return;
    }
    OcclusionCullPass pass;
    oc::Batcher* batcher = new oc::Batcher();
    batcher->retest = kEngineK;
    oe::Ledger* ledger = new oe::Ledger();
    oe::Table* table = new oe::Table();
    const HRESULT hr = pass.attach(d, native);
    check(SUCCEEDED(hr), "engine: attach (production buffer)");
    if (FAILED(hr)) {
        delete batcher;
        delete ledger;
        delete table;
        dev.targets(false);
        return;
    }
    const int h1 = 0, h2 = 1, h3 = 2, mi = 6;
    static char mark[kEngineFrames][kParts];
    unsigned published_total = 0, withheld_total = 0, partial_total = 0, overflow_total = 0, e_total = 0, e_after_full = 0;
    unsigned rej_model_total = 0, rej_stamp_total = 0, rej_pos_total = 0, rej_model_at_m = 0, rej_stamp_at_s = 0, published_at_s = 0;
    unsigned rej_pos_after_w = 0, visible_e = 0, partial_e = 0, h1_cycle_ok = 0, h1_cycle_bad = 0, h1_e_before_w = 0, h1_e_after_w = 0;
    unsigned skipped_draws_total = 0, state_failures = 0, errors = 0;
    int current_frame = 0;
    auto read_pos = [&](std::uintptr_t at, void* out, std::size_t n) {
        for (int i = 0; i < kParts; ++i)
            if (at == engine_node(i) + oe::position_offset && n == 12) {
                std::int32_t p[3];
                engine_pos(i, current_frame, p);
                std::memcpy(out, p, 12);
                return true;
            }
        return false;
    };
    const float reserved[8] = {7, 7, 7, 7, 9, 9, 9, 9};
    for (int frame = 0; frame < kEngineFrames; ++frame) {
        current_frame = frame;
        const std::uint32_t stamp = std::uint32_t(frame + 1);
        oe::PublishStats st{};
        unsigned published = 0;
        if (frame > 0 && ledger->frame == stamp - 1) published = table->publish(*ledger, stamp, kEngineK, &st);
        published_total += published;
        withheld_total += st.withheld;
        partial_total += st.partial;
        overflow_total += st.overflow;
        if (frame == kEngineS) published_at_s = published;
        pass.begin_frame(stamp);
        batcher->begin(stamp);
        ledger->begin(stamp, kEngineCamera);
        d->SetRenderTarget(0, dev.rt);
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        d->SetDepthStencilSurface(dev.depth);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x80336699, 1.0f, 0);
        d->BeginScene();
        d->SetVertexDeclaration(dev.decl);
        d->SetStreamSource(0, dev.vb, 0, sizeof(V));
        d->SetVertexShader(dev.vs);
        d->SetPixelShader(dev.ps);
        d->SetVertexShaderConstantF(252, reserved, 2);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        const float grey[4] = {.4f, .4f, .4f, 1};
        d->SetPixelShaderConstantF(0, grey, 1);
        const bool full_hull = frame < kEngineH;
        d->DrawPrimitive(D3DPT_TRIANGLELIST, full_hull ? hull_full : hull_upper, 2);
        batcher->note_hull(kShip, kHullNode, oc::draw_key(kHullNode, 0, full_hull ? hull_full : hull_upper, 6, 5), identity);
        unsigned rej_model = 0, rej_stamp = 0, rej_pos = 0;
        for (int i = 0; i < kParts; ++i) {
            const Part& p = parts[i];
            const std::uint32_t node = engine_node(i);
            const std::uint32_t model = i == h3 && frame >= kEngineM ? 8u : 7u;
            std::int32_t pos[3];
            engine_pos(i, frame, pos);
            const std::uint32_t lookup_stamp = frame == kEngineS ? stamp + 1 : stamp;
            const oe::Verdict verdict = table->lookup(node, model, pos, lookup_stamp);
            if (verdict == oe::Verdict::model) ++rej_model;
            if (verdict == oe::Verdict::stamp) ++rej_stamp;
            if (verdict == oe::Verdict::position) ++rej_pos;
            if (verdict == oe::Verdict::skip) { // the engine's pass culls the node: no visit, no draw, no ledger entry
                mark[frame][i] = 'E';
                ++e_total;
                if (const oe::Entry* e = table->find(node)) skipped_draws_total += e->draws;
                continue;
            }
            // The draw path: the part (and hidden2's second, visible draw under the same node).
            oe::Ledger::Slot* slot = ledger->draw(node);
            d->SetPixelShaderConstantF(0, p.color, 1);
            if (p.kind == alpha) {
                d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                d->SetRenderState(D3DRS_ALPHAREF, 0x80);
                d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            }
            bool skip = false;
            OcclusionCullPart part{};
            part.box = part_box(i, 0); // the part-1 teleport stays put here
            if (oc::test_rect(identity, part.box, kSize, kSize, oc::cmp_lessequal, &part.rect) == oc::RectStatus::ok) {
                part.key = oc::draw_key(node, 0, 0, 36, model);
                part.ship = kShip;
                part.model = model;
                part.rows = identity;
                part.hull_drawn = true;
                part.zfunc = oc::cmp_lessequal;
                part.vp_width = part.vp_height = float(kSize);
                OcclusionCullState state{};
                state.z_write = TRUE;
                state.alpha_test = p.kind == alpha;
                state.cull = D3DCULL_NONE;
                state.reserved = reserved;
                HRESULT restore = S_OK;
                skip = pass.part(*batcher, part, state, &restore) == OcclusionCullVerdict::skip;
                if (FAILED(restore) || !state_back(dev, D3DCULL_NONE, p.kind == alpha, reserved)) ++state_failures;
            }
            if (skip)
                ledger->skipped(slot, model, read_pos);
            else
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(parts_base + i * 36), 12);
            mark[frame][i] = skip ? 's' : 'D';
            if (i == h2) { // the companion: visible1's geometry under hidden2's node, another draw key
                oe::Ledger::Slot* slot2 = ledger->draw(node);
                OcclusionCullPart companion{};
                companion.box = part_box(3, 0);
                bool skip2 = false;
                if (oc::test_rect(identity, companion.box, kSize, kSize, oc::cmp_lessequal, &companion.rect) == oc::RectStatus::ok) {
                    companion.key = oc::draw_key(node, 0, std::uint32_t(parts_base + 3 * 36), 36, model);
                    companion.ship = kShip;
                    companion.model = model;
                    companion.rows = identity;
                    companion.hull_drawn = true;
                    companion.zfunc = oc::cmp_lessequal;
                    companion.vp_width = companion.vp_height = float(kSize);
                    OcclusionCullState state{};
                    state.z_write = TRUE;
                    state.cull = D3DCULL_NONE;
                    state.reserved = reserved;
                    HRESULT restore = S_OK;
                    skip2 = pass.part(*batcher, companion, state, &restore) == OcclusionCullVerdict::skip;
                }
                if (skip2)
                    ledger->skipped(slot2, model, read_pos);
                else
                    d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(parts_base + 3 * 36), 12);
            }
            if (p.kind == alpha) d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        d->EndScene();
        d->Present(nullptr, nullptr, nullptr, nullptr);
        wait_gpu(d);
        const auto& stats = pass.frame_stats();
        errors += stats.errors + stats.failed;
        pass.frame_stats() = {};
        rej_model_total += rej_model;
        rej_stamp_total += rej_stamp;
        rej_pos_total += rej_pos;
        if (frame == kEngineM) rej_model_at_m = rej_model;
        if (frame == kEngineS) rej_stamp_at_s = rej_stamp;
        if (frame > kEngineW) rej_pos_after_w += rej_pos;
        char marks[kParts + 1]{};
        for (int i = 0; i < kParts; ++i) marks[i] = mark[frame][i];
        std::printf("ENGINE frame=%d marks=%s published=%u withheld=%u partial=%u rejected=%u,%u,%u skipped_draws=%u\n", frame,
                    marks, published, st.withheld, st.partial, rej_model, rej_stamp, rej_pos, skipped_draws_total);
    }
    // The rules over the marks.
    for (int f = 0; f < kEngineFrames; ++f)
        for (int i = 0; i < kParts; ++i) {
            const Kind k = parts[i].kind;
            if (mark[f][i] == 'E') {
                if (f > 0 && mark[f - 1][i] == 's') ++e_after_full;
                if (k == visible || k == front || k == alpha) ++visible_e;
                if (i == h2) ++partial_e;
                if (i == h1) ++(f < kEngineW ? h1_e_before_w : h1_e_after_w);
            }
        }
    // hidden1 before W: after an E comes an s; after an s comes an E unless the frame is its forced phase or S.
    for (int f = 3; f < kEngineW; ++f) {
        const std::uint32_t stamp = std::uint32_t(f + 1);
        const bool forced = stamp % kEngineK == oc::retest_phase(engine_node(h1), 7, kEngineK);
        const char expected = mark[f - 1][h1] == 'E' ? 's' : (forced || f == kEngineS) ? 's' : 'E';
        if (mark[f][h1] == expected)
            ++h1_cycle_ok;
        else
            ++h1_cycle_bad;
    }
    bool mover_drawn = true;
    for (int f = kEngineH + 2; f < kEngineFrames; ++f) mover_drawn = mover_drawn && mark[f][mi] == 'D';
    const bool mover_hidden_before = mark[kEngineH - 1][mi] != 'D' && mark[kEngineH - 2][mi] != 'D';
    std::printf("ENGINESUMMARY e_total=%u e_after_full=%u visible_e=%u partial_e=%u h1_cycle=%u/%u h1_e_before_w=%u "
                "h1_e_after_w=%u rej_pos_after_w=%u rej_model=%u rej_model_at_m=%u rej_stamp=%u rej_stamp_at_s=%u "
                "published_at_s=%u published=%u withheld=%u partial=%u overflow=%u skipped_draws=%u mover_drawn_from_h2=%u "
                "state_failures=%u errors=%u rej_pos=%u\n",
                e_total, e_after_full, visible_e, partial_e, h1_cycle_ok, h1_cycle_ok + h1_cycle_bad, h1_e_before_w, h1_e_after_w,
                rej_pos_after_w, rej_model_total, rej_model_at_m, rej_stamp_total, rej_stamp_at_s, published_at_s, published_total,
                withheld_total, partial_total, overflow_total, skipped_draws_total, mover_drawn ? 1u : 0u, state_failures, errors,
                rej_pos_total);
    check(e_total >= 12 && e_after_full == e_total, "engine: every engine skip follows a frame where the proxy skipped every draw of the node");
    check(visible_e == 0, "engine: visible, in-front and alpha-tested parts are never engine-skipped");
    check(partial_e == 0 && partial_total >= 10, "engine: a node with an unskipped draw is never listed (partial, counted)");
    check(h1_cycle_bad == 0 && h1_e_before_w >= 5 && withheld_total >= 1,
          "engine: the duty cycle alternates proxy-skip and engine-skip, withheld on the node's phase frame and at the stale stamp");
    // After W hidden1 is listed every frame but its forced-phase frames (one in K), and rejected on every listing.
    const unsigned after_w = unsigned(kEngineFrames - kEngineW - 1);
    check(h1_e_after_w == 0 && rej_pos_after_w >= after_w - (after_w + kEngineK - 1) / kEngineK - 1 && mark[kEngineW + 1][h1] == 's',
          "engine: position guard: a node outside its window is not skipped by the engine (falls back to the proxy's verdict)");
    check(rej_model_total == rej_model_at_m && rej_model_at_m == (mark[kEngineM - 1][h3] == 's' ? 1u : 0u) && mark[kEngineM][h3] == 'D',
          "engine: model guard: a changed model id is not skipped; the new draw key draws untested");
    check(rej_stamp_total == rej_stamp_at_s && rej_stamp_at_s == published_at_s && published_at_s >= 1,
          "engine: stamp guard: every listed node rejects a stale stamp");
    check(mover_hidden_before && mover_drawn, "engine: the revealed part falls back to drawn (from H + 2 at the latest)");
    check(overflow_total == 0 && state_failures == 0 && errors == 0 && skipped_draws_total == e_total,
          "engine: no overflow, state back after every part, no query error, skipped draws = one per engine-skipped node");
    pass.detach();
    delete batcher;
    delete ledger;
    delete table;
    dev.targets(false);
}

// ---- part 2: the realistic state ----
namespace scene = occlusion_scene;
constexpr unsigned kRetest = 8;
constexpr int kRealFrames = 3 * int(kRetest) + 8, kReveal = 3 * int(kRetest) + 2;
struct RealHooks {
    OcclusionCullPass* pass = nullptr;
    oc::Batcher* batcher = nullptr;
    scene::Scene* sc = nullptr;
    bool honour = true, one_per_ship = false, check_state = false;
    bool skipped[scene::ships][scene::parts_per_ship]{};
    unsigned state_failures = 0;
    static std::uint32_t root(int s) { return 0x10000000u + std::uint32_t(s) * 0x10000u; }
    static std::uint32_t hull_node(int s, int piece) { return root(s) + 0x100u + std::uint32_t(piece) * 0x200u; }
    static std::uint64_t key(int s, int i) {
        return oc::draw_key(root(s) + 0x2000u + std::uint32_t(i) * 0x150u, 2, std::uint32_t(scene::parts_base + i * 36), 36,
                            900000u + std::uint32_t(i));
    }
    void hull(int s, int piece, const float* rows) {
        if (pass)
            batcher->note_hull(root(s), hull_node(s, piece),
                               oc::draw_key(hull_node(s, piece), 2, std::uint32_t(piece * 6), 6, 500u + std::uint32_t(piece)), rows);
    }
    bool part(int s, int i, const float* rows, const scene::PartDef& pd) {
        skipped[s][i] = false;
        if (!pass || (one_per_ship && i)) return false;
        OcclusionCullPart p{};
        p.box = oc::Box{{pd.lo[0], pd.lo[1], pd.lo[2]}, {pd.hi[0], pd.hi[1], pd.hi[2]}};
        if (oc::test_rect(rows, p.box, float(scene::width), float(scene::height), oc::cmp_lessequal, &p.rect) !=
            oc::RectStatus::ok)
            return false;
        p.key = key(s, i);
        p.ship = root(s);
        p.model = 900000u + std::uint32_t(i);
        p.rows = rows;
        p.hull_drawn = true;
        p.zfunc = oc::cmp_lessequal;
        p.vp_width = float(scene::width);
        p.vp_height = float(scene::height);
        OcclusionCullState state{};
        state.z_write = TRUE;
        state.cull = D3DCULL_CCW;
        HRESULT restore = S_OK;
        const auto verdict = pass->part(*batcher, p, state, &restore);
        if (check_state) {
            IDirect3DDevice9* d = sc->d;
            IDirect3DVertexShader9* vs = nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            IDirect3DVertexDeclaration9* decl = nullptr;
            IDirect3DVertexBuffer9* vb = nullptr;
            UINT offset = 1, stride = 0;
            DWORD zw = 0, ab = 1, cm = 0;
            d->GetVertexShader(&vs);
            d->GetPixelShader(&ps);
            d->GetVertexDeclaration(&decl);
            d->GetStreamSource(0, &vb, &offset, &stride);
            d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
            d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
            d->GetRenderState(D3DRS_CULLMODE, &cm);
            if (FAILED(restore) || vs != sc->vs || ps != sc->ps || decl != sc->decl || vb != sc->vb || offset ||
                stride != sizeof(scene::V) || zw != TRUE || ab != FALSE || cm != D3DCULL_CCW)
                ++state_failures;
            if (vs) vs->Release();
            if (ps) ps->Release();
            if (decl) decl->Release();
            if (vb) vb->Release();
        }
        skipped[s][i] = honour && verdict == OcclusionCullVerdict::skip;
        return skipped[s][i];
    }
};
struct NoHooks {
    void hull(int, int, const float*) {}
    bool part(int, int, const float*, const scene::PartDef&) { return false; }
};
// The parts of ship 0 behind its first hull piece (upper left), revealed from frame kReveal.
bool revealed(int s, int i) {
    const scene::PartDef pd = scene::part_def(i);
    return s == 0 && pd.hidden && pd.lo[0] < 0.f && pd.hi[1] > 0.f;
}
// Pixels differing between two images, and those outside the revealed parts' rectangles (2 px margin).
unsigned compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, unsigned bpp, unsigned* outside) {
    if (a.size() != b.size()) return 0xffffffffu;
    struct R {
        int x0, y0, x1, y1;
    } rects[scene::parts_per_ship];
    int n = 0;
    for (int i = 0; i < scene::parts_per_ship; ++i)
        if (revealed(0, i)) {
            const scene::PartDef pd = scene::part_def(i);
            const float tx = scene::ship_x(0);
            rects[n++] = {int((pd.lo[0] + tx + 1) * .5f * scene::width) - 2, int((1 - pd.hi[1]) * .5f * scene::height) - 2,
                          int((pd.hi[0] + tx + 1) * .5f * scene::width) + 2, int((1 - pd.lo[1]) * .5f * scene::height) + 2};
        }
    unsigned diff = 0;
    for (int y = 0; y < scene::height; ++y)
        for (int x = 0; x < scene::width; ++x) {
            const std::size_t at = (std::size_t(y) * scene::width + x) * bpp;
            if (!std::memcmp(a.data() + at, b.data() + at, bpp)) continue;
            ++diff;
            bool inside = false;
            for (int r = 0; r < n && !inside; ++r)
                inside = x >= rects[r].x0 && x <= rects[r].x1 && y >= rects[r].y0 && y <= rects[r].y1;
            if (!inside) ++*outside;
        }
    return diff;
}

void realistic_functional(IDirect3DDevice9* d, void* const* native, scene::Scene& sc) {
    OcclusionCullPass pass;
    oc::Batcher* batcher = new oc::Batcher();
    batcher->retest = kRetest;
    HRESULT hr = pass.attach(d, native);
    check(SUCCEEDED(hr), "realistic: attach (production buffer)");
    if (FAILED(hr)) {
        delete batcher;
        return;
    }
    RealHooks hooks;
    hooks.pass = &pass;
    hooks.batcher = batcher;
    hooks.sc = &sc;
    hooks.check_state = true;
    NoHooks none;
    std::vector<unsigned char> off[3], on[3];
    unsigned tests[scene::ships][scene::parts_per_ship]{}, hidden_expected = 0, hidden_skips = 0, visible_skips = 0;
    unsigned diff_frames = 0, diff_outside = 0, lane_diff_frames = 0, errors = 0, failed = 0, skip_without_ready = 0;
    unsigned visible_min = ~0u, visible_max = 0;
    int diff_last = -1, reveal_drawn_at = -1;
    bool reveal_late_ok = true, not_ready_at[kRealFrames + 1]{};
    std::vector<double> test_us;
    unsigned visible_parts = 0;
    for (int s = 0; s < scene::ships; ++s)
        for (int i = 0; i < scene::parts_per_ship; ++i) visible_parts += scene::part_def(i).hidden ? 0 : 1;
    for (int f = 0; f < kRealFrames; ++f) {
        sc.frame(f, kReveal, none);
        bool ok = sc.readback(off);
        const std::uint32_t stamp = std::uint32_t(f + 1);
        pass.begin_frame(stamp);
        batcher->begin(stamp);
        sc.frame(f, kReveal, hooks);
        const auto st = pass.frame_stats();
        pass.frame_stats() = {};
        ok = ok && sc.readback(on);
        if (!ok) {
            check(false, "realistic: readback");
            break;
        }
        not_ready_at[f] = st.not_ready > 0;
        errors += st.errors;
        failed += st.failed;
        if (st.skipped > st.age1 + st.age2) ++skip_without_ready;
        test_us.push_back(double(st.test_ns) / 1000.0);
        unsigned visible_tested = 0;
        bool reveal_all_drawn = true;
        for (int s = 0; s < scene::ships; ++s)
            for (int i = 0; i < scene::parts_per_ship; ++i) {
                const bool t = tested_now(*batcher, RealHooks::key(s, i));
                const scene::PartDef pd = scene::part_def(i);
                const bool sk = hooks.skipped[s][i];
                if (f >= 2 && f < 2 + 3 * int(kRetest)) {
                    tests[s][i] += t ? 1 : 0;
                    if (!pd.hidden && t) ++visible_tested;
                }
                if (!pd.hidden) visible_skips += sk ? 1 : 0;
                const bool exposed = revealed(s, i) && f >= kReveal;
                if (pd.hidden && !exposed && f >= 2 && !st.not_ready) {
                    ++hidden_expected;
                    hidden_skips += sk ? 1 : 0;
                }
                if (revealed(s, i) && f > kReveal) {
                    // drawn from X + 1; X + 2 when frame X + 1 read results not ready
                    if (sk && !(f == kReveal + 1 && not_ready_at[f])) reveal_late_ok = false;
                    if (sk) reveal_all_drawn = false;
                }
            }
        if (f > kReveal && reveal_all_drawn && reveal_drawn_at < 0) reveal_drawn_at = f;
        if (f >= 2 && f < 2 + 3 * int(kRetest)) {
            visible_min = visible_tested < visible_min ? visible_tested : visible_min;
            visible_max = visible_tested > visible_max ? visible_tested : visible_max;
        }
        unsigned outside = 0;
        const unsigned d0 = compare(on[0], off[0], 8, &outside), d1 = compare(on[1], off[1], 16, &outside),
                       d2 = compare(on[2], off[2], 4, &outside);
        diff_outside += outside;
        if (d0) ++diff_frames, diff_last = f;
        if (d1 || d2) ++lane_diff_frames;
        std::printf("REALFRAME frame=%d candidates=%u tested=%u visible_tested=%u cadence=%u forced=%u retest_skipped=%u "
                    "blocks=%u skipped=%u not_ready=%u ready_age=%u,%u,%u stale=%u test_us=%.1f diff_px=%u rt1_diff=%u "
                    "rt2_diff=%u\n",
                    f, st.candidates, st.tested, visible_tested, st.cadence, st.forced, st.retest_skipped, st.blocks,
                    st.skipped, st.not_ready, st.age1, st.age2, st.age_none, st.stale, double(st.test_ns) / 1000.0, d0,
                    d1, d2);
    }
    unsigned exact = 0, visible_count = 0, hidden_full = 0, hidden_count = 0;
    for (int s = 0; s < scene::ships; ++s)
        for (int i = 0; i < scene::parts_per_ship; ++i)
            if (!scene::part_def(i).hidden) {
                ++visible_count;
                exact += tests[s][i] == 3 ? 1 : 0;
            } else {
                ++hidden_count;
                hidden_full += tests[s][i] == 3 * kRetest ? 1 : 0;
            }
    const double per = double(visible_parts) / double(kRetest);
    std::printf("REALSUMMARY hidden_skips=%u hidden_expected=%u visible_skips=%u visible_exactly_3=%u/%u hidden_every_frame=%u/%u "
                "visible_tested_per_frame=%u..%u n_over_k=%.2f reveal_drawn_at=%d diff_frames=%u diff_last=%d "
                "diff_outside=%u lane_diff_frames=%u state_failures=%u errors=%u failed=%u test_us_p50=%.1f\n",
                hidden_skips, hidden_expected, visible_skips, exact, visible_count, hidden_full, hidden_count,
                visible_min, visible_max, per, reveal_drawn_at, diff_frames, diff_last, diff_outside, lane_diff_frames,
                hooks.state_failures, errors, failed, scene::percentile(test_us, 0.5));
    check(hidden_expected >= 100u * 20u && hidden_skips == hidden_expected,
          "realistic: hidden parts skipped from frame 2 (every frame with ready results)");
    check(visible_skips == 0, "realistic: visible parts never skipped");
    check(reveal_late_ok && reveal_drawn_at >= 0 && reveal_drawn_at <= kReveal + 2,
          "realistic: revealed parts drawn from X + 1 (X + 2 when not ready)");
    check(exact == visible_count && hidden_full == hidden_count,
          "realistic: over 3K frames every visible part tested exactly 3 times (once per K), every hidden part every frame");
    check(double(visible_min) >= 0.5 * per && double(visible_max) <= 1.5 * per,
          "realistic: visible re-tests per frame within +-50 % of N/K (staggered phases)");
    const bool late_frames_only = diff_last <= kReveal + 1 && diff_frames <= 2 && (diff_frames < 2 || not_ready_at[kReveal + 1]);
    check(diff_outside == 0 && late_frames_only && lane_diff_frames == diff_frames,
          "realistic: RT0, RT1 and RT2 byte-identical on and off except the late frame(s), inside the revealed parts");
    check(hooks.state_failures == 0 && errors == 0 && failed == 0 && skip_without_ready == 0,
          "realistic: state back after every part, no query error, no failed block, skips only on ready results");
    pass.detach();
    delete batcher;
}

// One timing configuration: 20 warm-up and 200 measured frames, paced at 16 ms, no readback.
const char* buffer_name(OcclusionCullBuffer b) {
    return b == OcclusionCullBuffer::dynamic   ? "dynamic"
           : b == OcclusionCullBuffer::managed ? "managed"
           : b == OcclusionCullBuffer::discard ? "discard"
                                               : "constants";
}
struct Cost {
    double tests = 0, blocks = 0, test_us = 0, test_us_p90 = 0, frame_us = 0, frame_us_p90 = 0, cpu_us = 0, skipped = 0;
};
Cost timing(IDirect3DDevice9* d, void* const* native, scene::Scene& sc, OcclusionCullBuffer buffer, const char* config,
            bool use_pass, unsigned retest, bool honour, bool one_per_ship, int repeat) {
    OcclusionCullPass pass;
    oc::Batcher* batcher = new oc::Batcher();
    batcher->retest = retest;
    Cost c{};
    if (use_pass && FAILED(pass.attach(d, native, buffer))) {
        delete batcher;
        std::printf("COST impl=batched buffer=%s config=%s attach=failed\n",
                    buffer_name(buffer), config);
        return c;
    }
    RealHooks hooks;
    hooks.pass = use_pass ? &pass : nullptr;
    hooks.batcher = batcher;
    hooks.sc = &sc;
    hooks.honour = honour;
    hooks.one_per_ship = one_per_ship;
    std::vector<double> cpu, tests, blocks, tus, fus, skipped;
    scene::Pacer pacer;
    for (int f = 0; f < 220; ++f) {
        const std::uint32_t stamp = std::uint32_t(f + 1);
        if (use_pass) {
            pass.begin_frame(stamp);
            batcher->begin(stamp);
        }
        const double frame_us = sc.frame(f % 16, 1 << 30, hooks); // the drift cycles; no reveal
        LARGE_INTEGER pf{}, p0{}, p1{};
        QueryPerformanceFrequency(&pf);
        QueryPerformanceCounter(&p0);
        d->Present(nullptr, nullptr, nullptr, nullptr);
        QueryPerformanceCounter(&p1);
        const double present_us = double(p1.QuadPart - p0.QuadPart) * 1e6 / double(pf.QuadPart);
        const auto st = pass.frame_stats();
        pass.frame_stats() = {};
        if (f >= 20) {
            tests.push_back(st.tested);
            blocks.push_back(st.blocks);
            tus.push_back(double(st.test_ns) / 1000.0);
            fus.push_back(frame_us);
            cpu.push_back(frame_us + present_us);
            skipped.push_back(st.skipped);
        }
        pacer.wait();
    }
    c.tests = scene::percentile(tests, .5);
    c.blocks = scene::percentile(blocks, .5);
    c.test_us = scene::percentile(tus, .5);
    c.test_us_p90 = scene::percentile(tus, .9);
    c.frame_us = scene::percentile(fus, .5);
    c.frame_us_p90 = scene::percentile(fus, .9);
    c.cpu_us = scene::percentile(cpu, .5);
    c.skipped = scene::percentile(skipped, .5);
    std::printf("COST impl=batched repeat=%d buffer=%s config=%s frames=200 tests_p50=%.0f blocks_p50=%.0f skipped_p50=%.0f "
                "test_us_p50=%.1f test_us_p90=%.1f frame_us_p50=%.1f frame_us_p90=%.1f cpu_us_p50=%.1f\n",
                repeat, use_pass ? (buffer_name(buffer)) : "-", config, c.tests,
                c.blocks, c.skipped, c.test_us, c.test_us_p90, c.frame_us, c.frame_us_p90, c.cpu_us);
    if (use_pass) pass.detach();
    delete batcher;
    return c;
}

LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}

void functional(Device& dev, void* const* native) {
    IDirect3DDevice9* d = dev.d;
    struct Shape {
        D3DFORMAT format;
        OcclusionCullBuffer buffer;
        const char* name;
    };
    const Shape shapes[] = {{D3DFMT_A16B16G16R16F, OcclusionCullBuffer::dynamic, "fp16_mrt_dynamic"},
                            {D3DFMT_A16B16G16R16F, OcclusionCullBuffer::managed, "fp16_mrt_managed"},
                            {D3DFMT_A16B16G16R16F, OcclusionCullBuffer::discard, "fp16_mrt_discard"},
                            {D3DFMT_A16B16G16R16F, OcclusionCullBuffer::constants, "fp16_mrt_constants"},
                            {D3DFMT_A8R8G8B8, OcclusionCullPass::default_buffer, "argb8"}};
    for (const Shape& shape : shapes) {
        // fp16: the HDR scene target with the route's lazy RT1/RT2 bound through the scene; argb8: one target.
        const char* fname = shape.name;
        const D3DFORMAT format = shape.format;
        dev.format = format;
        dev.mrt = format == D3DFMT_A16B16G16R16F;
        if (dev.sys) dev.sys->Release(), dev.sys = nullptr;
        const bool ok = dev.targets(true) &&
                        SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, format, D3DPOOL_SYSTEMMEM, &dev.sys, nullptr));
        if (!ok) {
            std::printf("FORMAT name=%s skipped=targets\n", fname);
            continue;
        }
        static FrameLog on[kFrames], off[kFrames];
        char label[160];
        unsigned state_failures = 0, resets_on = 0, resets_off = 0;
        const ULONG refs_before = references(d);
        OcclusionCullPass pass;
        oc::Batcher* batcher = new oc::Batcher();
        batcher->retest = kRetest;
        HRESULT hr = pass.attach(d, native, shape.buffer);
        std::printf("ATTACH format=%s hr=%08lx reason=%s queries=%u references_added=%ld\n", fname, (unsigned long)hr,
                    pass.reason(), pass.queries(), long(references(d)) - long(refs_before));
        std::snprintf(label, sizeof label, "%s: attach: the pool of 1,024 queries", fname);
        check(SUCCEEDED(hr) && pass.queries() == oc::pool_size, label);
        if (FAILED(hr)) {
            delete batcher;
            continue;
        }
        const bool ran_on = run(dev, &pass, batcher, on, &state_failures, &resets_on);
        unsigned nr = 0, nr_skipped = 0, nr_ready = 0, nr_tested = 0;
        if (ran_on) unready_frames(dev, pass, *batcher, &nr, &nr_skipped, &nr_ready, &nr_tested);
        std::printf("UNREADY format=%s frames=6 tested=%u not_ready=%u ready_decisions=%u skipped=%u\n", fname, nr_tested,
                    nr, nr_ready, nr_skipped);
        std::snprintf(label, sizeof label, "%s: back-to-back frames: a skip only on a ready hidden result (not ready = drawn)",
                      fname);
        check(ran_on && nr_tested > 0 && nr_skipped <= nr_ready, label);
        pass.detach();
        delete batcher;
        std::snprintf(label, sizeof label, "%s: detach: the device's reference count is back", fname);
        check(references(d) == refs_before, label);
        const bool ran_off = run(dev, nullptr, nullptr, off, &state_failures, &resets_off);
        std::snprintf(label, sizeof label, "%s: both runs completed, one Reset each", fname);
        check(ran_on && ran_off && resets_on == 1 && resets_off == 1, label);
        if (!ran_on || !ran_off) continue;
        unsigned diff_frames = 0, diff_outside_mover = 0, late = 0, errors = 0, skipped_over_hidden = 0;
        unsigned lane_diff_frames = 0, lane_diff_outside = 0;
        int diff_frame = -1, lane_diff_frame = -1;
        // Texels that differ between the runs, and those outside the mover's rectangle (x -0.6..-0.3, y -0.7..-0.4;
        // window y down; one pixel margin).
        auto compare_small = [&](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, unsigned bpp,
                                 unsigned* outside) {
            unsigned diff = 0;
            if (a.size() != b.size()) return 0xffffffffu;
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x) {
                    const std::size_t at = (std::size_t(y) * kSize + x) * bpp;
                    if (std::memcmp(a.data() + at, b.data() + at, bpp)) {
                        ++diff;
                        const float nx = (x + .5f) / kSize * 2 - 1, ny = 1 - (y + .5f) / kSize * 2;
                        if (nx < -0.61f || nx > -0.29f || ny < -0.71f || ny > -0.39f) ++*outside;
                    }
                }
            return diff;
        };
        for (int f = 0; f < kFrames; ++f) {
            char mask[kParts + 1]{}, tmask[kParts + 1]{};
            for (int i = 0; i < kParts; ++i) mask[i] = on[f].drawn[i] ? 'D' : 's', tmask[i] = on[f].tested[i] ? 'T' : '.';
            const unsigned diff =
                compare_small(on[f].image, off[f].image, format == D3DFMT_A16B16G16R16F ? 8 : 4, &diff_outside_mover);
            const unsigned diff1 = dev.mrt ? compare_small(on[f].image1, off[f].image1, 16, &lane_diff_outside) : 0;
            const unsigned diff2 = dev.mrt ? compare_small(on[f].image2, off[f].image2, 4, &lane_diff_outside) : 0;
            if (diff) ++diff_frames, diff_frame = f;
            if (diff1 || diff2) ++lane_diff_frames, lane_diff_frame = f;
            const auto& s = on[f].stats;
            late += s.drawn_late;
            errors += s.errors;
            if (s.skipped > s.age1 + s.age2) ++skipped_over_hidden; // a skip needs a ready result
            std::printf("FRAME format=%s frame=%d drawn=%s tested=%s candidates=%u tested=%u blocks=%u retest_skipped=%u "
                        "skipped=%u hidden=%u visible=%u not_ready=%u ready_age=%u,%u,%u errors=%u drawn_late=%u "
                        "unstable=%u pool_truncated=%u diff_px=%u rt1_diff=%u rt2_diff=%u\n",
                        fname, f, mask, tmask, s.candidates, s.tested, s.blocks, s.retest_skipped, s.skipped, s.hidden,
                        s.visible, s.not_ready, s.age1, s.age2, s.age_none, s.errors, s.drawn_late, s.unstable,
                        s.pool_truncated, diff, diff1, diff2);
        }
        // Expected skips: a hidden part from frame 2 on (frame 0 lists it, frame 1's block tests it), except the first
        // frame after the Reset and a frame whose previous results were not ready (counted by the pass).
        unsigned hidden_skips = 0, hidden_expected = 0, visible_skips = 0;
        for (int f = 0; f < kFrames; ++f) {
            const bool fresh = f == kR + 1, unready = on[f].stats.not_ready > 0;
            for (int i = 0; i < kParts; ++i) {
                const Kind k = parts[i].kind;
                if (k == visible || k == front || k == alpha) visible_skips += on[f].drawn[i] ? 0 : 1;
                if (k == hidden && f >= 2 && !fresh && !unready) {
                    ++hidden_expected;
                    hidden_skips += on[f].drawn[i] ? 0 : 1;
                }
            }
        }
        const int mi = 6, ti = 7;
        const bool not_ready_frames_ok = on[kNR + 1].stats.not_ready == 0 || on[kNR + 1].stats.skipped == 0 ||
                                         on[kNR + 1].stats.skipped <= on[kNR + 1].stats.hidden;
        std::printf("SUMMARY format=%s hidden_skips=%u hidden_expected=%u visible_skips=%u teleport_at_T=%c "
                    "mover_at_K=%c mover_after_K=%c drawn_late=%u diff_frames=%u diff_frame=%d diff_outside_mover=%u "
                    "state_failures=%u errors=%u not_ready_after_NR=%u fresh_skipped=%u lane_diff_frames=%u "
                    "lane_diff_frame=%d lane_diff_outside=%u\n",
                    fname, hidden_skips, hidden_expected, visible_skips, on[kT].drawn[ti] ? 'D' : 's',
                    on[kK].drawn[mi] ? 'D' : 's', on[kK + 1].drawn[mi] ? 'D' : 's', late, diff_frames, diff_frame,
                    diff_outside_mover, state_failures, errors, on[kNR + 1].stats.not_ready, on[kR + 1].stats.skipped,
                    lane_diff_frames, lane_diff_frame, lane_diff_outside);
        std::snprintf(label, sizeof label, "%s: hidden parts skipped from frame 2 on (every frame with ready results)", fname);
        check(hidden_expected >= 3 * 7 && hidden_skips == hidden_expected, label);
        std::snprintf(label, sizeof label, "%s: visible, in-front and alpha-tested parts always drawn", fname);
        check(visible_skips == 0, label);
        std::snprintf(label, sizeof label, "%s: the teleport is drawn the frame it leaves the hull (guard)", fname);
        check(on[kT].drawn[ti] && !on[kT - 1].drawn[ti], label);
        std::snprintf(label, sizeof label, "%s: the revealed part is late by exactly one frame (skipped at K, drawn from K+1)", fname);
        check(!on[kK].drawn[mi] && on[kK + 1].drawn[mi] && on[kK + 2].drawn[mi] && late == 1, label);
        std::snprintf(label, sizeof label, "%s: frames identical with the cull on and off except frame K, inside the part", fname);
        check(diff_frames == 1 && diff_frame == kK && diff_outside_mover == 0, label);
        if (dev.mrt) {
            std::snprintf(label, sizeof label,
                          "%s: RT1 (RGBA32F) and RT2 (R32F) byte-identical on and off except frame K, inside the part",
                          fname);
            check(lane_diff_frames == 1 && lane_diff_frame == kK && lane_diff_outside == 0, label);
        }
        std::snprintf(label, sizeof label, "%s: the application state is back after every part", fname);
        check(state_failures == 0, label);
        std::snprintf(label, sizeof label, "%s: the first frame after the Reset draws everything; no query errors", fname);
        check(on[kR + 1].stats.skipped == 0 && errors == 0 && skipped_over_hidden == 0 && not_ready_frames_ok, label);
        bool all_after_reset = true;
        for (int i = 0; i < kParts; ++i) all_after_reset = all_after_reset && on[kR + 1].tested[i];
        std::snprintf(label, sizeof label, "%s: every part is tested at the block after the Reset", fname);
        check(all_after_reset, label);
        dev.targets(false);
    }
}

int run_main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <d3d9 path|builtin>\n", argv[0]);
        return 2;
    }
    const bool builtin = !std::strcmp(argv[1], "builtin");
    HMODULE module = LoadLibraryA(builtin ? "d3d9.dll" : argv[1]);
    if (!module) return 1;
    using Create9 = IDirect3D9*(WINAPI*)(UINT);
    Create9 create = nullptr;
    {
        auto p = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &p, sizeof p);
    }
    IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return 1;
    D3DADAPTER_IDENTIFIER9 id{};
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    std::printf("ADAPTER description=%s\n", id.Description);
    WNDCLASSA wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3m_occlusion_cull";
    RegisterClassA(&wc);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "x3m occlusion cull", WS_OVERLAPPEDWINDOW, 0, 0, kSize, kSize,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    Device dev;
    dev.pp.BackBufferWidth = dev.pp.BackBufferHeight = kSize;
    dev.pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    dev.pp.BackBufferCount = 1;
    dev.pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    dev.pp.hDeviceWindow = window;
    dev.pp.Windowed = TRUE;
    dev.pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    // The game's device shape as the motion route creates it: HWVP | FPU_PRESERVE (PUREDEVICE stripped, capture.cpp).
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &dev.pp, &dev.d);
    check(SUCCEEDED(hr), "device");
    if (FAILED(hr)) return 1;
    IDirect3DDevice9* d = dev.d;
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    bool ok = SUCCEEDED(d->CreateVertexShader(game_vs, &dev.vs)) && SUCCEEDED(d->CreatePixelShader(game_ps, &dev.ps)) &&
              SUCCEEDED(d->CreateVertexDeclaration(elements, &dev.decl)) &&
              SUCCEEDED(d->CreateVertexBuffer(vertex_count * sizeof(V), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &dev.vb,
                                              nullptr));
    void* p = nullptr;
    if (ok && SUCCEEDED(dev.vb->Lock(0, 0, &p, 0))) {
        V* v = static_cast<V*>(p);
        quad(v + hull_full, -1, -1, 0, 1, 0.3f);
        quad(v + hull_upper, -1, 0, 0, 1, 0.3f);
        for (int i = 0; i < kParts; ++i)
            box(v + parts_base + i * 36, parts[i].x0, parts[i].y0, parts[i].x1, parts[i].y1, parts[i].z0, parts[i].z1);
        box(v + teleport_second, teleport_after[0], teleport_after[1], teleport_after[2], teleport_after[3], 0.5f, 0.7f);
        dev.vb->Unlock();
    } else
        ok = false;
    check(ok, "scene resources");
    if (!ok) return 1;
    void* const* native = *reinterpret_cast<void* const* const*>(d);
    ok = SUCCEEDED(d->CreatePixelShader(game_mrt_ps, &dev.mrt_ps)) &&
         SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &dev.sys1, nullptr)) &&
         SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &dev.sys2, nullptr));
    check(ok, "RT1/RT2 resources");
    functional(dev, native);
    engine_duty_cycle(dev, native);
    // Part 2: the realistic state.
    scene::Scene sc;
    ok = sc.create(d);
    check(ok, "realistic: scene resources (1280 x 720 fp16 + RT1 + RT2)");
    if (ok) {
        realistic_functional(d, native, sc);
        const int repeats = argc > 2 && std::atoi(argv[2]) > 0 && std::atoi(argv[2]) <= 9 ? std::atoi(argv[2]) : 1;
        // Per repeat, interleaved: off, then per buffer cull / all / one. Medians over the repeats.
        std::vector<double> off_frame, off_cpu, field[4][9];
        for (int r = 0; r < repeats; ++r) {
            const Cost off = timing(d, native, sc, OcclusionCullPass::default_buffer, "off", false, kRetest, false, false, r);
            off_frame.push_back(off.frame_us);
            off_cpu.push_back(off.cpu_us);
            for (int b = 0; b < 4; ++b) {
                const OcclusionCullBuffer buffer = OcclusionCullBuffer(b);
                const Cost cull = timing(d, native, sc, buffer, "cull", true, kRetest, true, false, r);
                const Cost all = timing(d, native, sc, buffer, "all", true, 1, false, false, r);
                const Cost one = timing(d, native, sc, buffer, "one", true, 1, false, true, r);
                const double per_test = all.tests > one.tests ? (all.test_us - one.test_us) / (all.tests - one.tests) : 0.0;
                field[b][0].push_back(per_test);
                field[b][1].push_back(one.blocks > 0 ? (one.test_us - one.tests * per_test) / one.blocks : 0.0);
                field[b][2].push_back(all.test_us);
                field[b][3].push_back(all.frame_us);
                field[b][4].push_back(cull.test_us);
                field[b][5].push_back(cull.tests);
                field[b][6].push_back(cull.frame_us);
                field[b][7].push_back(all.cpu_us);
                field[b][8].push_back(cull.cpu_us);
            }
        }
        const double off_us = scene::percentile(off_frame, .5), off_cpu_us = scene::percentile(off_cpu, .5);
        for (int b = 0; b < 4; ++b) {
            auto med = [&](int i) { return scene::percentile(field[b][i], .5); };
            std::printf("DERIVED impl=batched buffer=%s repeats=%d per_test_us=%.2f per_block_us=%.1f all_test_us=%.1f "
                        "all_frame_overhead_us=%.1f cull_test_us=%.1f cull_tests=%.0f cull_frame_delta_us=%.1f off_frame_us=%.1f "
                        "all_cpu_overhead_us=%.1f cull_cpu_delta_us=%.1f off_cpu_us=%.1f\n",
                        buffer_name(OcclusionCullBuffer(b)), repeats, med(0), med(1), med(2), med(3) - off_us, med(4), med(5),
                        med(6) - off_us, off_us, med(7) - off_cpu_us, med(8) - off_cpu_us, off_cpu_us);
        }
    }
    sc.release();
    if (dev.sys) dev.sys->Release();
    if (dev.sys1) dev.sys1->Release();
    if (dev.sys2) dev.sys2->Release();
    if (dev.mrt_ps) dev.mrt_ps->Release();
    dev.vb->Release();
    dev.decl->Release();
    dev.ps->Release();
    dev.vs->Release();
    d->Release();
    d3d->Release();
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return run_main(argc, argv);
}
