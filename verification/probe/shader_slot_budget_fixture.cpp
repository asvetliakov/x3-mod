// Shader instruction-slot budget probe: is the ps_3_0 / vs_3_0 static instruction
// budget on this runtime 512 slots, the reported cap, or something else, and which
// stage (the D3DX compiler, the runtime's Create*Shader, the draw) enforces it?
//
//   shader_slot_budget_fixture.exe <d3dx9_37 path> [max hlsl N]
//
// Loads the bottle's d3d9 by name (the backend the proxy forwards to), creates the
// game's device shape (HWVP | PUREDEVICE | FPU_PRESERVE, windowed, A8R8G8B8, D24S8,
// hidden window) and prints the shader caps. Then, per case, a dependent chain of N
// `mad x, x, t, k` (t = 1.0 from a white texture or a constant, k = 2^-16) is
//   hlsl: compiled with D3DXCompileShader (OPTIMIZATION_LEVEL3, as production; on
//         failure once more with SKIPVALIDATION), or
//   raw:  hand-assembled ps_3_0 / vs_3_0 tokens (bypasses D3DX entirely),
// disassembled with D3DXDisassembleShader ("approximately S instruction slots used"),
// created on the device, drawn once as a full-screen quad into a 512x512
// A32B32G32R32F target and read back at the centre: g = 1 + E * 2^-16 gives E, the
// number of mads the GPU executed. Pixel-shader cases that draw are timed (warm-up
// draw + event-query sync, then R draws + sync). Loop cases put a B-mad body in a
// [loop] of L iterations (static ~B, executed L*B).
// Documented D3D9 / Win32 / D3DX APIs only. Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr UINT kSize = 512;
constexpr float kStep = 1.0f / 65536.0f;
double qpc_hz = 1;
double now_ms() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000.0 * double(t.QuadPart) / qpc_hz;
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
    T** out() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
};

decltype(&D3DXCompileShader) compile_fn = nullptr;
decltype(&D3DXDisassembleShader) disassemble_fn = nullptr;
decltype(&D3DXAssembleShader) assemble_fn = nullptr;

std::string token(const char* s, size_t n = 160) { // one whitespace-free token for key=value output
    std::string t;
    for (; *s && t.size() < n; ++s) t += (*s == ' ' || *s == '\t') ? '_' : (*s == '\r' || *s == '\n') ? '|' : *s;
    return t.empty() ? "-" : t;
}

struct Slots {
    int total = -1, tex = -1, alu = -1, flow = -1;
};
Slots count_slots(const DWORD* code) {
    Slots s;
    Com<ID3DXBuffer> text;
    if (!disassemble_fn || FAILED(disassemble_fn(code, FALSE, nullptr, text.out())) || !text.p) return s;
    const char* t = static_cast<const char*>(text->GetBufferPointer());
    const char* a = std::strstr(t, "approximately ");
    if (a) {
        std::sscanf(a, "approximately %d instruction slots used", &s.total);
        const char* paren = std::strchr(a, '(');
        const char* eol = std::strchr(a, '\n');
        if (paren && (!eol || paren < eol)) {
            int v = 0;
            char word[32] = {};
            for (const char* q = paren + 1; q && (!eol || q < eol) && std::sscanf(q, "%d %31[a-z]", &v, word) == 2;) {
                if (!std::strcmp(word, "texture"))
                    s.tex = v;
                else if (!std::strcmp(word, "arithmetic"))
                    s.alu = v;
                else if (!std::strcmp(word, "flow"))
                    s.flow = v;
                q = std::strchr(q, ',');
                if (q) ++q;
            }
        }
    }
    return s;
}

