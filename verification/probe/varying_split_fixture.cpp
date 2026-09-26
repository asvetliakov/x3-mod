// Authored SM3 mixed-semantic interpolation experiment. No game shader bytes.
// Build with X3M_VARYING_HOST for the same token/contract checks without D3D.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef X3M_VARYING_HOST
#define WIN32_LEAN_AND_MEAN
#include <d3d9.h>
#include <windows.h>
#endif
using Word = std::uint32_t;
using Words = std::vector<Word>;
constexpr Word pp = 0x00200000, centroid = 0x00400000;
void need(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(label);
}
Word reg(unsigned t, unsigned n) {
    return 0x80000000u | ((t & 7) << 28) | ((t & 24) << 8) | n;
}
Word dst(unsigned t, unsigned n, unsigned mask = 15, Word flags = 0) {
    return reg(t, n) | (mask << 16) | flags;
}
Word src(unsigned t, unsigned n, unsigned sw = 0xe4) {
    return reg(t, n) | (sw << 16);
}
void ins(Words& w, unsigned op, std::initializer_list<Word> a) {
    w.push_back(op | (unsigned(a.size()) << 24));
    w.insert(w.end(), a);
}
void dcl(Words& w, unsigned usage, unsigned index, unsigned type, unsigned n, unsigned mask = 15, Word flags = 0) {
    ins(w, 31, {0x80000000u | usage | (index << 16), dst(type, n, mask, flags)});
}
// native: whole COLOR0 PP. split: COLOR0.w PP + TEXCOORD9.xyz full.
// reference: whole COLOR0 PP retains alpha; RGB uses its own v1/o2 register.
enum Kind {
    Native,
    SplitCentroid,
    SplitPlain,
    ReferenceCentroid,
    ReferencePlain,
    SplitColor,
    ReferenceColor,
    KindCount
};
const char* names[] = {"native",          "split_centroid", "split_plain",    "reference_centroid",
                       "reference_plain", "split_color",    "reference_color"};
bool split(unsigned k) {
    return k == SplitCentroid || k == SplitPlain || k == SplitColor;
}
bool reference(unsigned k) {
    return k == ReferenceCentroid || k == ReferencePlain || k == ReferenceColor;
}
bool color_rgb(unsigned k) {
    return k == SplitColor || k == ReferenceColor;
}
bool context_ok(unsigned k, bool flat, unsigned wrap9) {
    return k == Native || color_rgb(k) || (!flat && wrap9 == 0) || (reference(k) && wrap9 == 0);
}
bool centered(unsigned k) {
    return k == SplitCentroid || k == ReferenceCentroid;
}
struct Program {
    Words vs, ps;
    unsigned kind;
    bool depth;
};
Program program(unsigned kind, bool depth) {
    Program p{{0xfffe0300}, {0xffff0300}, kind, depth};
    auto& v = p.vs;
    auto& s = p.ps;
    dcl(v, 0, 0, 1, 0);
    dcl(v, 5, 0, 1, 1);
    dcl(v, 5, 1, 1, 2);
    dcl(v, 0, 0, 6, 0);
    dcl(v, 10, 0, 6, 1, split(kind) ? 8 : 15);
    dcl(s, 10, 0, 1, 0, split(kind) ? 8 : 15, pp);
    if (split(kind)) {
        dcl(v, color_rgb(kind) ? 10 : 5, color_rgb(kind) ? 1 : 9, 6, 1, 7);
        dcl(s, color_rgb(kind) ? 10 : 5, color_rgb(kind) ? 1 : 9, 1, 0, 7, centered(kind) ? centroid : 0);
    }
    for (unsigned i = 0; i < 7; ++i) {
        const bool rgb_reference = reference(kind) && i == 0;
        const unsigned usage = rgb_reference && color_rgb(kind) ? 10 : 5;
        const unsigned semantic = rgb_reference ? (color_rgb(kind) ? 1 : 9) : i;
        const unsigned mask = reference(kind) && i == 0 ? 7 : 15;
        dcl(v, usage, semantic, 6, i + 2, mask);
        dcl(s, usage, semantic, 1, i + 1, mask, reference(kind) && i == 0 && centered(kind) ? centroid : 0);
    }
    dcl(v, 5, 7, 6, 9);
    dcl(v, 5, 8, 6, 10);
    dcl(s, 5, 7, 1, 8);
    dcl(s, 5, 8, 1, 9);
    dcl(s, 0, 0, 10,
        0); // s0 sampler declaration is replaced with texture type token.
    s[s.size() - 2] = 0x90000000u;
    ins(v, 1, {dst(6, 0), src(1, 0)});
    ins(v, 1, {dst(6, 1, 7), reference(kind) ? src(2, 2) : src(1, 1)});
    for (unsigned i = 0; i < 7; ++i)
        ins(v, 1,
            {dst(6, i + 2, reference(kind) && i == 0 ? 7 : 15), reference(kind) && i == 0 ? src(1, 1) : src(2, i + 2)});
    // Identical native fog/plain alpha instructions in every producer.
    ins(v, 40, {src(14, 0)});
    ins(v, 1, {dst(0, 0, 1, 0x00100000u), src(1, 2, 0)}); // mov_sat r0.x, fog
    ins(v, 5, {dst(6, 1, 8), src(1, 1, 0xff), src(0, 0, 0)});
    ins(v, 42, {});
    ins(v, 1, {dst(6, 1, 8), src(1, 1, 0xff)});
    ins(v, 43, {});
    ins(v, 1, {dst(6, 9), src(1, 0)});
    ins(v, 1, {dst(6, 10), src(1, 0, 0xee)}); // z,w,z,w
    v.push_back(0xffff);
    ins(s, 1, {dst(0, 1), src(2, 0)});
    ins(s, 66, {dst(0, 2, 15, pp), src(0, 1), src(10, 0)});
    ins(s, 1, {dst(8, 0, 7, kind == Native ? pp | 0x00100000u : 0), src(1, reference(kind) ? 1 : 0)});
    // All filler inputs are live in RT1. Their fixed binary fractions are exact
    // even when reference COLOR0.xyz shares alpha's native PP declaration.
    ins(s, 1, {dst(0, 3), src(1, 8)});
    for (unsigned i = 0; i < 7; ++i)
        ins(s, 2, {dst(0, 3, 7), src(0, 3), src(1, reference(kind) && i == 0 ? 0 : i + 1)});
    ins(s, 1, {dst(8, 1), src(0, 3)});
    if (depth) {
        ins(s, 6, {dst(0, 4, 1), src(1, 9, 0x55)});
        ins(s, 5, {dst(0, 4, 7), src(1, 9, 0), src(0, 4, 0)});
        ins(s, 1, {dst(0, 4, 8), src(2, 0, 0xff)});
        ins(s, 1, {dst(8, 2), src(0, 4)});
    }
    // Identical native sampled-alpha/PP multiply in every consumer.
    ins(s, 5, {dst(8, 0, 8, pp), src(0, 2, 0xff), src(1, 0, 0xff)});
    s.push_back(0xffff);
    return p;
}

