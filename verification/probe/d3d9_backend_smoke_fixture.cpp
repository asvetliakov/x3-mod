// D3D9 backend smoke probe: can a given d3d9.dll (wined3d, CrossOver's DXVK, a
// DXVK build over MoltenVK) stand in as the backend the proxy forwards to?
//
//   d3d9_backend_smoke_fixture.exe <d3d9 path | builtin> <d3dx9_37 path> <shader list | -> [pipeline-cost list] [occlusion-cost]
//
// Loads the d3d9 by path (or "d3d9.dll" by name for builtin), creates the game's
// device shape (HWVP | PUREDEVICE | FPU_PRESERVE = 0x52, windowed, A8R8G8B8,
// D24S8, hidden window), draws small cases into a 256x256 A8R8G8B8 render
// target and reads each back (channel means, coverage = share of pixels with
// any channel > 16): a textured vs_3_0/ps_3_0 quad, alpha test, two samplers
// in ps_1_1 (aliased-sampler path), ps_3_0 2D + cube, a two-stage fixed-function
// draw, a MANAGED texture re-lock, StretchRect (RT->RT, backbuffer->RT, depth),
// event / occlusion queries, private data (unset GUID, set, round trip), RESZ into D24X8 and INTZ, a D3DXCreateEffect draw
// with the given d3dx9_37 and Present. Then every listed game program is created
// and, when that succeeds, drawn once with a generic partner and a GPU wait, so
// pipeline compilation happens inside SWEEP markers that are also written to
// stderr (where DXVK / MoltenVK / wined3d log) for attribution by the runner.
// With a pipeline-cost list (lines "<ps path>\t<vs path|->\t<second vs path|->"), each game pixel
// shader is drawn with its partner in a fixed step order and the QPC time of draw + event-query wait is
// printed per step (a: opaque S1/D1/A8R8G8B8, b: alpha blend, c: additive z-write off, d: position+uv
// declaration, e: A16B16G16R16F target, f: a again, g: second vertex shader), after a warm-up of every
// state/declaration/target with the fixture's own shaders so render passes and layouts exist already.
// With the literal argument "occlusion-cost", the per-part CPU cost of an occlusion-query box test (issued at a
// draw site, read with GetData(..., 0) next frame) is measured over 60 presented frames at 128, 256, 384 and 512 tests per
// frame and three paces (see occlusion_cost).
// Documented D3D9 / Win32 / D3DX APIs only. Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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