// Hand-assembled chain: mov r0, c1; N x mad r0, r0, c1, c0; outputs. (SM3 tokens, length in bits 24-27.)
std::vector<DWORD> raw_chain(bool pixel, unsigned n) {
    std::vector<DWORD> w;
    w.reserve(size_t(n) * 5 + 32);
    if (pixel) {
        w.push_back(0xFFFF0300);
    } else {
        w.push_back(0xFFFE0300);
        w.insert(w.end(), {0x0200001F, 0x80000000, 0x900F0000}); // dcl_position v0
        w.insert(w.end(), {0x0200001F, 0x80000000, 0xE00F0000}); // dcl_position o0
        w.insert(w.end(), {0x0200001F, 0x80010005, 0xE00F0001}); // dcl_texcoord1 o1
    }
    w.insert(w.end(), {0x02000001, 0x800F0000, 0xA0E40001}); // mov r0, c1
    for (unsigned i = 0; i < n; ++i)
        w.insert(w.end(), {0x04000004, 0x800F0000, 0x80E40000, 0xA0E40001, 0xA0E40000}); // mad r0, r0, c1, c0
    if (pixel) {
        w.insert(w.end(), {0x02000001, 0x800F0800, 0x80E40000}); // mov oC0, r0
    } else {
        w.insert(w.end(), {0x02000001, 0xE00F0001, 0x80E40000}); // mov o1, r0
        w.insert(w.end(), {0x02000001, 0xE00F0000, 0x90E40000}); // mov o0, v0
    }
    w.push_back(0x0000FFFF);
    return w;
}

std::string hlsl_chain(bool pixel, unsigned n, unsigned loop) {
    std::string s = "float4 k : register(c0);\nfloat4 one : register(c1);\n";
    std::string body;
    for (unsigned i = 0; i < n; ++i) body += "    x = x * t + k;\n";
    if (loop) body = "    [loop] for (int i = 0; i < " + std::to_string(loop) + "; i++) {\n" + body + "    }\n";
    if (pixel)
        return s +
               "sampler2D s : register(s0);\nfloat4 main(float2 uv : TEXCOORD0) : COLOR {\n"
               "    float4 t = tex2D(s, uv);\n    float4 x = t;\n" +
               body + "    return x;\n}\n";
    return s +
           "struct O { float4 p : POSITION; float4 c : TEXCOORD1; };\n"
           "O main(float4 p : POSITION) {\n    float4 t = one;\n    float4 x = t;\n" +
           body + "    O o; o.p = p; o.c = x; return o;\n}\n";
}

bool compile_hlsl(const std::string& src, const char* profile, DWORD flags, std::vector<DWORD>& code, HRESULT& hr,
                  std::string& err) {
    Com<ID3DXBuffer> out, errors;
    hr = compile_fn(src.data(), UINT(src.size()), nullptr, nullptr, "main", profile, flags, out.out(), errors.out(),
                    nullptr);
    err.clear();
    if (errors.p) {
        const char* e = static_cast<const char*>(errors->GetBufferPointer());
        err.assign(e, strnlen(e, errors->GetBufferSize()));
    }
    if (FAILED(hr) || !out.p) return false;
    code.assign(static_cast<const DWORD*>(out->GetBufferPointer()),
                static_cast<const DWORD*>(out->GetBufferPointer()) + out->GetBufferSize() / 4);
    return true;
}

std::string first_error(const std::string& err) { // first line mentioning "error", path prefix stripped
    size_t at = err.find("error");
    if (at == std::string::npos) at = 0;
    size_t end = err.find('\n', at);
    return token(err.substr(at, end == std::string::npos ? std::string::npos : end - at).c_str(), 240);
}

struct Ctx {
    IDirect3DDevice9* d = nullptr;
    Com<IDirect3DTexture9> rt, white;
    Com<IDirect3DSurface9> rt_surface, readback;
    Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DVertexShader9> vs_pass;
    Com<IDirect3DPixelShader9> ps_color;
    Com<IDirect3DQuery9> event;
};

bool sync(Ctx& c) {
    if (!c.event.p || FAILED(c.event->Issue(D3DISSUE_END))) return false;
    const double start = now_ms();
    while (c.event->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE)
        if (now_ms() - start > 120000) return false;
    return true;
}

HRESULT quad(Ctx& c) {
    struct V {
        float x, y, z, w, u, v;
    };
    const V q[4] = {{-1, 1, 0.5f, 1, 0, 0}, {1, 1, 0.5f, 1, 1, 0}, {-1, -1, 0.5f, 1, 0, 1}, {1, -1, 0.5f, 1, 1, 1}};
    HRESULT hr = c.d->BeginScene();
    if (FAILED(hr)) return hr;
    hr = c.d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    c.d->EndScene();
    return hr;
}

