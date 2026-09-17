// Technique-lookup microbenchmark: what the engine's per-draw
// `GetTechniqueByName` + `SetTechnique` (and the `End`/`Begin` bookends) cost on
// the game's own compiled effects under the game's own `d3dx9_37.dll`. No game
// launch and no game code: one synthetic windowed HAL device (the
// effect_beginpass_fixture pattern, whose effect loading and timing method this
// fixture reuses) and one compiled effect blob handed in on the command line
// (the runner extracts it from the bottle's archives into a scratch directory
// outside the repository; no game bytes enter the tree).
//
//   effect_technique_lookup_fixture <effect.fb> <iterations> [repetitions]
//
// The engine's site (effect-pass-loop.md section 7): every sub-mesh joins at
// 0x004c0b85 and calls `GetTechniqueByName` (0x004c0bfa, vtable +0x34) with one
// of the literal material technique names DEFAULT / BUMPMAP / BUMPMAP_LOW
// (xt-materials.md: the bump-resource local and the object flag pick which),
// `FindNextValidTechnique` only on a name miss, then `SetTechnique`
// (0x004c0c34, +0xe8) — once per draw, whether or not the technique changed.
//
// The bottle's QueryPerformanceCounter ticks at 0.1 us, far coarser than these
// calls, so a per-call stamp pair measures only the quantisation (a first run
// returned 0.100 us for every measurement including the empty region). Timing is
// therefore batched: one QPC pair around a run of `batch` identical calls, the
// difference divided by `batch`, and the median/p90 taken over the
// `iterations / batch` batches of a repetition. Each batch spans 10^2-10^4
// ticks, so the quantisation error per batch is below 1 %.
//
// Measurements, each timed the same way:
//
//   baseline           the same batch loop with an empty body: loop overhead
//                      plus the QPC pair, subtracted from every other
//                      measurement
//   gtbn:<NAME>        GetTechniqueByName(<NAME>) for every engine name the
//                      effect declares (the handle is kept in a sink)
//   settech_same:<N>   SetTechnique(h) when h is already the current technique
//                      (the engine's common case: same material, same name)
//   settech_alt:<A|B>  SetTechnique alternating between two techniques
//   beginend:<NAME>    Begin(&passes, D3DXFX_DONOTSAVESTATE) + End() as one
//                      timed pair, for reference
//
// No state manager is installed: none of these calls reaches the device (the
// counting manager of the BeginPass fixture reported callbacks only inside
// BeginPass), so the numbers are D3DX's own work.
//
// Documented Windows APIs only (d3d9, d3dx9 through GetProcAddress, PE headers
// for the module identity); the same source builds and runs on native Windows.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

unsigned checks = 0;
void check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
void ok(HRESULT hr, const char* label) { check(hr == S_OK, label); }
template <class T> struct Com { T* p = nullptr; ~Com() { if (p) p->Release(); } T* operator->() const { return p; } };

using Create9 = IDirect3D9* (WINAPI*)(UINT);
using CreateEffectEx = HRESULT (WINAPI*)(IDirect3DDevice9*, const void*, UINT, const D3DXMACRO*, ID3DXInclude*,
                                         const char*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);

double frequency_hz = 1;
std::uint64_t ticks() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return static_cast<std::uint64_t>(t.QuadPart); }

// Keeps a returned handle live so nothing is optimised away.
volatile std::uintptr_t sink = 0;

// The engine's material technique names, in the order the material setup tests
// them (xt-materials.md 0x004c0996-0x004c0b85).
const char* const kEngineNames[] = {"DEFAULT", "BUMPMAP", "BUMPMAP_LOW"};

// What the mapped image says about itself: under Wine a builtin module keeps
// the native file's FullDllName, so only the image can tell native from
// builtin. Bounds-checked, documented PE structures (proxy_identity.cpp).
struct ModuleImage {
    unsigned long size = 0, stamp = 0, exports = 0;
    int wine_builtin = 0;
};
ModuleImage module_image(HMODULE module) {
    ModuleImage info;
    if (!module) return info;
    const unsigned char* base = reinterpret_cast<const unsigned char*>(module);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return info;
    const LONG lfanew = dos->e_lfanew;
    if (lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) ||
        lfanew > static_cast<LONG>(0x1000u - sizeof(IMAGE_NT_HEADERS32))) return info;
    const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return info;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return info;
    info.size = nt->OptionalHeader.SizeOfImage;
    info.stamp = nt->FileHeader.TimeDateStamp;
    if (nt->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT) {
        const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory.VirtualAddress && directory.Size >= sizeof(IMAGE_EXPORT_DIRECTORY) &&
            info.size >= sizeof(IMAGE_EXPORT_DIRECTORY) &&
            directory.VirtualAddress <= info.size - sizeof(IMAGE_EXPORT_DIRECTORY))
            info.exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress)->NumberOfFunctions;
    }
    static const char marker[] = "Wine builtin DLL";
    const std::size_t marker_size = sizeof(marker) - 1;
    for (std::size_t at = 0x40; at + marker_size <= 0x80; ++at)
        if (std::memcmp(base + at, marker, marker_size) == 0) { info.wine_builtin = 1; break; }
    return info;
}

