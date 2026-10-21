// Scratch: native cost of the volumetric_fog_memory row's queries (not part of the repository).
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
static double us_since(LARGE_INTEGER a, LARGE_INTEGER f) { LARGE_INTEGER b; QueryPerformanceCounter(&b); return double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart); }
static void walk(unsigned long long& regions, unsigned long long& largest) {
    regions = largest = 0; std::uint64_t next = 0;
    for (;;) { MEMORY_BASIC_INFORMATION i; if (VirtualQuery((LPCVOID)(std::uintptr_t)next, &i, sizeof i) != sizeof i) break;
        ++regions; if (i.State == MEM_FREE && i.RegionSize > largest) largest = i.RegionSize;
        next = (std::uintptr_t)i.BaseAddress + i.RegionSize; if (next >= 0xFFFF0000ull || next == 0) break; }
}
int main() {
    LARGE_INTEGER f, t; QueryPerformanceFrequency(&f);
    const int N = 1000;
    PROCESS_MEMORY_COUNTERS_EX c{}; c.cb = sizeof c;
    QueryPerformanceCounter(&t); for (int i = 0; i < N; ++i) GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof c);
    std::printf("GetProcessMemoryInfo us=%.2f private=%llu\n", us_since(t, f) / N, (unsigned long long)c.PrivateUsage);
    MEMORYSTATUSEX s{}; s.dwLength = sizeof s;
    QueryPerformanceCounter(&t); for (int i = 0; i < N; ++i) GlobalMemoryStatusEx(&s);
    std::printf("GlobalMemoryStatusEx us=%.2f avail_virtual=%llu\n", us_since(t, f) / N, (unsigned long long)s.ullAvailVirtual);
    unsigned long long regions, largest;
    QueryPerformanceCounter(&t); walk(regions, largest); double w = us_since(t, f);
    std::printf("walk own regions=%llu us=%.1f per_region_us=%.3f largest=%llu\n", regions, w, w / regions, largest);
    // Fragment: 20000 64 KiB committed regions with free gaps between (reserve 128 KiB, release the upper half not possible; use alternate allocations).
    void* blocks[40000]; int n = 0;
    for (int i = 0; i < 40000; ++i) { blocks[i] = VirtualAlloc(nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); if (blocks[i]) ++n; }
    for (int i = 0; i < 40000; i += 2) if (blocks[i]) VirtualFree(blocks[i], 0, MEM_RELEASE);
    QueryPerformanceCounter(&t); walk(regions, largest); w = us_since(t, f);
    std::printf("walk fragmented regions=%llu us=%.1f per_region_us=%.3f largest=%llu\n", regions, w, w / regions, largest);
    HWND h = CreateWindowExW(0, L"STATIC", L"memcost", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = h; pp.BackBufferWidth = 64; pp.BackBufferHeight = 64;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d ? d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev) : E_FAIL;
    if (SUCCEEDED(hr)) {
        UINT m = 0; QueryPerformanceCounter(&t); for (int i = 0; i < N; ++i) m = dev->GetAvailableTextureMem();
        std::printf("GetAvailableTextureMem us=%.2f value=%u\n", us_since(t, f) / N, m);
        dev->Release();
    } else std::printf("CreateDevice hr=%08lx\n", hr);
    if (d3d) d3d->Release();
    DestroyWindow(h);
    return 0;
}
