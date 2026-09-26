// D3D9 backend smoke probe: can a given d3d9.dll (wined3d, CrossOver's DXVK, a
// DXVK build over MoltenVK) stand in as the backend the proxy forwards to?
//
//   d3d9_backend_smoke_fixture.exe <d3d9 path | builtin> <d3dx9_37 path> <shader list | ->
//
// Loads the d3d9 by path (or "d3d9.dll" by name for builtin), creates the game's
// device shape (HWVP | PUREDEVICE | FPU_PRESERVE = 0x52, windowed, A8R8G8B8,
// D24S8, hidden window), draws small cases into a 256x256 A8R8G8B8 render
// target and reads each back (channel means, coverage = share of pixels with
// any channel > 16): a textured vs_3_0/ps_3_0 quad, alpha test, two samplers
// in ps_1_1 (aliased-sampler path), ps_3_0 2D + cube, a two-stage fixed-function
// draw, a MANAGED texture re-lock, StretchRect (RT->RT, backbuffer->RT, depth),
// event / occlusion queries, RESZ into D24X8 and INTZ, a D3DXCreateEffect draw
// with the given d3dx9_37 and Present. Then every listed game program is created
// and, when that succeeds, drawn once with a generic partner and a GPU wait, so
// pipeline compilation happens inside SWEEP markers that are also written to
// stderr (where DXVK / MoltenVK / wined3d log) for attribution by the runner.
// Documented D3D9 / Win32 / D3DX APIs only. Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) ++failures;
    std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL");
}
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T* operator->() const { return p; }
    T** out() {
        reset();
        return &p;
    }
};

using Create9 = IDirect3D9*(WINAPI*)(UINT);
using AssembleFn = HRESULT(WINAPI*)(LPCSTR, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXBuffer**, ID3DXBuffer**);
using CreateEffectFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const void*, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD,
                                        ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
AssembleFn assemble_fn = nullptr;
CreateEffectFn create_effect_fn = nullptr;

double qpc_hz = 1;
double now_ms() {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return 1000.0 * double(t.QuadPart) / qpc_hz;
}

std::string token(std::string s) {
    for (char& c : s)
        if (c == ' ') c = '_';
    return s.empty() ? "-" : s;
}

int wine_builtin(HMODULE module) {
    static const char marker[] = "Wine builtin DLL";
    const unsigned char* base = reinterpret_cast<const unsigned char*>(module);
    for (std::size_t at = 0x40; at + sizeof(marker) - 1 <= 0x80; ++at)
        if (!std::memcmp(base + at, marker, sizeof(marker) - 1)) return 1;
    return 0;
}

void print_module(const char* role, HMODULE module) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    std::printf("MODULE role=%s path=%s wine_builtin=%d\n", role, token(path).c_str(), wine_builtin(module));
}

// --- shader assembly (the given d3dx9_37) ---
std::vector<DWORD> assemble(const char* source, const char* label) {
    std::vector<DWORD> code;
    if (!assemble_fn) return code;
    Com<ID3DXBuffer> shader, errors;
    HRESULT hr = assemble_fn(source, UINT(std::strlen(source)), nullptr, nullptr, 0, shader.out(), errors.out());
    if (FAILED(hr) || !shader.p) {
        std::printf("ASSEMBLE %s hr=%08lx error=%s\n", label, (unsigned long)hr,
                    errors.p ? token(static_cast<const char*>(errors->GetBufferPointer())).substr(0, 200).c_str()
                             : "-");
        return code;
    }
    code.resize(shader->GetBufferSize() / 4);
    std::memcpy(code.data(), shader->GetBufferPointer(), code.size() * 4);
    return code;
}

const char* kVs30 =
    "vs_3_0\n dcl_position v0\n dcl_texcoord v1\n dcl_position o0\n dcl_texcoord o1\n mov o0, v0\n mov o1, v1\n";
const char* kPs30Tex = "ps_3_0\n dcl_texcoord v0.xy\n dcl_2d s0\n texld r0, v0, s0\n mov oC0, r0\n";
const char* kPs30TexCube =
    "ps_3_0\n def c0, 0, 0, 1, 0\n dcl_texcoord v0.xy\n dcl_2d s0\n dcl_cube s1\n texld r0, v0, s0\n mov r2, c0\n"
    " texld r1, r2, s1\n add r0, r0, r1\n mov oC0, r0\n";
const char* kVs11 = "vs_1_1\n dcl_position v0\n dcl_texcoord v1\n mov oPos, v0\n mov oT0, v1\n mov oT1, v1\n";
const char* kPs11Two = "ps_1_1\n tex t0\n tex t1\n add r0, t0, t1\n";
// Raw depth read (INTZ) and projected comparison read (D24X8 shadow): c0.z is the reference.
const char* kPs30Depth =
    "ps_3_0\n dcl_texcoord v0.xy\n dcl_2d s0\n mov r1, v0\n mov r1.z, c0.z\n mov r1.w, c0.w\n texldp r0, r1, s0\n"
    " mov r0.yzw, r0.x\n mov oC0, r0\n";