// Draws with the bound shaders; returns the centre pixel's g and optionally times the pixel shader.
void draw_and_read(Ctx& c, bool timed, unsigned executed_hint, HRESULT& draw_hr, float& g, double& first_ms,
                   double& per_draw_ms, unsigned& reps) {
    g = NAN;
    first_ms = per_draw_ms = -1;
    reps = 0;
    c.d->SetRenderTarget(0, c.rt_surface.p);
    c.d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 0, 255), 1.0f, 0); // g = 0: never written
    double t0 = now_ms();
    draw_hr = quad(c);
    if (FAILED(draw_hr) || !sync(c)) {
        if (SUCCEEDED(draw_hr)) draw_hr = E_FAIL;
        return;
    }
    first_ms = now_ms() - t0;
    if (FAILED(c.d->GetRenderTargetData(c.rt_surface.p, c.readback.p))) return;
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(c.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        const float* px = reinterpret_cast<const float*>(static_cast<const char*>(lr.pBits) + lr.Pitch * (kSize / 2)) +
                          4 * (kSize / 2);
        g = px[1];
        c.readback->UnlockRect();
    }
    if (!timed) return;
    reps = executed_hint <= 4096 ? 40 : executed_hint <= 32768 ? 10 : 3;
    t0 = now_ms();
    for (unsigned i = 0; i < reps; ++i) quad(c);
    if (sync(c)) per_draw_ms = (now_ms() - t0) / reps;
}

void report(Ctx& c, const char* stage, const char* kind, unsigned n, unsigned loop, const std::vector<DWORD>* code,
            HRESULT compile_hr, double compile_ms, HRESULT skipval_hr, const std::string& err, bool draw = true) {
    const bool pixel = stage[0] == 'p';
    const unsigned executed = loop ? n * loop : n;
    std::printf(
        "CASE stage=%s kind=%s n=%u loop=%u executed_expected=%u compile_hr=%08lx compile_ms=%.0f skipval_hr=%08lx",
        stage, kind, n, loop, executed, (unsigned long)compile_hr, compile_ms, (unsigned long)skipval_hr);
    if (!code) {
        std::printf(" words=0 slots=-1 create_hr=-\n");
        if (!err.empty())
            std::printf("ERROR stage=%s kind=%s n=%u loop=%u text=%s\n", stage, kind, n, loop,
                        first_error(err).c_str());
        std::fflush(stdout);
        return;
    }
    const Slots s = count_slots(code->data());
    std::printf(" words=%u slots=%d slots_tex=%d slots_alu=%d slots_flow=%d", unsigned(code->size()), s.total, s.tex,
                s.alu, s.flow);
    std::fflush(stdout);
    HRESULT create_hr;
    Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexShader9> vs;
    double t0 = now_ms();
    if (pixel)
        create_hr = c.d->CreatePixelShader(code->data(), ps.out());
    else
        create_hr = c.d->CreateVertexShader(code->data(), vs.out());
    std::printf(" create_hr=%08lx create_ms=%.1f", (unsigned long)create_hr, now_ms() - t0);
    if (FAILED(create_hr) || !draw) {
        std::printf("%s\n", draw ? "" : " draw=skipped");
        std::fflush(stdout);
        return;
    }
    c.d->SetVertexShader(pixel ? c.vs_pass.p : vs.p);
    c.d->SetPixelShader(pixel ? ps.p : c.ps_color.p);
    HRESULT draw_hr;
    float g;
    double first_ms, per_ms;
    unsigned reps;
    draw_and_read(c, pixel, executed, draw_hr, g, first_ms, per_ms, reps);
    // An unread pixel (failed draw or readback) prints null, never a non-finite number.
    char value[64] = "g=null executed=null";
    if (std::isfinite(g))
        std::snprintf(value, sizeof value, "g=%.9g executed=%.1f", double(g), double(g - 1.0f) / kStep);
    std::printf(" draw_hr=%08lx %s first_draw_ms=%.1f reps=%u ms_per_draw=%.4f coop=%08lx\n", (unsigned long)draw_hr,
                value, first_ms, reps, per_ms, (unsigned long)c.d->TestCooperativeLevel());
    c.d->SetVertexShader(nullptr);
    c.d->SetPixelShader(nullptr);
    std::fflush(stdout);
}

