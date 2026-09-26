// Original SM3 GPU and structural verification of the production radiance patch.
// No extracted game programs, game launch, installation or settings changes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include "../../src/renderer/material_radiance.h"

template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
};
struct Module {
    HMODULE h;
    explicit Module(const char* path)
        : h(LoadLibraryA(path)) {
        if (!h) throw std::runtime_error("LoadLibrary");
        char resolved[MAX_PATH]{};
        GetModuleFileNameA(h, resolved, MAX_PATH);
        std::printf("MODULE requested=%s resolved=%s\n", path, resolved);
    }
    ~Module() { FreeLibrary(h); }
};
template <class T> T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    if (!result) throw std::runtime_error(name);
    return result;
}
void check(const char* name, HRESULT result) {
    std::printf("API %s result=%08lx\n", name, result);
    if (FAILED(result)) throw std::runtime_error(name);
}
using Compiler = decltype(&D3DXCompileShader);
void compile(Compiler compiler, const std::string& source, const char* target, ID3DXBuffer** code) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source.c_str(), UINT(source.size()), nullptr, nullptr, "main", target,
                          D3DXSHADER_OPTIMIZATION_LEVEL3, code, &errors.p, nullptr);
    if (errors.p) std::printf("COMPILER %s\n", static_cast<char*>(errors->GetBufferPointer()));
    check(target, hr);
}
float halfFloat(unsigned short h) {
    unsigned e = (h >> 10) & 31;
    float sign = h & 0x8000 ? -1.f : 1.f;
    if (e == 31) return NAN;
    return sign * (e ? std::ldexp(float(1024 + (h & 1023)), int(e) - 25) : std::ldexp(float(h & 1023), -24));
}