// Generic partners for the game-program sweep.
const char* kVs30Generic =
    "vs_3_0\n dcl_position v0\n dcl_texcoord v1\n dcl_position o0\n dcl_color o1\n dcl_color1 o2\n"
    " dcl_texcoord o3\n dcl_texcoord1 o4\n dcl_texcoord2 o5\n dcl_texcoord3 o6\n dcl_texcoord4 o7\n"
    " dcl_texcoord5 o8\n dcl_texcoord6 o9\n dcl_texcoord7 o10\n mov o0, v0\n mov o1, v1\n mov o2, v1\n"
    " mov o3, v1\n mov o4, v1\n mov o5, v1\n mov o6, v1\n mov o7, v1\n mov o8, v1\n mov o9, v1\n mov o10, v1\n";
const char* kVs20Generic =
    "vs_2_0\n dcl_position v0\n dcl_texcoord v1\n mov oPos, v0\n mov oD0, v1\n mov oD1, v1\n mov oT0, v1\n"
    " mov oT1, v1\n mov oT2, v1\n mov oT3, v1\n mov oT4, v1\n mov oT5, v1\n mov oT6, v1\n mov oT7, v1\n";
const char* kPs30Const = "ps_3_0\n def c0, 1, 0, 1, 1\n mov oC0, c0\n";
const char* kPs20Const = "ps_2_0\n def c0, 1, 0, 1, 1\n mov oC0, c0\n";

const char*
    kEffect = "float4 tint = float4(0.25, 0.5, 0.75, 1.0);\n"
              "texture tex; sampler2D s = sampler_state { Texture = <tex>; MinFilter = POINT; MagFilter = POINT; };\n"
              "struct V { float4 p : POSITION; float2 t : TEXCOORD0; };\n"
              "V vs(V v) { return v; }\n"
              "float4 ps(float2 t : TEXCOORD0) : COLOR { return tint + 0.0 * tex2D(s, t); }\n"
              "technique T { pass P { VertexShader = compile vs_3_0 vs(); PixelShader = compile ps_3_0 ps(); "
              "ZEnable = FALSE; AlphaTestEnable = FALSE; } }\n";

struct Vertex {
    float x, y, z, w, u, v;
};
struct FfVertex {
    float x, y, z, rhw, u0, v0, u1, v1;
};
constexpr int kSize = 256;

struct Context {
    IDirect3DDevice9* device = nullptr;
    Com<IDirect3DTexture9> rt, tex_a, tex_b;
    Com<IDirect3DCubeTexture9> cube_b;
    Com<IDirect3DVolumeTexture9> volume;
    Com<IDirect3DSurface9> rt_surface, readback, backbuffer, depth;
    Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DVertexShader9> vs30, vs11, vs30_generic, vs20_generic;
    Com<IDirect3DPixelShader9> ps30_tex, ps30_tex_cube, ps11_two, ps30_depth, ps30_const, ps20_const;
};

struct Stats {
    double r = 0, g = 0, b = 0;
    double coverage = 0;
    HRESULT hr = E_FAIL;
};

Stats read_rt(Context& c, IDirect3DSurface9* source = nullptr) {
    Stats s;
    s.hr = c.device->GetRenderTargetData(source ? source : c.rt_surface.p, c.readback.p);
    if (FAILED(s.hr)) return s;
    D3DLOCKED_RECT lr{};
    s.hr = c.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    if (FAILED(s.hr)) return s;
    double sr = 0, sg = 0, sb = 0;
    unsigned covered = 0;
    for (int y = 0; y < kSize; ++y) {
        const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(lr.pBits) +
                                                                          y * lr.Pitch);
        for (int x = 0; x < kSize; ++x) {
            const unsigned r = (row[x] >> 16) & 255, g = (row[x] >> 8) & 255, b = row[x] & 255;
            sr += r;
            sg += g;
            sb += b;
            if (r > 16 || g > 16 || b > 16) ++covered;
        }
    }
    c.readback->UnlockRect();
    const double n = double(kSize) * kSize;
    s.r = sr / n;
    s.g = sg / n;
    s.b = sb / n;
    s.coverage = covered / n;
    return s;
}

void report(const char* name, HRESULT draw_hr, const Stats& s, double er, double eg, double eb, double ecov) {
    std::printf("DRAW name=%s draw_hr=%08lx read_hr=%08lx mean_r=%.1f mean_g=%.1f mean_b=%.1f coverage=%.3f "
                "expect_r=%.0f expect_g=%.0f expect_b=%.0f expect_coverage=%.2f\n",
                name, (unsigned long)draw_hr, (unsigned long)s.hr, s.r, s.g, s.b, s.coverage, er, eg, eb, ecov);
    const bool pass = SUCCEEDED(draw_hr) && SUCCEEDED(s.hr) && std::fabs(s.r - er) < 12 && std::fabs(s.g - eg) < 12 &&
                      std::fabs(s.b - eb) < 12 && std::fabs(s.coverage - ecov) < 0.05;
    check(pass, name);
}