void hlsl_case(Ctx& c, bool pixel, unsigned n, unsigned loop, double& last_ms) {
    const char* profile = pixel ? "ps_3_0" : "vs_3_0";
    const std::string src = hlsl_chain(pixel, n, loop);
    std::vector<DWORD> code;
    HRESULT hr = S_OK, skip_hr = S_OK;
    std::string err, skip_err;
    const double t0 = now_ms();
    bool ok = compile_hlsl(src, profile, D3DXSHADER_OPTIMIZATION_LEVEL3, code, hr, err);
    last_ms = now_ms() - t0;
    if (!ok) { // does the refusal come from the validator? (the draw still uses only a D3DX-validated program)
        std::vector<DWORD> unvalidated;
        compile_hlsl(src, profile, D3DXSHADER_OPTIMIZATION_LEVEL3 | D3DXSHADER_SKIPVALIDATION, unvalidated, skip_hr,
                     skip_err);
        if (SUCCEEDED(skip_hr)) {
            const Slots s = count_slots(unvalidated.data());
            std::printf("SKIPVALIDATION stage=%s n=%u loop=%u hr=%08lx slots=%d\n", profile, n, loop,
                        (unsigned long)skip_hr, s.total);
        } else {
            std::printf("SKIPVALIDATION stage=%s n=%u loop=%u hr=%08lx text=%s\n", profile, n, loop,
                        (unsigned long)skip_hr, first_error(skip_err).c_str());
        }
    }
    report(c, profile, "hlsl", n, loop, ok ? &code : nullptr, hr, last_ms, skip_hr, err);
}

void raw_case(Ctx& c, bool pixel, unsigned n, bool draw = true) {
    const std::vector<DWORD> code = raw_chain(pixel, n);
    report(c, pixel ? "ps_3_0" : "vs_3_0", "raw", n, 0, &code, S_OK, 0, S_OK, std::string(), draw);
}