double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0;
    const std::size_t at = std::min(sorted.size() - 1, static_cast<std::size_t>(q * double(sorted.size())));
    return sorted[at];
}

enum class What { Baseline, GetTechniqueByName, SetTechniqueSame, SetTechniqueAlternating, BeginEnd };

struct Measurement {
    What what;
    std::string name;    // measure label
    std::string detail;  // technique name(s) involved
    const char* lookup = nullptr;
    D3DXHANDLE handle_a = nullptr;
    D3DXHANDLE handle_b = nullptr;
};

// One batch of `batch` identical calls, between one QPC pair. `first` is the
// global iteration index of the batch, so the alternating case keeps alternating
// across batch boundaries.
double run_batch(ID3DXEffect* effect, const Measurement& m, unsigned batch, unsigned first) {
    UINT passes = 0;
    HRESULT hr = S_OK;
    const std::uint64_t t0 = ticks();
    switch (m.what) {
    case What::Baseline:
        for (unsigned j = 0; j < batch; ++j) sink = first + j;
        break;
    case What::GetTechniqueByName:
        for (unsigned j = 0; j < batch; ++j) sink = reinterpret_cast<std::uintptr_t>(effect->GetTechniqueByName(m.lookup));
        break;
    case What::SetTechniqueSame:
        for (unsigned j = 0; j < batch; ++j) hr |= effect->SetTechnique(m.handle_a);
        break;
    case What::SetTechniqueAlternating:
        for (unsigned j = 0; j < batch; ++j) hr |= effect->SetTechnique(((first + j) & 1u) ? m.handle_b : m.handle_a);
        break;
    case What::BeginEnd:
        for (unsigned j = 0; j < batch; ++j) { hr |= effect->Begin(&passes, D3DXFX_DONOTSAVESTATE); hr |= effect->End(); }
        break;
    }
    const std::uint64_t t1 = ticks();
    ok(hr, m.name.c_str());
    if (m.what == What::GetTechniqueByName) check(sink != 0, "GetTechniqueByName returned NULL");
    return double(t1 - t0) * 1e6 / frequency_hz / double(batch);
}

void run_measurement(ID3DXEffect* effect, const Measurement& m, std::vector<double>& microseconds,
                     unsigned iterations, unsigned warmup, unsigned batch, unsigned rep) {
    microseconds.clear();
    microseconds.reserve(iterations / batch);
    // Leave the effect on the technique the measurement expects as its state.
    if (m.what == What::SetTechniqueSame || m.what == What::BeginEnd || m.what == What::GetTechniqueByName)
        if (m.handle_a) ok(effect->SetTechnique(m.handle_a), "SetTechnique seed");
    for (unsigned i = 0; i < warmup; i += batch) run_batch(effect, m, batch, i);
    for (unsigned i = 0; i < iterations; i += batch)
        microseconds.push_back(run_batch(effect, m, batch, i));
    std::sort(microseconds.begin(), microseconds.end());
    double sum = 0;
    for (double value : microseconds) sum += value;
    std::printf("MEASURE measure=%s detail=%s rep=%u iterations=%u batch=%u batches=%lu us_median=%.4f us_p90=%.4f "
                "us_mean=%.4f us_p10=%.4f us_p99=%.4f\n",
                m.name.c_str(), m.detail.empty() ? "-" : m.detail.c_str(), rep, iterations, batch,
                static_cast<unsigned long>(microseconds.size()),
                percentile(microseconds, 0.5), percentile(microseconds, 0.9), sum / double(microseconds.size()),
                percentile(microseconds, 0.1), percentile(microseconds, 0.99));
}