bool fill_2d(IDirect3DTexture9* t, std::uint32_t rgb, bool alpha_ramp) {
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) return false;
    for (int y = 0; y < 64; ++y) {
        std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<char*>(lr.pBits) + y * lr.Pitch);
        for (int x = 0; x < 64; ++x) row[x] = ((alpha_ramp ? std::uint32_t(x * 255 / 63) : 255u) << 24) | rgb;
    }
    return SUCCEEDED(t->UnlockRect(0));
}

HRESULT quad(Context& c) {
    const Vertex v[4] = {
        {-1, 1, 0.5f, 1, 0, 0}, {1, 1, 0.5f, 1, 1, 0}, {-1, -1, 0.5f, 1, 0, 1}, {1, -1, 0.5f, 1, 1, 1}};
    return c.device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
}

void reset_state(Context& c) {
    IDirect3DDevice9* d = c.device;
    d->SetRenderTarget(0, c.rt_surface.p);
    d->SetDepthStencilSurface(c.depth.p);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    for (DWORD i = 0; i < 16; ++i) {
        d->SetTexture(i, nullptr);
        d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    d->SetVertexDeclaration(c.decl.p);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
}

bool wait_gpu(IDirect3DDevice9* d, double* ms) {
    Com<IDirect3DQuery9> q;
    const double start = now_ms();
    if (FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT, q.out()))) {
        *ms = -1;
        return false;
    }
    q->Issue(D3DISSUE_END);
    for (;;) {
        BOOL done = FALSE;
        const HRESULT hr = q->GetData(&done, sizeof done, D3DGETDATA_FLUSH);
        if (hr == S_OK) {
            *ms = now_ms() - start;
            return true;
        }
        if (FAILED(hr) || now_ms() - start > 5000) {
            *ms = now_ms() - start;
            return false;
        }
        Sleep(0);
    }
}

// --- the game-program sweep: leading def/dcl parse for declared inputs and sampler types ---
struct Declared {
    std::vector<std::pair<BYTE, BYTE>> inputs;
    int sampler_type[16] = {};
    bool any_sampler = false;
};

Declared parse_declarations(const std::vector<DWORD>& code, bool vertex) {
    Declared d;
    std::size_t i = 1;
    while (i < code.size()) {
        const DWORD t = code[i], op = t & 0xFFFF;
        if (op == 0xFFFE) {
            i += 1 + ((t >> 16) & 0x7FFF);
            continue;
        }
        if (op == 0x51 || op == 0x30) {
            i += 6;
            continue;
        } // def, defi
        if (op == 0x2F) {
            i += 3;
            continue;
        } // defb
        if (op != 0x1F || i + 2 >= code.size()) break; // first non-declaration
        const DWORD usage = code[i + 1], reg = code[i + 2];
        const DWORD type = ((reg >> 28) & 7) | ((reg >> 8) & 0x18), number = reg & 0x7FF;
        if (vertex && type == 0) d.inputs.push_back({BYTE(usage & 0x1F), BYTE((usage >> 16) & 0xF)});
        if (!vertex && type == 10 && number < 16) {
            d.sampler_type[number] = int((usage >> 27) & 0xF);
            d.any_sampler = true;
        }
        i += 3;
    }
    return d;
}

std::vector<DWORD> read_program(const std::string& path) {
    std::vector<DWORD> code;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return code;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0 && size % 4 == 0) {
        code.resize(std::size_t(size) / 4);
        if (std::fread(code.data(), 4, code.size(), f) != code.size()) code.clear();
    }
    std::fclose(f);
    return code;
}

