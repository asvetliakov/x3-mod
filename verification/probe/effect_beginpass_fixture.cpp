// Native-BeginPass attribution: what one ID3DXEffect::BeginPass costs on the
// game's own compiled effect, and what it makes the state manager do, under
// three setter regimes. No game launch, no game code: one synthetic windowed
// HAL device (the state_hook_benchmark pattern), the game's own
// `d3dx9_37.dll` loaded by name so the bottle's DLL override decides whether
// the native redistributable or Wine's builtin answers, and one compiled
// effect blob handed in on the command line (the runner extracts
// `shader/3_0/argon.fb` from the bottle's archives into a scratch directory
// outside the repository and deletes it afterwards; no game bytes are read
// from or written into the repository).
//
//   effect_beginpass_fixture <effect.fb> [technique] [iterations] [repetitions]
//
// Per repetition and regime the loop is the game's own pass loop
// (effect-pass-loop.md section 2): Begin(&passes, D3DXFX_DONOTSAVESTATE),
// BeginPass(pass), EndPass, End, and never CommitChanges. Only BeginPass is
// timed, with QueryPerformanceCounter around the single call; the per-call
// times are kept and the median reported, so a stray scheduling spike cannot
// move the number. The three regimes differ only in what runs before
// BeginPass:
//
//   unchanged  every settable top-level parameter is set again to the value it
//              already holds (the game's per-draw Set* traffic, same values)
//   changed    the same parameters are set to a value that differs each
//              iteration
//   none       no Set* at all between passes
//
// `unchanged` minus `none` is exactly whether native D3DX dirties a parameter
// on a same-value Set*: if D3DX compared values, the two regimes would issue
// the same state-manager callbacks and cost the same.
//
// The counting ID3DXEffectStateManager forwards every callback to the device
// (the game installs a state manager too, effect-pass-loop.md section 1) and
// counts, per kind, only the callbacks issued inside the timed BeginPass.
// Shader-constant callbacks are counted both as calls and as registers.
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

// d3dx9.lib is not linked (every D3DX entry point comes from GetProcAddress),
// so the state-manager IID is defined here from its documented value
// (d3dx9effect.h DEFINE_GUID).
static const GUID kEffectStateManagerIID =
    {0x79aab587, 0x6dbc, 0x4fa7, {0x82, 0xde, 0x37, 0xfa, 0x17, 0x81, 0xc5, 0xce}};

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

// What the mapped image says about itself (the proxy_identity.cpp reading):
// under Wine a builtin module keeps the native file's FullDllName, so only the
// image can tell native from builtin. Bounds-checked, documented PE structures.
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

// Counting, forwarding state manager. `counting` is raised only around the
// timed BeginPass, so Begin/End and the setters contribute nothing.
struct Counters {
    unsigned long long render_state = 0, sampler_state = 0, texture = 0, texture_stage = 0,
                       vertex_shader = 0, pixel_shader = 0, shader_constant = 0, constant_registers = 0, other = 0;
    unsigned long long total() const {
        return render_state + sampler_state + texture + texture_stage + vertex_shader + pixel_shader + shader_constant + other;
    }
};

class CountingManager final : public ID3DXEffectStateManager {
public:
    explicit CountingManager(IDirect3DDevice9* device) : device_(device) {}
    Counters counters;
    bool counting = false;