bool wait_gpu(IDirect3DDevice9* d, double* ms, double limit_ms = 5000) {
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
        if (FAILED(hr) || now_ms() - start > limit_ms) {
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

// --- pipeline cost: what does a first-use (shader, state) combination cost on this backend? ---
struct PcVertex1 { // D1: position + normal + uv
    float x, y, z, nx, ny, nz, u, v;
};
struct PcVertex2 { // D2: position + uv
    float x, y, z, u, v;
};

std::string leaf(const std::string& path) {
    const std::size_t at = path.find_last_of('\\');
    std::string name = at == std::string::npos ? path : path.substr(at + 1);
    const std::size_t dot = name.rfind(".bin");
    return dot == std::string::npos ? name : name.substr(0, dot);
}

double median(std::vector<double> v) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

void pipeline_cost(Context& c, const char* list_path) {
    IDirect3DDevice9* d = c.device;
    struct Entry {
        std::string ps_path, vs_path, vs2_path;
        Com<IDirect3DPixelShader9> ps;
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DVertexShader9* vs2 = nullptr;
        Declared decl;
        double create_us = -1;
    };
    std::vector<std::unique_ptr<Entry>> entries; // Com is neither copyable nor movable
    FILE* list = std::fopen(list_path, "r");
    if (!list) {
        std::printf("PIPELINE_COST_STATUS status=missing path=%s\n", list_path);
        return;
    }
    char line[2048];
    while (std::fgets(line, sizeof line, list)) {
        char a[900] = {}, b[900] = {}, e[900] = {};
        if (std::sscanf(line, "%899[^\t\r\n]\t%899[^\t\r\n]\t%899[^\t\r\n]", a, b, e) != 3) continue;
        entries.push_back(std::make_unique<Entry>());
        entries.back()->ps_path = a;
        entries.back()->vs_path = b;
        entries.back()->vs2_path = e;
    }
    std::fclose(list);

    // Resources: two vertex buffers (D1 32-byte and D2 20-byte vertices), two declarations, an FP16 target.
    const D3DVERTEXELEMENT9 d1[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                    {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
                                    {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                    D3DDECL_END()};
    const D3DVERTEXELEMENT9 d2[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                    {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                    D3DDECL_END()};
    const PcVertex1 v1[4] = {{-1, 1, 0.5f, 0, 0, -1, 0, 0},
                             {1, 1, 0.5f, 0, 0, -1, 1, 0},
                             {-1, -1, 0.5f, 0, 0, -1, 0, 1},
                             {1, -1, 0.5f, 0, 0, -1, 1, 1}};
    const PcVertex2 v2[4] = {{-1, 1, 0.5f, 0, 0}, {1, 1, 0.5f, 1, 0}, {-1, -1, 0.5f, 0, 1}, {1, -1, 0.5f, 1, 1}};
    Com<IDirect3DVertexDeclaration9> decl1, decl2;
    Com<IDirect3DVertexBuffer9> vb1, vb2;
    Com<IDirect3DTexture9> fp16;
    Com<IDirect3DSurface9> fp16_surface;
    bool ok = SUCCEEDED(d->CreateVertexDeclaration(d1, decl1.out())) &&
              SUCCEEDED(d->CreateVertexDeclaration(d2, decl2.out())) &&
              SUCCEEDED(d->CreateVertexBuffer(sizeof v1, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, vb1.out(), nullptr)) &&
              SUCCEEDED(d->CreateVertexBuffer(sizeof v2, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, vb2.out(), nullptr)) &&
              SUCCEEDED(d->CreateTexture(kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                         D3DPOOL_DEFAULT, fp16.out(), nullptr)) &&
              SUCCEEDED(fp16->GetSurfaceLevel(0, fp16_surface.out()));
    for (int i = 0; ok && i < 2; ++i) {
        IDirect3DVertexBuffer9* vb = i ? vb2.p : vb1.p;
        const void* src = i ? static_cast<const void*>(v2) : static_cast<const void*>(v1);
        const UINT size = i ? sizeof v2 : sizeof v1;
        void* dst = nullptr;
        ok = SUCCEEDED(vb->Lock(0, size, &dst, 0));
        if (ok) {
            std::memcpy(dst, src, size);
            vb->Unlock();
        }
    }
    if (!ok) {
        std::printf("PIPELINE_COST_STATUS status=resources_failed\n");
        check(false, "pipeline_cost_resources");
        return;
    }

    // Shader creation, timed (DXSO translation happens here on DXVK; wined3d defers GLSL to the draw).
    std::vector<std::pair<std::string, std::unique_ptr<Com<IDirect3DVertexShader9>>>> vertex_shaders;
    auto vertex_shader = [&](const std::string& path, IDirect3DVertexShader9* fallback) -> IDirect3DVertexShader9* {
        if (path == "-") return fallback;
        for (auto& vs : vertex_shaders)
            if (vs.first == path) return vs.second->p;
        const std::vector<DWORD> code = read_program(path);
        auto holder = std::make_unique<Com<IDirect3DVertexShader9>>();
        const double t = now_ms();
        const HRESULT h = code.empty() ? E_FAIL : d->CreateVertexShader(code.data(), holder->out());
        std::printf("PIPELINE_COST_SHADER name=%s create_hr=%08lx create_us=%.0f words=%u\n", leaf(path).c_str(),
                    (unsigned long)h, 1000.0 * (now_ms() - t), unsigned(code.size()));
        IDirect3DVertexShader9* created = SUCCEEDED(h) ? holder->p : fallback;
        vertex_shaders.emplace_back(path, std::move(holder));
        return created;
    };
    for (auto& entry : entries) {
        Entry& e = *entry;
        const std::vector<DWORD> code = read_program(e.ps_path);
        const double t = now_ms();
        const HRESULT h = code.empty() ? E_FAIL : d->CreatePixelShader(code.data(), e.ps.out());
        e.create_us = 1000.0 * (now_ms() - t);
        std::printf("PIPELINE_COST_SHADER name=%s create_hr=%08lx create_us=%.0f words=%u\n", leaf(e.ps_path).c_str(),
                    (unsigned long)h, e.create_us, unsigned(code.size()));
        if (SUCCEEDED(h)) e.decl = parse_declarations(code, false);
        e.vs = vertex_shader(e.vs_path, c.vs30_generic.p);
        e.vs2 = vertex_shader(e.vs2_path, c.vs30_generic.p);
        if (e.vs2 == e.vs) e.vs2 = c.vs30_generic.p; // the second partner must differ from the first
    }

    std::vector<float> constants(256 * 4, 0.5f);
    auto bind = [&](char step, IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps, const Declared* decl) {
        reset_state(c);
        d->SetVertexShaderConstantF(0, constants.data(), 256);
        d->SetPixelShaderConstantF(0, constants.data(), 224);
        d->SetRenderTarget(0, step == 'e' ? fp16_surface.p : c.rt_surface.p);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, step == 'c' ? FALSE : TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, step == 'b' || step == 'c');
        d->SetRenderState(D3DRS_SRCBLEND, step == 'c' ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, step == 'c' ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        if (step == 'd') {
            d->SetVertexDeclaration(decl2.p);
            d->SetStreamSource(0, vb2.p, 0, sizeof(PcVertex2));
        } else {
            d->SetVertexDeclaration(decl1.p);
            d->SetStreamSource(0, vb1.p, 0, sizeof(PcVertex1));
        }
        for (DWORD s = 0; s < 16; ++s) {
            const int type = !decl ? 0 : decl->any_sampler ? decl->sampler_type[s] : (s < 8 ? 2 : 0);
            IDirect3DBaseTexture9* t = type == 2   ? static_cast<IDirect3DBaseTexture9*>(c.tex_a.p)
                                       : type == 3 ? static_cast<IDirect3DBaseTexture9*>(c.cube_b.p)
                                       : type == 4 ? static_cast<IDirect3DBaseTexture9*>(c.volume.p)
                                                   : nullptr;
            d->SetTexture(s, t);
        }
        d->SetVertexShader(vs);
        d->SetPixelShader(ps);
        double drain = 0;
        wait_gpu(d, &drain, 60000); // the clear and state changes land before the clock starts
    };
    // One timed draw + event-query wait (60 s limit); the draw is where the backend builds the pipeline.
    auto timed = [&](HRESULT* draw_hr, bool* waited) {
        const double t = now_ms();
        d->BeginScene();
        *draw_hr = d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
        d->EndScene();
        double ms = 0;
        *waited = wait_gpu(d, &ms, 60000);
        return 1000.0 * (now_ms() - t);
    };
    const char steps[] = "abcdefg";
    // Warm-up with the fixture's own pair: every target, declaration and blend/depth state once, so the
    // measured steps carry only what is specific to the game program.
    for (const char* s = steps; *s; ++s) {
        HRESULT h = E_FAIL;
        bool waited = false;
        bind(*s, c.vs30_generic.p, c.ps30_const.p, nullptr);
        const double us = timed(&h, &waited);
        std::printf("PIPELINE_WARMUP step=%c us=%.0f draw_hr=%08lx waited=%d\n", *s, us, (unsigned long)h, int(waited));
    }
    Sleep(3000); // let a backend's background workers (DXVK state cache) finish what shader creation queued
    std::vector<double> per_step[7];
    unsigned failed_draws = 0;
    for (auto& entry : entries) {
        Entry& e = *entry;
        if (!e.ps.p) continue;
        const std::string name = leaf(e.ps_path);
        for (int i = 0; i < 7; ++i) {
            const char step = steps[i];
            IDirect3DVertexShader9* vs = step == 'g' ? e.vs2 : e.vs;
            bind(step, vs, e.ps.p, &e.decl);
            std::fprintf(stderr, "X3M-PC-BEGIN %s %c\n", name.c_str(), step);
            std::fflush(stderr);
            HRESULT h = E_FAIL;
            bool waited = false;
            const double us = timed(&h, &waited);
            std::fprintf(stderr, "X3M-PC-END %s %c\n", name.c_str(), step);
            std::fflush(stderr);
            if (FAILED(h) || !waited) ++failed_draws;
            per_step[i].push_back(us);
            std::printf("pipeline_cost ps=%s step=%c us=%.0f vs=%s draw_hr=%08lx waited=%d", name.c_str(), step, us,
                        step == 'g' ? (e.vs2 == c.vs30_generic.p ? "vs30_generic" : leaf(e.vs2_path).c_str())
                                    : (e.vs == c.vs30_generic.p ? "vs30_generic" : leaf(e.vs_path).c_str()),
                        (unsigned long)h, int(waited));
            if (step == 'a') std::printf(" ps_create_us=%.0f", e.create_us);
            std::printf("\n");
        }
        if (d->TestCooperativeLevel() != D3D_OK) {
            std::printf("PIPELINE_COST_STATUS status=device_lost\n");
            break;
        }
    }
    for (int i = 0; i < 7; ++i) {
        double max = per_step[i].empty() ? -1 : *std::max_element(per_step[i].begin(), per_step[i].end());
        std::printf("pipeline_cost_summary step=%c n=%u median_us=%.0f max_us=%.0f\n", steps[i],
                    unsigned(per_step[i].size()), median(per_step[i]), max);
    }
    check(failed_draws == 0 && !per_step[0].empty(), "pipeline_cost_draws");
}

// --- occlusion cost: what does one occlusion query around a box test cost per part on this backend? ---
// Per frame into the backbuffer (D24S8): a hull quad (z write on) covering the left half, then four timed loops
// of n iterations each (QPC around each loop), n = 128 and 512:
//   A  part draws alone (textured vs_3_0/ps_3_0 box, the game state), the baseline;
//   B  part draw + a 12-triangle box test with a per-test Lock(NOOVERWRITE) of 8 corners in a shared dynamic
//      buffer: box state set (shaders, declaration, stream, indices, colour write 0, z write off), query Begin,
//      DrawIndexedPrimitive, query End, game state restored; (B - A) / n is the per-part cost of that test;
//   C  n such box tests in a tight loop (box state set once), the raw query + box cost;
//   D  part draw + the proxy's test shape: the box's screen rectangle at its nearest depth in two
//      vertex-shader constants (c252 origin, c253 extent) over a static 4-vertex strip, no Lock, no indices,
//      z write off and the colour write mask left alone: blend ZERO/ONE keeps every target's value (a colour
//      write mask of 0 costs ~50 us per test on DXVK over MoltenVK, measured below).
// all_sets runs B, C and D (the CPU breakdown); otherwise only A and D run (the production shape alone, so
// readiness and GPU time are those of n queries per frame), followed by a GPU timing of D (event query, with and
// without the tests) and a drain. Then Present; at the start of the next frame GetData(..., 0) (no flush) on the
// previous frame's queries, timed, counting ready / not ready (and, two frames on, what became ready by then), checking hidden
// boxes (behind the hull) read 0 and visible ones > 0. Two warm-up frames are excluded. Paces: unpaced (0), 13 ms
// per frame (the game's measured DXVK dt_p50) and 20 ms (longer than a 60 Hz present interval, so a
// display-throttled hidden window cannot keep the GPU behind).
constexpr int kOcMax = 512;
constexpr int kOcFrames = 60, kOcWarm = 2, kOcRing = 3;

void occlusion_cost(Context& c, int n, double pace_ms, bool all_sets) {
    const int k_first = all_sets ? 0 : 2, sets = 3 - k_first;
    IDirect3DDevice9* d = c.device;
    struct BoxVertex {
        float x, y, z, w;
    };
    const D3DVERTEXELEMENT9 box_elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> box_decl;
    Com<IDirect3DVertexBuffer9> box_vb, part_vb, rect_vb;
    Com<IDirect3DIndexBuffer9> box_ib;
    Com<IDirect3DVertexShader9> box_vs, rect_vs;
    Com<IDirect3DPixelShader9> box_ps;
    std::vector<std::unique_ptr<Com<IDirect3DQuery9>[]>> queries; // [slot * 3 + set][part]
    const std::vector<DWORD> vs_code = assemble("vs_3_0\n dcl_position v0\n dcl_position o0\n mov o0, v0\n", "box_vs");
    const std::vector<DWORD> ps_code = assemble("ps_3_0\n def c0, 0, 0, 0, 0\n mov oC0, c0\n", "box_ps");
    const std::vector<DWORD> rect_code =
        assemble("vs_3_0\n dcl_position v0\n dcl_position o0\n mad o0, v0, c253, c252\n", "rect_vs");
    bool ok = n > 0 && n <= kOcMax && !vs_code.empty() && !ps_code.empty() && !rect_code.empty() &&
              SUCCEEDED(d->CreateVertexShader(vs_code.data(), box_vs.out())) &&
              SUCCEEDED(d->CreateVertexShader(rect_code.data(), rect_vs.out())) &&
              SUCCEEDED(d->CreatePixelShader(ps_code.data(), box_ps.out())) &&
              SUCCEEDED(d->CreateVertexDeclaration(box_elements, box_decl.out())) &&
              SUCCEEDED(d->CreateVertexBuffer(2 * kOcMax * 8 * sizeof(BoxVertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                              D3DPOOL_DEFAULT, box_vb.out(), nullptr)) &&
              SUCCEEDED(d->CreateIndexBuffer(36 * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                             box_ib.out(), nullptr)) &&
              SUCCEEDED(d->CreateVertexBuffer(kOcMax * 36 * sizeof(Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                              part_vb.out(), nullptr)) &&
              SUCCEEDED(d->CreateVertexBuffer(4 * sizeof(BoxVertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                              rect_vb.out(), nullptr));
    for (int q = 0; ok && q < kOcRing * 3; ++q) {
        queries.emplace_back(new Com<IDirect3DQuery9>[kOcMax]);
        for (int i = 0; ok && i < n; ++i) ok = SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION, queries[q][i].out()));
    }
    static const WORD kBoxIndex[36] = {0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7, 0, 4, 1, 1, 4, 5,
                                       2, 3, 6, 6, 3, 7, 0, 2, 4, 4, 2, 6, 1, 5, 3, 3, 5, 7};
    // Part i: a box on a 16-column grid of n / 16 rows; even columns sit on the left half (behind the hull at z 0.3),
    // odd columns on the right half (visible).
    const int rows = n / 16 ? n / 16 : 1;
    auto corner = [rows](int i, int k, float* out) {
        const int col = i % 16, row = i / 16;
        const float cx = -1.0f + float(col / 2 + (col & 1) * 8) * 0.125f + 0.0625f;
        const float cy = -1.0f + (float(row) + 0.5f) * 2.0f / float(rows), hy = 0.6f / float(rows);
        out[0] = cx + ((k & 1) ? 0.05f : -0.05f);
        out[1] = cy + ((k & 2) ? hy : -hy);
        out[2] = (k & 4) ? 0.7f : 0.6f;
        out[3] = 1.0f;
    };
    void* p = nullptr;
    if (ok && SUCCEEDED(box_ib->Lock(0, 0, &p, 0))) {
        std::memcpy(p, kBoxIndex, sizeof kBoxIndex);
        box_ib->Unlock();
    } else
        ok = false;
    if (ok && SUCCEEDED(part_vb->Lock(0, 0, &p, 0))) {
        Vertex* v = static_cast<Vertex*>(p);
        for (int i = 0; i < n; ++i)
            for (int t = 0; t < 36; ++t) {
                float q[4];
                corner(i, kBoxIndex[t], q);
                v[i * 36 + t] = {q[0], q[1], q[2], 1.0f, float(kBoxIndex[t] & 1), float((kBoxIndex[t] >> 1) & 1)};
            }
        part_vb->Unlock();
    } else
        ok = false;
    if (ok && SUCCEEDED(rect_vb->Lock(0, 0, &p, 0))) { // strip corners (0,0) (1,0) (0,1) (1,1)
        BoxVertex* v = static_cast<BoxVertex*>(p);
        for (int k = 0; k < 4; ++k) v[k] = {float(k & 1), float(k >> 1), 0, 0};
        rect_vb->Unlock();
    } else
        ok = false;
    check(ok, "occlusion_cost_resources");
    if (!ok) return;
    Com<IDirect3DSurface9> backbuffer;
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backbuffer.out());
    const Vertex hull[4] = {{-1, 1, 0.3f, 1, 0, 0}, {0, 1, 0.3f, 1, 1, 0}, {-1, -1, 0.3f, 1, 0, 1}, {0, -1, 0.3f, 1, 1, 1}};
    auto game_state = [&]() {
        d->SetVertexDeclaration(c.decl.p);
        d->SetStreamSource(0, part_vb.p, 0, sizeof(Vertex));
        d->SetVertexShader(c.vs30.p);
        d->SetPixelShader(c.ps30_tex.p);
        d->SetTexture(0, c.tex_b.p);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    };
    unsigned box_slot = 0; // ring position in the dynamic buffer (8 corners per test)
    auto box_test = [&](int i, IDirect3DQuery9* q, bool switch_state) {
        void* bp = nullptr;
        const DWORD flags = box_slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD;
        if (SUCCEEDED(box_vb->Lock(box_slot * 8 * sizeof(BoxVertex), 8 * sizeof(BoxVertex), &bp, flags))) {
            float* out = static_cast<float*>(bp);
            for (int k = 0; k < 8; ++k) corner(i, k, out + 4 * k);
            box_vb->Unlock();
        }
        if (switch_state) {
            d->SetVertexDeclaration(box_decl.p);
            d->SetStreamSource(0, box_vb.p, 0, sizeof(BoxVertex));
            d->SetIndices(box_ib.p);
            d->SetVertexShader(box_vs.p);
            d->SetPixelShader(box_ps.p);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        }
        q->Issue(D3DISSUE_BEGIN);
        d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, INT(box_slot * 8), 0, 8, 0, 12);
        q->Issue(D3DISSUE_END);
        box_slot = (box_slot + 1) % (2 * kOcMax);
        if (switch_state) game_state();
    };
    // D: the screen rectangle of the 8 corners at their nearest depth, in c252 (origin) and c253 (extent).
    // mode: 3 the production state (colour write mask untouched, blend ZERO/ONE so every target keeps its value, z write
    // off); 0 colour write 0 instead of the neutral blend; 1 as 0 with z write left on and 2 as 0 with colour write left
    // on (timing controls only: the rectangle writes depth / colour).
    auto rect_test = [&](int i, IDirect3DQuery9* q, int keep = 3) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
        for (int k = 0; k < 8; ++k) {
            float v[4];
            corner(i, k, v);
            lo[0] = std::min(lo[0], v[0]), lo[1] = std::min(lo[1], v[1]), lo[2] = std::min(lo[2], v[2]);
            hi[0] = std::max(hi[0], v[0]), hi[1] = std::max(hi[1], v[1]);
        }
        const float px = 2.0f / float(kSize); // one pixel of inflation
        const float constants[8] = {lo[0] - px, lo[1] - px, lo[2], 1, hi[0] - lo[0] + 2 * px, hi[1] - lo[1] + 2 * px, 0, 0};
        d->SetVertexShaderConstantF(252, constants, 2);
        d->SetVertexDeclaration(box_decl.p);
        d->SetStreamSource(0, rect_vb.p, 0, sizeof(BoxVertex));
        d->SetVertexShader(rect_vs.p);
        d->SetPixelShader(box_ps.p);
        if (keep == 3) {
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
            d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        } else if (keep != 2)
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        if (keep != 1) d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        if (q) q->Issue(D3DISSUE_BEGIN);
        d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
        if (q) q->Issue(D3DISSUE_END);
        if (keep == 3) d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        game_state();
    };
    std::vector<double> a_us, b_us, c_us, d_us, get_us, delta, rect_delta, frame_test_us;
    unsigned ready_set[3] = {}; // lag-1 ready per set B, C, D
    unsigned ready = 0, ready_lag2 = 0, not_ready = 0, errors = 0, wrong = 0, read_total = 0;
    std::vector<std::uint8_t> done(std::size_t(kOcRing) * 3 * kOcMax, 0);
    int drain_slot = 0;
    auto read_back = [&](int slot, bool count_lag1) {
        unsigned r = 0, nr = 0, e = 0, w = 0;
        for (int k = k_first; k < 3; ++k)
            for (int i = 0; i < n; ++i) {
                std::uint8_t& seen = done[(std::size_t(slot) * 3 + k) * kOcMax + i];
                if (seen) continue;
                DWORD pixels = 0;
                const HRESULT h = queries[slot * 3 + k][i]->GetData(&pixels, sizeof pixels, 0);
                if (h == S_OK) {
                    ++r;
                    if (count_lag1) ++ready_set[k];
                    seen = 1;
                    const bool hidden = (i % 16) % 2 == 0;
                    if (hidden != (pixels == 0)) ++w;
                } else if (h == S_FALSE)
                    ++nr;
                else
                    ++e;
            }
        if (count_lag1)
            ready += r, not_ready += nr, read_total += unsigned(sets * n);
        else
            ready_lag2 += r;
        errors += e, wrong += w;
    };
    for (int frame = 0; frame <= kOcFrames + kOcWarm + 1; ++frame) {
        const double frame_start = now_ms();
        const int cur = frame % kOcRing, lag1 = (frame + kOcRing - 1) % kOcRing, lag2 = (frame + kOcRing - 2) % kOcRing;
        if (frame > kOcWarm + 1) read_back(lag2, false); // what lag 1 missed, two frames on
        double get_ms = 0;
        if (frame > kOcWarm && frame <= kOcFrames + kOcWarm) { // the previous frame's queries, no flush
            const double t0 = now_ms();
            read_back(lag1, true);
            get_ms = now_ms() - t0;
            get_us.push_back(1000.0 * get_ms / (sets * n));
        }
        if (frame >= kOcFrames + kOcWarm) {
            if (frame == kOcFrames + kOcWarm + 1) break;
            Sleep(DWORD(pace_ms > 0 ? pace_ms : 0)); // one more period so the last frame's lag-2 read happens
            continue;
        }
        if (frame == kOcFrames + kOcWarm - 1) drain_slot = cur; // the last drawn frame: drained afterwards
        for (int k = 0; k < 3; ++k) std::memset(&done[(std::size_t(cur) * 3 + k) * kOcMax], 0, kOcMax);
        reset_state(c);
        d->SetRenderTarget(0, backbuffer.p);
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00202020, 1.0f, 0);
        d->BeginScene();
        game_state();
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, hull, sizeof(Vertex));
        d->SetStreamSource(0, part_vb.p, 0, sizeof(Vertex));
        const double t0 = now_ms();
        for (int i = 0; i < n; ++i) d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(i * 36), 12);
        const double t1 = now_ms();
        for (int i = 0; all_sets && i < n; ++i) {
            d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(i * 36), 12);
            box_test(i, queries[cur * 3 + 0][i].p, true);
        }
        const double t2 = now_ms();
        if (all_sets) {
            d->SetVertexDeclaration(box_decl.p);
            d->SetStreamSource(0, box_vb.p, 0, sizeof(BoxVertex));
            d->SetIndices(box_ib.p);
            d->SetVertexShader(box_vs.p);
            d->SetPixelShader(box_ps.p);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            for (int i = 0; i < n; ++i) box_test(i, queries[cur * 3 + 1][i].p, false);
        }
        const double t3 = now_ms();
        game_state();
        for (int i = 0; i < n; ++i) {
            d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(i * 36), 12);
            rect_test(i, queries[cur * 3 + 2][i].p);
        }
        const double t4 = now_ms();
        d->EndScene();
        d->Present(nullptr, nullptr, nullptr, nullptr);
        if (frame >= kOcWarm) {
            a_us.push_back(1000.0 * (t1 - t0) / n);
            if (all_sets) {
                b_us.push_back(1000.0 * (t2 - t1) / n);
                c_us.push_back(1000.0 * (t3 - t2) / n);
                delta.push_back(b_us.back() - a_us.back());
            }
            d_us.push_back(1000.0 * (t4 - t3) / n);
            rect_delta.push_back(d_us.back() - a_us.back());
            // The per-frame cost of n production-shape tests: (D - A) plus this frame's share of the readback.
            frame_test_us.push_back(1000.0 * ((t4 - t3) - (t1 - t0)) + (get_ms > 0 ? 1000.0 * get_ms / sets : 0));
        }
        while (pace_ms > 0 && now_ms() - frame_start < pace_ms) Sleep(1); // the frame period
    }
    // Drain: how long until every D query of the last drawn frame is ready without a flush (polled, Sleep(1),
    // 1000 ms cap), then, if not, with D3DGETDATA_FLUSH (another 1000 ms cap); -1 = not within the cap.
    double drain_noflush_ms = -1, drain_flush_ms = -1;
    for (int pass = 0; pass < 2 && drain_noflush_ms < 0; ++pass) {
        const double start = now_ms();
        for (;;) {
            int pending = 0;
            for (int i = 0; i < n; ++i) {
                DWORD pixels = 0;
                if (queries[drain_slot * 3 + 2][i]->GetData(&pixels, sizeof pixels, pass ? D3DGETDATA_FLUSH : 0) != S_OK)
                    ++pending;
            }
            if (!pending) {
                (pass ? drain_flush_ms : drain_noflush_ms) = now_ms() - start;
                break;
            }
            if (now_ms() - start > 1000) break;
            Sleep(1);
        }
    }
    // GPU time of the production shape (production phase only): unpresented frames of hull + n part draws, in turn
    // without tests, with the n rectangle tests, and with the same rectangle draws and state switches but no query
    // Issue (the control), ten each, timed from the first draw to an event query's completion (spun with
    // D3DGETDATA_FLUSH); (with - without) / n is the pipeline cost per test as the CPU observes it, (with - control)
    // / n the share of the query itself. Kinds 3 and 4 repeat without / with on a game-like baseline where
    // consecutive part draws alternate between two pixel shaders (every draw binds a new pipeline, as the game's
    // draws do), so (alt_with - alt_without) / n is the incremental cost of a test between differing game draws.
    // Kinds 5 and 6: the colour-write-0 control with z write left on / colour write left on, kind 7 the colour-write-0
    // test with its query (which state change carries the cost).
    std::vector<double> gpu_with, gpu_without, gpu_control, gpu_alt_with, gpu_alt_without, gpu_keep_z, gpu_keep_color,
        gpu_mask;
    Com<IDirect3DQuery9> event;
    if (!all_sets && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_EVENT, event.out())))
        for (int frame = 0; frame < 88; ++frame) {
            // 0 without, 1 with, 2 control, 3/4 alternating without/with, 5 keep z, 6 keep colour, 7 colour write 0
            const int kind = frame % 8;
            const bool alt = kind == 3 || kind == 4;
            reset_state(c);
            d->SetRenderTarget(0, backbuffer.p);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00202020, 1.0f, 0);
            double ms = 0;
            wait_gpu(d, &ms); // idle before timing
            d->BeginScene();
            game_state();
            const double t0 = now_ms();
            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, hull, sizeof(Vertex));
            d->SetStreamSource(0, part_vb.p, 0, sizeof(Vertex));
            for (int i = 0; i < n; ++i) {
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(i * 36), 12);
                if (alt) d->SetPixelShader((i & 1) ? c.ps30_const.p : c.ps30_tex.p);
                if (kind && kind != 3) {
                    rect_test(i, kind == 1 || kind == 4 || kind == 7 ? queries[2][i].p : nullptr,
                              kind == 5 ? 1 : kind == 6 ? 2 : kind == 7 ? 0 : 3);
                    if (alt) d->SetStreamSource(0, part_vb.p, 0, sizeof(Vertex));
                }
            }
            d->EndScene();
            event->Issue(D3DISSUE_END);
            BOOL done_flag = FALSE;
            HRESULT h;
            while ((h = event->GetData(&done_flag, sizeof done_flag, D3DGETDATA_FLUSH)) == S_FALSE && now_ms() - t0 < 2000)
                ;
            std::vector<double>* const sink[8] = {&gpu_without,  &gpu_with,   &gpu_control,    &gpu_alt_without,
                                                  &gpu_alt_with, &gpu_keep_z, &gpu_keep_color, &gpu_mask};
            if (frame >= 8 && h == S_OK) sink[kind]->push_back(now_ms() - t0);
        }
    reset_state(c);
    auto p95 = [](std::vector<double> v) {
        if (v.empty()) return -1.0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, std::size_t(0.95 * double(v.size())))];
    };
    std::printf("occlusion_cost n=%d pace_ms=%.0f frames=%u part_draw_us_p50=%.2f part_plus_test_us_p50=%.2f "
                "test_delta_us_p50=%.2f test_delta_us_p95=%.2f tight_test_us_p50=%.2f tight_test_us_p95=%.2f "
                "rect_delta_us_p50=%.2f rect_delta_us_p95=%.2f getdata_us_p50=%.3f getdata_us_p95=%.3f "
                "per_part_us=%.2f rect_per_part_us=%.2f frame_us_p50=%.1f frame_us_p95=%.1f ready=%u ready_lag2=%u "
                "not_ready=%u errors=%u wrong=%u read=%u ready_b=%u ready_c=%u ready_d=%u drain_noflush_ms=%.1f "
                "drain_flush_ms=%.1f sets=%d gpu_with_ms=%.3f gpu_without_ms=%.3f gpu_control_ms=%.3f gpu_us_per_test=%.2f "
                "gpu_query_us_per_test=%.2f gpu_alt_without_ms=%.3f gpu_alt_with_ms=%.3f gpu_alt_us_per_test=%.2f "
                "gpu_keep_z_us_per_test=%.2f gpu_keep_color_us_per_test=%.2f gpu_mask_us_per_test=%.2f\n",
                n, pace_ms, unsigned(a_us.size()), median(a_us), median(b_us), median(delta), p95(delta), median(c_us),
                p95(c_us), median(rect_delta), p95(rect_delta), median(get_us), p95(get_us),
                median(delta) + median(get_us), median(rect_delta) + median(get_us), median(frame_test_us),
                p95(frame_test_us), ready, ready_lag2, not_ready, errors, wrong, read_total, ready_set[0],
                ready_set[1], ready_set[2], drain_noflush_ms, drain_flush_ms, sets, median(gpu_with), median(gpu_without),
                median(gpu_control),
                gpu_with.empty() || gpu_without.empty() ? -1.0 : 1000.0 * (median(gpu_with) - median(gpu_without)) / n,
                gpu_with.empty() || gpu_control.empty() ? -1.0 : 1000.0 * (median(gpu_with) - median(gpu_control)) / n,
                median(gpu_alt_without), median(gpu_alt_with),
                gpu_alt_with.empty() || gpu_alt_without.empty() ? -1.0
                                                                : 1000.0 * (median(gpu_alt_with) - median(gpu_alt_without)) / n,
                gpu_keep_z.empty() ? -1.0 : 1000.0 * (median(gpu_keep_z) - median(gpu_without)) / n,
                gpu_keep_color.empty() ? -1.0 : 1000.0 * (median(gpu_keep_color) - median(gpu_without)) / n,
                gpu_mask.empty() ? -1.0 : 1000.0 * (median(gpu_mask) - median(gpu_without)) / n);
    queries.clear();
    check(errors == 0 && wrong == 0 && read_total > 0, "occlusion_cost_results");
}

