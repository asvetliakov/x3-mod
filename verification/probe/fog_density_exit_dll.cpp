// Process-exit witness for the stored-density worker (fog_density_exit_fixture.cpp loads this).
// It reproduces the proxy's shape: a module-static owner whose destructor releases the cache the
// way ~MotionOutput -> FogPass::detach does, and a DllMain that, like src/proxy/loader.cpp,
// abandons the worker first. Marks go through WriteFile only (no CRT, no lock of ours).
#include "../../src/fog/fog_density_cache.h"
#include <vector>
#include <windows.h>
using namespace x3m::fog;
namespace {
DensityCache* cache = nullptr;
HANDLE marks = INVALID_HANDLE_VALUE;
int abandon_at_detach = 0;
void mark(const char* text) {
    if (marks == INVALID_HANDLE_VALUE) return;
    char line[128];
    const int n = wsprintfA(line, "%s tick=%lu\r\n", text, GetTickCount());
    DWORD written = 0;
    WriteFile(marks, line, DWORD(n), &written, nullptr);
    FlushFileBuffers(marks);
}
struct Owner {
    ~Owner() {
        mark("static_destructor_begin");
        DensityCache::retire(cache);
        cache = nullptr;
        mark("static_destructor_end");
    }
} owner;
}
extern "C" __declspec(dllexport) int __cdecl fixture_start(const wchar_t* path, int abandon, int midfill) {
    marks = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    abandon_at_detach = abandon;
    cache = new DensityCache;
    if (marks == INVALID_HANDLE_VALUE || !cache->start()) return 0;
    cache->gpu_reset();
    const double camera[3] = {95576., 97323., 82698.};
    std::vector<std::uint8_t> staging[kLevelCount];
    for (auto& s : staging) s.assign(kAtlasBytes, 0);
    std::uint64_t frame = 0;
    const DWORD begin = GetTickCount();
    for (;;) {
        cache->step(camera, ++frame);
        if (cache->has_work()) {
            StagingView views[kLevelCount];
            for (int l = 0; l < kLevelCount; ++l) views[l] = {staging[l].data(), kAtlasPitch};
            TileRect rects[kDefaultUploadRects];
            cache->take_uploads(views, kDefaultUploadBudget, rects, kDefaultUploadRects);
            cache->confirm_uploads(true);
        }
        if (midfill ? GetTickCount() - begin > 150 : cache->idle()) break;
        if (GetTickCount() - begin > 120000) return 0;
        Sleep(1);
    }
    if (midfill) { // the worker must be generating right now, not parked
        const std::uint64_t a = cache->stats().nodes_generated;
        Sleep(40);
        if (cache->stats().nodes_generated == a || cache->idle()) return 0;
        mark("worker_generating");
    } else
        mark("worker_parked");
    mark("ready");
    return 1;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH) {
        mark(reserved ? "dllmain_detach_process_exit" : "dllmain_detach_freelibrary");
        if (abandon_at_detach && cache) {
            cache->abandon();
            cache = nullptr;
        } // FogPass::abandon_density_worker
        mark("dllmain_detach_end");
    }
    return TRUE;
}
