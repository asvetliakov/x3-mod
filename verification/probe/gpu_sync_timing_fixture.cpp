// Detached qualification of the production GpuSyncTiming (src/renderer/gpu_sync_timing.cpp;
// docs/architecture/engine-frame-time.md, "GPU sync timing") on the bottle's D3D9 device:
// the event-query support probe, the soft-fail paths (CreateQuery refused by the
// support probe, and refused part-way with the partial creation rolled back), the
// device references the queries hold, 48 frames of fake passes through the
// production protocol (BeginScene opens Scene and Engine, heavy / light / empty /
// nested pairs, the Present pair, frame()) with a 16-frame window so three windows
// close, the window and session figures, the repair-pixel census (one occlusion
// query around the fog_repair quad, exact pixel count per window), CPU-state and LastError preservation
// across a boundary, the sync cost of an empty pair, then Reset (queries released
// before, recreated after, 16 more frames), a failed Reset and detach. Built by
// CMake (target gpu_sync_timing_fixture); run through
// verification/probe/run_gpu_sync_timing.py under wine_lock.py. Never launches the game.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/gpu_sync_timing.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
using namespace x3m::renderer;
namespace gst = x3m::gpu_sync_timing;

static unsigned checks = 0, failures = 0;
static void check_hr(const char* label, HRESULT hr) { if (FAILED(hr)) { std::printf("API FAIL %s %08lx\n", label, (unsigned long)hr); throw std::runtime_error(label); } }
static bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); return value; }
static ULONG probe(IDirect3DDevice9* d) { d->AddRef(); return d->Release(); }

