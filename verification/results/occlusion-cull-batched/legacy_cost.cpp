// The "before" cost: the Run137 occlusion cull pass (commit 833f1ac7, a test at every part's own draw site) on the same
// realistic-state scene the batched fixture measures (verification/probe/occlusion_cull_scene_inc.h). Built by
// legacy_build.sh against that commit's pass and core (extracted with git show, outside the repository); run by
// verification/probe/run_occlusion_cull.py --name legacy-<backend> under the Wine lock.
//
//   legacy_cost.exe <d3d9 path | builtin> [repeats, default 1]
//
// Rows: COST impl=legacy config=off|cull|all (cull: the Run137 behaviour, every part tested every frame and skipped on
// its verdict; all: the same tests with the verdict ignored), test_us = QPC around the pass's candidate() calls,
// frame_us = CPU from BeginScene to EndScene; DERIVED per_test_us = all.test_us / tests.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "occlusion_cull_pass.h"
#include "occlusion_cull_scene_inc.h"

namespace {
namespace oc = x3m::occlusion_cull::core;
namespace scene = occlusion_scene;
using x3m::renderer::OcclusionCullPass;
using x3m::renderer::OcclusionCullState;
using x3m::renderer::OcclusionCullVerdict;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", what, ok ? "PASS" : "FAIL");
}
struct LegacyHooks {
    OcclusionCullPass* pass = nullptr;
    bool honour = true;
    double test_us = 0.0, ns = 0.0;
    unsigned tests = 0, skipped = 0;
    void hull(int, int, const float*) {}
    bool part(int s, int i, const float* rows, const scene::PartDef& pd) {
        if (!pass) return false;
        const oc::Box box{{pd.lo[0], pd.lo[1], pd.lo[2]}, {pd.hi[0], pd.hi[1], pd.hi[2]}};
        oc::Rect rect{};
        if (oc::test_rect(rows, box, float(scene::width), float(scene::height), oc::cmp_lessequal, &rect) != oc::RectStatus::ok)
            return false;
        OcclusionCullState state{};
        state.z_write = TRUE;
        state.cull = D3DCULL_CCW;
        HRESULT restore = S_OK;
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        const auto verdict = pass->candidate(
            oc::draw_key(0x10000000u + std::uint32_t(s) * 0x10000u + 0x2000u + std::uint32_t(i) * 0x150u, 2,
                         std::uint32_t(scene::parts_base + i * 36), 36, 900000u + std::uint32_t(i)),
            rect, true, state, &restore);
        QueryPerformanceCounter(&t1);
        test_us += double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart);
        ++tests;
        const bool skip = honour && verdict == OcclusionCullVerdict::skip;
        skipped += skip ? 1 : 0;
        return skip;
    }
};
struct Cost {
    double tests = 0, test_us = 0, test_us_p90 = 0, frame_us = 0, frame_us_p90 = 0, cpu_us = 0, skipped = 0;
};
Cost timing(IDirect3DDevice9* d, void* const* native, scene::Scene& sc, const char* config, bool use_pass, bool honour,
            int repeat) {
    OcclusionCullPass pass;
    Cost c{};
    if (use_pass && FAILED(pass.attach(d, native))) {
        check(false, "legacy attach");
        return c;
    }
    LegacyHooks hooks;
    hooks.pass = use_pass ? &pass : nullptr;
    hooks.honour = honour;
    std::vector<double> cpu, tests, tus, fus, skipped;
    scene::Pacer pacer;
    for (int f = 0; f < 220; ++f) {
        if (use_pass) pass.begin_frame(std::uint32_t(f + 1));
        hooks.test_us = 0;
        hooks.tests = hooks.skipped = 0;
        const double frame_us = sc.frame(f % 16, 1 << 30, hooks);
        LARGE_INTEGER pf{}, p0{}, p1{};
        QueryPerformanceFrequency(&pf);
        QueryPerformanceCounter(&p0);
        d->Present(nullptr, nullptr, nullptr, nullptr);
        QueryPerformanceCounter(&p1);
        const double present_us = double(p1.QuadPart - p0.QuadPart) * 1e6 / double(pf.QuadPart);
        pass.frame_stats() = {};
        if (f >= 20) {
            tests.push_back(hooks.tests);
            tus.push_back(hooks.test_us);
            fus.push_back(frame_us);
            cpu.push_back(frame_us + present_us);
            skipped.push_back(hooks.skipped);
        }
        pacer.wait();
    }
    c.tests = scene::percentile(tests, .5);
    c.test_us = scene::percentile(tus, .5);
    c.test_us_p90 = scene::percentile(tus, .9);
    c.frame_us = scene::percentile(fus, .5);
    c.frame_us_p90 = scene::percentile(fus, .9);
    c.cpu_us = scene::percentile(cpu, .5);
    c.skipped = scene::percentile(skipped, .5);
    std::printf("COST impl=legacy repeat=%d buffer=- config=%s frames=200 tests_p50=%.0f blocks_p50=0 skipped_p50=%.0f "
                "test_us_p50=%.1f test_us_p90=%.1f frame_us_p50=%.1f frame_us_p90=%.1f cpu_us_p50=%.1f\n",
                repeat, config, c.tests, c.skipped, c.test_us, c.test_us_p90, c.frame_us, c.frame_us_p90, c.cpu_us);
    if (use_pass) pass.detach();
    return c;
}
LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}
int run_main(int argc, char** argv) {
    if (argc < 2) return 2;
    HMODULE module = LoadLibraryA(!std::strcmp(argv[1], "builtin") ? "d3d9.dll" : argv[1]);
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
    wc.lpszClassName = "x3m_occlusion_legacy";
    RegisterClassA(&wc);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "x3m occlusion legacy", WS_OVERLAPPEDWINDOW, 0, 0, 256, 256,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = pp.BackBufferHeight = 256;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d = nullptr;
    const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                         D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &d);
    check(SUCCEEDED(hr), "device");
    if (FAILED(hr)) return 1;
    void* const* native = *reinterpret_cast<void* const* const*>(d);
    scene::Scene sc;
    const bool ok = sc.create(d);
    check(ok, "realistic scene resources");
    if (ok) {
        const int repeats = argc > 2 && std::atoi(argv[2]) > 0 && std::atoi(argv[2]) <= 9 ? std::atoi(argv[2]) : 1;
        std::vector<double> off_f, all_t, all_f, cull_t, cull_n, cull_f, all_n, off_c, all_c, cull_c;
        for (int r = 0; r < repeats; ++r) {
            const Cost off = timing(d, native, sc, "off", false, false, r);
            const Cost cull = timing(d, native, sc, "cull", true, true, r);
            const Cost all = timing(d, native, sc, "all", true, false, r);
            off_f.push_back(off.frame_us), all_t.push_back(all.test_us), all_f.push_back(all.frame_us);
            cull_t.push_back(cull.test_us), cull_n.push_back(cull.tests), cull_f.push_back(cull.frame_us);
            all_n.push_back(all.tests);
            off_c.push_back(off.cpu_us), all_c.push_back(all.cpu_us), cull_c.push_back(cull.cpu_us);
        }
        const double off_us = scene::percentile(off_f, .5), tests = scene::percentile(all_n, .5);
        std::printf("DERIVED impl=legacy buffer=- repeats=%d per_test_us=%.2f per_block_us=0 all_test_us=%.1f "
                    "all_frame_overhead_us=%.1f cull_test_us=%.1f cull_tests=%.0f cull_frame_delta_us=%.1f off_frame_us=%.1f "
                    "all_cpu_overhead_us=%.1f cull_cpu_delta_us=%.1f off_cpu_us=%.1f\n",
                    repeats, tests ? scene::percentile(all_t, .5) / tests : 0.0, scene::percentile(all_t, .5),
                    scene::percentile(all_f, .5) - off_us, scene::percentile(cull_t, .5), scene::percentile(cull_n, .5),
                    scene::percentile(cull_f, .5) - off_us, off_us,
                    scene::percentile(all_c, .5) - scene::percentile(off_c, .5),
                    scene::percentile(cull_c, .5) - scene::percentile(off_c, .5), scene::percentile(off_c, .5));
        check(tests == 150, "every part tested every frame (Run137)");
    }
    sc.release();
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