    // IUnknown: the effect holds a reference for its lifetime; the object is a
    // stack member of main, so Release never frees it and the count is only a
    // liveness check at shutdown.
    HRESULT WINAPI QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, kEffectStateManagerIID)) {
            *out = this; ++references_; return S_OK;
        }
        *out = nullptr; return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() override { return ++references_; }
    ULONG WINAPI Release() override { return --references_; }
    ULONG references() const { return references_; }

    HRESULT WINAPI SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override {
        counters.other += counting; return device_->SetTransform(state, matrix); }
    HRESULT WINAPI SetMaterial(const D3DMATERIAL9* material) override {
        counters.other += counting; return device_->SetMaterial(material); }
    HRESULT WINAPI SetLight(DWORD index, const D3DLIGHT9* light) override {
        counters.other += counting; return device_->SetLight(index, light); }
    HRESULT WINAPI LightEnable(DWORD index, WINBOOL enable) override {
        counters.other += counting; return device_->LightEnable(index, enable); }
    HRESULT WINAPI SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override {
        counters.render_state += counting; return device_->SetRenderState(state, value); }
    HRESULT WINAPI SetTexture(DWORD stage, IDirect3DBaseTexture9* texture) override {
        counters.texture += counting; return device_->SetTexture(stage, texture); }
    HRESULT WINAPI SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override {
        counters.texture_stage += counting; return device_->SetTextureStageState(stage, type, value); }
    HRESULT WINAPI SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override {
        counters.sampler_state += counting; return device_->SetSamplerState(sampler, type, value); }
    HRESULT WINAPI SetNPatchMode(FLOAT segments) override {
        counters.other += counting; return device_->SetNPatchMode(segments); }
    HRESULT WINAPI SetFVF(DWORD format) override {
        counters.other += counting; return device_->SetFVF(format); }
    HRESULT WINAPI SetVertexShader(IDirect3DVertexShader9* shader) override {
        counters.vertex_shader += counting; return device_->SetVertexShader(shader); }
    HRESULT WINAPI SetVertexShaderConstantF(UINT index, const FLOAT* data, UINT count) override {
        constant(count); return device_->SetVertexShaderConstantF(index, data, count); }
    HRESULT WINAPI SetVertexShaderConstantI(UINT index, const INT* data, UINT count) override {
        constant(count); return device_->SetVertexShaderConstantI(index, data, count); }
    HRESULT WINAPI SetVertexShaderConstantB(UINT index, const WINBOOL* data, UINT count) override {
        constant(count); return device_->SetVertexShaderConstantB(index, data, count); }
    HRESULT WINAPI SetPixelShader(IDirect3DPixelShader9* shader) override {
        counters.pixel_shader += counting; return device_->SetPixelShader(shader); }
    HRESULT WINAPI SetPixelShaderConstantF(UINT index, const FLOAT* data, UINT count) override {
        constant(count); return device_->SetPixelShaderConstantF(index, data, count); }
    HRESULT WINAPI SetPixelShaderConstantI(UINT index, const INT* data, UINT count) override {
        constant(count); return device_->SetPixelShaderConstantI(index, data, count); }
    HRESULT WINAPI SetPixelShaderConstantB(UINT index, const WINBOOL* data, UINT count) override {
        constant(count); return device_->SetPixelShaderConstantB(index, data, count); }

private:
    void constant(UINT count) { if (counting) { ++counters.shader_constant; counters.constant_registers += count; } }
    IDirect3DDevice9* device_;
    ULONG references_ = 1;
};

// One settable top-level parameter and how it is written.
enum class Kind { Float, Int, Bool, Vector, Matrix, Texture2D, TextureCube };
struct Param { D3DXHANDLE handle; Kind kind; };

struct Textures {
    IDirect3DTexture9* flat[2];
    IDirect3DCubeTexture9* cube[2];
};

// `vary == false` writes the same values on every iteration; `vary == true`
// writes a value that differs from the previous iteration's.
void apply(ID3DXEffect* effect, const std::vector<Param>& params, const Textures& textures, bool vary, unsigned i) {
    const float f = vary ? 1.0f + float(i) * 0.001f : 1.0f;
    const D3DXVECTOR4 vector(f, f * 0.5f, f * 0.25f, 1.0f);
    D3DXMATRIX matrix;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) matrix.m[row][column] = 0.0f;
    matrix.m[0][0] = f; matrix.m[1][1] = f; matrix.m[2][2] = f; matrix.m[3][3] = 1.0f;
    const unsigned slot = vary ? (i & 1u) : 0u;
    for (const Param& param : params) {
        switch (param.kind) {
        case Kind::Float: effect->SetFloat(param.handle, f); break;
        case Kind::Int: effect->SetInt(param.handle, vary ? int(i & 7u) : 1); break;
        case Kind::Bool: effect->SetBool(param.handle, vary ? WINBOOL(i & 1u) : TRUE); break;
        case Kind::Vector: effect->SetVector(param.handle, &vector); break;
        case Kind::Matrix: effect->SetMatrix(param.handle, &matrix); break;
        case Kind::Texture2D: effect->SetTexture(param.handle, textures.flat[slot]); break;
        case Kind::TextureCube: effect->SetTexture(param.handle, textures.cube[slot]); break;
        }
    }
}

double percentile(std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0;
    const std::size_t at = std::min(sorted.size() - 1, static_cast<std::size_t>(q * double(sorted.size())));
    return sorted[at];
}

struct Regime { const char* name; bool set; bool vary; };
const Regime regimes[3] = {{"unchanged", true, false}, {"changed", true, true}, {"none", false, false}};