void sweep(Context& c, const char* list_path) {
    FILE* list = std::fopen(list_path, "r");
    if (!list) {
        std::printf("SWEEPLIST status=missing path=%s\n", list_path);
        return;
    }
    IDirect3DDevice9* d = c.device;
    std::vector<float> constants(256 * 4, 0.5f);
    char line[1024];
    unsigned index = 0, created = 0, create_failed = 0, drawn = 0, draw_failed = 0, wait_failed = 0;
    unsigned kinds[2][2] = {};
    std::string first_failure = "-";
    while (std::fgets(line, sizeof line, list)) {
        char kind[8] = {}, path[900] = {};
        if (std::sscanf(line, "%7s %899[^\r\n]", kind, path) != 2) continue;
        const bool vertex = !std::strcmp(kind, "vs");
        const char* name = std::strrchr(path, '\\') ? std::strrchr(path, '\\') + 1 : path;
        const std::vector<DWORD> code = read_program(path);
        std::fprintf(stderr, "X3M-SWEEP-BEGIN %u %s\n", index, name);
        std::fflush(stderr);
        HRESULT create_hr = E_FAIL, draw_hr = E_FAIL;
        double wait_ms = -1;
        bool waited = false;
        const DWORD version = code.empty() ? 0 : code[0];
        const unsigned major = (version >> 8) & 0xFF;
        if (!code.empty()) {
            reset_state(c);
            d->SetVertexShaderConstantF(0, constants.data(), 256);
            d->SetPixelShaderConstantF(0, constants.data(), 224);
            if (vertex) {
                Com<IDirect3DVertexShader9> vs;
                create_hr = d->CreateVertexShader(code.data(), vs.out());
                if (SUCCEEDED(create_hr)) {
                    const Declared decl = parse_declarations(code, true);
                    std::vector<D3DVERTEXELEMENT9> elements;
                    WORD offset = 0;
                    for (const auto& in : decl.inputs) {
                        elements.push_back({0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, in.first, in.second});
                        offset += 16;
                    }
                    if (elements.empty()) {
                        elements.push_back({0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
                        offset = 16;
                    }
                    elements.push_back(D3DDECL_END());
                    Com<IDirect3DVertexDeclaration9> vd;
                    std::vector<float> vertices(3 * offset / 4, 0.25f);
                    draw_hr = d->CreateVertexDeclaration(elements.data(), vd.out());
                    if (SUCCEEDED(draw_hr)) {
                        d->SetVertexDeclaration(vd.p);
                        d->SetVertexShader(vs.p);
                        d->SetPixelShader(major >= 3 ? c.ps30_const.p : c.ps20_const.p);
                        d->BeginScene();
                        draw_hr = d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices.data(), offset);
                        d->EndScene();
                    }
                }
            } else {
                Com<IDirect3DPixelShader9> ps;
                create_hr = d->CreatePixelShader(code.data(), ps.out());
                if (SUCCEEDED(create_hr)) {
                    const Declared decl = parse_declarations(code, false);
                    for (DWORD s = 0; s < 16; ++s) {
                        const int type = decl.any_sampler ? decl.sampler_type[s] : (s < 8 ? 2 : 0);
                        IDirect3DBaseTexture9* t = type == 2   ? static_cast<IDirect3DBaseTexture9*>(c.tex_a.p)
                                                   : type == 3 ? static_cast<IDirect3DBaseTexture9*>(c.cube_b.p)
                                                   : type == 4 ? static_cast<IDirect3DBaseTexture9*>(c.volume.p)
                                                               : nullptr;
                        d->SetTexture(s, t);
                    }
                    d->SetVertexShader(major >= 3 ? c.vs30_generic.p : c.vs20_generic.p);
                    d->SetPixelShader(ps.p);
                    d->BeginScene();
                    draw_hr = quad(c);
                    d->EndScene();
                }
            }
            if (SUCCEEDED(create_hr)) waited = wait_gpu(d, &wait_ms);
        }
        std::fprintf(stderr, "X3M-SWEEP-END %u\n", index);
        std::fflush(stderr);
        const unsigned k = vertex ? 0 : 1;
        if (SUCCEEDED(create_hr)) {
            ++created;
            ++kinds[k][0];
        } else {
            ++create_failed;
            ++kinds[k][1];
            if (first_failure == "-") first_failure = name;
        }
        if (SUCCEEDED(create_hr)) {
            if (SUCCEEDED(draw_hr))
                ++drawn;
            else
                ++draw_failed;
            if (!waited) ++wait_failed;
        }
        std::printf(
            "SWEEP index=%u kind=%s name=%s version=%08lx words=%u create_hr=%08lx draw_hr=%08lx wait_ms=%.2f\n", index,
            kind, name, (unsigned long)version, unsigned(code.size()), (unsigned long)create_hr, (unsigned long)draw_hr,
            wait_ms);
        ++index;
        if (d->TestCooperativeLevel() != D3D_OK) {
            std::printf("SWEEPSTOP reason=device_lost index=%u\n", index);
            break;
        }
    }
    std::fclose(list);
    std::printf(
        "SWEEPSUMMARY programs=%u created=%u create_failed=%u vs_created=%u vs_failed=%u ps_created=%u ps_failed=%u "
        "drawn=%u draw_failed=%u wait_failed=%u first_create_failure=%s\n",
        index, created, create_failed, kinds[0][0], kinds[0][1], kinds[1][0], kinds[1][1], drawn, draw_failed,
        wait_failed, first_failure.c_str());
}

LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}