namespace radiance = x3m::renderer;
using Words = std::vector<std::uint32_t>;
static unsigned checks = 0, samples = 0;
void require(const char* name, bool condition) {
    ++checks;
    std::printf("CHECK %s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) throw std::runtime_error(name);
}
using Assembler = decltype(&D3DXAssembleShader);
using Disassembler = decltype(&D3DXDisassembleShader);
Words assemble(Assembler assembler) {
    // Both branch arms consume vertex COLOR0. Alpha and a separate MRT value
    // deliberately retain unrelated saturation in the variant.
    const char* source = R"ASM(ps_3_0
        def c0, 0, 1, 2, 3
        dcl_color0 v0
        if b0
            mov_sat_pp r0.xyz, v0
        else
            mov_sat_pp r1.xyz, v0
            mov r0.xyz, r1
        endif
        mov_sat r0.w, v0.w
        mov_sat r2.x, c1.x
        mov r2.yzw, c0.x
        mov oC0, r0
        mov oC1, r2
)ASM";
    Com<ID3DXBuffer> code, errors;
    HRESULT hr = assembler(source, UINT(std::strlen(source)), nullptr, nullptr, 0, &code.p, &errors.p);
    if (errors.p) std::printf("ASSEMBLER %s\n", static_cast<char*>(errors->GetBufferPointer()));
    check("assemble original PS", hr);
    const auto* first = static_cast<const std::uint32_t*>(code->GetBufferPointer());
    return Words(first, first + code->GetBufferSize() / 4);
}
radiance::RadianceProfile profile(const Words& words) {
    radiance::RadianceProfile p{};
    p.name = "original-two-branch-fixture";
    p.word_count = words.size();
    p.fnv = radiance::shader_fingerprint(words.data(), words.size());
    for (std::size_t at = 1; at < words.size();) {
        const auto op = words[at];
        if (op == 0x0000ffff) break;
        const unsigned n = (op & 0xffff) == 0xfffe ? ((op >> 16) & 0x7fff) : ((op >> 24) & 15);
        require("original instruction framing", n < words.size() - at);
        if (op == 0x05000051 && !p.zero_def_offset) {
            for (unsigned c = 0; c < 4; ++c)
                if (words[at + 2 + c] == 0) {
                    p.zero_def_offset = at;
                    p.zero_component = c;
                    break;
                }
        }
        if (op == 0x02000001 && n == 2 && words[at + 2] == 0x90e40000 &&
            (words[at + 1] & ~std::uint32_t(0x0020001f)) == 0x80170000) {
            require("at most two original RGB sites", p.site_count < 2);
            p.sites[p.site_count++] = {at, words[at + 1], words[at + 2]};
        }
        at += n + 1;
    }
    require("walked two real RGB clamp sites", p.site_count == 2 && p.zero_def_offset != 0);
    std::printf("PROFILE fnv=%016llx words=%u sites=%u,%u zero_def=%u component=%u\n",
                static_cast<unsigned long long>(p.fnv), unsigned(p.word_count), unsigned(p.sites[0].offset),
                unsigned(p.sites[1].offset), unsigned(p.zero_def_offset), p.zero_component);
    return p;
}
void refresh(radiance::RadianceProfile& p, const Words& words) {
    p.word_count = words.size();
    p.fnv = radiance::shader_fingerprint(words.data(), words.size());
}
void rejected(const char* name, const radiance::RadianceProfile& p, const Words& words,
              radiance::RadianceResult expected) {
    Words output = {0x12345678, 0xabcdef01}, saved = output;
    const auto result = radiance::apply_radiance_profile(p, words.data(), words.size(), output);
    require(name, result == expected && output == saved);
}
void structural(const Words& source, const radiance::RadianceProfile& p, const Words& variant) {
    using Result = radiance::RadianceResult;
    Words sentinel = {11, 22, 33};
    require("unknown program rejected by production dispatch",
            radiance::material_radiance_variant(source.data(), source.size(), sentinel) == Result::UnsupportedShader &&
                sentinel == Words({11, 22, 33}));
    require("null input leaves output unchanged",
            radiance::apply_radiance_profile(p, nullptr, source.size(), sentinel) == Result::InvalidInput &&
                sentinel == Words({11, 22, 33}));
    auto q = p;
    q.fnv ^= 1;
    rejected("wrong fingerprint atomic failure", q, source, Result::ProfileMismatch);
    q = p;
    ++q.word_count;
    rejected("wrong word count atomic failure", q, source, Result::ProfileMismatch);
    auto bytes = source;
    bytes.back() ^= 1;
    rejected("modified byte rejected", p, bytes, Result::ProfileMismatch);
    bytes = source;
    bytes.pop_back();
    q = p;
    refresh(q, bytes);
    rejected("missing END structural failure", q, bytes, Result::InvalidProfile);
    q = p;
    q.site_count = 1;
    rejected("omitted branch site rejected", q, source, Result::InvalidProfile);
    q = p;
    q.sites[1] = q.sites[0];
    rejected("duplicate sites rejected", q, source, Result::InvalidProfile);
    q = p;
    q.zero_component = 4;
    rejected("invalid zero component rejected", q, source, Result::InvalidProfile);
    bytes = source;
    bytes[p.zero_def_offset + 2 + p.zero_component] = 0x3f000000;
    q = p;
    refresh(q, bytes);
    rejected("nonzero DEF rejected", q, bytes, Result::InvalidProfile);
    bytes = source;
    bytes[p.zero_def_offset + 2 + p.zero_component] = 0x80000000;
    q = p;
    refresh(q, bytes);
    rejected("negative zero literal not silently accepted", q, bytes, Result::InvalidProfile);
    bytes = source;
    bytes[p.sites[0].offset + 1] &= ~0x00010000u;
    q = p;
    q.sites[0].destination = bytes[p.sites[0].offset + 1];
    refresh(q, bytes);
    rejected("wrong RGB write mask rejected", q, bytes, Result::InvalidProfile);
    // A fake MOV inside COMMENT must not appear as an unnamed third clamp.
    const std::size_t insertion = p.zero_def_offset + 6;
    const Words comment = {0x0003fffe, 0x02000001, p.sites[0].destination, p.sites[0].source};
    bytes = source;
    bytes.insert(bytes.begin() + insertion, comment.begin(), comment.end());
    q = p;
    for (auto& site : q.sites) site.offset += comment.size();
    refresh(q, bytes);
    Words output;
    require("instruction-like comment ignored",
            radiance::apply_radiance_profile(q, bytes.data(), bytes.size(), output) == Result::Applied);
    auto fake = q;
    fake.sites[0].offset = insertion + 1;
    rejected("comment payload cannot be a site", fake, bytes, Result::InvalidProfile);
    bytes[insertion] = 0x7ffefffe;
    refresh(q, bytes);
    rejected("truncated oversized comment rejected", q, bytes, Result::InvalidProfile);
    // DEF literals can contain the exact bit patterns of MOV + its operands.
    const Words definition = {0x05000051, 0xa00f000a, 0x02000001, p.sites[0].destination, p.sites[0].source, 0};
    bytes = source;
    bytes.insert(bytes.begin() + insertion, definition.begin(), definition.end());
    q = p;
    for (auto& site : q.sites) site.offset += definition.size();
    refresh(q, bytes);
    require("instruction-like DEF literals ignored",
            radiance::apply_radiance_profile(q, bytes.data(), bytes.size(), output) == Result::Applied);
    fake = q;
    fake.sites[0].offset = insertion + 2;
    rejected("DEF literal cannot be a site", fake, bytes, Result::InvalidProfile);
    bytes = source;
    bytes.insert(bytes.end() - 1, {0x05000051, 0xa00f000a, 0});
    q = p;
    refresh(q, bytes);
    rejected("truncated final DEF rejected", q, bytes, Result::InvalidProfile);
    bytes = source;
    q = p;
    q.zero_def_offset = p.sites[0].offset;
    rejected("zero definition must identify DEF", q, bytes, Result::InvalidProfile);
    Words alias = source;
    require("input output alias succeeds",
            radiance::apply_radiance_profile(p, alias.data(), alias.size(), alias) == Result::Applied &&
                alias == variant);
    alias = source;
    q = p;
    q.fnv ^= 1;
    require("alias failure preserves source/output",
            radiance::apply_radiance_profile(q, alias.data(), alias.size(), alias) == Result::ProfileMismatch &&
                alias == source);
    require("variant grows once per reviewed site", variant.size() == source.size() + 2);
    std::size_t originalAt = 0, variantAt = 0;
    bool unchanged = true, precisePatch = true;
    for (const auto& site : p.sites) {
        while (originalAt < site.offset) unchanged &= source[originalAt++] == variant[variantAt++];
        precisePatch &= variant[variantAt] == 0x0300000b &&
                        variant[variantAt + 1] == (site.destination & ~0x00100000u) &&
                        variant[variantAt + 2] == site.source && variant[variantAt + 3] == 0xa0000000;
        originalAt += 3;
        variantAt += 4;
    }
    while (originalAt < source.size()) unchanged &= source[originalAt++] == variant[variantAt++];
    require("MAX keeps PP mask input and local zero source", precisePatch);
    require("every unrelated instruction word unchanged", unchanged && variantAt == variant.size());
}
constexpr UINT W = 16;
struct Vertex {
    float p[4], color[4];
};
struct Gpu {
    IDirect3DDevice9* d;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps[2];
    Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DSurface9> target[2], readback[2], back;
    Gpu(IDirect3DDevice9* device, Compiler compiler, const Words& original, const Words& variant)
        : d(device) {
        Com<ID3DXBuffer> vc;
        compile(
            compiler,
            "struct O{float4 p:POSITION0;float4 c:COLOR0;};O main(float4 p:POSITION0,float4 c:TEXCOORD0){O o;o.p=p;o.c=c;return o;}",
            "vs_3_0", &vc.p);
        check("VS", d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()), &vs.p));
        check("original PS", d->CreatePixelShader(reinterpret_cast<const DWORD*>(original.data()), &ps[0].p));
        check("variant PS", d->CreatePixelShader(reinterpret_cast<const DWORD*>(variant.data()), &ps[1].p));
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        check("declaration", d->CreateVertexDeclaration(elements, &decl.p));
        check("backbuffer", d->GetRenderTarget(0, &back.p));
        check("depth null", d->SetDepthStencilSurface(nullptr));
        for (unsigned i = 0; i < 2; ++i) {
            check("FP16 target", d->CreateRenderTarget(W, W, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                       &target[i].p, nullptr));
            check("readback", d->CreateOffscreenPlainSurface(W, W, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM,
                                                             &readback[i].p, nullptr));
            check("bind target", d->SetRenderTarget(i, target[i].p));
        }
        D3DVIEWPORT9 viewport = {0, 0, W, W, 0, 1};
        check("viewport", d->SetViewport(&viewport));
        check("bind VS", d->SetVertexShader(vs.p));
        check("bind declaration", d->SetVertexDeclaration(decl.p));
        for (auto pair : {std::pair<D3DRENDERSTATETYPE, DWORD>{D3DRS_ZENABLE, FALSE},
                          {D3DRS_ZWRITEENABLE, FALSE},
                          {D3DRS_ALPHABLENDENABLE, FALSE},
                          {D3DRS_ALPHATESTENABLE, FALSE},
                          {D3DRS_FOGENABLE, FALSE},
                          {D3DRS_SRGBWRITEENABLE, FALSE},
                          {D3DRS_CULLMODE, D3DCULL_NONE},
                          {D3DRS_COLORWRITEENABLE, 15},
                          {D3DRS_COLORWRITEENABLE1, 15}})
            check("render state", d->SetRenderState(pair.first, pair.second));
    }
    ~Gpu() {
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(0, back.p);
        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetVertexDeclaration(nullptr);
    }
    void draw(unsigned generation, bool changed, bool branch, float value, float alpha) {
        check("bind PS", d->SetPixelShader(ps[changed ? 1 : 0].p));
        BOOL flag = branch;
        check("branch bool", d->SetPixelShaderConstantB(0, &flag, 1));
        float other[4] = {value * 2, 0, 0, 0};
        check("unrelated clamp constant", d->SetPixelShaderConstantF(1, other, 1));
        const float externalZeroOverride[4] = {.875f, .875f, .875f, .875f};
        check("overwrite app c0 while local DEF remains zero", d->SetPixelShaderConstantF(0, externalZeroOverride, 1));
        const Vertex vertices[] = {{{-1 - 1.f / W, 1 + 1.f / W, 0, 1}, {value, value, value, alpha}},
                                   {{3 - 1.f / W, 1 + 1.f / W, 0, 1}, {value, value, value, alpha}},
                                   {{-1 - 1.f / W, -3 + 1.f / W, 0, 1}, {value, value, value, alpha}}};
        check("begin", d->BeginScene());
        check("draw", d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex)));
        check("end", d->EndScene());
        auto sat = [](float x) { return std::max(0.f, std::min(1.f, x)); };
        const float rgb = changed ? std::max(0.f, value) : sat(value);
        const float expected[2][4] = {{rgb, rgb, rgb, sat(alpha)}, {sat(value * 2), 0, 0, 0}};
        for (unsigned i = 0; i < 2; ++i) {
            check("read target", d->GetRenderTargetData(target[i].p, readback[i].p));
            D3DLOCKED_RECT lr{};
            check("lock readback", readback[i]->LockRect(&lr, nullptr, D3DLOCK_READONLY));
            float actual[4]{};
            for (unsigned c = 0; c < 4; ++c) {
                unsigned short bits;
                std::memcpy(&bits, static_cast<const unsigned char*>(lr.pBits) + 8 * lr.Pitch + 8 * 8 + c * 2, 2);
                actual[c] = halfFloat(bits);
            }
            check("unlock readback", readback[i]->UnlockRect());
            bool pass = true;
            for (unsigned c = 0; c < 4; ++c)
                pass &= std::isfinite(actual[c]) && std::fabs(actual[c] - expected[i][c]) <= .002f;
            ++samples;
            std::printf(
                "SAMPLE generation=%u variant=%u branch=%u input=%.3f alpha=%.3f target=%u rgba=%.6f,%.6f,%.6f,%.6f expected=%.6f,%.6f,%.6f,%.6f %s\n",
                generation, changed, branch, value, alpha, i, actual[0], actual[1], actual[2], actual[3],
                expected[i][0], expected[i][1], expected[i][2], expected[i][3], pass ? "PASS" : "FAIL");
            if (!pass) throw std::runtime_error("numeric mismatch");
        }
    }
};
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3MaterialRadianceFixture";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 original material radiance fixture", WS_OVERLAPPEDWINDOW, 0, 0,
                                128, 128, nullptr, nullptr, cls.hInstance, nullptr);
    int result = 1;
    try {
        if (argc != 2 || !window) throw std::runtime_error("expected D3DX path and hidden window");
        Module d3dx(argv[1]), runtime("d3d9.dll");
        const auto compiler = symbol<Compiler>(d3dx.h, "D3DXCompileShader");
        const auto assembler = symbol<Assembler>(d3dx.h, "D3DXAssembleShader");
        const auto disassemble = symbol<Disassembler>(d3dx.h, "D3DXDisassembleShader");
        const Words original = assemble(assembler);
        const auto p = profile(original);
        Words variant;
        require("production profile applied",
                radiance::apply_radiance_profile(p, original.data(), original.size(), variant) ==
                    radiance::RadianceResult::Applied);
        structural(original, p, variant);
        for (const auto* words : std::array<const Words*, 2>{&original, &variant}) {
            Com<ID3DXBuffer> dis;
            check("disassemble original synthetic program",
                  disassemble(reinterpret_cast<const DWORD*>(words->data()), FALSE, nullptr, &dis.p));
            std::printf("ASSEMBLY %s\n%s\n", words == &original ? "baseline" : "variant",
                        static_cast<char*>(dis->GetBufferPointer()));
        }
        auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h, "Direct3DCreate9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        require("native factory", api.p != nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = W;
        pp.BackBufferHeight = W;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check("CreateDevice",
              api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
                                &pp, &device.p));
        for (unsigned generation = 0; generation < 2; ++generation) {
            {
                Gpu gpu(device.p, compiler, original, variant);
                for (bool changed : {false, true})
                    for (bool branch : {false, true})
                        for (float value : {-2.f, 0.f, .25f, 1.f, 4.f, 16.f})
                            for (float alpha : {.25f, 1.5f}) gpu.draw(generation, changed, branch, value, alpha);
            }
            if (!generation) {
                check("Reset", device->Reset(&pp));
                std::puts("RESET PASS");
            }
        }
        std::printf("RESULT PASS checks=%u samples=%u generations=2\n", checks, samples);
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s checks=%u samples=%u\n", e.what(), checks, samples);
    }
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return result;
}