// The same chain as assembly text through D3DXAssembleShader (the D3DX assembler and its validator); created, not
// drawn.
void asm_case(Ctx& c, bool pixel, unsigned n) {
    std::string src = pixel ? "ps_3_0\n" : "vs_3_0\ndcl_position v0\ndcl_position o0\ndcl_texcoord1 o1\n";
    src += "mov r0, c1\n";
    for (unsigned i = 0; i < n; ++i) src += "mad r0, r0, c1, c0\n";
    src += pixel ? "mov oC0, r0\n" : "mov o1, r0\nmov o0, v0\n";
    Com<ID3DXBuffer> out, errors;
    const double t0 = now_ms();
    const HRESULT hr = assemble_fn(src.data(), UINT(src.size()), nullptr, nullptr, 0, out.out(), errors.out());
    const double ms = now_ms() - t0;
    std::string err;
    if (errors.p)
        err.assign(static_cast<const char*>(errors->GetBufferPointer()),
                   strnlen(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    std::vector<DWORD> code;
    if (SUCCEEDED(hr) && out.p)
        code.assign(static_cast<const DWORD*>(out->GetBufferPointer()),
                    static_cast<const DWORD*>(out->GetBufferPointer()) + out->GetBufferSize() / 4);
    report(c, pixel ? "ps_3_0" : "vs_3_0", "asm", n, 0, code.empty() ? nullptr : &code, hr, ms, S_OK, err, false);
}

LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(w, m, wp, lp);
}

int run(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <d3dx9_37 path> [max hlsl N]\n", argv[0]);
        return 2;
    }
    const unsigned max_hlsl = argc > 2 ? unsigned(std::strtoul(argv[2], nullptr, 10)) : 16384;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    qpc_hz = double(f.QuadPart);
    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    HMODULE d3dx = LoadLibraryA(argv[1]);
    std::printf("STEP load d3d9=%s d3dx=%s\n", d3d9 ? "ok" : "failed", d3dx ? "ok" : "failed");
    if (!d3d9 || !d3dx) return 1;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(d3d9, path, MAX_PATH);
    std::printf("MODULE name=d3d9 path=%s\n", token(path, 260).c_str());
    for (const char* name : {"wined3d.dll", "winevulkan.dll", "d3d11.dll", "dxgi.dll", "winemetal.dll"})
        if (HMODULE m = GetModuleHandleA(name)) {
            GetModuleFileNameA(m, path, MAX_PATH);
            std::printf("MODULE name=%s path=%s\n", name, token(path, 260).c_str());
        }
    {
        auto p = GetProcAddress(d3dx, "D3DXCompileShader");
        std::memcpy(&compile_fn, &p, sizeof p);
    }
    {
        auto p = GetProcAddress(d3dx, "D3DXDisassembleShader");
        std::memcpy(&disassemble_fn, &p, sizeof p);
    }
    {
        auto p = GetProcAddress(d3dx, "D3DXAssembleShader");
        std::memcpy(&assemble_fn, &p, sizeof p);
    }
    using Create9 = IDirect3D9*(WINAPI*)(UINT);
    Create9 create = nullptr;
    {
        auto p = GetProcAddress(d3d9, "Direct3DCreate9");
        std::memcpy(&create, &p, sizeof p);
    }
    if (!compile_fn || !disassemble_fn || !assemble_fn || !create) {
        std::printf("STEP entry_points failed\n");
        return 1;
    }
    Com<IDirect3D9> d3d;
    d3d.p = create(D3D_SDK_VERSION);
    if (!d3d.p) {
        std::printf("STEP direct3dcreate9 failed\n");
        return 1;
    }

    D3DADAPTER_IDENTIFIER9 id{};
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    std::printf("ADAPTER description=%s driver=%s vendor=%04lx device=%04lx\n", token(id.Description).c_str(),
                token(id.Driver).c_str(), (unsigned long)id.VendorId, (unsigned long)id.DeviceId);

    WNDCLASSA wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3m_slot_budget";
    RegisterClassA(&wc);
    HWND window = CreateWindowExA(0, wc.lpszClassName, "x3m slot budget", WS_OVERLAPPEDWINDOW, 0, 0, 256, 256, nullptr,
                                  nullptr, wc.hInstance, nullptr); // never shown
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 256;
    pp.BackBufferHeight = 256;
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
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, behavior, &pp, device.out());
    std::printf("DEVICE hr=%08lx behavior=%08lx\n", (unsigned long)hr, (unsigned long)behavior);
    if (FAILED(hr)) return 1;

    D3DCAPS9 caps{};
    HRESULT caps_hr = device->GetDeviceCaps(&caps);
    std::printf(
        "CAPS hr=%08lx vs_version=%04lx ps_version=%04lx max_ps30_slots=%lu max_vs30_slots=%lu "
        "max_ps_executed=%lu max_vs_executed=%lu ps20_slots=%d ps20_dyn_flow=%d ps20_temps=%d ps20_static_flow=%d ps20_caps=%08lx "
        "vs20_dyn_flow=%d vs20_temps=%d vs20_static_flow=%d vs20_caps=%08lx max_vs_consts=%lu\n",
        (unsigned long)caps_hr, (unsigned long)(caps.VertexShaderVersion & 0xFFFF),
        (unsigned long)(caps.PixelShaderVersion & 0xFFFF), (unsigned long)caps.MaxPixelShader30InstructionSlots,
        (unsigned long)caps.MaxVertexShader30InstructionSlots, (unsigned long)caps.MaxPShaderInstructionsExecuted,
        (unsigned long)caps.MaxVShaderInstructionsExecuted, caps.PS20Caps.NumInstructionSlots,
        caps.PS20Caps.DynamicFlowControlDepth, caps.PS20Caps.NumTemps, caps.PS20Caps.StaticFlowControlDepth,
        (unsigned long)caps.PS20Caps.Caps, caps.VS20Caps.DynamicFlowControlDepth, caps.VS20Caps.NumTemps,
        caps.VS20Caps.StaticFlowControlDepth, (unsigned long)caps.VS20Caps.Caps,
        (unsigned long)caps.MaxVertexShaderConst);
    std::fflush(stdout);

    Ctx c;
    c.d = device.p;
    bool ok = SUCCEEDED(c.d->CreateTexture(kSize, kSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                           D3DPOOL_DEFAULT, c.rt.out(), nullptr));
    ok = ok && SUCCEEDED(c.rt->GetSurfaceLevel(0, c.rt_surface.out()));
    ok = ok && SUCCEEDED(c.d->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM,
                                                          c.readback.out(), nullptr));
    ok = ok && SUCCEEDED(c.d->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, c.white.out(), nullptr));
    ok = ok && SUCCEEDED(c.d->CreateQuery(D3DQUERYTYPE_EVENT, c.event.out()));
    if (ok) {
        D3DLOCKED_RECT lr{};
        ok = SUCCEEDED(c.white->LockRect(0, &lr, nullptr, 0));
        if (ok) {
            for (int y = 0; y < 4; ++y) std::memset(static_cast<char*>(lr.pBits) + y * lr.Pitch, 0xFF, 16);
            c.white->UnlockRect(0);
        }
    }
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    ok = ok && SUCCEEDED(c.d->CreateVertexDeclaration(elements, c.decl.out()));
    std::vector<DWORD> code;
    HRESULT chr;
    std::string err;
    ok = ok &&
         compile_hlsl("struct O { float4 p : POSITION; float2 uv : TEXCOORD0; };\n"
                      "O main(float4 p : POSITION, float2 uv : TEXCOORD0) { O o; o.p = p; o.uv = uv; return o; }\n",
                      "vs_3_0", D3DXSHADER_OPTIMIZATION_LEVEL3, code, chr, err) &&
         SUCCEEDED(c.d->CreateVertexShader(code.data(), c.vs_pass.out()));
    ok = ok &&
         compile_hlsl("float4 main(float4 c : TEXCOORD1) : COLOR { return c; }\n", "ps_3_0",
                      D3DXSHADER_OPTIMIZATION_LEVEL3, code, chr, err) &&
         SUCCEEDED(c.d->CreatePixelShader(code.data(), c.ps_color.out()));
    std::printf("STEP resources status=%s\n", ok ? "ok" : "failed");
    if (!ok) return 1;
    c.d->SetVertexDeclaration(c.decl.p);
    c.d->SetTexture(0, c.white.p);
    c.d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    c.d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    c.d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    c.d->SetRenderState(D3DRS_ZENABLE, FALSE);
    c.d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    D3DVIEWPORT9 vp{0, 0, kSize, kSize, 0, 1};
    c.d->SetRenderTarget(0, c.rt_surface.p);
    c.d->SetViewport(&vp);
    c.d->SetDepthStencilSurface(nullptr);
    const float consts[8] = {kStep, kStep, kStep, kStep, 1, 1, 1, 1}; // c0 = k, c1 = 1
    c.d->SetPixelShaderConstantF(0, consts, 2);
    c.d->SetVertexShaderConstantF(0, consts, 2);

    // Straight-line HLSL, ascending; stop compiling larger programs after one compile over 90 s.
    const unsigned hlsl_n[] = {256, 500,  505,  506,  507,  508,   509,   510,   511,   512,  513,
                               600, 1024, 2048, 4096, 8192, 16384, 32760, 32768, 32769, 65536};
    double last_ms = 0;
    for (unsigned n : hlsl_n) {
        if (n > max_hlsl) break;
        if (last_ms > 90000) {
            std::printf("HLSLSTOP n=%u reason=previous_compile_ms_%.0f\n", n, last_ms);
            break;
        }
        hlsl_case(c, true, n, 0, last_ms);
    }
    // Rolled loops: static ~B, executed L*B.
    // 128 x 500 = 64000 and 132 x 500 = 66000 executed straddle MaxPShaderInstructionsExecuted (65535).
    const unsigned loops[][2] = {{10, 16}, {500, 64}, {500, 128}, {500, 132}, {500, 255}};
    for (const auto& l : loops) hlsl_case(c, true, l[0], l[1], last_ms);
    // Hand-assembled ps_3_0: the runtime without D3DX in the way.
    // (The raw chain's operands are all constants: its value is uniform, so its draw time is not per-pixel ALU cost.)
    for (unsigned n : {509u, 510u, 511u, 512u, 1024u, 4096u, 32766u, 32767u, 32768u, 65536u}) raw_case(c, true, n);
    raw_case(c, true, 262144u, false);
    // The D3DX assembler's validator on the same chain.
    for (unsigned n : {510u, 511u, 4096u, 32765u, 32766u, 32767u, 32768u}) asm_case(c, true, n);
    // vs_3_0: a few sizes through each path.
    for (unsigned n : {500u, 600u, 4096u}) hlsl_case(c, false, n, 0, last_ms);
    for (unsigned n : {510u, 511u, 32765u, 32766u}) asm_case(c, false, n);
    raw_case(c, false, 600u);
    for (unsigned n : {32766u, 32767u, 65536u}) raw_case(c, false, n, false);
    std::printf("RESULT done=1 coop=%08lx\n", (unsigned long)c.d->TestCooperativeLevel());
    std::fflush(stdout);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const int code = run(argc, argv);
    std::fflush(stdout);
    return code;
}
