// Stabiliser mask bench (docs/architecture/engine-frame-time.md, "TAA stage cost", mask draws). Compiles
// line_mask_ps.hlsl / line_mask_camera_ps.hlsl from each directory with the pinned d3dx9_37 (same flags as the
// generator), runs the three mask draws (tests -> x -> y + composition) at 1920x1080 on sky / hull / hostile inputs
// under 11 constant sets and compares every draw's A8R8G8B8 bytes with the first directory; then times the flown
// constant set: back-to-back chains, repeated single draws alternating targets (a TBDR merges repeated draws into
// one target), and one chain / one draw per event wait (the in-game split's bracketing), floor subtracted.
// Evidence only, not a fixture: synthetic content, one machine; see bench_out.txt beside this file. The rows there come
// from successive versions of this file (the scene 3, --configs / --scenes and the one-per-wait timing were added along
// the way); this is the last one and reproduces every row kind.
// Build: i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static
// mask_bench.cpp -o mask_bench.exe
// Run (no game running): X3M_FIXTURE_BOTTLE=X3 WINEDLLOVERRIDES=d3d9=b python3 verification/probe/wine_lock.py \
//   "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" --bottle X3 --no-update --dll
//   d3d9=b \
//   --workdir <dir> mask_bench.exe 'C:\X3\d3dx9_37.dll' Z:<old dir> Z:<new dir> [--scenes 012] [--configs a,b] [--time
//   N R]
// <old dir> holds the HEAD copies of both .hlsl files. It writes camera.asm / plain.asm (+ .bin) into each directory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& o)
        : p(o.p) {
        o.p = nullptr;
    }
    Com& operator=(Com&& o) {
        if (this != &o) {
            if (p) p->Release();
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
};
void check(const char* l, HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("API FAIL %s %08lx\n", l, hr);
        throw std::runtime_error(l);
    }
}
template <class T> T symbol(HMODULE m, const char* n) {
    FARPROC p = GetProcAddress(m, n);
    T t = nullptr;
    std::memcpy(&t, &p, sizeof(t));
    if (!t) throw std::runtime_error(n);
    return t;
}
std::string file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error(path);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    const std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    std::istringstream lines(text);
    std::string line, out;
    while (std::getline(lines, line)) {
        if (line.rfind("#include \"", 0) == 0) {
            const auto end = line.find('"', 10);
            out += file(dir + line.substr(10, end - 10));
        } else
            out += line;
        out += '\n';
    }
    return out;
}
constexpr UINT W = 1920, H = 1080;
using Compiler = decltype(&D3DXCompileShader);
using Disasm = decltype(&D3DXDisassembleShader);
struct Program {
    std::string label;
    Com<IDirect3DPixelShader9> ps;
    unsigned slots = 0, words = 0;
};
unsigned slotsOf(Disasm dis, const DWORD* w) {
    Com<ID3DXBuffer> a;
    check("disasm", dis(w, FALSE, nullptr, &a.p));
    std::istringstream s(static_cast<const char*>(a->GetBufferPointer()));
    std::string l;
    unsigned n = 0;
    while (std::getline(s, l))
        if (l.find("instruction slots used") != std::string::npos) {
            auto st = l.find_first_of("0123456789");
            n = unsigned(std::stoul(l.substr(st)));
        }
    return n;
}
Program build(IDirect3DDevice9* d, Compiler c, Disasm dis, const std::string& dir, bool camera, const char* dump) {
    static int serial = 0;
    Program p;
    p.label = dir.substr(dir.find_last_of("/\\") + 1) + (camera ? "/camera#" : "/plain#") +
              std::to_string(serial++ / 2);
    std::string src = file(dir + (camera ? "/line_mask_camera_ps.hlsl" : "/line_mask_ps.hlsl"));
    Com<ID3DXBuffer> code, err;
    HRESULT hr = c(src.c_str(), UINT(src.size()), nullptr, nullptr, "main", "ps_3_0", D3DXSHADER_OPTIMIZATION_LEVEL3,
                   &code.p, &err.p, nullptr);
    if (err.p) std::printf("COMPILER %s: %s\n", p.label.c_str(), static_cast<char*>(err->GetBufferPointer()));
    check("compile", hr);
    auto* w = static_cast<const DWORD*>(code->GetBufferPointer());
    p.words = code->GetBufferSize() / 4;
    p.slots = slotsOf(dis, w);
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned i = 0; i < p.words; ++i) {
        h ^= w[i];
        h *= 1099511628211ull;
    }
    std::printf("PROGRAM %s words=%u slots=%u fnv=%016llx\n", p.label.c_str(), p.words, p.slots, (unsigned long long)h);
    if (dump) {
        Com<ID3DXBuffer> a;
        check("disasm", dis(w, FALSE, nullptr, &a.p));
        std::ofstream(dump) << static_cast<const char*>(a->GetBufferPointer());
        std::ofstream(std::string(dump) + ".bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(w), p.words * 4);
    }
    check("create ps", d->CreatePixelShader(w, &p.ps.p));
    return p;
}
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t v)
        : s(v) {}
    std::uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float unit() { return (next() >> 8) * (1.f / 16777216.f); }
};
unsigned short toHalf(float f) {
    unsigned b;
    std::memcpy(&b, &f, 4);
    unsigned s = (b >> 16) & 0x8000;
    if (f != f) return 0x7e00;
    int e = int((b >> 23) & 255) - 127 + 15;
    if (e >= 31) return s | 0x7c00;
    if (e <= 0) return s;
    return s | (unsigned(e) << 10) | ((b & 0x7fffff) >> 13);
}
struct Scene {
    std::vector<float> depth, lane, motion, color;
};
const float kNaN = std::nanf(""), kInf = INFINITY;
Scene makeScene(int kind) {
    Scene s;
    s.depth.assign(W * H, -1.f);
    s.lane.assign(W * H * 4, 0.f);
    s.motion.assign(W * H * 4, 0.f);
    s.color.assign(W * H * 4, 0.f);
    Rng r(1234567u + kind);
    auto set = [&](UINT x, UINT y, float d, bool routed, float lum) {
        const UINT i = y * W + x;
        s.depth[i] = d;
        float* l = &s.lane[i * 4];
        l[0] = d;
        l[1] = 0;
        l[2] = d >= 0 && d <= 1 ? 0.5f / (1.001f - d) : 0;
        l[3] = 1;
        float* m = &s.motion[i * 4];
        m[0] = (x + .5f) / W + (routed ? .35f / W : 0);
        m[1] = (y + .5f) / H - (routed ? .2f / H : 0);
        m[2] = 0;
        m[3] = routed ? 1.f : -1.f;
        float* c = &s.color[i * 4];
        c[0] = c[1] = c[2] = lum;
        c[3] = 1;
    };
    for (UINT y = 0; y < H; ++y)
        for (UINT x = 0; x < W; ++x) {
            set(x, y, -1.f, false, (r.next() % 997 == 0) ? 4.f : .01f);
        }
    if (kind == 0) return s;
    if (kind == 1 || kind == 3) { // hull (kind 3: the same with hull luma above E = 1, as lit HDR panels): a station of
                                  // panels (planar depth), a front module, struts over sky (lattice), bright strips;
                                  // about 60 % coverage
        for (UINT y = 0; y < H; ++y)
            for (UINT x = 0; x < W; ++x) {
                const float fx = (x - 960.f) / 960.f, fy = (y - 540.f) / 540.f;
                bool hull = (fx * fx * 0.6f + fy * fy * 0.9f) < 0.62f;
                bool front = (x > 700 && x < 1100 && y > 350 && y < 700);
                bool lattice = (x > 1500 && x < 1880 && y > 100 && y < 500) && ((x % 7) < 2 || (y % 9) < 2);
                bool lattice2 = (x > 60 && x < 420 && y > 650 && y < 1000) && (((x + y) % 8) < 2);
                if (hull || front || lattice || lattice2) {
                    float d = front                 ? 0.990f + 0.00001f * x
                              : lattice || lattice2 ? 0.9990f
                                                    : 0.9965f + 0.0000008f * x + 0.0000011f * y;
                    const bool strip = (y % 37 == 0 && x % 5 != 0 && hull);
                    set(x, y, d, true,
                        strip ? (kind == 3 ? 5.f : 3.f) : (kind == 3 ? 1.2f : .2f) + .05f * ((x / 13 + y / 11) % 3));
                }
            }
        return s;
    }
    // hostile
    const float depths[] = {-1.f,  0.f,  1.f,    .5f, .999f, .9991f, kNaN,       kInf,
                            -kInf, -.3f, -1e31f, 2.f, -.5f,  -1e30f, 1.0000001f, -0.f};
    const float alphas[] = {1.f, -1.f, 0.f, kNaN, 1.0000001f, -1.0000001f, kInf};
    const float lanes[] = {1.f, .5f, 0.f, -1.f, kNaN, 1e-40f, kInf, 1e-30f, 300.f};
    const float cols[] = {.1f, 1.f, 3.f, kNaN, kInf, -kInf, 70000.f, -2.f, .9f, 1.2f};
    for (UINT y = 0; y < H; ++y)
        for (UINT x = 0; x < W; ++x) {
            const UINT i = y * W + x;
            const std::uint32_t v = r.next();
            // blocks keep neighbourhoods coherent sometimes (2x2 .. 8x8 regions of one class) and noisy elsewhere
            float d = (v & 3) == 0 ? r.unit() : depths[r.next() % 16];
            if ((x / 8 + y / 8) % 5 == 0) d = .99f + .001f * ((x / 8) % 3);
            s.depth[i] = d;
            float* l = &s.lane[i * 4];
            l[0] = d;
            l[2] = lanes[r.next() % 9];
            l[3] = 1;
            float* m = &s.motion[i * 4];
            m[0] = (x + .5f) / W + (r.unit() - .5f) * .01f;
            m[1] = (y + .5f) / H + (r.unit() - .5f) * .01f;
            if (r.next() % 31 == 0) m[0] = kNaN;
            if (r.next() % 37 == 0) m[1] = kInf;
            m[3] = alphas[r.next() % 7];
            float* c = &s.color[i * 4];
            c[0] = cols[r.next() % 10];
            c[1] = cols[r.next() % 10];
            c[2] = cols[r.next() % 10];
            c[3] = 1;
        }
    return s;
}
struct Targets {
    Com<IDirect3DTexture9> depth, lane, motion, color, mask[3];
    Com<IDirect3DSurface9> maskS[3], read;
};
Com<IDirect3DTexture9> upload(IDirect3DDevice9* d, D3DFORMAT f, UINT bpp, const void* data, bool half) {
    Com<IDirect3DTexture9> sys, dst;
    check("sys tex", d->CreateTexture(W, H, 1, 0, f, D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
    D3DLOCKED_RECT lr;
    check("lock", sys->LockRect(0, &lr, nullptr, 0));
    for (UINT y = 0; y < H; ++y) {
        auto* row = static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch;
        if (half) {
            const float* src = static_cast<const float*>(data) + y * W * 4;
            auto* o = reinterpret_cast<unsigned short*>(row);
            for (UINT i = 0; i < W * 4; ++i) o[i] = toHalf(src[i]);
        } else
            std::memcpy(row, static_cast<const unsigned char*>(data) + size_t(y) * W * bpp, W * bpp);
    }
    sys->UnlockRect(0);
    check("rt tex", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &dst.p, nullptr));
    check("update", d->UpdateTexture(sys.p, dst.p));
    return dst;
}
void load(IDirect3DDevice9* d, Targets& t, const Scene& s) {
    t.depth = upload(d, D3DFMT_R32F, 4, s.depth.data(), false);
    t.lane = upload(d, D3DFMT_A32B32G32R32F, 16, s.lane.data(), false);
    t.motion = upload(d, D3DFMT_A32B32G32R32F, 16, s.motion.data(), false);
    t.color = upload(d, D3DFMT_A16B16G16R16F, 8, s.color.data(), true);
}
struct Config {
    const char* name;
    float S, thinOn, optX, E, laneW, optW;
};
void constants(IDirect3DDevice9* d, const Config& c, float mode) {
    const float reproj[16] = {1.f, .0012f, 0, .0004f, -.0012f, 1.f, 0, -.0003f, 0, 0, 0, 0, 0, 0, 0, 1.f};
    check("c0", d->SetPixelShaderConstantF(0, reproj, 4));
    const float c4[4] = {1.f / W, 1.f / H, .3f / W, -.2f / H};
    check("c4", d->SetPixelShaderConstantF(4, c4, 1));
    const float c5[4] = {.998f, 500.f, 1.f, 1.f};
    check("c5", d->SetPixelShaderConstantF(5, c5, 1));
    const float c6[4] = {c.S, c.thinOn, .25f, 1.f / (1.5f - .25f)};
    check("c6", d->SetPixelShaderConstantF(6, c6, 1));
    const float c7[4] = {c.optX, 0, mode, c.optW};
    check("c7", d->SetPixelShaderConstantF(7, c7, 1));
    const float c8[4] = {.001f, .0005f, .0002f, .9f};
    check("c8", d->SetPixelShaderConstantF(8, c8, 1));
    const float c9[4] = {c.laneW > 0 ? .002f : 0, c.laneW > 0 ? -.001f : 0, c.laneW > 0 ? .0005f : 0, c.laneW};
    check("c9", d->SetPixelShaderConstantF(9, c9, 1));
    const float c10[4] = {c.E, 0, 0, 0};
    check("c10", d->SetPixelShaderConstantF(10, c10, 1));
}
void quad(IDirect3DDevice9* d) {
    struct V {
        float x, y, z, w, u, v;
    };
    const V v[4] = {{-.5f, -.5f, 0, 1, 0, 0},
                    {W - .5f, -.5f, 0, 1, 1, 0},
                    {-.5f, H - .5f, 0, 1, 0, 1},
                    {W - .5f, H - .5f, 0, 1, 1, 1}};
    check("draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V)));
}
// pass 0: tests -> mask0; pass 1: x -> mask1; pass 2: y + composition -> mask0
void draw(IDirect3DDevice9* d, Targets& t, Program& p, const Config& c, int pass, bool spare = false) {
    const float mode = pass == 0 ? 0.f : pass == 1 ? 1.f : 3.f;
    constants(d, c, mode);
    check("ps", d->SetPixelShader(p.ps.p));
    check("rt", d->SetRenderTarget(0, t.maskS[spare ? 2 : (pass == 1 ? 1 : 0)].p));
    for (DWORD s : {0u, 1u, 4u, 5u, 6u}) check("tex", d->SetTexture(s, nullptr));
    check("s0", d->SetTexture(0, pass == 0 && c.E > 0 ? t.color.p : nullptr));
    check("s1", d->SetTexture(1, pass == 0 ? t.depth.p : t.mask[pass == 1 ? 0 : 1].p));
    check("s4", d->SetTexture(4, t.motion.p));
    check("s5", d->SetTexture(5, pass == 0 && c.laneW > 0 ? t.lane.p : nullptr));
    check("s6", d->SetTexture(6, pass == 2 && c.S > 0 ? t.depth.p : nullptr));
    quad(d);
}
void readMask(IDirect3DDevice9* d, Targets& t, int which, std::vector<unsigned>& out) {
    check("rtdata", d->GetRenderTargetData(t.maskS[which].p, t.read.p));
    D3DLOCKED_RECT lr;
    check("lock read", t.read->LockRect(&lr, nullptr, D3DLOCK_READONLY));
    out.resize(W * H);
    for (UINT y = 0; y < H; ++y) std::memcpy(&out[y * W], static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch, W * 4);
    t.read->UnlockRect();
}
double wait(IDirect3DDevice9* d, IDirect3DQuery9* q) {
    check("issue", q->Issue(D3DISSUE_END));
    while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {}
    return 0;
}
double now() {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1000.0 / double(f.QuadPart);
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        std::vector<std::string> dirs;
        unsigned N = 30, R = 9;
        bool time = false;
        std::string scenesSel = "012", configSel = "";
        for (int i = 2; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--time")) {
                time = true;
                N = unsigned(std::atoi(argv[i + 1]));
                R = unsigned(std::atoi(argv[i + 2]));
                i += 2;
            } else if (!std::strcmp(argv[i], "--scenes"))
                scenesSel = argv[++i];
            else if (!std::strcmp(argv[i], "--configs"))
                configSel = std::string(",") + argv[++i] + ",";
            else
                dirs.push_back(argv[i]);
        }
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "MaskBench";
        RegisterClassA(&cls);
        HWND win = CreateWindowA(cls.lpszClassName, "mask bench", WS_OVERLAPPEDWINDOW, 90, 90, 64, 64, nullptr, nullptr,
                                 cls.hInstance, nullptr);
        HMODULE rt = LoadLibraryA("d3d9.dll"), dx = LoadLibraryA(argv[1]);
        if (!rt || !dx) throw std::runtime_error("load");
        auto compiler = symbol<Compiler>(dx, "D3DXCompileShader");
        auto dis = symbol<Disasm>(dx, "D3DXDisassembleShader");
        Com<IDirect3D9> api;
        api.p = symbol<IDirect3D9*(WINAPI*)(UINT)>(rt, "Direct3DCreate9")(D3D_SDK_VERSION);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = win;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        Com<IDirect3DDevice9> dev;
        check("device", api->CreateDevice(0, D3DDEVTYPE_HAL, win, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev.p));
        IDirect3DDevice9* d = dev.p;
        D3DADAPTER_IDENTIFIER9 id{};
        api->GetAdapterIdentifier(0, 0, &id);
        std::printf("ADAPTER %s\n", id.Description);
        std::vector<Program> cam, plain;
        for (size_t i = 0; i < dirs.size(); ++i) {
            std::string dump = dirs[i] + "/camera.asm";
            cam.push_back(build(d, compiler, dis, dirs[i], true, dump.c_str()));
            std::string dp = dirs[i] + "/plain.asm";
            plain.push_back(build(d, compiler, dis, dirs[i], false, dp.c_str()));
        }
        Program nullProgram;
        nullProgram.label = "null";
        {
            const char* src = "float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(uv,0,1);}";
            Com<ID3DXBuffer> code, err;
            check("null compile", compiler(src, UINT(std::strlen(src)), nullptr, nullptr, "main", "ps_3_0",
                                           D3DXSHADER_OPTIMIZATION_LEVEL3, &code.p, &err.p, nullptr));
            check("null ps",
                  d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &nullProgram.ps.p));
        }
        check("fvf", d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1));
        check("z", d->SetRenderState(D3DRS_ZENABLE, FALSE));
        check("cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        for (DWORD s : {0u, 1u, 4u, 5u, 6u}) {
            d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            d->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
        }
        Targets t;
        for (int i = 0; i < 3; ++i) {
            check("mask", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                           &t.mask[i].p, nullptr));
            check("mask s", t.mask[i]->GetSurfaceLevel(0, &t.maskS[i].p));
        }
        check("read", d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &t.read.p, nullptr));
        Com<IDirect3DQuery9> q;
        check("query", d->CreateQuery(D3DQUERYTYPE_EVENT, &q.p));
        const Config flown{"flown", .7f, 1, 1, 1, 1, 0};
        const Config configs[] = {flown,
                                  {"S0", 0, 1, 1, 1, 1, 0},
                                  {"E0", .7f, 1, 1, 0, 1, 0},
                                  {"optX0", .7f, 1, 0, 1, 1, 0},
                                  {"lane0", .7f, 1, 1, 1, 0, 0},
                                  {"thinoff", .7f, 0, 1, 1, 1, 0},
                                  {"thinInf", .7f, kInf, 1, 1, 1, 0},
                                  {"thinNeg", .7f, -1, 1, 0, 1, 0},
                                  {"thinZero", .7f, 0, 1, 1, 1, 0},
                                  {"plainline1", 0, 1, 1, 1, 0, 1},
                                  {"plainline2", 0, 1, 1, 1, 0, 2}};
        const char* sceneNames[] = {"sky", "hull", "hostile", "hullbright"};
        unsigned mismatches = 0;
        check("begin", d->BeginScene());
        for (int kind = 0; kind < 4; ++kind) {
            if (scenesSel.find(char('0' + kind)) == std::string::npos) continue;
            Scene s = makeScene(kind);
            load(d, t, s);
            for (const auto& c : configs)
                if (configSel.empty() || configSel.find(std::string(",") + c.name + ",") != std::string::npos)
                    for (int variant = 0; variant < 2; ++variant) {
                        auto& progs = variant ? plain : cam;
                        std::vector<std::vector<unsigned>> ref(3);
                        for (size_t pi = 0; pi < progs.size(); ++pi) {
                            unsigned fragPix = 0, bSet = 0;
                            for (int pass = 0; pass < 3; ++pass) {
                                draw(d, t, progs[pi], c, pass);
                                std::vector<unsigned> got;
                                readMask(d, t, pass == 1 ? 1 : 0, got);
                                if (pi == 0) {
                                    ref[pass] = got;
                                    if (pass == 0)
                                        for (unsigned v : got) fragPix += ((v & 0xff) != 0);
                                    if (pass == 2)
                                        for (unsigned v : got) bSet += ((v & 0xff) != 0);
                                } else {
                                    unsigned diff = 0;
                                    size_t first = 0;
                                    for (size_t i = 0; i < got.size(); ++i)
                                        if (got[i] != ref[pass][i]) {
                                            if (!diff) first = i;
                                            ++diff;
                                        }
                                    mismatches += diff;
                                    std::printf("IDENTITY scene=%s config=%s program=%s pass=%d differing=%u",
                                                sceneNames[kind], c.name, progs[pi].label.c_str(), pass, diff);
                                    if (diff) {
                                        std::printf(" first=(%zu,%zu) ref=%08x got=%08x", first % W, first / W,
                                                    ref[pass][first], got[first]);
                                        unsigned cnt[4]{}, mx[4]{};
                                        for (size_t i = 0; i < got.size(); ++i)
                                            for (int ch = 0; ch < 4; ++ch) {
                                                int a = (ref[pass][i] >> (8 * ch)) & 255,
                                                    b = (got[i] >> (8 * ch)) & 255;
                                                if (a != b) {
                                                    ++cnt[ch];
                                                    mx[ch] = std::max(mx[ch], unsigned(std::abs(a - b)));
                                                }
                                            }
                                        std::printf(" per_channel(b,g,r,a) count=%u,%u,%u,%u maxdiff=%u,%u,%u,%u",
                                                    cnt[0], cnt[1], cnt[2], cnt[3], mx[0], mx[1], mx[2], mx[3]);
                                    }
                                    std::printf("\n");
                                }
                            }
                            if (pi == 0)
                                std::printf(
                                    "CONTENT scene=%s config=%s program=%s pass0_b_pixels=%u final_b_pixels=%u of %u\n",
                                    sceneNames[kind], c.name, progs[0].label.c_str(), fragPix, bSet, W * H);
                        }
                    }
            if (time && kind != 2) {
                const Config& c = flown;
                // warm and populate
                for (auto& p : cam)
                    for (int pass = 0; pass < 3; ++pass) draw(d, t, p, c, pass);
                wait(d, q.p);
                for (int pass = -1; pass < 4; ++pass)
                    for (size_t pi = 0; pi < cam.size() + 1; ++pi) {
                        std::vector<double> per;
                        const bool nul = pi == cam.size();
                        Program& prog = nul ? nullProgram : cam[pi];
                        if (nul && pass >= 3) continue;
                        for (unsigned round = 0; round < R; ++round) {
                            for (int k = 0; k < (pass < 0 ? 0 : pass % 3); ++k) draw(d, t, cam[0], c, k);
                            wait(d, q.p);
                            const double t0 = now();
                            for (unsigned n = 0; n < N; ++n) {
                                if (pass < 0) {
                                    for (int k = 0; k < 3; ++k) draw(d, t, prog, c, k);
                                } else if (pass < 3)
                                    draw(d, t, prog, c, pass, n & 1);
                                else
                                    draw(d, t, prog, c, 0);
                            }
                            wait(d, q.p);
                            per.push_back((now() - t0) / N);
                        }
                        std::sort(per.begin(), per.end());
                        std::printf(
                            "TIMING scene=%s program=%s pass=%s N=%u R=%u min_ms=%.4f median_ms=%.4f max_ms=%.4f\n",
                            sceneNames[kind], prog.label.c_str(),
                            pass < 0    ? "chain"
                            : pass == 0 ? "tests_alt"
                            : pass == 1 ? "x_alt"
                            : pass == 2 ? "y_compose_alt"
                                        : "tests_same_target",
                            N, R, per.front(), per[per.size() / 2], per.back());
                    }
                // one chain (or one pass) per event wait, as the in-game split brackets the stage; interleaved
                // programs, floor subtracted
                {
                    const unsigned R1 = 301;
                    std::vector<std::vector<double>> chain(cam.size()), pass0(cam.size()), pass1(cam.size()),
                        pass2(cam.size());
                    std::vector<double> floor1;
                    for (unsigned round = 0; round < R1; ++round) {
                        {
                            wait(d, q.p);
                            const double t0 = now();
                            wait(d, q.p);
                            floor1.push_back(now() - t0);
                        }
                        for (size_t pi = 0; pi < cam.size(); ++pi) {
                            wait(d, q.p);
                            double t0 = now();
                            for (int k = 0; k < 3; ++k) draw(d, t, cam[pi], c, k);
                            wait(d, q.p);
                            chain[pi].push_back(now() - t0);
                            std::vector<double>* per[3] = {&pass0[pi], &pass1[pi], &pass2[pi]};
                            for (int k = 0; k < 3; ++k) {
                                wait(d, q.p);
                                t0 = now();
                                draw(d, t, cam[pi], c, k);
                                wait(d, q.p);
                                per[k]->push_back(now() - t0);
                            }
                        }
                    }
                    auto med = [](std::vector<double> v) {
                        std::sort(v.begin(), v.end());
                        double s = 0;
                        size_t n = 0;
                        for (size_t i = v.size() / 20; i < v.size() - v.size() / 20; ++i) {
                            s += v[i];
                            ++n;
                        }
                        return s / n;
                    };
                    const double f = med(floor1);
                    for (size_t pi = 0; pi < cam.size(); ++pi)
                        std::printf(
                            "SINGLE scene=%s program=%s R=%u floor_ms=%.4f chain_ms=%.4f tests_ms=%.4f x_ms=%.4f y_compose_ms=%.4f (5%%-trimmed means, floor subtracted)\n",
                            sceneNames[kind], cam[pi].label.c_str(), R1, f, med(chain[pi]) - f, med(pass0[pi]) - f,
                            med(pass1[pi]) - f, med(pass2[pi]) - f);
                }
                // floor: N empty event waits
                std::vector<double> fl;
                for (unsigned round = 0; round < R; ++round) {
                    const double t0 = now();
                    wait(d, q.p);
                    fl.push_back(now() - t0);
                }
                std::sort(fl.begin(), fl.end());
                std::printf("FLOOR median_ms=%.4f\n", fl[fl.size() / 2]);
            }
        }
        check("end", d->EndScene());
        std::printf("RESULT mismatches=%u\n", mismatches);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), mismatches ? 2 : 0);
        return 0;
    } catch (const std::exception& e) {
        std::printf("ERROR %s\n", e.what());
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 1);
        return 1;
    }
}