// Diagnostic-only controls. These mutations never replace the qualification
// programs above. Each control isolates one declaration/output boundary.
const char* diagnostic_names[] = {"native",
                                  "separate",
                                  "packed",
                                  "vs_reverse",
                                  "ps_reverse",
                                  "both_reverse",
                                  "packed_no_pp_decl",
                                  "native_no_pp_decl",
                                  "vs_full_write",
                                  "ps_full_write",
                                  "scalar_separate",
                                  "packed_alpha_tap",
                                  "separate_alpha_tap"};
void reverse_color_dcls(Words& w) {
    std::size_t first = 0, second = 0;
    for (std::size_t i = 1; i < w.size() && w[i] != 0xffff; i += 1 + ((w[i] >> 24) & 15))
        if ((w[i] & 0xffff) == 31 && (w[i + 1] & 15) == 10) {
            if (!first)
                first = i;
            else {
                second = i;
                break;
            }
        }
    need(first && second, "two COLOR declarations");
    for (unsigned j = 0; j < 3; ++j) std::swap(w[first + j], w[second + j]);
}
Program diagnostic_program(unsigned control, bool depth) {
    auto p = program(control == 0 || control == 7                     ? Native
                     : control == 1 || control == 10 || control == 12 ? ReferenceColor
                                                                      : SplitColor,
                     depth);
    if (control == 3 || control == 5) reverse_color_dcls(p.vs);
    if (control == 4 || control == 5) reverse_color_dcls(p.ps);
    if (control == 6 || control == 7)
        for (std::size_t i = 1; i < p.ps.size() && p.ps[i] != 0xffff; i += 1 + ((p.ps[i] >> 24) & 15))
            if ((p.ps[i] & 0xffff) == 31 && (p.ps[i + 1] & 0xfffff) == 10) p.ps[i + 2] &= ~pp;
    if (control == 8 || control == 9) {
        auto& w = control == 8 ? p.vs : p.ps;
        for (std::size_t i = 1; i < w.size() && w[i] != 0xffff; i += 1 + ((w[i] >> 24) & 15)) {
            const auto op = w[i] & 0xffff;
            if (op == 31 || ((w[i] >> 24) & 15) == 0) continue;
            Word& d = w[i + 1];
            const unsigned type = ((d >> 28) & 7) | ((d >> 8) & 24);
            if (type == (control == 8 ? 6u : 8u) && (d & 0x7ff) == (control == 8 ? 1u : 0u))
                d = dst(0, 5, (d >> 16) & 15, d & 0x00700000);
        }
        w.pop_back();
        ins(w, 1, {control == 8 ? dst(6, 1) : dst(8, 0), src(0, 5)});
        w.push_back(0xffff);
    }
    if (control == 10) {
        // COLOR0.w alone in its physical register; COLOR1.xyz lives separately.
        // The displaced zero filler is read from c1, not undeclared COLOR0.xyz.
        for (auto* w : {&p.vs, &p.ps})
            for (std::size_t i = 1; i < w->size() && (*w)[i] != 0xffff; i += 1 + (((*w)[i] >> 24) & 15))
                if (((*w)[i] & 0xffff) == 31 && ((*w)[i + 1] & 0xfffff) == 10)
                    (*w)[i + 2] = ((*w)[i + 2] & ~0x000f0000u) | 0x00080000u;
        Words v{p.vs[0]};
        for (std::size_t i = 1; i < p.vs.size() && p.vs[i] != 0xffff;) {
            const auto size = 1 + ((p.vs[i] >> 24) & 15);
            if (!(p.vs[i] == 0x02000001 && p.vs[i + 1] == dst(6, 1, 7)))
                v.insert(v.end(), p.vs.begin() + i, p.vs.begin() + i + size);
            i += size;
        }
        v.push_back(0xffff);
        p.vs = v;
        for (std::size_t i = 1; i < p.ps.size() && p.ps[i] != 0xffff; i += 1 + ((p.ps[i] >> 24) & 15))
            if ((p.ps[i] & 0xffff) == 2 && p.ps[i + 3] == src(1, 0)) p.ps[i + 3] = src(2, 1);
    }
    if (control == 11 || control == 12) {
        p.ps.resize(p.ps.size() - 5); // replace final native alpha multiply
        ins(p.ps, 1, {dst(8, 0, 8), src(1, 0, 0xff)});
        p.ps.push_back(0xffff);
    }
    return p;
}