// --- occlusion test with the route's RT1 (A32B32G32R32F motion) and RT2 (R32F depth) bound (its lazy mode) ---
// The game draw writes oC0..oC2 (RT1 gets -0.0 in .x, RT2 2.5). Per variant, 60 frames after 4 warm-ups, each frame:
// clear, hull, then n x (test + part draw), an event query spun with FLUSH (pipeline time); the test protects RT1/RT2 by
//   none    nothing (the unprotected test: the reference for corruption);
//   flush   SetRenderTarget(2/1, null) before the test and both rebound before the part draw (the lazy flush);
//   mask    COLORWRITEENABLE1/2 = 0 for the test, 15 after;
//   zeros   a program writing 0 to oC0..oC2 under the same ZERO/ONE blend (needs MRT blending of the FP32 formats);
// and "base" draws the parts without tests. Row: CPU per test (loop QPC vs base), pipeline per test, and RT1/RT2
// texels that differ from base after the last frame (byte compare).
void occlusion_mrt_cost(Context& c, int n) {
    IDirect3DDevice9* d = c.device;
    D3DCAPS9 caps{};
    d->GetDeviceCaps(&caps);
    Com<IDirect3D9> d3d;
    d->GetDirect3D(d3d.out());
    auto blendable = [&](D3DFORMAT f) {
        return d3d.p && d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                               D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
                                               D3DRTYPE_TEXTURE, f) == D3D_OK;
    };
    std::printf("occlusion_mrt_caps rts=%lu independent_masks=%d mrt_blending=%d blend_rgba32f=%d blend_r32f=%d\n",
                (unsigned long)caps.NumSimultaneousRTs, (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS) ? 1 : 0,
                (caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING) ? 1 : 0,
                int(blendable(D3DFMT_A32B32G32R32F)), int(blendable(D3DFMT_R32F)));
    struct BoxVertex {
        float x, y, z, w;
    };
    const D3DVERTEXELEMENT9 pos[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    const D3DVERTEXELEMENT9 pos2[] = {{0, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> decl, rect_decl;
    Com<IDirect3DVertexBuffer9> part_vb, strip;
    Com<IDirect3DVertexShader9> vs, rect_vs;
    Com<IDirect3DPixelShader9> game_ps, zero_ps, zeros3_ps;
    Com<IDirect3DTexture9> rt1, rt2;
    Com<IDirect3DSurface9> rt1s, rt2s, sys1, sys2, base1, base2, backbuffer;
    std::unique_ptr<Com<IDirect3DQuery9>[]> queries(new Com<IDirect3DQuery9>[std::size_t(n)]);
    const auto vs_code = assemble("vs_3_0\n dcl_position v0\n dcl_position o0\n mov o0, v0\n", "mrt_vs");
    const auto rect_code = assemble("vs_3_0\n dcl_position v0\n dcl_position o0\n mad o0, v0, c253, c252\n", "mrt_rect_vs");
    const auto game_code = assemble("ps_3_0\n mov oC0, c0\n mov oC1, c1\n mov oC2, c2\n", "mrt_game_ps");
    const auto zero_code = assemble("ps_3_0\n def c0, 0, 0, 0, 0\n mov oC0, c0\n", "mrt_zero_ps");
    const auto zeros3_code =
        assemble("ps_3_0\n def c0, 0, 0, 0, 0\n mov oC0, c0\n mov oC1, c0\n mov oC2, c0\n", "mrt_zeros3_ps");
    bool ok = !vs_code.empty() && !rect_code.empty() && !game_code.empty() && !zero_code.empty() && !zeros3_code.empty() &&
              SUCCEEDED(d->CreateVertexShader(vs_code.data(), vs.out())) &&
              SUCCEEDED(d->CreateVertexShader(rect_code.data(), rect_vs.out())) &&
              SUCCEEDED(d->CreatePixelShader(game_code.data(), game_ps.out())) &&
              SUCCEEDED(d->CreatePixelShader(zero_code.data(), zero_ps.out())) &&
              SUCCEEDED(d->CreatePixelShader(zeros3_code.data(), zeros3_ps.out())) &&
              SUCCEEDED(d->CreateVertexDeclaration(pos, decl.out())) &&
              SUCCEEDED(d->CreateVertexDeclaration(pos2, rect_decl.out())) &&
              SUCCEEDED(d->CreateVertexBuffer(UINT(n + 1) * 6 * sizeof(BoxVertex), D3DUSAGE_WRITEONLY, 0,
                                              D3DPOOL_MANAGED, part_vb.out(), nullptr)) &&
              SUCCEEDED(d->CreateVertexBuffer(4 * 8, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, strip.out(), nullptr)) &&
              SUCCEEDED(d->CreateTexture(kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT,
                                         rt1.out(), nullptr)) &&
              SUCCEEDED(d->CreateTexture(kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, rt2.out(),
                                         nullptr)) &&
              SUCCEEDED(rt1->GetSurfaceLevel(0, rt1s.out())) && SUCCEEDED(rt2->GetSurfaceLevel(0, rt2s.out())) &&
              SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, sys1.out(), nullptr)) &&
              SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, sys2.out(), nullptr)) &&
              SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, base1.out(), nullptr)) &&
              SUCCEEDED(d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, base2.out(), nullptr)) &&
              SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backbuffer.out()));
    for (int i = 0; i < n; ++i) ok = ok && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION, queries[std::size_t(i)].out()));
    void* p = nullptr;
    // Quads: [0] the hull (left half, z 0.3), then n parts on a 16-column grid (even columns behind the hull).
    const int rows = n / 16 ? n / 16 : 1;
    auto part_rect = [rows](int i, float* r) {
        const int col = i % 16, row = i / 16;
        const float cx = -1.0f + float(col / 2 + (col & 1) * 8) * 0.125f + 0.0625f;
        const float cy = -1.0f + (float(row) + 0.5f) * 2.0f / float(rows), hy = 0.6f / float(rows);
        r[0] = cx - 0.05f, r[1] = cy - hy, r[2] = cx + 0.05f, r[3] = cy + hy;
    };
    if (ok && SUCCEEDED(part_vb->Lock(0, 0, &p, 0))) {
        BoxVertex* v = static_cast<BoxVertex*>(p);
        auto quad6 = [](BoxVertex* o, float x0, float y0, float x1, float y1, float z) {
            o[0] = {x0, y0, z, 1}, o[1] = {x1, y0, z, 1}, o[2] = {x0, y1, z, 1};
            o[3] = {x0, y1, z, 1}, o[4] = {x1, y0, z, 1}, o[5] = {x1, y1, z, 1};
        };
        quad6(v, -1, -1, 0, 1, 0.3f);
        for (int i = 0; i < n; ++i) {
            float r[4];
            part_rect(i, r);
            quad6(v + 6 * (i + 1), r[0], r[1], r[2], r[3], 0.6f);
        }
        part_vb->Unlock();
    } else
        ok = false;
    if (ok && SUCCEEDED(strip->Lock(0, 0, &p, 0))) {
        const float s[8] = {1, 0, 0, 0, 1, 1, 0, 1}; // the winding CULL NONE keeps either way
        std::memcpy(p, s, sizeof s);
        strip->Unlock();
    } else
        ok = false;
    check(ok, "occlusion_mrt_resources");
    if (!ok) return;
    const char* const names[5] = {"base", "none", "flush", "mask", "zeros"};
    const float minus_zero = -0.0f;
    const float c1[4] = {minus_zero, 2.5f, -1.0f, 1e-30f}, c2[4] = {2.5f, 0, 0, 1};
    double base_cpu = 0, base_gpu = 0;
    for (int variant = 0; variant < 5; ++variant) {
        std::vector<double> cpu, gpu;
        for (int frame = 0; frame < 64; ++frame) {
            reset_state(c);
            d->SetRenderTarget(0, backbuffer.p);
            d->SetRenderTarget(1, rt1s.p);
            d->SetRenderTarget(2, rt2s.p);
            d->SetDepthStencilSurface(c.depth.p);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00202020, 1.0f, 0);
            double ms = 0;
            wait_gpu(d, &ms);
            d->BeginScene();
            d->SetVertexDeclaration(decl.p);
            d->SetStreamSource(0, part_vb.p, 0, sizeof(BoxVertex));
            d->SetVertexShader(vs.p);
            d->SetPixelShader(game_ps.p);
            d->SetPixelShaderConstantF(1, c1, 1);
            d->SetPixelShaderConstantF(2, c2, 1);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
            d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15);
            d->SetRenderState(D3DRS_COLORWRITEENABLE2, 15);
            const float grey[4] = {.4f, .4f, .4f, 1};
            d->SetPixelShaderConstantF(0, grey, 1);
            const double t0 = now_ms();
            d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
            for (int i = 0; i < n; ++i) {
                if (variant) {
                    float r[4];
                    part_rect(i, r);
                    const float k[8] = {r[0] - 2.f / kSize, r[1] - 2.f / kSize, 0.6f, 1,
                                        r[2] - r[0] + 4.f / kSize, r[3] - r[1] + 4.f / kSize, 0, 0};
                    if (variant == 2) d->SetRenderTarget(2, nullptr), d->SetRenderTarget(1, nullptr);
                    if (variant == 3) {
                        d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
                        d->SetRenderState(D3DRS_COLORWRITEENABLE2, 0);
                    }
                    d->SetVertexShaderConstantF(252, k, 2);
                    d->SetVertexDeclaration(rect_decl.p);
                    d->SetStreamSource(0, strip.p, 0, 8);
                    d->SetVertexShader(rect_vs.p);
                    d->SetPixelShader(variant == 4 ? zeros3_ps.p : zero_ps.p);
                    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
                    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                    queries[std::size_t(i)]->Issue(D3DISSUE_BEGIN);
                    d->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
                    queries[std::size_t(i)]->Issue(D3DISSUE_END);
                    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
                    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                    d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                    d->SetPixelShader(game_ps.p);
                    d->SetVertexShader(vs.p);
                    d->SetStreamSource(0, part_vb.p, 0, sizeof(BoxVertex));
                    d->SetVertexDeclaration(decl.p);
                    if (variant == 3) {
                        d->SetRenderState(D3DRS_COLORWRITEENABLE2, 15);
                        d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15);
                    }
                    if (variant == 2) d->SetRenderTarget(1, rt1s.p), d->SetRenderTarget(2, rt2s.p);
                }
                d->DrawPrimitive(D3DPT_TRIANGLELIST, UINT(6 * (i + 1)), 2);
            }
            const double t1 = now_ms();
            d->EndScene();
            wait_gpu(d, &ms);
            const double t2 = now_ms();
            if (frame >= 4) {
                cpu.push_back(1000.0 * (t1 - t0));
                gpu.push_back(1000.0 * (t2 - t0));
            }
        }
        d->SetRenderTarget(1, nullptr);
        d->SetRenderTarget(2, nullptr);
        d->GetRenderTargetData(rt1s.p, variant ? sys1.p : base1.p);
        d->GetRenderTargetData(rt2s.p, variant ? sys2.p : base2.p);
        unsigned changed1 = 0, changed2 = 0;
        if (variant) {
            D3DLOCKED_RECT a{}, b{};
            for (int t = 0; t < 2; ++t) {
                IDirect3DSurface9* now = t ? sys2.p : sys1.p;
                IDirect3DSurface9* was = t ? base2.p : base1.p;
                const int bpp = t ? 4 : 16;
                if (SUCCEEDED(now->LockRect(&a, nullptr, D3DLOCK_READONLY)) &&
                    SUCCEEDED(was->LockRect(&b, nullptr, D3DLOCK_READONLY))) {
                    for (int y = 0; y < kSize; ++y)
                        for (int x = 0; x < kSize; ++x)
                            if (std::memcmp(static_cast<const char*>(a.pBits) + y * a.Pitch + x * bpp,
                                            static_cast<const char*>(b.pBits) + y * b.Pitch + x * bpp, bpp))
                                ++(t ? changed2 : changed1);
                    was->UnlockRect();
                    now->UnlockRect();
                }
            }
        }
        const double cpu50 = median(cpu), gpu50 = median(gpu);
        if (!variant) base_cpu = cpu50, base_gpu = gpu50;
        std::printf("occlusion_mrt variant=%s n=%d frames=%u cpu_us_p50=%.1f gpu_us_p50=%.1f cpu_us_per_test=%.2f "
                    "pipeline_us_per_test=%.2f rt1_changed=%u rt2_changed=%u\n",
                    names[variant], n, unsigned(cpu.size()), cpu50, gpu50, variant ? (cpu50 - base_cpu) / n : 0.0,
                    variant ? (gpu50 - base_gpu) / n : 0.0, changed1, changed2);
    }
    reset_state(c);
}

LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}

int run(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <d3d9 path|builtin> <d3dx9_37 path> <shader list|-> [pipeline-cost list] [occlusion-cost]\n", argv[0]);
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
    { // Private data as the proxy's resource identity uses it (src/proxy/capture_state.cpp): an unset GUID, an
      // 8-byte POD set, the round trip, and a 4-byte read of the 8-byte value (the too-small-buffer form).
        const GUID guid = {0x5c0e7a31, 0x94d2, 0x4b6e, {0x8f, 0x13, 0x2a, 0x77, 0xc1, 0x05, 0xe9, 0x4b}};
        struct Target {
            const char* name;
            IDirect3DResource9* resource;
        };
        for (const Target& target : {Target{"rt_surface", c.rt_surface.p}, Target{"managed_texture", c.tex_a.p}}) {
            IDirect3DResource9* r = target.resource;
            if (!r) continue;
            const bool rt = target.resource == c.rt_surface.p;
            std::uint64_t value = 0;
            DWORD unset_size = sizeof value;
            const HRESULT unset = r->GetPrivateData(guid, &value, &unset_size);
            const std::uint64_t written = 0x58334d0000000001ull;
            const HRESULT set = r->SetPrivateData(guid, &written, sizeof written, 0);
            value = 0;
            DWORD size = sizeof value;
            const HRESULT get = r->GetPrivateData(guid, &value, &size);
            std::uint32_t half = 0;
            DWORD small_size = sizeof half;
            const HRESULT small = r->GetPrivateData(guid, &half, &small_size);
            r->FreePrivateData(guid);
            std::printf("PRIVATEDATA resource=%s unset_hr=%08lx unset_size=%lu set_hr=%08lx get_hr=%08lx get_size=%lu "
                        "value=%016llx value_ok=%d small_hr=%08lx small_size=%lu\n",
                        target.name, (unsigned long)unset, (unsigned long)unset_size, (unsigned long)set,
                        (unsigned long)get, (unsigned long)size, (unsigned long long)value, int(value == written),
                        (unsigned long)small, (unsigned long)small_size);
            // The two not-found forms the proxy accepts (ownership::private_data_not_found).
            check(unset == D3DERR_NOTFOUND || (unset == D3DERR_INVALIDCALL && unset_size == 0),
                  rt ? "privatedata_unset_rt" : "privatedata_unset_managed");
            check(set == S_OK && get == S_OK && size == sizeof value && value == written,
                  rt ? "privatedata_roundtrip_rt" : "privatedata_roundtrip_managed");
        }
    }
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
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "occlusion-cost")) {
            for (int n : {128, 512}) occlusion_cost(c, n, 0, true); // CPU cost of every variant
            for (int n : {128, 256, 384, 512})
                for (double pace : {13.0, 20.0}) occlusion_cost(c, n, pace, false); // the production shape alone
            occlusion_mrt_cost(c, 128); // the route's RT1/RT2 bound: which protection, at what cost
        } else
            pipeline_cost(c, argv[i]);
    }
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return 0; // Context, device and Direct3D release in reverse declaration order.
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return run(argc, argv);
}