void run_regime(ID3DXEffect* effect, CountingManager& manager, const std::vector<Param>& params, const Textures& textures,
                const Regime& regime, D3DXHANDLE technique, unsigned pass_index, unsigned iterations, unsigned warmup, unsigned rep) {
    UINT passes = 0;
    std::vector<double> microseconds;
    microseconds.reserve(iterations);
    double setter_ticks = 0;
    manager.counters = Counters{};
    ok(effect->SetTechnique(technique), "SetTechnique");
    for (unsigned i = 0; i < warmup + iterations; ++i) {
        const bool timed = i >= warmup;
        ok(effect->Begin(&passes, D3DXFX_DONOTSAVESTATE), "Begin");
        check(passes > pass_index, "pass index");
        const std::uint64_t s0 = ticks();
        if (regime.set) apply(effect, params, textures, regime.vary, i);
        const std::uint64_t s1 = ticks();
        manager.counting = timed;
        const std::uint64_t t0 = ticks();
        const HRESULT hr = effect->BeginPass(pass_index);
        const std::uint64_t t1 = ticks();
        manager.counting = false;
        ok(hr, "BeginPass");
        ok(effect->EndPass(), "EndPass");
        ok(effect->End(), "End");
        if (timed) {
            microseconds.push_back(double(t1 - t0) * 1e6 / frequency_hz);
            setter_ticks += double(s1 - s0);
        }
    }
    std::sort(microseconds.begin(), microseconds.end());
    const double median = percentile(microseconds, 0.5);
    double sum = 0;
    for (double value : microseconds) sum += value;
    const Counters& c = manager.counters;
    const double n = double(iterations);
    std::printf("REGIME name=%s rep=%u iterations=%u us_median=%.4f us_mean=%.4f us_p05=%.4f us_p95=%.4f "
                "setters_us_mean=%.4f cb_render_state=%.4f cb_sampler_state=%.4f cb_texture=%.4f cb_texture_stage=%.4f "
                "cb_vertex_shader=%.4f cb_pixel_shader=%.4f cb_shader_constant=%.4f cb_constant_registers=%.4f "
                "cb_other=%.4f cb_total=%.4f\n",
                regime.name, rep, iterations, median, sum / n, percentile(microseconds, 0.05), percentile(microseconds, 0.95),
                setter_ticks * 1e6 / frequency_hz / n, double(c.render_state) / n, double(c.sampler_state) / n,
                double(c.texture) / n, double(c.texture_stage) / n, double(c.vertex_shader) / n, double(c.pixel_shader) / n,
                double(c.shader_constant) / n, double(c.constant_registers) / n, double(c.other) / n, double(c.total()) / n);
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
        check(argc >= 2, "usage: effect_beginpass_fixture <effect.fb> [technique] [iterations] [repetitions]");
        const char* effect_path = argv[1];
        const char* wanted_technique = argc >= 3 && argv[2][0] ? argv[2] : nullptr;
        const unsigned iterations = argc >= 4 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 10000u;
        const unsigned repetitions = argc >= 5 ? unsigned(std::strtoul(argv[4], nullptr, 10)) : 3u;
        const unsigned warmup = 500;
        check(iterations >= 10000, "at least 10000 iterations");
        LARGE_INTEGER f{}; check(QueryPerformanceFrequency(&f) && f.QuadPart > 0, "QPC frequency");
        frequency_hz = double(f.QuadPart);

        const std::vector<unsigned char> blob = read_file(effect_path);
        // The backend d3d9, never a proxy: this fixture measures D3DX, not the proxy.
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

        HWND window = CreateWindowExA(0, "STATIC", "effect beginpass fixture", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96,
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

            Com<IDirect3DTexture9> flat_a, flat_b; Com<IDirect3DCubeTexture9> cube_a, cube_b;
            ok(device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat_a.p, nullptr), "texture a");
            ok(device->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat_b.p, nullptr), "texture b");
            ok(device->CreateCubeTexture(32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube_a.p, nullptr), "cube a");
            ok(device->CreateCubeTexture(32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube_b.p, nullptr), "cube b");
            const Textures textures{{flat_a.p, flat_b.p}, {cube_a.p, cube_b.p}};

            Com<ID3DXEffect> effect; Com<ID3DXBuffer> errors;
            const HRESULT created = create_effect(device.p, blob.data(), UINT(blob.size()), nullptr, nullptr, nullptr,
                                                  0, nullptr, &effect.p, &errors.p);
            if (created != S_OK && errors.p)
                std::printf("EFFECT_ERRORS hr=%08lx bytes=%lu\n", static_cast<unsigned long>(created),
                            static_cast<unsigned long>(errors->GetBufferSize()));
            ok(created, "D3DXCreateEffectEx");

            D3DXEFFECT_DESC desc{};
            ok(effect->GetDesc(&desc), "GetDesc");

            // Technique: the requested one, else the first that validates on
            // this device, else technique 0 (reported either way).
            D3DXHANDLE technique = nullptr;
            const char* technique_source = "index0";
            if (wanted_technique) {
                technique = effect->GetTechniqueByName(wanted_technique);
                if (technique) technique_source = "named";
            }
            if (!technique) {
                D3DXHANDLE valid = nullptr;
                if (effect->FindNextValidTechnique(nullptr, &valid) == S_OK && valid) {
                    technique = valid; technique_source = "valid";
                }
            }
            if (!technique) technique = effect->GetTechnique(0);
            check(technique != nullptr, "technique");
            D3DXTECHNIQUE_DESC technique_desc{};
            ok(effect->GetTechniqueDesc(technique, &technique_desc), "GetTechniqueDesc");
            check(technique_desc.Passes > 0, "technique has no pass");
            D3DXPASS_DESC pass_desc{};
            ok(effect->GetPassDesc(effect->GetPass(technique, 0), &pass_desc), "GetPassDesc");

            // Every settable top-level parameter: the per-draw Set* traffic the
            // regimes replay. Arrays and structs are skipped (the game's own
            // setter mix is not known per parameter; the count is reported).
            std::vector<Param> params;
            unsigned skipped = 0;
            for (UINT i = 0; i < desc.Parameters; ++i) {
                D3DXHANDLE handle = effect->GetParameter(nullptr, i);
                D3DXPARAMETER_DESC pd{};
                if (!handle || effect->GetParameterDesc(handle, &pd) != S_OK) { ++skipped; continue; }
                if (pd.Elements) { ++skipped; continue; }
                bool taken = true;
                if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_FLOAT) params.push_back({handle, Kind::Float});
                else if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_INT) params.push_back({handle, Kind::Int});
                else if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_BOOL) params.push_back({handle, Kind::Bool});
                else if (pd.Class == D3DXPC_VECTOR && pd.Type == D3DXPT_FLOAT && pd.Columns <= 4) params.push_back({handle, Kind::Vector});
                else if ((pd.Class == D3DXPC_MATRIX_ROWS || pd.Class == D3DXPC_MATRIX_COLUMNS) && pd.Type == D3DXPT_FLOAT
                         && pd.Rows == 4 && pd.Columns == 4) params.push_back({handle, Kind::Matrix});
                else if (pd.Type == D3DXPT_TEXTURE || pd.Type == D3DXPT_TEXTURE2D) params.push_back({handle, Kind::Texture2D});
                else if (pd.Type == D3DXPT_TEXTURECUBE) params.push_back({handle, Kind::TextureCube});
                else { taken = false; ++skipped; }
                (void)taken;
            }
            std::printf("FIXTURE effect=%s bytes=%lu parameters=%lu settable=%lu skipped=%u technique=%s technique_source=%s "
                        "passes=%lu pass=%s iterations=%u repetitions=%u warmup=%u frequency=%lld\n",
                        effect_path, static_cast<unsigned long>(blob.size()), static_cast<unsigned long>(desc.Parameters),
                        static_cast<unsigned long>(params.size()), skipped, technique_desc.Name ? technique_desc.Name : "?",
                        technique_source, static_cast<unsigned long>(technique_desc.Passes),
                        pass_desc.Name ? pass_desc.Name : "?", iterations, repetitions, warmup,
                        static_cast<long long>(f.QuadPart));
            check(!params.empty(), "no settable parameter");

            CountingManager manager(device.p);
            ok(effect->SetStateManager(&manager), "SetStateManager");
            // Seed the values regime `unchanged` and `none` then keep re-writing
            // or leave untouched, so both start from the same parameter state.
            apply(effect.p, params, textures, false, 0);
            for (unsigned rep = 0; rep < repetitions; ++rep)
                for (const Regime& regime : regimes)
                    run_regime(effect.p, manager, params, textures, regime, technique, 0, iterations, warmup, rep);
            ok(effect->SetStateManager(nullptr), "clear state manager");
            std::printf("MANAGER references=%lu\n", static_cast<unsigned long>(manager.references()));
            ok(device->SetTexture(0, nullptr), "unbind texture");
            ok(device->SetVertexShader(nullptr), "unbind vs");
            ok(device->SetPixelShader(nullptr), "unbind ps");
        }
        DestroyWindow(window);
        std::printf("RESULT checks=%u status=pass\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::printf("RESULT checks=%u status=fail reason=%s\n", checks, error.what());
        return 1;
    }
}