struct Decl {
    unsigned usage, index, reg, mask;
    Word flags;
};
std::vector<Decl> declarations(const Words& w, bool vertex) {
    std::vector<Decl> out;
    for (std::size_t i = 1; i < w.size() && w[i] != 0xffff;) {
        const unsigned count = (w[i] >> 24) & 15;
        need(i + count < w.size(), "bounded token length");
        if ((w[i] & 0xffff) == 31) {
            need(count == 2, "DCL length");
            const Word d = w[i + 2];
            const unsigned type = ((d >> 28) & 7) | ((d >> 8) & 24);
            if (type == (vertex ? 6u : 1u))
                out.push_back({w[i + 1] & 15, (w[i + 1] >> 16) & 15, d & 0x7ff, (d >> 16) & 15, d & 0x00700000});
        }
        i += count + 1;
    }
    return out;
}
bool contract(const Program& p) {
    const auto v = declarations(p.vs, true), s = declarations(p.ps, false);
    if (v.size() != 11u + split(p.kind) || s.size() != 10u + split(p.kind)) return false;
    for (const auto* list : {&v, &s})
        for (std::size_t i = 0; i < list->size(); ++i) {
            if (!(*list)[i].mask) return false;
            for (std::size_t j = 0; j < i; ++j) {
                const auto& a = (*list)[i];
                const auto& b = (*list)[j];
                if ((a.usage == b.usage && a.index == b.index) || (a.reg == b.reg && (a.mask & b.mask))) return false;
            }
        }
    for (const auto& a : s) {
        unsigned matches = 0;
        for (const auto& b : v)
            if (a.usage == b.usage && a.index == b.index && a.mask == b.mask && a.reg + 1 == b.reg) ++matches;
        if (matches != 1) return false;
    }
    bool alpha = false, rgb = p.kind == Native;
    for (const auto& a : s) {
        if (a.usage == 10 && a.index == 0) alpha = a.reg == 0 && a.mask == (split(p.kind) ? 8u : 15u) && a.flags == pp;
        if (a.usage == (color_rgb(p.kind) ? 10u : 5u) && a.index == (color_rgb(p.kind) ? 1u : 9u))
            rgb = a.reg == (reference(p.kind) ? 1u : 0u) && a.mask == 7 && a.flags == (centered(p.kind) ? centroid : 0);
    }
    return alpha && rgb;
}
unsigned negative_contracts() {
    unsigned n = 0;
    for (unsigned fault = 0; fault < 6; ++fault) {
        auto p = program(SplitCentroid, true);
        for (std::size_t i = 1; i < p.ps.size(); i += 1 + ((p.ps[i] >> 24) & 15)) {
            if (p.ps[i] == 0xffff) break;
            if ((p.ps[i] & 0xffff) != 31) continue;
            const bool color = (p.ps[i + 1] & 15) == 10;
            const bool rgb = (p.ps[i + 1] & 0xfffff) == 0x90005;
            if (fault == 0 && color) p.ps[i + 2] |= 7u << 16; // overlapping lanes
            if (fault == 1 && rgb) p.ps[i + 1] = 0x8000000a;  // duplicate semantic
            if (fault == 2 && rgb) p.ps[i + 2] |= pp;
            if (fault == 3 && color) p.ps[i + 2] &= ~pp;
            if (fault == 4 && rgb) p.ps[i + 2] &= ~centroid;
            if (fault == 5 && rgb) p.ps[i + 1] = 0x800a0005; // mismatched semantic
        }
        need(!contract(p), "invalid authored contract accepted");
        ++n;
    }
    for (unsigned fault = 0; fault < 2; ++fault) {
        auto p = program(SplitColor, true);
        for (std::size_t i = 1; i < p.ps.size() && p.ps[i] != 0xffff; i += 1 + ((p.ps[i] >> 24) & 15))
            if ((p.ps[i] & 0xffff) == 31 && (p.ps[i + 1] & 0xfffff) == 0x1000a) {
                if (fault == 0)
                    p.ps[i + 2] |= pp;
                else
                    p.ps[i + 1] = 0x8000000a;
            }
        need(!contract(p), "invalid COLOR1 contract accepted");
        ++n;
    }
    need(!context_ok(SplitCentroid, true, 0), "mixed flat undefined");
    need(!context_ok(SplitPlain, false, 1), "wrapped TEX9 excluded");
    need(context_ok(SplitColor, true, 1), "COLOR1 independent of WRAP9");
    return n + 2;
}
void write_words(const std::string& path, const Words& w) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.data()), w.size() * 4);
    need(bool(f), "write authored program");
}
void dump_programs(const char* folder, bool diagnostic = false) {
    for (unsigned d = 0; d < 2; ++d)
        for (unsigned k = 0; k < (diagnostic ? 13u : unsigned(KindCount)); ++k) {
            auto p = diagnostic ? diagnostic_program(k, d) : program(k, d);
            if (!diagnostic) need(contract(p), "positive authored contract");
            const auto stem = std::string(folder) + "/" + (diagnostic ? diagnostic_names[k] : names[k]) + "_" +
                              std::to_string(d);
            write_words(stem + ".vs", p.vs);
            write_words(stem + ".ps", p.ps);
        }
}
#ifdef X3M_VARYING_HOST
int main(int argc, char** argv) {
    try {
        need(argc == 2 || argc == 3, "dump directory required");
        const bool diagnostic = argc == 3;
        dump_programs(argv[1], diagnostic);
        std::printf("CONTRACT programs=%u negatives=%u\n", diagnostic ? 26u : 14u, negative_contracts());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
}
#else
void api(HRESULT hr, const char* where) {
    if (FAILED(hr)) {
        std::printf("API_FAIL where=%s hr=%08lx\n", where, hr);
        throw std::runtime_error(where);
    }
}
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
};
constexpr unsigned W = 64, H = 64;
struct Vertex {
    float clip[4], rgba[4], fog[4];
};
using Image = std::vector<float>;
struct Targets {
    Com<IDirect3DSurface9> rt[3], read[3];
};
struct Shaders {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    unsigned draws = 0, coverage = 0;
};
Vertex vertices[3];
void inputs(unsigned pattern, bool perspective) {
    const float xy[3][2] = {{4.25f, 4.5f}, {59.5f, 7.25f}, {10.25f, 59.5f}};
    const float colors[4][3][3] = {
        {{1 - 1.f / 4096, 1 + 1.f / 4096, .5f},
         {1 + 1.f / 4096, 1 - 1.f / 4096, .5f + 1.f / 8192},
         {1, .75f, .5f - 1.f / 8192}},
        {{32 - 1.f / 128, 64 + 1.f / 64, 16},
         {32 + 1.f / 128, 64 - 1.f / 64, 16 + 1.f / 256},
         {32, 64, 16 - 1.f / 256}},
        {{1.f / 65536, 1.f / 16384, 1.f / 4096},
         {1.f / 65536 + 1.f / 16777216, 1.f / 16384 + 1.f / 4194304, 1.f / 4096 + 1.f / 1048576},
         {1.f / 65536 - 1.f / 16777216, 1.f / 16384 - 1.f / 4194304, 1.f / 4096 - 1.f / 1048576}},
        {{.125f, 128, 32768}, {64, .0625f, 49152}, {.5f, 4096, 65504}}};
    const float alpha[3] = {.5f - 1.f / 4096, .5f + 1.f / 4096, 1.f / 16384 + 1.f / 16777216};
    const float fog[3] = {-.125f, .625f, 1.25f};
    for (unsigned i = 0; i < 3; ++i) {
        const float w = perspective ? (.75f + .5f * i) : 1.f;
        vertices[i] = {{(2 * xy[i][0] / W - 1) * w, (1 - 2 * xy[i][1] / H) * w, (.2f + .3f * i) * w, w},
                       {colors[pattern][i][0], colors[pattern][i][1], colors[pattern][i][2], alpha[i]},
                       {fog[i], 0, 0, 0}};
    }
}
std::array<double, 3> weights(unsigned x, unsigned y) {
    double px[3], py[3];
    for (unsigned i = 0; i < 3; ++i) {
        px[i] = (double(vertices[i].clip[0]) / vertices[i].clip[3] + 1) * W / 2;
        py[i] = (1 - double(vertices[i].clip[1]) / vertices[i].clip[3]) * H / 2;
    }
    const double det = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
    const double a = ((py[1] - py[2]) * (x - px[2]) + (px[2] - px[1]) * (y - py[2])) / det;
    const double b = ((py[2] - py[0]) * (x - px[2]) + (px[0] - px[2]) * (y - py[2])) / det;
    return {a, b, 1 - a - b};
}
Image read(IDirect3DDevice9* d, IDirect3DSurface9* rt, IDirect3DSurface9* copy) {
    api(d->GetRenderTargetData(rt, copy), "readback");
    D3DLOCKED_RECT l{};
    api(copy->LockRect(&l, nullptr, D3DLOCK_READONLY), "read lock");
    Image a(W * H * 4);
    for (unsigned y = 0; y < H; ++y) std::memcpy(&a[y * W * 4], static_cast<char*>(l.pBits) + y * l.Pitch, W * 16);
    api(copy->UnlockRect(), "read unlock");
    return a;
}
void save(const char* label, unsigned id, const Image& a) {
    char file[80];
    std::snprintf(file, sizeof file, "%s_%u.rgba32f", label, id);
    std::ofstream f(file, std::ios::binary);
    f.write(reinterpret_cast<const char*>(a.data()), a.size() * 4);
    need(bool(f), "raw output");
}
int main(int argc, char** argv) {
    HMODULE module = nullptr;
    HWND window = nullptr;
    try {
        const std::string mode = argc > 1 ? argv[1] : "qualification";
        const bool diagnostic = mode == "diagnostic", separate = mode == "separate";
        need(diagnostic || separate || mode == "qualification", "known mode");
        dump_programs(".", diagnostic);
        const unsigned count = diagnostic ? 13 : KindCount;
        const auto labels = diagnostic ? diagnostic_names : names;
        const auto admitted = [&](unsigned k) {
            return !separate || k == Native || k == ReferenceCentroid || k == ReferenceColor;
        };
        const unsigned negatives = negative_contracts();
        module = LoadLibraryA("d3d9.dll");
        need(module != nullptr, "system D3D9");
        using Create = IDirect3D9*(WINAPI*)(UINT);
        Create create = nullptr;
        const auto address = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &address, sizeof create);
        need(create != nullptr, "D3D9 factory");
        window = CreateWindowA("STATIC", "varying split", WS_OVERLAPPED, 0, 0, W, H, nullptr, nullptr,
                               GetModuleHandle(nullptr), nullptr);
        need(window != nullptr, "window");
        {
            Com<IDirect3D9> factory;
            factory.p = create(D3D_SDK_VERSION);
            need(factory.p != nullptr, "factory");
            D3DCAPS9 caps{};
            api(factory->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps), "caps");
            need(caps.VertexShaderVersion >= D3DVS_VERSION(3, 0) && caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) &&
                     caps.NumSimultaneousRTs >= 3,
                 "SM3/three MRT capability");
            D3DDISPLAYMODE mode{};
            api(factory->GetAdapterDisplayMode(0, &mode), "mode");
            api(factory->CheckDeviceFormat(0, D3DDEVTYPE_HAL, mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                           D3DFMT_A32B32G32R32F),
                "FP32 RT format");
            api(factory->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, mode.Format, D3DFMT_A32B32G32R32F, D3DFMT_D24X8),
                "D24 match");
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE;
            pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow = window;
            pp.BackBufferWidth = W;
            pp.BackBufferHeight = H;
            pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            Com<IDirect3DDevice9> device;
            api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
                "device");
            auto* d = device.p;
            Com<IDirect3DSurface9> back, depth;
            api(d->GetRenderTarget(0, &back.p), "backbuffer");
            api(d->CreateDepthStencilSurface(W, H, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr),
                "depth");
            Targets targets;
            for (unsigned i = 0; i < 3; ++i) {
                api(d->CreateRenderTarget(W, H, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &targets.rt[i].p,
                                          nullptr),
                    "target");
                api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &targets.read[i].p,
                                                   nullptr),
                    "staging");
            }
            Com<IDirect3DVertexDeclaration9> declaration;
            const D3DVERTEXELEMENT9 elements[] = {
                {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                D3DDECL_END()};
            api(d->CreateVertexDeclaration(elements, &declaration.p), "declaration");
            Com<IDirect3DTexture9> texture;
            api(d->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture.p, nullptr),
                "sampled alpha texture");
            D3DLOCKED_RECT lock{};
            api(texture->LockRect(0, &lock, nullptr, 0), "texture lock");
            *static_cast<Word*>(lock.pBits) = 0x9f4080c0;
            api(texture->UnlockRect(0), "texture unlock");
            Shaders bank[2][13];
            unsigned creates = 0;
            for (unsigned dep = 0; dep < 2; ++dep)
                for (unsigned kind = 0; kind < count; ++kind) {
                    if (!admitted(kind)) continue;
                    const auto p = diagnostic ? diagnostic_program(kind, dep) : program(kind, dep);
                    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(p.vs.data()), &bank[dep][kind].vs.p),
                        "authored VS create");
                    api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.ps.data()), &bank[dep][kind].ps.p),
                        "authored PS create");
                    creates += 2;
                    std::printf("CREATE depth=%u kind=%s vs=1 ps=1 inputs=10 outputs=11\n", dep, labels[kind]);
                }
            std::printf("CAPS mrt=%lu vs=%08lx ps=%08lx msaa=0 format=116\n", caps.NumSimultaneousRTs,
                        caps.VertexShaderVersion, caps.PixelShaderVersion);
            std::printf("CONTEXT shading=gouraud+flat wrap9=0 no_msaa=1\n");
            api(d->SetDepthStencilSurface(nullptr), "detach depth");
            for (unsigned i = 0; i < 3; ++i) api(d->SetRenderTarget(i, targets.rt[i].p), "bind target");
            api(d->SetDepthStencilSurface(depth.p), "bind depth");
            api(d->SetVertexDeclaration(declaration.p), "bind declaration");
            api(d->SetTexture(0, texture.p), "bind texture");
            for (auto state : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE, D3DRS_DITHERENABLE,
                               D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_LIGHTING})
                api(d->SetRenderState(state, FALSE), "disabled state");
            api(d->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD), "Gouraud contract");
            api(d->SetRenderState(D3DRS_WRAP9, 0), "TEX9 no-wrap contract");
            api(d->SetRenderState(D3DRS_ZENABLE, TRUE), "depth test");
            api(d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE), "depth write");
            api(d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL), "depth compare");
            api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "cull");
            if (caps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE)
                api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "mask0");
            if (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS) {
                api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 15), "mask1");
                api(d->SetRenderState(D3DRS_COLORWRITEENABLE2, 15), "mask2");
            }
            for (auto state : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
                api(d->SetSamplerState(0, state, D3DTEXF_POINT), "point filter");
            api(d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "no mip");
            api(d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE), "data sampler");
            float constants[9][4]{};
            constants[0][0] = constants[0][1] = .5f;
            constants[0][3] = 1;
            for (unsigned i = 0; i < 7; ++i) {
                constants[i + 2][0] = (i + 1) / 64.f;
                constants[i + 2][1] = (i + 1) / 128.f;
                constants[i + 2][2] = (i + 1) / 256.f;
                constants[i + 2][3] = 1;
            }
            constants[2][0] = constants[2][1] = constants[2][2] = 0; // exact PP/full filler reference
            api(d->SetVertexShaderConstantF(0, constants[0], 9), "VS constants");
            api(d->SetPixelShaderConstantF(0, constants[0], 2), "PS constants");
            bool native_flat = true;
            if (separate) {
                // Native-only effective-mode classifier, before any Gouraud draw.
                // Fog at submitted vertex0 is negative -> MOV_SAT produces +0 ->
                // native sampled alpha is exactly +0, regardless of allowed PP width.
                // Candidate RGB never participates in choosing the CPU oracle.
                inputs(0, false);
                BOOL fog = TRUE;
                api(d->SetVertexShaderConstantB(0, &fog, 1), "classifier fog");
                const char* labels[] = {"first_flat", "gouraud", "repeat_flat"};
                std::array<std::array<Image, 3>, 3> native;
                for (unsigned step = 0; step < 3; ++step) {
                    const DWORD requested = step == 1 ? D3DSHADE_GOURAUD : D3DSHADE_FLAT;
                    api(d->SetRenderState(D3DRS_SHADEMODE, requested), "classifier set shade");
                    DWORD before = 0, after = 0;
                    api(d->GetRenderState(D3DRS_SHADEMODE, &before), "classifier get shade before");
                    need(before == requested, "classifier requested state retained before");
                    api(d->SetVertexShader(bank[0][Native].vs.p), "classifier VS");
                    api(d->SetPixelShader(bank[0][Native].ps.p), "classifier PS");
                    api(d->SetRenderTarget(2, targets.rt[2].p), "classifier clear RT2");
                    api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0), "classifier clear");
                    api(d->SetRenderTarget(2, nullptr), "classifier depth-off");
                    api(d->BeginScene(), "classifier begin");
                    api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex)), "classifier draw");
                    api(d->EndScene(), "classifier end");
                    api(d->GetRenderState(D3DRS_SHADEMODE, &after), "classifier get shade after");
                    need(after == requested, "classifier requested state retained after");
                    unsigned coverage = 0;
                    for (unsigned t = 0; t < 3; ++t) {
                        native[step][t] = read(d, targets.rt[t].p, targets.read[t].p);
                        const auto label = std::string("classifier_") + labels[step] + "_rt" + std::to_string(t);
                        save(label.c_str(), 0, native[step][t]);
                    }
                    for (unsigned p = 0; p < W * H; ++p) coverage += native[step][1][p * 4 + 3] != 0;
                    ++bank[0][Native].draws;
                    bank[0][Native].coverage += coverage;
                    std::printf("CLASSIFIER name=%s requested=%lu before=%lu after=%lu "
                                "coverage=%u\n",
                                labels[step], requested, before, after, coverage);
                }
                bool exact_zero = true, exact_gouraud = true, gouraud_nonconstant = false;
                Word first_gouraud = 0;
                unsigned coverage = 0;
                for (unsigned p = 0; p < W * H; ++p) {
                    const unsigned i = p * 4;
                    for (unsigned t = 1; t < 3; ++t)
                        need(!std::memcmp(&native[0][t][i], &native[1][t][i], 16) &&
                                 !std::memcmp(&native[0][t][i], &native[2][t][i], 16),
                             "classifier unchanged temporal");
                    need(!std::memcmp(&native[0][0][i + 3], &native[2][0][i + 3], 4), "classifier stable native alpha");
                    if (native[0][1][i + 3] == 0) continue;
                    Word a, g;
                    std::memcpy(&a, &native[0][0][i + 3], 4);
                    std::memcpy(&g, &native[1][0][i + 3], 4);
                    need(std::isfinite(native[0][0][i + 3]) && std::isfinite(native[1][0][i + 3]),
                         "classifier finite native alpha");
                    exact_zero &= a == 0;
                    exact_gouraud &= a == g;
                    if (!coverage)
                        first_gouraud = g;
                    else
                        gouraud_nonconstant |= g != first_gouraud;
                    ++coverage;
                }
                need(coverage > 1000 && gouraud_nonconstant && exact_zero != exact_gouraud,
                     "classifier unambiguous native interpolation");
                native_flat = exact_zero;
                std::printf("CLASSIFICATION native_flat_conformance=%u "
                            "effective_flat=%s stable=1\n",
                            native_flat, native_flat ? "flat" : "gouraud");
            }
            unsigned id = 0, total = 0;
            double worst = 0;
            for (unsigned flat = 0; flat < (diagnostic ? 1u : 2u); ++flat)
                for (unsigned dep = 0; dep < 2; ++dep)
                    for (unsigned pattern = 0; pattern < (diagnostic ? 1u : 4u); ++pattern)
                        for (unsigned fog = 0; fog < 2; ++fog)
                            for (unsigned perspective = 0; perspective < (diagnostic ? 1u : 2u); ++perspective, ++id) {
                                const bool effective_flat = flat && (!separate || native_flat);
                                inputs(pattern, diagnostic ? dep : perspective);
                                BOOL flag = fog;
                                api(d->SetVertexShaderConstantB(0, &flag, 1), "fog flag");
                                api(d->SetRenderState(D3DRS_SHADEMODE, flat ? D3DSHADE_FLAT : D3DSHADE_GOURAUD),
                                    "case shading");
                                DWORD shade_before = flat ? D3DSHADE_FLAT : D3DSHADE_GOURAUD;
                                if (separate) {
                                    api(d->GetRenderState(D3DRS_SHADEMODE, &shade_before), "case shade before");
                                    need(shade_before == (flat ? D3DSHADE_FLAT : D3DSHADE_GOURAUD),
                                         "case requested shade before");
                                }
                                std::vector<unsigned> selected = flat
                                                                     ? std::vector<unsigned>{Native, ReferenceCentroid,
                                                                                             SplitColor, ReferenceColor}
                                                                     : std::vector<unsigned>{
                                                                           Native,         SplitCentroid,
                                                                           SplitPlain,     ReferenceCentroid,
                                                                           ReferencePlain, SplitColor,
                                                                           ReferenceColor};
                                if (diagnostic) {
                                    selected.clear();
                                    for (unsigned k = 0; k < count; ++k) selected.push_back(k);
                                }
                                if (separate) selected = {Native, ReferenceCentroid, ReferenceColor};
                                std::array<std::array<Image, 3>, 13> images;
                                for (unsigned k : selected) {
                                    need(diagnostic || context_ok(k, flat, 0), "defined interpolation context");
                                    auto& shader = bank[dep][k];
                                    api(d->SetVertexShader(shader.vs.p), "bind VS");
                                    api(d->SetPixelShader(shader.ps.p), "bind PS");
                                    api(d->SetRenderTarget(2, targets.rt[2].p), "clear depth-output target");
                                    api(d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0), "clear");
                                    if (!dep) api(d->SetRenderTarget(2, nullptr), "depth-off target unbound");
                                    Vertex submitted[3];
                                    std::memcpy(submitted, vertices, sizeof vertices);
                                    // Independent TEX full reference for flat COLOR: constant RGB
                                    // from the documented first triangle-list vertex; alpha/fog,
                                    // position and all shader instructions remain unchanged.
                                    if (effective_flat && k == ReferenceCentroid)
                                        for (auto& v : submitted)
                                            std::memcpy(v.rgba, vertices[0].rgba, 3 * sizeof(float));
                                    api(d->BeginScene(), "begin");
                                    api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, submitted, sizeof(Vertex)), "draw");
                                    api(d->EndScene(), "end");
                                    ++shader.draws;
                                    for (unsigned t = 0; t < 3; ++t) {
                                        images[k][t] = read(d, targets.rt[t].p, targets.read[t].p);
                                        const auto label = std::string(labels[k]) + "_rt" + std::to_string(t);
                                        save(label.c_str(), id, images[k][t]);
                                    }
                                }
                                DWORD shade_after = shade_before;
                                if (separate) {
                                    api(d->GetRenderState(D3DRS_SHADEMODE, &shade_after), "case shade after");
                                    need(shade_after == shade_before, "case requested shade after");
                                }
                                if (diagnostic) {
                                    unsigned coverage = 0;
                                    for (unsigned p = 0; p < W * H; ++p) coverage += images[0][1][p * 4 + 3] != 0;
                                    for (unsigned k : selected) {
                                        const unsigned ref = k == 6 ? 7 : k == 11 || k == 12 ? 12 : 0;
                                        unsigned alpha_diff = 0, nonfinite = 0, rgb_diff = 0, temporal_diff = 0;
                                        Word first_bits = 0;
                                        for (unsigned p = 0; p < W * H; ++p) {
                                            const auto i = p * 4;
                                            if (images[0][1][i + 3] == 0) continue;
                                            Word bits;
                                            std::memcpy(&bits, &images[k][0][i + 3], 4);
                                            if (!first_bits) first_bits = bits;
                                            nonfinite += !std::isfinite(images[k][0][i + 3]);
                                            alpha_diff += std::memcmp(&images[k][0][i + 3], &images[ref][0][i + 3],
                                                                      4) != 0;
                                            // Native RGB is intentionally saturated; separate full
                                            // RGB is the control.
                                            rgb_diff += std::memcmp(&images[k][0][i], &images[1][0][i], 12) != 0;
                                            for (unsigned t = 1; t < 3; ++t)
                                                temporal_diff += std::memcmp(&images[k][t][i], &images[0][t][i], 16) !=
                                                                 0;
                                        }
                                        bank[dep][k].coverage += coverage;
                                        std::printf("OBSERVE id=%u depth=%u fog=%u perspective=%u kind=%s "
                                                    "reference=%s "
                                                    "coverage=%u alpha_diff=%u alpha_nonfinite=%u "
                                                    "rgb_diff=%u temporal_diff=%u first_alpha_bits=%08x\n",
                                                    id, dep, fog, dep, labels[k], labels[ref], coverage, alpha_diff,
                                                    nonfinite, rgb_diff, temporal_diff, first_bits);
                                    }
                                    total += coverage;
                                    continue;
                                }
                                unsigned coverage = 0, analytic = 0;
                                double maximum = 0;
                                auto exact = [&](unsigned k, unsigned t, unsigned i, unsigned count, unsigned ref,
                                                 const char* label) {
                                    if (std::memcmp(&images[ref][t][i], &images[k][t][i], count * 4)) {
                                        std::printf("DIFF case=%u kind=%s rt=%u pixel=%u channel=%u "
                                                    "actual=%.9g reference=%.9g check=%s\n",
                                                    id, labels[k], t, i / 4, i % 4, images[k][t][i], images[ref][t][i],
                                                    label);
                                        Word actual_bits, reference_bits;
                                        std::memcpy(&actual_bits, &images[k][t][i], 4);
                                        std::memcpy(&reference_bits, &images[ref][t][i], 4);
                                        std::printf("DIFF_BITS actual=%08x reference=%08x\n", actual_bits,
                                                    reference_bits);
                                        need(false, label);
                                    }
                                };
                                for (unsigned y = 0; y < H; ++y)
                                    for (unsigned x = 0; x < W; ++x) {
                                        const unsigned i = (y * W + x) * 4;
                                        const bool covered = images[0][1][i + 3] != 0;
                                        for (unsigned k : selected) {
                                            exact(k, 0, i + 3, 1, Native, "native_PP_alpha");
                                            for (unsigned t = 1; t < 3; ++t) exact(k, t, i, 4, Native, "motion_depth");
                                        }
                                        if (!separate)
                                            exact(SplitColor, 0, i, 3, ReferenceColor, "dedicated_COLOR1_RGB");
                                        if (!flat && !separate) {
                                            exact(SplitCentroid, 0, i, 3, ReferenceCentroid, "dedicated_TEX9_RGB");
                                            exact(SplitPlain, 0, i, 3, ReferencePlain, "dedicated_TEX9_RGB");
                                            exact(SplitCentroid, 0, i, 3, SplitPlain, "no_MSAA_centroid");
                                        }
                                        if (!dep)
                                            for (unsigned c = 0; c < 4; ++c)
                                                need(images[0][2][i + c] == 0, "depth-off target untouched");
                                        if (!covered) continue;
                                        ++coverage;
                                        const auto b = weights(x, y);
                                        if (*std::min_element(b.begin(), b.end()) < .05) continue;
                                        ++analytic;
                                        double denominator = 0;
                                        for (unsigned v = 0; v < 3; ++v) denominator += b[v] / vertices[v].clip[3];
                                        for (unsigned c = 0; c < 3; ++c) {
                                            double expected = 0;
                                            for (unsigned v = 0; v < 3; ++v)
                                                expected += b[v] * vertices[v].rgba[c] / vertices[v].clip[3];
                                            expected = effective_flat ? vertices[0].rgba[c] : expected / denominator;
                                            // Predeclared experimental envelope, not a universal D3D
                                            // guarantee: 64 float32 eps allows setup/interpolation
                                            // arithmetic on this well-conditioned positive triangle.
                                            // It is 64x tighter than a half relative rounding step;
                                            // tiny gradients receive the same relative
                                            // discrimination.
                                            const double tolerance = 1e-12 + std::ldexp(64., -23) * std::abs(expected);
                                            for (unsigned k : selected) {
                                                if (k == Native) continue;
                                                const double actual = images[k][0][i + c];
                                                const double error = std::abs(actual - expected);
                                                maximum = std::max(maximum, error / tolerance);
                                                if (!std::isfinite(actual) || error > tolerance) {
                                                    std::printf("DIFF case=%u kind=%s pixel=%u channel=%u "
                                                                "actual=%.12g expected=%.12g tolerance=%.12g\n",
                                                                id, labels[k], i / 4, c, actual, expected, tolerance);
                                                    need(false, "CPU full RGB interpolation");
                                                }
                                            }
                                        }
                                    }
                                need(coverage > 1000 && analytic > 700, "substantial raster interior");
                                for (unsigned k : selected) bank[dep][k].coverage += coverage;
                                total += coverage;
                                worst = std::max(worst, maximum);
                                std::printf("CASE id=%u flat=%u effective_flat=%u depth=%u pattern=%u "
                                            "fog=%u "
                                            "perspective=%u draws=%u coverage=%u analytic=%u "
                                            "rgb_exact=%u alpha_exact=1 motion_exact=1 depth_exact=1 "
                                            "centroid_exact=%u shade_before=%lu shade_after=%lu "
                                            "max_fraction=%.9g\n",
                                            id, flat, effective_flat, dep, pattern, fog, perspective,
                                            unsigned(selected.size()), coverage, analytic, !separate,
                                            !flat && !separate, shade_before, shade_after, maximum);
                            }
            for (unsigned dep = 0; dep < 2; ++dep)
                for (unsigned k = 0; k < count; ++k)
                    if (admitted(k))
                        std::printf("PROGRAM depth=%u kind=%s draws=%u coverage=%u\n", dep, labels[k],
                                    bank[dep][k].draws, bank[dep][k].coverage);
            api(d->SetTexture(0, nullptr), "unbind texture");
            api(d->SetVertexShader(nullptr), "unbind VS");
            api(d->SetPixelShader(nullptr), "unbind PS");
            api(d->SetVertexDeclaration(nullptr), "unbind declaration");
            api(d->SetStreamSource(0, nullptr, 0, 0), "unbind stream");
            for (unsigned i = 1; i < 3; ++i) api(d->SetRenderTarget(i, nullptr), "unbind MRT");
            api(d->SetDepthStencilSurface(nullptr), "unbind depth");
            api(d->SetRenderTarget(0, back.p), "restore back");
            std::printf("RESULT %s cases=%u creates=%u negatives=%u coverage=%u "
                        "max_fraction=%.9g\n",
                        diagnostic ? "DIAGNOSTIC" : "PASS", id, creates, negatives, total, worst);
        }
        DestroyWindow(window);
        FreeLibrary(module);
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s\n", e.what());
        if (window) DestroyWindow(window);
        if (module) FreeLibrary(module);
        return 1;
    }
}
#endif