// CreateQuery stand-ins behind a copied vtable (the production path calls native[118]).
using CreateQueryFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DQUERYTYPE, IDirect3DQuery9**);
static CreateQueryFn real_create_query = nullptr;
static unsigned create_budget = 0; // successful creations before the stand-in refuses
static HRESULT WINAPI refuse_create_query(IDirect3DDevice9*, D3DQUERYTYPE, IDirect3DQuery9** out) { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
static HRESULT WINAPI budget_create_query(IDirect3DDevice9* d, D3DQUERYTYPE type, IDirect3DQuery9** out) {
    if (!out) return real_create_query(d, type, out); // the support probe passes
    if (!create_budget) { *out = nullptr; return E_OUTOFMEMORY; }
    --create_budget; return real_create_query(d, type, out);
}

struct Vertex { float x, y, z, rhw; DWORD color; };
static void quad(IDirect3DDevice9* d, float x0, float y0, float x1, float y1, DWORD color) {
    const Vertex v[4] = {{x0, y0, 0.f, 1.f, color}, {x1, y0, 0.f, 1.f, color}, {x0, y1, 0.f, 1.f, color}, {x1, y1, 0.f, 1.f, color}};
    check_hr("DrawPrimitiveUP", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]));
}
static void bind(IDirect3DDevice9* d) {
    check_hr("fvf", d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
    check_hr("zenable", d->SetRenderState(D3DRS_ZENABLE, FALSE)); check_hr("lighting", d->SetRenderState(D3DRS_LIGHTING, FALSE));
    check_hr("cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    check_hr("blend", d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
    check_hr("src", d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE)); check_hr("dst", d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
}

constexpr unsigned W = 1280, H = 720, heavy_quads = 96, engine_quads = 32;
// One frame of the production protocol with fake work. Heavy: ShadowDepth, FogRoute,
// HdrWriteback; light: SunApply, Taa, Motes (nested in FogRoute), Meter (nested in
// HdrWriteback), Bloom (two pairs, as prepare + commit), FogFill, HdrReadback, the five taa_* sub-passes (nested in Taa,
// as TemporalPass::run marks them; taa_mask nests taa_mask_tests / _x / _y, one light quad each), fog_march / fog_composite / fog_repair (nested in FogRoute before Motes, as
// FogPass::execute marks them; the repair's light quad inside the census bracket, and the 8x8 needs-repair quad in its own
// bracket between composite and repair, drawn only while needs_wanted() as FogPass does); empty: Retention.
static HRESULT run_frame(IDirect3DDevice9* d, GpuSyncTiming& t, std::uint64_t index, gst::Report* report, bool* closed) {
    auto heavy = [&] { for (unsigned i = 0; i < heavy_quads; ++i) quad(d, -0.5f, -0.5f, W - 0.5f, H - 0.5f, 0x00010101); };
    auto light = [&] { quad(d, 0.f, 0.f, 16.f, 16.f, 0x00010101); };
    check_hr("Clear", d->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.f, 0));
    check_hr("BeginScene", d->BeginScene());
    t.begin(gst::Scene); t.begin(gst::Engine);
    t.begin(gst::HdrReadback); light(); t.end(gst::HdrReadback);   // at the HDR latch, inside the engine span
    t.begin(gst::FogFill); light(); t.end(gst::FogFill);
    for (unsigned i = 0; i < engine_quads; ++i) quad(d, -0.5f, -0.5f, W - 0.5f, H - 0.5f, 0x00010101);
    t.begin(gst::Scene); t.begin(gst::Engine); // repeated begins: ignored (first per frame only)
    t.end(gst::Engine);
    t.begin(gst::Retention); t.end(gst::Retention);
    t.begin(gst::ShadowDepth); heavy(); t.end(gst::ShadowDepth);
    t.begin(gst::SunApply); light(); t.end(gst::SunApply);
    t.begin(gst::FogRoute); heavy();
    t.begin(gst::FogMarch); light(); t.end(gst::FogMarch);
    t.begin(gst::FogComposite); light(); t.end(gst::FogComposite);
    if (t.needs_wanted()) { gst::NeedsSpan needs(&t, W * H, 4); quad(d, 0.f, 0.f, 8.f, 8.f, 0x00010101); }
    t.begin(gst::FogRepair); { gst::CensusSpan census(&t, W * H); light(); } t.end(gst::FogRepair);
    { gst::CensusSpan second(&t, W * H); quad(d, 0.f, 0.f, 32.f, 32.f, 0x00010101); } // a second bracket in the frame: not counted
    { gst::NeedsSpan second(&t, W * H, 2); quad(d, 0.f, 0.f, 32.f, 32.f, 0x00010101); } // likewise for the needs census
    t.begin(gst::Motes); light(); t.end(gst::Motes); t.end(gst::FogRoute);
    t.begin(gst::Taa);
    for (unsigned sub : {gst::TaaCopy, gst::TaaMask, gst::TaaBox, gst::TaaResolve, gst::TaaDisplay}) {
        t.begin(sub);
        if (sub == gst::TaaMask) for (unsigned draw : {gst::TaaMaskTests, gst::TaaMaskX, gst::TaaMaskY}) { t.begin(draw); light(); t.end(draw); } // one Span per mask draw
        else light();
        t.end(sub);
    }
    t.end(gst::Taa);
    t.begin(gst::HdrWriteback); t.begin(gst::Meter); light(); t.end(gst::Meter); heavy(); t.end(gst::HdrWriteback);
    t.begin(gst::Bloom); light(); t.end(gst::Bloom);
    t.end(gst::Taa); // an end without a begin: ignored
    check_hr("EndScene", d->EndScene());
    t.begin(gst::Bloom); light(); t.end(gst::Bloom); // the commit's pair adds to the prepare's
    t.begin(gst::Present);
    t.end(gst::Scene);
    const HRESULT present = d->Present(nullptr, nullptr, nullptr, nullptr);
    t.end(gst::Present);
    *closed = t.frame(index, report);
    return present;
}

struct Phase { unsigned windows = 0, frames = 0; bool all_full = true, ordered = true, nested = true, dt_ok = true, waits_bounded = true, sequence = true, census = true; std::uint64_t last_window = 0; };
static void print_window(const char* phase, const gst::Report& r) {
    std::printf("WINDOW phase=%s window=%llu frames=%llu..%llu n_frames=%u dropped=%u unclosed=%u dt_n=%u dt_median_us=%u dt_p90_us=%u\n", phase,
                (unsigned long long)r.window, (unsigned long long)r.first_frame, (unsigned long long)r.last_frame, r.frames, r.dropped, r.unclosed,
                r.dt_window.n, r.dt_window.median, r.dt_window.p90);
    for (unsigned p = 0; p < gst::pass_count; ++p)
        std::printf("PASS phase=%s window=%llu pass=%s n=%u median_us=%u p90_us=%u wait_median_us=%u session_n=%u session_median_us=%u session_p90_us=%u\n", phase,
                    (unsigned long long)r.window, gst::pass_name(p), r.pass[p].window.n, r.pass[p].window.median, r.pass[p].window.p90, r.pass[p].wait_median,
                    r.pass[p].session.n, r.pass[p].session.median, r.pass[p].session.p90);
    std::printf("CENSUS phase=%s window=%llu n=%u median_ppm=%u p90_ppm=%u max_ppm=%u last_pixels=%u area=%u unread=%u lost=%u failed=%u needs_n=%u needs_px=%u needs_p90_px=%u needs_max_px=%u needs_scale=%u needs_missed=%u\n",
                phase, (unsigned long long)r.window, r.census.ppm.n, r.census.ppm.median, r.census.ppm.p90, r.census.max_ppm, r.census.pixels, r.census.area, r.census.unread, r.census.lost, r.census.failed,
                r.needs.px.n, r.needs.px.median, r.needs.px.p90, r.needs.max_px, r.needs.tag, r.needs.unread + r.needs.lost + r.needs.failed);
}
static Phase run(IDirect3DDevice9* d, GpuSyncTiming& t, unsigned frames, std::uint64_t first_index, const char* phase, bool first_dt_missing) {
    Phase m; m.last_window = t.tracker().windows();
    for (unsigned k = 0; k < frames; ++k) {
        gst::Report r; bool closed = false;
        const HRESULT present = run_frame(d, t, first_index + k, &r, &closed);
        if (FAILED(present)) std::printf("NOTE present=%08lx frame=%u\n", (unsigned long)present, k);
        ++m.frames;
        if (!closed) continue;
        ++m.windows; print_window(phase, r);
        m.sequence = m.sequence && r.window == m.last_window + 1 && r.frames == t.tracker().window() && r.last_frame == first_index + k && r.dropped == 0 && r.unclosed == 0;
        m.last_window = r.window;
        const auto& P = r.pass;
        for (unsigned p = 0; p < gst::pass_count; ++p) {
            m.all_full = m.all_full && P[p].window.n == r.frames;
            m.waits_bounded = m.waits_bounded && P[p].wait_median <= P[p].window.median && P[p].window.median <= P[p].window.p90;
        }
        const unsigned light_max = P[gst::SunApply].window.median > P[gst::TaaResolve].window.median ? P[gst::SunApply].window.median : P[gst::TaaResolve].window.median;
        m.ordered = m.ordered && P[gst::ShadowDepth].window.median > light_max && P[gst::FogRoute].window.median > light_max
                    && P[gst::HdrWriteback].window.median > light_max && P[gst::Engine].window.median > light_max;
        m.nested = m.nested && P[gst::Scene].window.median >= P[gst::Engine].window.median && P[gst::FogRoute].window.median >= P[gst::Motes].window.median
                   && P[gst::HdrWriteback].window.median >= P[gst::Meter].window.median && P[gst::Engine].window.median >= P[gst::FogFill].window.median;
        for (unsigned sub = gst::TaaCopy; sub <= gst::TaaDisplay; ++sub) m.nested = m.nested && P[gst::Taa].window.median >= P[sub].window.median;
        for (unsigned sub = gst::FogMarch; sub <= gst::FogRepair; ++sub) m.nested = m.nested && P[gst::FogRoute].window.median >= P[sub].window.median;
        for (unsigned sub = gst::TaaMaskTests; sub <= gst::TaaMaskY; ++sub) m.nested = m.nested && P[gst::TaaMask].window.median >= P[sub].window.median;
        // The repair's 16x16 light quad, exactly, every frame; the frame's second bracket (a 32x32 quad) never counts.
        const std::uint32_t ppm = gst::census_ppm(256, W * H);
        m.census = m.census && t.census_available() && r.census.ppm.n == r.frames && r.census.ppm.median == ppm && r.census.max_ppm == ppm
                   && r.census.pixels == 256 && r.census.area == W * H && r.census.unread == 0 && r.census.lost == 0 && r.census.failed == 0;
        // The needs-repair census (step C): the 8x8 quad exactly, tagged with its spacing, every frame; the second bracket never counts.
        m.census = m.census && t.needs_available() && r.needs.px.n == r.frames && r.needs.px.median == 64 && r.needs.max_px == 64 && r.needs.tag == 4
                   && r.needs.area == W * H && r.needs.unread == 0 && r.needs.lost == 0 && r.needs.failed == 0;
        const bool first_window = m.windows == 1;
        m.dt_ok = m.dt_ok && r.dt_window.n == (first_window && first_dt_missing ? r.frames - 1 : r.frames) && r.dt_window.median >= P[gst::Scene].window.median;
    }
    return m;
}
static void judge(const char* phase, const Phase& m, unsigned expected_windows) {
    char label[80];
    std::snprintf(label, sizeof label, "%s_windows_closed", phase); require(label, m.windows == expected_windows);
    std::snprintf(label, sizeof label, "%s_window_sequence_and_ring", phase); require(label, m.sequence);
    std::snprintf(label, sizeof label, "%s_every_pass_every_frame", phase); require(label, m.all_full);
    std::snprintf(label, sizeof label, "%s_heavy_above_light", phase); require(label, m.ordered);
    std::snprintf(label, sizeof label, "%s_nested_spans_cover_inner", phase); require(label, m.nested);
    std::snprintf(label, sizeof label, "%s_dt_counted_and_covers_scene", phase); require(label, m.dt_ok);
    std::snprintf(label, sizeof label, "%s_wait_median_p90_ordered", phase); require(label, m.waits_bounded);
    std::snprintf(label, sizeof label, "%s_repair_census_exact", phase); require(label, m.census);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3GpuSyncTimingFixture"; RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 gpu sync timing fixture", WS_OVERLAPPEDWINDOW, 90, 90, 320, 200, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll"); if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        IDirect3D9* api = create(D3D_SDK_VERSION); if (!api) throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        IDirect3DDevice9* d = nullptr;
        check_hr("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &d));
        void* const* native = *reinterpret_cast<void* const* const*>(d);
        D3DADAPTER_IDENTIFIER9 ident{}; api->GetAdapterIdentifier(0, 0, &ident);
        std::printf("DEVICE adapter=%s driver=%s\n", ident.Description, ident.Driver);
        std::printf("PROBE event=%08lx\n", (unsigned long)d->CreateQuery(D3DQUERYTYPE_EVENT, nullptr));
        const ULONG baseline = probe(d);
        auto t = std::make_unique<GpuSyncTiming>();

        // Soft fail 1: the support probe refuses (a device without event queries).
        void* table[119]; std::memcpy(table, native, sizeof table);
        real_create_query = reinterpret_cast<CreateQueryFn>(native[118]);
        table[118] = reinterpret_cast<void*>(&refuse_create_query);
        HRESULT hr = t->attach(d, table, 16);
        std::printf("SOFTFAIL kind=unsupported available=%u reason=%s result=%08lx references=%u\n", unsigned(t->available()), t->reason(), (unsigned long)hr, t->references());
        require("softfail_unsupported_reason", !t->available() && hr == D3DERR_NOTAVAILABLE && !std::strcmp(t->reason(), "event_unsupported"));
        t->begin(gst::Scene); t->end(gst::Scene); gst::Report r0; const bool closed0 = t->frame(1, &r0);
        require("softfail_no_per_frame_work", !closed0 && t->stats().syncs == 0 && t->stats().polls == 0 && t->references() == 0 && probe(d) == baseline);
        t->before_reset(); t->after_reset(S_OK);
        require("softfail_reset_stays_unavailable", !t->available() && t->references() == 0 && probe(d) == baseline);
        // Soft fail 2: refused part-way (the ninth creation): everything created is released.
        table[118] = reinterpret_cast<void*>(&budget_create_query); create_budget = 8;
        hr = t->attach(d, table, 16);
        std::printf("SOFTFAIL kind=partial available=%u reason=%s result=%08lx references=%u device=%lu baseline=%lu\n", unsigned(t->available()), t->reason(), (unsigned long)hr,
                    t->references(), (unsigned long)probe(d), (unsigned long)baseline);
        require("softfail_partial_rolled_back", !t->available() && hr == E_OUTOFMEMORY && !std::strcmp(t->reason(), "create_failed") && t->references() == 0 && probe(d) == baseline);
        t->detach();

        // The measured path on the native table.
        hr = t->attach(d, native, 16);
        const ULONG attached = probe(d);
        std::printf("SUPPORT available=%u reason=%s result=%08lx references=%u device_delta=%lu queries=%u census=%u frequency=%llu\n", unsigned(t->available()), t->reason(), (unsigned long)hr,
                    t->references(), (unsigned long)(attached - baseline), gst::boundary_count, unsigned(t->census_available()), (unsigned long long)t->frequency());
        if (!require("attach_available", hr == S_OK && t->available())) throw std::runtime_error("event queries unavailable on this device");
        require("references_match_device_delta", t->references() == unsigned(attached - baseline) && t->references() > 0);
        bind(d);
        // CPU state across a boundary: LastError, MXCSR and the x87 control word survive the sync (PreserveCpuState).
        {
            unsigned mxcsr = 0, rounding = 0; asm volatile("stmxcsr %0" : "=m"(mxcsr));
            unsigned short control = 0, control_after = 0; asm volatile("fnstcw %0" : "=m"(control));
            const unsigned changed = (mxcsr & ~0x6000u) | 0x6000u; asm volatile("ldmxcsr %0" :: "m"(changed));
            const unsigned short control_changed = static_cast<unsigned short>(control ^ 0x0c00u); asm volatile("fldcw %0" :: "m"(control_changed)); // x87 rounding flipped
            SetLastError(0x13572468u);
            t->begin(gst::Retention); t->end(gst::Retention);
            const DWORD error = GetLastError(); asm volatile("stmxcsr %0" : "=m"(rounding)); asm volatile("fnstcw %0" : "=m"(control_after));
            asm volatile("ldmxcsr %0" :: "m"(mxcsr)); asm volatile("fldcw %0" :: "m"(control));
            std::printf("CPUSTATE lasterror=%08lx mxcsr=%08x/%08x x87_cw=%04x/%04x\n", (unsigned long)error, rounding, changed, control_after, control_changed);
            require("boundary_preserves_lasterror_mxcsr_x87cw", error == 0x13572468u && rounding == changed && control_after == control_changed);
            t->before_reset(); t->after_reset(S_OK); // drops the probe's marks (release clears the frame); the next dt has no predecessor
            require("recreate_after_probe", t->available() && t->references() == unsigned(attached - baseline) && probe(d) == attached);
        }
        const std::uint64_t syncs_before = t->stats().syncs;
        Phase first = run(d, *t, 48, 1, "first", true);
        judge("first", first, 3);
        const auto& s = t->stats();
        std::printf("STATS phase=first syncs=%llu polls=%llu issue_failures=%llu data_failures=%llu timeouts=%llu dropped_frames=%llu\n", (unsigned long long)(s.syncs - syncs_before),
                    (unsigned long long)s.polls, (unsigned long long)s.issue_failures, (unsigned long long)s.data_failures, (unsigned long long)s.timeouts, (unsigned long long)s.dropped_frames);
        // 50 boundaries per frame plus the commit's second Bloom pair (the repeated begins and the stray end never sync).
        require("first_syncs_exact", s.syncs - syncs_before == 48ull * (gst::boundary_count + 2));
        require("first_no_failures", s.issue_failures == 0 && s.data_failures == 0 && s.timeouts == 0 && s.dropped_frames == 0);
        const gst::Report session = t->summary();
        std::printf("SESSION windows=%llu dt_n=%u dt_median_us=%u retention_median_us=%u fog_route_median_us=%u taa_median_us=%u\n", (unsigned long long)session.window,
                    session.dt_session.n, session.dt_session.median, session.pass[gst::Retention].session.median, session.pass[gst::FogRoute].session.median, session.pass[gst::Taa].session.median);
        require("session_counts", session.pass[gst::Taa].session.n == 48 && session.pass[gst::Retention].session.n == 48 && session.dt_session.n == 47);
        // The light pair is one sub-pass pair (the Taa pair now nests five of them).
        std::printf("SYNC_COST empty_pair_median_us=%u light_pair_median_us=%u\n", session.pass[gst::Retention].session.median, session.pass[gst::TaaResolve].session.median);

        // Reset: the queries go before, come back after; 16 more frames close one window.
        t->before_reset();
        require("before_reset_releases", !t->available() && t->references() == 0 && probe(d) == baseline);
        hr = d->Reset(&pp);
        t->after_reset(hr);
        const ULONG after_reset = probe(d);
        std::printf("RESET result=%08lx available=%u reason=%s references=%u device_delta=%lu\n", (unsigned long)hr, unsigned(t->available()), t->reason(), t->references(),
                    (unsigned long)(after_reset - baseline));
        require("native_reset_recreates", SUCCEEDED(hr) && t->available() && t->references() == unsigned(after_reset - baseline));
        bind(d);
        Phase second = run(d, *t, 16, 100, "after_reset", true);
        judge("after_reset", second, 1);
        // A failed Reset leaves them released; the next successful one recreates.
        t->before_reset(); t->after_reset(D3DERR_DEVICELOST);
        require("failed_reset_stays_released", !t->available() && t->references() == 0 && !std::strcmp(t->reason(), "reset_failed") && probe(d) == baseline);
        t->after_reset(S_OK);
        require("later_reset_recreates", t->available() && t->references() > 0);
        t->detach();
        require("detach_releases_references", !t->available() && t->references() == 0 && probe(d) == baseline);
        t.reset();
        d->Release(); api->Release();
        std::printf("RESULT checks=%u failures=%u path=measured %s\n", checks, failures, failures ? "FAIL" : "PASS");
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::printf("RESULT checks=%u failures=%u path=exception error=%s FAIL\n", checks, failures + 1, error.what());
        return 2;
    }
}
