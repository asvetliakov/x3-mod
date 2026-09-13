// Detached numerical qualification: local original game programs stay
// untracked. Binary cases are authored by run_linear_material.py; it owns the
// independent float64 oracle. World position is constant while clip geometry
// covers the RT, making the lighting varyings uniform without redefining their
// angular math.
#define WIN32_LEAN_AND_MEAN
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
void api(HRESULT h) {
  if (FAILED(h)) {
    std::printf("API_FAIL %08lx\n", h);
    throw std::runtime_error("D3D9 API");
  }
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> struct Com {
  T *p = nullptr;
  Com() = default;
  Com(const Com &) = delete;
  ~Com() {
    if (p)
      p->Release();
  }
  T *operator->() const { return p; }
};
struct Case {
  unsigned id, pair, depth, lights, reverse, affine, valid, fp16;
  float f[32];
};
static_assert(sizeof(Case) == 160, "binary case ABI");
const char *vertex_ids[] = {"53a0a641107ed76c", "719856ce0c213220",
                            "badefd5143b3024f"};
const char *pixel_ids[] = {"63f96eba9eea7880", "8759c7838bbc86c2",
                           "593e5dea9b3457d5", "7a0bb00a8070496a",
                           "8d5b2ba0fb4d13bf", "dab93928f26906f7"};
const unsigned pair_v[] = {0, 0, 1, 1, 1, 1, 2, 2, 2, 2},
               pair_p[] = {0, 1, 2, 3, 4, 5, 2, 3, 4, 5};
Words load(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  require(bool(in), "missing local program");
  auto bytes = in.tellg();
  require(bytes > 0 && bytes % 4 == 0 && bytes < 65536, "shader framing");
  Words w(std::size_t(bytes) / 4);
  in.seekg(0);
  in.read(reinterpret_cast<char *>(w.data()), bytes);
  require(bool(in), "shader read");
  return w;
}
// Executable opcode count is a framing/budget lower-bound screen; documented
// multi-slot instructions are not claimed to cost one slot. CreateShader below
// is the actual device qualification of each complete program.
unsigned instructions(const Words &w) {
  unsigned result = 0;
  for (std::size_t i = 1; i < w.size();) {
    unsigned op = w[i] & 65535;
    if (op == 65535) {
      require(i + 1 == w.size(), "trailing shader bytes");
      return result;
    }
    unsigned n = op == 65534 ? (w[i] >> 16) & 32767 : (w[i] >> 24) & 15;
    require(i + n < w.size(), "instruction framing");
    if (op != 65534 && op != 31 && op != 81 && op != 47 && op != 48)
      ++result;
    i += 1 + n;
  }
  throw std::runtime_error("missing shader END");
}
float from_half(unsigned short h) {
  unsigned sign = unsigned(h & 0x8000) << 16, exponent = (h >> 10) & 31,
           mantissa = h & 1023, b;
  if (exponent == 0) {
    if (!mantissa)
      b = sign;
    else {
      int e = -14;
      while (!(mantissa & 1024)) {
        mantissa <<= 1;
        --e;
      }
      b = sign | unsigned(e + 127) << 23 | (mantissa & 1023) << 13;
    }
  } else
    b = sign | (exponent == 31 ? 255 : exponent + 112) << 23 | mantissa << 13;
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
struct Pixel {
  float f[4];
};
struct Shaders {
  IDirect3DDevice9 *d;
  D3DCAPS9 caps;
  Words originals[2][6];
  std::map<std::string, IDirect3DVertexShader9 *> vertices;
  std::map<std::string, IDirect3DPixelShader9 *> pixels;
  Shaders(IDirect3DDevice9 *device, const std::string &path) : d(device) {
    api(d->GetDeviceCaps(&caps));
    for (unsigned i = 0; i < 3; ++i)
      originals[0][i] = load(path + "\\vs_" + vertex_ids[i] + ".bin");
    for (unsigned i = 0; i < 6; ++i)
      originals[1][i] = load(path + "\\ps_" + pixel_ids[i] + ".bin");
  }
  ~Shaders() {
    for (auto &x : vertices)
      x.second->Release();
    for (auto &x : pixels)
      x.second->Release();
  }
  std::string key(const Case &c, unsigned mode, bool pixel) {
    const char *id =
        pixel ? pixel_ids[pair_p[c.pair]] : vertex_ids[pair_v[c.pair]];
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%s_%u_%u_%.9g_%.9g_%.9g", id, mode,
                  mode ? c.depth : 0, mode == 2 ? c.f[0] : 0,
                  mode == 2 ? c.f[1] : 0, mode == 2 ? c.f[2] : 0);
    return buffer;
  }
  Words transform(const Case &c, unsigned mode, bool pixel) {
    const auto &original =
        originals[pixel][pixel ? pair_p[c.pair] : pair_v[c.pair]];
    const auto before = original;
    Words output = {0xdeadbeef};
    LinearMaterialConfig config{c.f[0], c.f[1], c.f[2]};
    if (mode == 0)
      output = original;
    else if (mode == 1)
      require((pixel ? material_motion_pixel_variant(
                           original.data(), original.size(), output, c.depth)
                     : material_motion_vertex_variant(
                           original.data(), original.size(), output,
                           c.depth)) == MaterialMotionResult::Applied,
              "motion transform");
    else {
      require((pixel ? linear_material_pixel_variant(original.data(),
                                                     original.size(), config,
                                                     output, c.depth)
                     : linear_material_vertex_variant(
                           original.data(), original.size(), config, output,
                           c.depth)) == LinearMaterialResult::Applied,
              "combined transform");
      Words sentinel = {0x12345678, 0xcafebabe};
      const auto preserved = sentinel;
      auto invalid = config;
      invalid.direct_gain = -1;
      require((pixel ? linear_material_pixel_variant(original.data(),
                                                     original.size(), invalid,
                                                     sentinel, c.depth)
                     : linear_material_vertex_variant(
                           original.data(), original.size(), invalid, sentinel,
                           c.depth)) == LinearMaterialResult::InvalidConfig &&
                  sentinel == preserved,
              "invalid config publication");
      Words damaged = original;
      damaged.back() = 0;
      require(
          (pixel ? linear_material_pixel_variant(damaged.data(), damaged.size(),
                                                 config, sentinel, c.depth)
                 : linear_material_vertex_variant(
                       damaged.data(), damaged.size(), config, sentinel,
                       c.depth)) != LinearMaterialResult::Applied &&
              sentinel == preserved,
          "invalid source publication");
    }
    require(original == before, "original mutated");
    return output;
  }
  void bind(const Case &c, unsigned mode) {
    for (bool pixel : {false, true}) {
      auto k = key(c, mode, pixel);
      if (pixel ? pixels.count(k) : vertices.count(k))
        continue;
      LARGE_INTEGER start, end, freq;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&start);
      auto w = transform(c, mode, pixel);
      const unsigned count = instructions(w);
      require(count <= (pixel ? caps.MaxPixelShader30InstructionSlots
                              : caps.MaxVertexShader30InstructionSlots),
              "device instruction budget");
      if (pixel) {
        IDirect3DPixelShader9 *p = nullptr;
        api(d->CreatePixelShader(reinterpret_cast<const DWORD *>(w.data()),
                                 &p));
        pixels.emplace(k, p);
      } else {
        IDirect3DVertexShader9 *v = nullptr;
        api(d->CreateVertexShader(reinterpret_cast<const DWORD *>(w.data()),
                                  &v));
        vertices.emplace(k, v);
      }
      QueryPerformanceCounter(&end);
      std::printf("CREATE stage=%s key=%s instructions=%u words=%zu "
                  "completed_ms=%.6f\n",
                  pixel ? "ps" : "vs", k.c_str(), count, w.size(),
                  1000. * (end.QuadPart - start.QuadPart) / freq.QuadPart);
    }
    api(d->SetVertexShader(vertices.at(key(c, mode, false))));
    api(d->SetPixelShader(pixels.at(key(c, mode, true))));
  }
};
struct Gpu {
  IDirect3DDevice9 *d;
  Shaders &shaders;
  unsigned width;
  Com<IDirect3DSurface9> back, color[2], motion, current;
  Com<IDirect3DTexture9> textures[3];
  Com<IDirect3DCubeTexture9> cube;
  Com<IDirect3DVertexDeclaration9> declaration;
  Gpu(IDirect3DDevice9 *device, Shaders &s, unsigned size)
      : d(device), shaders(s), width(size) {
    api(d->GetRenderTarget(0, &back.p));
    for (unsigned f = 0; f < 2; ++f)
      api(d->CreateRenderTarget(
          width, width, f ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F,
          D3DMULTISAMPLE_NONE, 0, FALSE, &color[f].p, nullptr));
    api(d->CreateRenderTarget(width, width, D3DFMT_A32B32G32R32F,
                              D3DMULTISAMPLE_NONE, 0, FALSE, &motion.p,
                              nullptr));
    api(d->CreateRenderTarget(width, width, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0,
                              FALSE, &current.p, nullptr));
    for (auto &t : textures)
      api(d->CreateTexture(1, 1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                           &t.p, nullptr));
    api(d->CreateCubeTexture(1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                             &cube.p, nullptr));
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 0},
        {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,
         0},
        D3DDECL_END()};
    api(d->CreateVertexDeclaration(elements, &declaration.p));
  }
  ~Gpu() {
    for (unsigned i = 0; i < 4; ++i)
      d->SetTexture(i, nullptr);
    d->SetRenderTarget(2, nullptr);
    d->SetRenderTarget(1, nullptr);
    d->SetRenderTarget(0, back.p);
    d->SetVertexDeclaration(nullptr);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
  }
  void state(const Case &c, unsigned mode) {
    api(d->SetRenderTarget(2, nullptr));
    api(d->SetRenderTarget(1, nullptr));
    api(d->SetDepthStencilSurface(nullptr));
    api(d->SetRenderTarget(0, color[c.fp16].p));
    api(d->SetRenderTarget(1, mode ? motion.p : nullptr));
    api(d->SetRenderTarget(2, mode && c.depth ? current.p : nullptr));
    D3DVIEWPORT9 viewport{0, 0, width, width, 0, 1};
    api(d->SetViewport(&viewport));
    for (auto s :
         {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRGBWRITEENABLE,
          D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE,
          D3DRS_SCISSORTESTENABLE, D3DRS_FOGENABLE, D3DRS_DITHERENABLE,
          D3DRS_CLIPPLANEENABLE, D3DRS_LIGHTING})
      api(d->SetRenderState(s, FALSE));
    for (auto s : {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1,
                   D3DRS_COLORWRITEENABLE2})
      api(d->SetRenderState(s, 15));
    api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    api(d->SetVertexDeclaration(declaration.p));
    shaders.bind(c, mode);
    const float mask[4] = {c.f[15], 0, 0, 1};
    const float *texels[] = {c.f + 3, mask, c.f + 7};
    for (unsigned i = 0; i < 3; ++i) {
      D3DLOCKED_RECT lock{};
      api(textures[i]->LockRect(0, &lock, nullptr, 0));
      std::memcpy(lock.pBits, texels[i], 16);
      api(textures[i]->UnlockRect(0));
      api(d->SetTexture(i, textures[i].p));
    }
    for (unsigned face = 0; face < 6; ++face) {
      D3DLOCKED_RECT lock{};
      api(cube->LockRect(D3DCUBEMAP_FACES(face), 0, &lock, nullptr, 0));
      std::memcpy(lock.pBits, c.f + 11, 16);
      api(cube->UnlockRect(D3DCUBEMAP_FACES(face), 0));
    }
    api(d->SetTexture(3, cube.p));
    for (unsigned i = 0; i < 4; ++i) {
      for (auto s : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
        api(d->SetSamplerState(i, s, D3DTEXF_POINT));
      api(d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
      api(d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE));
    }
    float v[256][4]{};
    bool fixed = pair_v[c.pair] == 2;
    unsigned matrix = fixed ? 0 : 24, normal = fixed ? 10 : 31,
             camera = fixed ? 13 : 34, emissive = fixed ? 19 : 40,
             alpha = fixed ? 18 : 39, tex = fixed ? 16 : 37;
    for (unsigned k = 0; k < 4; ++k) {
      v[matrix + k][k] = 1;
      v[252 + k][k] = 1;
    }
    v[252][3] = -.125f;
    for (unsigned k = 0; k < 3; ++k)
      v[normal + k][k] = 1;
    v[camera + 2][3] = 4;
    v[alpha][0] = .625f;
    v[tex][0] = v[tex + 1][1] = 1;
    std::memcpy(v[emissive], c.f + 16, 12);
    for (unsigned i = 0; i < (fixed ? 1u : 8u); ++i) {
      unsigned base = fixed ? 4 : i * 3;
      v[base][2] = 2;
      std::memcpy(v[base + 1], c.f + 19, 12);
      v[base + 2][0] = 2;
      v[base + 2][1] = .25f;
      v[base + 2][2] = .125f;
    }
    api(d->SetVertexShaderConstantF(0, v[0], 256));
    int lights[4] = {int(c.lights), 0, 1, 0};
    api(d->SetVertexShaderConstantI(0, lights, 1));
    BOOL fog = FALSE;
    api(d->SetVertexShaderConstantB(0, &fog, 1));
    float p[221][4]{};
    unsigned profile = pair_p[c.pair];
    bool affine = profile != 2 && profile != 5;
    unsigned glow = affine ? 3 : 0, dir = glow + 1;
    if (affine) {
      if (c.affine) {
        const float rows[12] = {.75f, .125f,   0,     .0625f, 0,     .5f,
                                .25f, .03125f, .125f, 0,      .875f, -.03125f};
        std::memcpy(p, rows, sizeof rows);
      } else
        for (unsigned k = 0; k < 3; ++k)
          p[k][k] = 1;
    }
    p[glow][0] = c.f[31];
    p[dir][2] = 1;
    std::memcpy(p[dir + 1], c.f + 22, 12);
    if (profile < 2) {
      p[dir + 2][2] = -1;
      std::memcpy(p[dir + 3], c.f + 25, 12);
    }
    p[216][0] = p[216][1] = 1.f / width;
    p[216][2] = .25f / width;
    p[216][3] = -.375f / width;
    p[217][0] = c.valid ? 1.f : 0.f;
    api(d->SetPixelShaderConstantF(0, p[0], 221));
  }
  void draw(const Case &c, unsigned repeats = 1) {
    float vertices[3][8] = {{-1, 1, .5f, 0, 0, 0, 0, 1},
                            {3, 1, .5f, 1, 0, 0, 0, 1},
                            {-1, -3, .5f, 0, 1, 0, 0, 1}};
    for (auto &v : vertices)
      std::memcpy(v + 5, c.f + 28, 12);
    if (c.reverse)
      for (unsigned k = 0; k < 8; ++k)
        std::swap(vertices[1][k], vertices[2][k]);
    api(d->BeginScene());
    for (unsigned i = 0; i < repeats; ++i)
      api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 32));
    api(d->EndScene());
  }
  std::vector<Pixel> read(IDirect3DSurface9 *target, D3DFORMAT format) {
    Com<IDirect3DSurface9> sys;
    api(d->CreateOffscreenPlainSurface(width, width, format, D3DPOOL_SYSTEMMEM,
                                       &sys.p, nullptr));
    api(d->GetRenderTargetData(target, sys.p));
    D3DLOCKED_RECT lock{};
    api(sys->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    std::vector<Pixel> result(width * width);
    for (unsigned y = 0; y < width; ++y)
      for (unsigned x = 0; x < width; ++x) {
        auto &p = result[y * width + x];
        auto *bytes = static_cast<char *>(lock.pBits) + y * lock.Pitch +
                      x * (format == D3DFMT_R32F            ? 4
                           : format == D3DFMT_A16B16G16R16F ? 8
                                                            : 16);
        if (format == D3DFMT_R32F)
          std::memcpy(p.f, bytes, 4);
        else if (format == D3DFMT_A16B16G16R16F) {
          unsigned short half[4];
          std::memcpy(half, bytes, 8);
          for (unsigned k = 0; k < 4; ++k)
            p.f[k] = from_half(half[k]);
        } else
          std::memcpy(p.f, bytes, 16);
      }
    api(sys->UnlockRect());
    return result;
  }
  void test(const Case &c) {
    state(c, 1);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    draw(c);
    auto before = read(color[c.fp16].p,
                       c.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F),
         motion_before = read(motion.p, D3DFMT_A32B32G32R32F);
    auto depth_before =
        c.depth ? read(current.p, D3DFMT_R32F) : std::vector<Pixel>{};
    state(c, 2);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    draw(c);
    auto after = read(color[c.fp16].p,
                      c.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F),
         motion_after = read(motion.p, D3DFMT_A32B32G32R32F);
    auto depth_after =
        c.depth ? read(current.p, D3DFMT_R32F) : std::vector<Pixel>{};
    unsigned alpha_bad = 0, motion_bad = 0, depth_bad = 0;
    for (unsigned i = 0; i < width * width; ++i) {
      alpha_bad += std::memcmp(&before[i].f[3], &after[i].f[3], 4) != 0;
      motion_bad += std::memcmp(&motion_before[i], &motion_after[i], 16) != 0;
      if (c.depth)
        depth_bad +=
            std::memcmp(&depth_before[i].f[0], &depth_after[i].f[0], 4) != 0;
    }
    std::printf(
        "INVARIANT id=%u pixels=%u alpha_bad=%u motion_bad=%u depth_bad=%u\n",
        c.id, width * width, alpha_bad, motion_bad, depth_bad);
    require(!alpha_bad && !motion_bad && !depth_bad,
            "alpha or temporal identity");
    for (unsigned y : {width / 4, width / 2, 3 * width / 4})
      for (unsigned x : {width / 4, width / 2, 3 * width / 4}) {
        const auto &p = after[y * width + x];
        std::printf("SAMPLE id=%u x=%u y=%u rgba=%.9g,%.9g,%.9g,%.9g\n", c.id,
                    x, y, p.f[0], p.f[1], p.f[2], p.f[3]);
      }
  }
  void wait(IDirect3DQuery9 *event) {
    BOOL done = FALSE;
    const auto start = GetTickCount();
    for (;;) {
      HRESULT h = event->GetData(&done, sizeof done, D3DGETDATA_FLUSH);
      if (h == S_OK && done)
        return;
      require(!FAILED(h) && GetTickCount() - start < 10000, "event wait");
      Sleep(0);
    }
  }
  void timing(Case c) {
    c.fp16 = 1;
    c.depth = 1;
    // A managed grid gives the VS a meaningful workload: 24,576 distinct
    // vertices per draw, with one normal and constant world lighting. Setup
    // allocation/upload is excluded from all timed windows.
    struct Vertex {
      float x, y, z, u, v, nx, ny, nz;
    };
    std::vector<Vertex> grid;
    grid.reserve(64 * 64 * 6);
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x) {
        float left = -1 + x / 32.f, right = left + 1 / 32.f, top = 1 - y / 32.f,
              bottom = top - 1 / 32.f;
        for (auto xy : {std::pair<float, float>{left, top},
                        {right, top},
                        {left, bottom},
                        {right, top},
                        {right, bottom},
                        {left, bottom}})
          grid.push_back({xy.first, xy.second, .5f, 0, 0, 0, 0, 1});
      }
    Com<IDirect3DVertexBuffer9> buffer;
    api(d->CreateVertexBuffer(UINT(grid.size() * sizeof(Vertex)), 0, 0,
                              D3DPOOL_MANAGED, &buffer.p, nullptr));
    void *data = nullptr;
    api(buffer->Lock(0, 0, &data, 0));
    std::memcpy(data, grid.data(), grid.size() * sizeof(Vertex));
    api(buffer->Unlock());
    Com<IDirect3DQuery9> event;
    api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p));
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    for (unsigned lights : {0u, 8u}) {
      c.lights = lights;
      for (unsigned i = 0; i < 24; ++i) {
        unsigned mode = (i / 3) % 2 ? 2 - i % 3 : i % 3;
        state(c, mode);
        api(d->SetStreamSource(0, buffer.p, 0, sizeof(Vertex)));
        api(event->Issue(D3DISSUE_END));
        wait(event.p);
        LARGE_INTEGER begin, end;
        QueryPerformanceCounter(&begin);
        api(d->BeginScene());
        for (unsigned repeat = 0; repeat < 4; ++repeat)
          api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 8192));
        api(d->EndScene());
        api(event->Issue(D3DISSUE_END));
        wait(event.p);
        QueryPerformanceCounter(&end);
        if (i >= 6)
          std::printf("TIMING lights=%u mode=%u iteration=%u draws=4 "
                      "vertices=98304 width=%u completed_ms=%.6f\n",
                      lights, mode, i - 6, width,
                      1000. * (end.QuadPart - begin.QuadPart) /
                          frequency.QuadPart);
      }
    }
    api(d->SetStreamSource(0, nullptr, 0, 0));
  }
};
int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    require(argc == 3, "args: local programs directory, binary cases");
    std::ifstream file(argv[2], std::ios::binary);
    unsigned count = 0;
    file.read(reinterpret_cast<char *>(&count), 4);
    require(count > 0 && count < 2000, "case count");
    std::vector<Case> cases(count);
    file.read(reinterpret_cast<char *>(cases.data()),
              cases.size() * sizeof(Case));
    require(bool(file) && file.peek() == EOF, "case file framing");
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3LinearMaterialFixture";
    require(RegisterClassA(&cls) != 0, "window class");
    HWND window = CreateWindowA(cls.lpszClassName, "Detached linear material",
                                WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr,
                                nullptr, cls.hInstance, nullptr);
    require(window != nullptr, "window");
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    require(runtime != nullptr, "D3D9 runtime");
    auto raw = GetProcAddress(runtime, "Direct3DCreate9");
    IDirect3D9 *(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &raw, sizeof create);
    require(create != nullptr, "D3D9 factory export");
    {
      Com<IDirect3D9> factory;
      factory.p = create(D3D_SDK_VERSION);
      require(factory.p != nullptr, "factory");
      D3DPRESENT_PARAMETERS pp{};
      pp.Windowed = TRUE;
      pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
      pp.hDeviceWindow = window;
      pp.BackBufferWidth = pp.BackBufferHeight = 32;
      pp.BackBufferFormat = D3DFMT_A8R8G8B8;
      Com<IDirect3DDevice9> device;
      api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                &device.p));
      Shaders shaders(device.p, argv[1]);
      std::printf("CAPS mrt=%lu vs_slots=%lu ps_slots=%lu\n",
                  shaders.caps.NumSimultaneousRTs,
                  shaders.caps.MaxVertexShader30InstructionSlots,
                  shaders.caps.MaxPixelShader30InstructionSlots);
      require(shaders.caps.NumSimultaneousRTs >= 3, "three MRTs");
      {
        Gpu gpu(device.p, shaders, 16);
        for (const auto &c : cases) {
          require(c.pair < 10 && c.fp16 < 2 && c.depth < 2, "case bounds");
          gpu.test(c);
        }
      }
      {
        Gpu gpu(device.p, shaders, 256);
        gpu.timing(cases.front());
      }
    } // Release every D3D object before destroying its device window.
    require(DestroyWindow(window) != 0, "destroy window");
    require(FreeLibrary(runtime) != 0, "unload D3D9 runtime");
    require(UnregisterClassA(cls.lpszClassName, cls.hInstance) != 0,
            "unregister window class");
    std::printf("RESULT PASS cases=%u\n", count);
    return 0;
  } catch (const std::exception &e) {
    std::printf("RESULT FAIL %s\n", e.what());
    return 1;
  }
}