std::vector<unsigned char> read_file(const char* path) {
    FILE* stream = std::fopen(path, "rb");
    check(stream != nullptr, "open effect");
    std::vector<unsigned char> data;
    unsigned char buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof buffer, stream)) > 0) data.insert(data.end(), buffer, buffer + got);
    std::fclose(stream);
    check(!data.empty(), "empty effect");
    return data;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        check(argc >= 2, "usage: effect_technique_lookup_fixture <effect.fb> [iterations] [repetitions] [batch]");
        const char* effect_path = argv[1];
        const unsigned iterations = argc >= 3 ? unsigned(std::strtoul(argv[2], nullptr, 10)) : 100000u;
        const unsigned repetitions = argc >= 4 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 3u;
        const unsigned batch = argc >= 5 ? unsigned(std::strtoul(argv[4], nullptr, 10)) : 100u;
        const unsigned warmup = 4 * batch;
        check(iterations >= 100000, "at least 100000 iterations");
        check(batch >= 2 && iterations % batch == 0, "batch divides iterations");
        LARGE_INTEGER f{}; check(QueryPerformanceFrequency(&f) && f.QuadPart > 0, "QPC frequency");
        frequency_hz = double(f.QuadPart);

        const std::vector<unsigned char> blob = read_file(effect_path);
        // The backend d3d9, never a proxy: this fixture measures D3DX.
        HMODULE d3d9 = LoadLibraryA("C:\\windows\\system32\\d3d9.dll");
        check(d3d9 != nullptr, "load d3d9");
        // By name, so the process-local DLL override (`--dll d3dx9_37=n|b`) decides.
        HMODULE d3dx = LoadLibraryA("d3dx9_37.dll");
        check(d3dx != nullptr, "load d3dx9_37");
        {
            char path[2048]{};
            GetModuleFileNameA(d3dx, path, sizeof path);
            const ModuleImage image = module_image(d3dx);
            std::printf("MODULE name=d3dx9_37 path=%s image_size=%lu stamp=%08lx exports=%lu wine_builtin=%d\n",
                        path, image.size, image.stamp, image.exports, image.wine_builtin);
        }
        Create9 create = nullptr; CreateEffectEx create_effect = nullptr;
        { const auto entry = GetProcAddress(d3d9, "Direct3DCreate9"); std::memcpy(&create, &entry, sizeof create); }
        { const auto entry = GetProcAddress(d3dx, "D3DXCreateEffectEx"); std::memcpy(&create_effect, &entry, sizeof create_effect); }
        check(create != nullptr, "Direct3DCreate9");
        check(create_effect != nullptr, "D3DXCreateEffectEx");

        HWND window = CreateWindowExA(0, "STATIC", "effect technique lookup fixture", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96,
                                      nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        check(window != nullptr, "window");
        {
            Com<IDirect3D9> factory; factory.p = create(D3D_SDK_VERSION); check(factory.p != nullptr, "factory");
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE; pp.hDeviceWindow = window; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.BackBufferWidth = pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
            pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            Com<IDirect3DDevice9> device;
            ok(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p), "device");
            D3DCAPS9 caps{};
            ok(device->GetDeviceCaps(&caps), "caps");
            std::printf("DEVICE vs=%lx ps=%lx\n", static_cast<unsigned long>(caps.VertexShaderVersion),
                        static_cast<unsigned long>(caps.PixelShaderVersion));

            Com<ID3DXEffect> effect; Com<ID3DXBuffer> errors;
            const HRESULT created = create_effect(device.p, blob.data(), UINT(blob.size()), nullptr, nullptr, nullptr,
                                                  0, nullptr, &effect.p, &errors.p);
            if (created != S_OK && errors.p)
                std::printf("EFFECT_ERRORS hr=%08lx bytes=%lu\n", static_cast<unsigned long>(created),
                            static_cast<unsigned long>(errors->GetBufferSize()));
            ok(created, "D3DXCreateEffectEx");

            D3DXEFFECT_DESC desc{};
            ok(effect->GetDesc(&desc), "GetDesc");

            // The engine names this effect actually declares, in engine order.
            std::vector<std::string> names;
            std::vector<D3DXHANDLE> handles;
            for (const char* name : kEngineNames) {
                D3DXHANDLE handle = effect->GetTechniqueByName(name);
                if (!handle) continue;
                names.push_back(name);
                handles.push_back(handle);
            }
            check(!names.empty(), "effect declares no engine technique name");
            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) { if (i) list += ","; list += names[i]; }
            std::printf("FIXTURE effect=%s bytes=%lu parameters=%lu techniques=%lu engine_techniques=%s "
                        "iterations=%u repetitions=%u warmup=%u batch=%u frequency=%lld\n",
                        effect_path, static_cast<unsigned long>(blob.size()),
                        static_cast<unsigned long>(desc.Parameters), static_cast<unsigned long>(desc.Techniques),
                        list.c_str(), iterations, repetitions, warmup, batch, static_cast<long long>(f.QuadPart));

            std::vector<Measurement> plan;
            plan.push_back({What::Baseline, "baseline", "", nullptr, nullptr, nullptr});
            for (std::size_t i = 0; i < names.size(); ++i)
                plan.push_back({What::GetTechniqueByName, "gtbn:" + names[i], names[i], names[i].c_str(), handles[i], nullptr});
            plan.push_back({What::SetTechniqueSame, "settech_same:" + names[0], names[0], nullptr, handles[0], nullptr});
            if (names.size() >= 2)
                plan.push_back({What::SetTechniqueAlternating, "settech_alt:" + names[0] + "|" + names[1],
                                names[0] + "|" + names[1], nullptr, handles[0], handles[1]});
            plan.push_back({What::BeginEnd, "beginend:" + names[0], names[0], nullptr, handles[0], nullptr});

            std::vector<double> microseconds;
            for (unsigned rep = 0; rep < repetitions; ++rep)
                for (const Measurement& m : plan)
                    run_measurement(effect.p, m, microseconds, iterations, warmup, batch, rep);
            // Leave the effect on its first technique; the device holds nothing.
            ok(effect->SetTechnique(handles[0]), "SetTechnique restore");
        }
        DestroyWindow(window);
        std::printf("RESULT checks=%u status=pass\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::printf("RESULT checks=%u status=fail reason=%s\n", checks, error.what());
        return 1;
    }
}