int run(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <d3d9 path|builtin> <d3dx9_37 path> <shader list|->\n", argv[0]);
        return 2;
    }
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    qpc_hz = double(f.QuadPart);
    // Which launcher-passed variables reached the process (dyld prunes DYLD_* it will not honour).
    for (const char* name : {"X3M_ENV_PROBE", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DXVK_LOG_LEVEL",
                             "MVK_CONFIG_LOG_LEVEL"}) {
        char value[512] = {};
        const DWORD n = GetEnvironmentVariableA(name, value, sizeof value);
        std::printf("ENV name=%s present=%d value=%s\n", name, n > 0 && n < sizeof value,
                    n > 0 && n < sizeof value ? token(value).c_str() : "-");
    }
    const bool builtin = !std::strcmp(argv[1], "builtin");
    HMODULE d3d9 = LoadLibraryA(builtin ? "d3d9.dll" : argv[1]);
    std::printf("STEP load_d3d9 status=%s error=%lu\n", d3d9 ? "ok" : "failed", d3d9 ? 0ul : GetLastError());
    if (!d3d9) return 1;
    print_module("d3d9", d3d9);
    HMODULE d3dx = LoadLibraryA(argv[2]);
    std::printf("STEP load_d3dx status=%s error=%lu\n", d3dx ? "ok" : "failed", d3dx ? 0ul : GetLastError());
    if (d3dx) {
        print_module("d3dx9_37", d3dx);
        auto a = GetProcAddress(d3dx, "D3DXAssembleShader");
        std::memcpy(&assemble_fn, &a, sizeof a);
        auto e = GetProcAddress(d3dx, "D3DXCreateEffect");
        std::memcpy(&create_effect_fn, &e, sizeof e);
    }
    Create9 create = nullptr;
    {
        auto p = GetProcAddress(d3d9, "Direct3DCreate9");
        std::memcpy(&create, &p, sizeof p);
    }
    Com<IDirect3D9> d3d;
    if (create) d3d.p = create(D3D_SDK_VERSION);
    std::printf("STEP direct3dcreate9 status=%s\n", d3d.p ? "ok" : "failed");
    if (!d3d.p) return 1;
    // Every module in the process whose name marks a backend (after Direct3DCreate9 loads Vulkan).
    for (const char* name : {"winevulkan.dll", "vulkan-1.dll", "wined3d.dll", "dxgi.dll", "d3d11.dll"}) {
        HMODULE m = GetModuleHandleA(name);
        if (m) print_module(name, m);
    }

    D3DADAPTER_IDENTIFIER9 id{};
    HRESULT hr = d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    std::printf(
        "ADAPTER hr=%08lx driver=%s description=%s device_name=%s vendor=%04lx device=%04lx driver_version=%lu.%lu.%lu.%lu\n",
        (unsigned long)hr, token(id.Driver).c_str(), token(id.Description).c_str(), token(id.DeviceName).c_str(),
        (unsigned long)id.VendorId, (unsigned long)id.DeviceId, (unsigned long)HIWORD(id.DriverVersion.HighPart),
        (unsigned long)LOWORD(id.DriverVersion.HighPart), (unsigned long)HIWORD(id.DriverVersion.LowPart),
        (unsigned long)LOWORD(id.DriverVersion.LowPart));
    D3DCAPS9 caps{};
    hr = d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    std::printf("CAPS hr=%08lx vs=%04lx ps=%04lx max_textures=%lu rts=%lu vs_consts=%lu max_tex=%lux%lu\n",
                (unsigned long)hr, (unsigned long)(caps.VertexShaderVersion & 0xFFFF),
                (unsigned long)(caps.PixelShaderVersion & 0xFFFF), (unsigned long)caps.MaxSimultaneousTextures,
                (unsigned long)caps.NumSimultaneousRTs, (unsigned long)caps.MaxVertexShaderConst,
                (unsigned long)caps.MaxTextureWidth, (unsigned long)caps.MaxTextureHeight);
    struct {
        const char* name;
        DWORD usage;
        D3DRESOURCETYPE type;
        D3DFORMAT format;
    } formats[] = {
        {"RESZ", D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, D3DFORMAT(MAKEFOURCC('R', 'E', 'S', 'Z'))},
        {"INTZ", D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'))},
        {"D24X8_texture", D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24X8},
        {"A16B16G16R16F_rt", D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F},
        {"A32B32G32R32F_rt", D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F},
        {"R32F_rt", D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F},
        {"G32R32F_rt", D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_G32R32F},
        {"NULL_rt", D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, D3DFORMAT(MAKEFOURCC('N', 'U', 'L', 'L'))},
    };
    for (const auto& fmt : formats)
        std::printf("FORMAT name=%s hr=%08lx\n", fmt.name,
                    (unsigned long)d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                          fmt.usage, fmt.type, fmt.format));

    WNDCLASSA wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3m_d3d9_smoke";
    RegisterClassA(&wc);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "x3m d3d9 smoke", WS_OVERLAPPEDWINDOW, 0, 0, kSize, kSize,
                                  nullptr, nullptr, wc.hInstance, nullptr); // never shown
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = kSize;
    pp.BackBufferHeight = kSize;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const DWORD behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE | D3DCREATE_FPU_PRESERVE;
    Com<IDirect3DDevice9> device;
    const double t0 = now_ms();
    hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, behavior, &pp, device.out());
    std::printf("DEVICE hr=%08lx behavior=%08lx create_ms=%.1f\n", (unsigned long)hr, (unsigned long)behavior,
                now_ms() - t0);
    check(SUCCEEDED(hr), "device_create");
    if (FAILED(hr)) return 1;

    Context c;
    c.device = device.p;
    IDirect3DDevice9* d = device.p;
    bool ok = SUCCEEDED(d->CreateTexture(kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                         c.rt.out(), nullptr));
    ok = ok && SUCCEEDED(c.rt->GetSurfaceLevel(0, c.rt_surface.out()));
    ok = ok && SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                        c.readback.out(), nullptr));
    ok = ok && SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, c.backbuffer.out()));
    ok = ok && SUCCEEDED(d->GetDepthStencilSurface(c.depth.out()));
    const bool managed = SUCCEEDED(d->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, c.tex_a.out(),
                                                    nullptr)) &&
                         SUCCEEDED(d->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, c.tex_b.out(),
                                                    nullptr)) &&
                         fill_2d(c.tex_a.p, 0xC82828u, true) && fill_2d(c.tex_b.p, 0x2828C8u, false);
    check(managed, "managed_textures");
    bool cube = SUCCEEDED(d->CreateCubeTexture(16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, c.cube_b.out(), nullptr));
    for (int face = 0; cube && face < 6; ++face) {
        D3DLOCKED_RECT lr{};
        cube = SUCCEEDED(c.cube_b->LockRect(D3DCUBEMAP_FACES(face), 0, &lr, nullptr, 0));
        for (int y = 0; cube && y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                reinterpret_cast<std::uint32_t*>(static_cast<char*>(lr.pBits) + y * lr.Pitch)[x] = 0xFF2828C8u;
        if (cube) c.cube_b->UnlockRect(D3DCUBEMAP_FACES(face), 0);
    }
    check(cube, "managed_cube");
    if (SUCCEEDED(d->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, c.volume.out(), nullptr))) {
        D3DLOCKED_BOX box{};
        if (SUCCEEDED(c.volume->LockBox(0, &box, nullptr, 0))) {
            for (int z = 0; z < 4; ++z)
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        reinterpret_cast<std::uint32_t*>(static_cast<char*>(box.pBits) + z * box.SlicePitch +
                                                         y * box.RowPitch)[x] = 0xFF808080u;
            c.volume->UnlockBox(0);
        }
    }
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    ok = ok && SUCCEEDED(d->CreateVertexDeclaration(elements, c.decl.out()));
    check(ok, "resources");
    if (!ok) return 1;

    struct VsSrc {
        const char* src;
        Com<IDirect3DVertexShader9>* out;
        const char* name;
    };
    struct PsSrc {
        const char* src;
        Com<IDirect3DPixelShader9>* out;
        const char* name;
    };
    for (const VsSrc& v :
         {VsSrc{kVs30, &c.vs30, "vs30"}, VsSrc{kVs11, &c.vs11, "vs11"},
          VsSrc{kVs30Generic, &c.vs30_generic, "vs30_generic"}, VsSrc{kVs20Generic, &c.vs20_generic, "vs20_generic"}}) {
        const std::vector<DWORD> code = assemble(v.src, v.name);
        const HRESULT h = code.empty() ? E_FAIL : d->CreateVertexShader(code.data(), v.out->out());
        std::printf("SHADER name=%s hr=%08lx\n", v.name, (unsigned long)h);
    }
    for (const PsSrc& p :
         {PsSrc{kPs30Tex, &c.ps30_tex, "ps30_tex"}, PsSrc{kPs30TexCube, &c.ps30_tex_cube, "ps30_tex_cube"},
          PsSrc{kPs11Two, &c.ps11_two, "ps11_two"}, PsSrc{kPs30Depth, &c.ps30_depth, "ps30_depth"},
          PsSrc{kPs30Const, &c.ps30_const, "ps30_const"}, PsSrc{kPs20Const, &c.ps20_const, "ps20_const"}}) {
        const std::vector<DWORD> code = assemble(p.src, p.name);
        const HRESULT h = code.empty() ? E_FAIL : d->CreatePixelShader(code.data(), p.out->out());
        std::printf("SHADER name=%s hr=%08lx\n", p.name, (unsigned long)h);
    }

    auto draw_case = [&](const char* name, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps, auto setup, double er,
                         double eg, double eb, double ecov) {
        reset_state(c);
        setup();
        d->SetVertexShader(vs);
        d->SetPixelShader(ps);
        d->BeginScene();
        const HRESULT h = quad(c);
        d->EndScene();
        report(name, h, read_rt(c), er, eg, eb, ecov);
    };
    // texA = (200,40,40) with alpha = x ramp, texB = (40,40,200) opaque, cube faces = texB.
    draw_case("quad_vs30_ps30", c.vs30.p, c.ps30_tex.p, [&] { d->SetTexture(0, c.tex_a.p); }, 200, 40, 40, 1.0);
    draw_case(
        "alpha_test", c.vs30.p, c.ps30_tex.p,
        [&] {
            d->SetTexture(0, c.tex_a.p);
            d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            d->SetRenderState(D3DRS_ALPHAREF, 128);
            d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        },
        100, 20, 20, 0.5);
    draw_case(
        "two_samplers_ps11", c.vs11.p, c.ps11_two.p,
        [&] {
            d->SetTexture(0, c.tex_a.p);
            d->SetTexture(1, c.tex_b.p);
        },
        240, 80, 240, 1.0);
    draw_case(
        "two_samplers_ps30_2d_cube", c.vs30.p, c.ps30_tex_cube.p,
        [&] {
            d->SetTexture(0, c.tex_a.p);
            d->SetTexture(1, c.cube_b.p);
        },
        240, 80, 240, 1.0);
    { // Two-stage fixed function (pretransformed, two texture coordinate sets).
        reset_state(c);
        d->SetVertexDeclaration(nullptr);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX2);
        d->SetTexture(0, c.tex_a.p);
        d->SetTexture(1, c.tex_b.p);
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
        d->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        d->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
        d->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        d->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        const float s = float(kSize) - 0.5f;
        const FfVertex v[4] = {{-0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 0},
                               {s, -0.5f, 0.5f, 1, 1, 0, 1, 0},
                               {-0.5f, s, 0.5f, 1, 0, 1, 0, 1},
                               {s, s, 0.5f, 1, 1, 1, 1, 1}};
        d->BeginScene();
        const HRESULT h = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FfVertex));
        d->EndScene();
        report("two_stage_fixed_function", h, read_rt(c), 240, 80, 240, 1.0);
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        d->SetFVF(0);
    }
    // MANAGED re-lock after first use: the new contents must reach the GPU.
    fill_2d(c.tex_a.p, 0x28C828u, false);
    draw_case("managed_relock", c.vs30.p, c.ps30_tex.p, [&] { d->SetTexture(0, c.tex_a.p); }, 40, 200, 40, 1.0);
    fill_2d(c.tex_a.p, 0xC82828u, true);

    { // StretchRect: RT -> smaller RT (linear), backbuffer -> RT, depth -> depth.
        draw_case("stretch_source", c.vs30.p, c.ps30_tex.p, [&] { d->SetTexture(0, c.tex_b.p); }, 40, 40, 200, 1.0);
        Com<IDirect3DSurface9> half;
        HRESULT h = d->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, half.out(),
                                          nullptr);
        if (SUCCEEDED(h)) {
            RECT r = {0, 0, kSize / 2, kSize / 2};
            d->ColorFill(half.p, nullptr, 0);
            h = d->StretchRect(c.rt_surface.p, nullptr, half.p, &r, D3DTEXF_LINEAR);
        }
        report("stretchrect_rt_scaled", h, SUCCEEDED(h) ? read_rt(c, half.p) : Stats{}, 10, 10, 50, 0.25);
        h = d->ColorFill(c.backbuffer.p, nullptr, D3DCOLOR_XRGB(10, 200, 10));
        if (SUCCEEDED(h)) h = d->StretchRect(c.backbuffer.p, nullptr, c.rt_surface.p, nullptr, D3DTEXF_NONE);
        report("stretchrect_backbuffer", h, read_rt(c), 10, 200, 10, 1.0);
        Com<IDirect3DSurface9> depth_copy;
        h = d->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, depth_copy.out(),
                                         nullptr);
        if (SUCCEEDED(h)) h = d->StretchRect(c.depth.p, nullptr, depth_copy.p, nullptr, D3DTEXF_NONE);
        std::printf("STRETCHDEPTH hr=%08lx\n", (unsigned long)h);
        check(SUCCEEDED(h), "stretchrect_depth");
    }
    { // Event and occlusion queries.
        double ms = 0;
        const bool event = wait_gpu(d, &ms);
        std::printf("QUERY type=event ok=%d ms=%.2f\n", int(event), ms);
        check(event, "event_query");
        Com<IDirect3DQuery9> occlusion;
        HRESULT h = d->CreateQuery(D3DQUERYTYPE_OCCLUSION, occlusion.out());
        DWORD pixels = 0;
        if (SUCCEEDED(h)) {
            reset_state(c);
            d->SetTexture(0, c.tex_b.p);
            d->SetVertexShader(c.vs30.p);
            d->SetPixelShader(c.ps30_tex.p);
            d->BeginScene();
            occlusion->Issue(D3DISSUE_BEGIN);
            quad(c);
            occlusion->Issue(D3DISSUE_END);
            d->EndScene();
            const double start = now_ms();
            do {
                h = occlusion->GetData(&pixels, sizeof pixels, D3DGETDATA_FLUSH);
            } while (h == S_FALSE && now_ms() - start < 5000);
        }
        std::printf("QUERY type=occlusion hr=%08lx pixels=%lu expect=%d\n", (unsigned long)h, (unsigned long)pixels,
                    kSize * kSize);
        check(h == S_OK && pixels == DWORD(kSize * kSize), "occlusion_query");
        for (D3DQUERYTYPE t : {D3DQUERYTYPE_TIMESTAMP, D3DQUERYTYPE_TIMESTAMPDISJOINT, D3DQUERYTYPE_TIMESTAMPFREQ})
            std::printf("QUERY type=%d supported_hr=%08lx\n", int(t), (unsigned long)d->CreateQuery(t, nullptr));
    }
    for (const char* depth_format : {"D24X8", "INTZ"}) {
        // RESZ as the proxy does it (ownership copy_depth): texture on s0, POINTSIZE = RESZ code, no draw.
        const D3DFORMAT format = !std::strcmp(depth_format, "INTZ") ? D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'))
                                                                    : D3DFMT_D24X8;
        Com<IDirect3DTexture9> snapshot;
        HRESULT h = d->CreateTexture(kSize, kSize, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT, snapshot.out(),
                                     nullptr);
        HRESULT resz = E_FAIL;
        if (SUCCEEDED(h)) {
            reset_state(c);
            d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 0.5f, 0);
            d->SetTexture(0, snapshot.p);
            resz = d->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000u);
            d->SetRenderState(D3DRS_POINTSIZE, 0x3f800000u);
        }
        for (float reference : {0.25f, 0.75f}) {
            const float constants[4] = {0, 0, reference, 1};
            char name[64];
            std::snprintf(name, sizeof name, "resz_%s_ref%.2f", depth_format, reference);
            Stats s;
            HRESULT dh = h;
            if (SUCCEEDED(h)) {
                reset_state(c);
                d->SetTexture(0, snapshot.p);
                d->SetPixelShaderConstantF(0, constants, 1);
                d->SetVertexShader(c.vs30.p);
                d->SetPixelShader(c.ps30_depth.p);
                d->BeginScene();
                dh = quad(c);
                d->EndScene();
                s = read_rt(c);
            }
            std::printf(
                "RESZ format=%s create_hr=%08lx resz_hr=%08lx reference=%.2f draw_hr=%08lx read_hr=%08lx mean_r=%.1f\n",
                depth_format, (unsigned long)h, (unsigned long)resz, reference, (unsigned long)dh, (unsigned long)s.hr,
                s.r);
        }
    }
    { // D3DXCreateEffect with the given d3dx9_37 (HLSL compile, Begin/BeginPass, draw).
        Com<ID3DXEffect> effect;
        Com<ID3DXBuffer> errors;
        HRESULT h = create_effect_fn ? create_effect_fn(d, kEffect, UINT(std::strlen(kEffect)), nullptr, nullptr, 0,
                                                        nullptr, effect.out(), errors.out())
                                     : E_NOINTERFACE;
        std::printf("EFFECT create_hr=%08lx error=%s\n", (unsigned long)h,
                    errors.p ? token(static_cast<const char*>(errors->GetBufferPointer())).substr(0, 200).c_str()
                             : "-");
        Stats s;
        HRESULT dh = h;
        if (SUCCEEDED(h)) {
            reset_state(c);
            effect->SetTexture("tex", c.tex_a.p);
            UINT passes = 0;
            dh = effect->Begin(&passes, 0);
            if (SUCCEEDED(dh)) {
                d->BeginScene();
                dh = effect->BeginPass(0);
                if (SUCCEEDED(dh)) {
                    dh = quad(c);
                    effect->EndPass();
                }
                effect->End();
                d->EndScene();
            }
            s = read_rt(c);
        }
        report("d3dx_effect", dh, s, 64, 128, 191, 1.0);
    }
    {
        reset_state(c);
        const HRESULT h = d->Present(nullptr, nullptr, nullptr, nullptr);
        std::printf("PRESENT hr=%08lx\n", (unsigned long)h);
        check(SUCCEEDED(h), "present_hidden_window");
    }
    if (std::strcmp(argv[3], "-")) sweep(c, argv[3]);
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return 0; // Context, device and Direct3D release in reverse declaration order.
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return run(argc, argv);
}
